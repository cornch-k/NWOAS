#!/usr/bin/env python3
"""Classify D59-D62 HID-watch logs without controlling the target."""

import argparse
import json
from pathlib import Path
import re


PHYSICAL_RE = re.compile(
    r"HIDACT PHYSICAL-REPORT slot=(\d+) ep=(\d+) baseline=(\d+) now=(\d+)"
)
REPORT_RE = re.compile(
    r"HIDACT REPORT ep=(\d+) index=(\d+).*?nonzero=(\d+)"
)
FB_RE = re.compile(r"HIDACT FB .*?changed=(\d+)")
TILE_RE = re.compile(
    r"HIDACT FB .*?changed=(\d+).*?tiles=(\d+) "
    r"bbox=(\d+),(\d+)-(\d+),(\d+)"
)
ALIVE_RE = re.compile(r"\[evtdump\] alive t=(\d+)s")


def classify(text: str, source: Path) -> dict:
    physical = [
        {
            "slot": int(match.group(1)),
            "endpoint": int(match.group(2)),
            "baseline_count": int(match.group(3)),
            "observed_count": int(match.group(4)),
        }
        for match in PHYSICAL_RE.finditer(text)
    ]
    reports = [
        {
            "endpoint": int(match.group(1)),
            "index": int(match.group(2)),
            "nonzero_bytes": int(match.group(3)),
        }
        for match in REPORT_RE.finditer(text)
    ]
    tiles = [
        {
            "changed": bool(int(match.group(1))),
            "changed_tiles": int(match.group(2)),
            "bbox": [int(match.group(i)) for i in range(3, 7)],
        }
        for match in TILE_RE.finditer(text)
    ]
    framebuffer_changed = any(int(match.group(1)) for match in FB_RE.finditer(text))
    error_counts = {
        "whea": text.count("HVLOG: WHEA"),
        "cper": text.count("HVLOG: CPER"),
        "bugcheck": text.count("BUGCHECK"),
        "guest_exception": text.count("Guest exception") + text.count("Exception taken"),
    }
    errors = any(error_counts.values())
    armed = "HVLOG: FBMASK END" in text and "hid_watch=1" in text
    nonzero_report = any(item["nonzero_bytes"] > 0 for item in reports)
    localized_change = any(
        item["changed"]
        and 0 < item["changed_tiles"] <= 64
        and item["bbox"][2] - item["bbox"][0] + 1 <= 128
        and item["bbox"][3] - item["bbox"][1] + 1 <= 128
        for item in tiles
    )
    if errors:
        verdict = "GUEST_ERROR"
    elif physical and nonzero_report:
        verdict = "PHYSICAL_HID_WITH_NONZERO_REPORT"
    elif physical:
        verdict = "PHYSICAL_HID_COMPLETION"
    elif armed:
        verdict = "WAITING_FOR_PHYSICAL_INPUT"
    else:
        verdict = "NOT_ARMED"

    alive = [int(match.group(1)) for match in ALIVE_RE.finditer(text)]
    return {
        "source": str(source.resolve()),
        "verdict": verdict,
        "watch_armed": armed,
        "last_alive_seconds": alive[-1] if alive else None,
        "error_counts": error_counts,
        "physical_completions": physical,
        "saved_report_samples": reports,
        "nonzero_report_observed": nonzero_report,
        "framebuffer_changed_after_hid": framebuffer_changed,
        "tile_samples": tiles,
        "localized_scanout_change": localized_change,
        "physical_cursor_movement_observed": False,
        "cursor_evidence_limit": (
            "A localized framebuffer change is supporting evidence only; "
            "the final physical cursor criterion requires direct display observation."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        text = args.log.read_text(errors="replace")
    except OSError as exc:
        parser.error(str(exc))
    result = classify(text, args.log)
    encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        temp = args.output.with_suffix(args.output.suffix + ".tmp")
        temp.write_text(encoded)
        temp.replace(args.output)
    else:
        print(encoded, end="")
    if result["verdict"] == "GUEST_ERROR":
        return 1
    if result["verdict"] == "NOT_ARMED":
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
