#!/usr/bin/env python3
"""Persist coarse D59 supervisor milestones without touching the target.

The live supervisor atomically replaces one JSON state file.  This companion
copies only meaningful transitions and elapsed-time milestones to JSONL so an
overnight run retains an audit trail even if the interactive shell disconnects.
It never opens the serial device or signals the guest runner.
"""

import json
from pathlib import Path
import sys
import time


ERROR_FIELDS = ("whea_count", "cper_count", "bugcheck_count", "guest_exception_count")


def read_state(path: Path) -> dict | None:
    try:
        value = json.loads(path.read_text())
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return None
    return value if isinstance(value, dict) else None


def append_record(path: Path, state: dict, reason: str) -> None:
    record = {
        "recorded_epoch": int(time.time()),
        "reason": reason,
        "last_alive_seconds": state.get("last_alive_seconds"),
        "runner_alive": state.get("runner_alive"),
        "verdict": state.get("verdict"),
        "setup_framebuffer_proved": state.get("setup_framebuffer_proved"),
        "hid_watch_armed": state.get("hid_watch_armed"),
        "physical_hid_report": state.get("physical_hid_report"),
        "framebuffer_changed_after_hid": state.get("framebuffer_changed_after_hid"),
        **{name: state.get(name) for name in ERROR_FIELDS},
    }
    with path.open("a") as stream:
        stream.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")
        stream.flush()


def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(f"usage: {sys.argv[0]} STATE_JSON JOURNAL_JSONL [INTERVAL_SECONDS]", file=sys.stderr)
        return 2
    state_path = Path(sys.argv[1]).resolve()
    journal_path = Path(sys.argv[2]).resolve()
    interval = int(sys.argv[3]) if len(sys.argv) == 4 else 1800
    if interval <= 0:
        print("INTERVAL_SECONDS must be positive", file=sys.stderr)
        return 2

    last_bucket: int | None = None
    last_signature: tuple | None = None
    while True:
        state = read_state(state_path)
        if state is None:
            time.sleep(30)
            continue

        alive_seconds = state.get("last_alive_seconds")
        bucket = alive_seconds // interval if isinstance(alive_seconds, int) else None
        signature = (
            state.get("runner_alive"),
            state.get("verdict"),
            state.get("physical_hid_report"),
            state.get("framebuffer_changed_after_hid"),
            *(state.get(name) for name in ERROR_FIELDS),
        )

        if last_signature is None:
            append_record(journal_path, state, "monitor-start")
        elif signature != last_signature:
            append_record(journal_path, state, "state-transition")
        elif bucket is not None and bucket != last_bucket:
            append_record(journal_path, state, f"elapsed-{bucket * interval}s")

        last_bucket = bucket
        last_signature = signature
        if not state.get("runner_alive", False):
            return 0
        time.sleep(30)


if __name__ == "__main__":
    raise SystemExit(main())
