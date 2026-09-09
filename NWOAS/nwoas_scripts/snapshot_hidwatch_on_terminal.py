#!/usr/bin/env python3
"""Snapshot a live HID-watch log when physical input or a failure appears."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import time

from snapshot_hidwatch_at import ERROR_FIELDS, copy_prefix_atomic, sha256


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("state", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--poll-seconds", type=float, default=5.0)
    args = parser.parse_args()

    if args.poll_seconds <= 0:
        parser.error("poll interval must be positive")
    if args.destination.exists():
        raise SystemExit(f"refusing to overwrite snapshot: {args.destination}")

    while True:
        state = json.loads(args.state.read_text())
        reasons: list[str] = []
        if state.get("physical_hid_report"):
            reasons.append("physical_hid_report")
        for field in ERROR_FIELDS:
            if int(state.get(field, 0)):
                reasons.append(field)
        if not state.get("runner_alive"):
            reasons.append("runner_stopped")

        if reasons:
            source = Path(state["log"])
            byte_count = source.stat().st_size
            args.destination.parent.mkdir(parents=True, exist_ok=True)
            copy_prefix_atomic(source, args.destination, byte_count)
            result = {
                "snapshot": str(args.destination.resolve()),
                "bytes": args.destination.stat().st_size,
                "sha256": sha256(args.destination),
                "observed_alive_seconds": int(state.get("last_alive_seconds", 0)),
                "verdict": state.get("verdict"),
                "reasons": reasons,
            }
            print(json.dumps(result, ensure_ascii=False, sort_keys=True), flush=True)
            return 0
        time.sleep(args.poll_seconds)


if __name__ == "__main__":
    raise SystemExit(main())
