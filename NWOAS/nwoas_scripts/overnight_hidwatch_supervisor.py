#!/usr/bin/env python3
"""Read-only supervisor for the live D59 serial log.

It never opens the serial device and never controls the target.  It only writes
an atomic JSON summary beside the logs so an overnight physical HID completion
or a fatal guest error remains easy to audit after the interactive session.
"""

import json
import os
from pathlib import Path
import sys
import time


def process_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except (ProcessLookupError, PermissionError):
        return False


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} LOG RUNNER_PID", file=sys.stderr)
        return 2
    log = Path(sys.argv[1]).resolve()
    runner_pid = int(sys.argv[2])
    state_path = log.parent / "overnight-hidwatch-state.json"
    evidence_path = log.parent / "overnight-hidwatch-evidence.txt"

    while True:
        try:
            text = log.read_text(errors="replace")
        except FileNotFoundError:
            text = ""
        alive = process_alive(runner_pid)
        hidact_lines = [line for line in text.splitlines() if "HVLOG: HIDACT" in line][-32:]
        whea_count = text.count("HVLOG: WHEA")
        cper_count = text.count("HVLOG: CPER")
        bugcheck_count = text.count("BUGCHECK")
        guest_exception_count = text.count("Guest exception") + text.count("Exception taken")
        physical_hid = "HIDACT PHYSICAL-REPORT" in text
        boot_ready = "HVLOG: FBMASK END" in text and "hid_watch=1" in text
        if whea_count or cper_count or bugcheck_count or guest_exception_count:
            verdict = "GUEST_ERROR"
        elif physical_hid:
            verdict = "PHYSICAL_HID_PASS"
        elif boot_ready:
            verdict = "WAITING_FOR_PHYSICAL_INPUT"
        else:
            verdict = "BOOTING"

        state = {
            "updated_epoch": int(time.time()),
            "log": str(log),
            "runner_pid": runner_pid,
            "runner_alive": alive,
            "verdict": verdict,
            "setup_framebuffer_proved": "HVLOG: FBMASK END" in text,
            "hid_watch_armed": "hid_watch=1" in text,
            "physical_hid_report": physical_hid,
            "framebuffer_changed_after_hid": "HIDACT FB" in text and "changed=1" in text,
            "whea_count": whea_count,
            "cper_count": cper_count,
            "bugcheck_count": bugcheck_count,
            "guest_exception_count": guest_exception_count,
            "last_alive_seconds": None,
            "hidact_lines": hidact_lines,
        }
        for line in reversed(text.splitlines()):
            if "[evtdump] alive t=" in line:
                try:
                    state["last_alive_seconds"] = int(line.split("alive t=", 1)[1].split("s", 1)[0])
                except ValueError:
                    pass
                break
        temp = state_path.with_suffix(".json.tmp")
        temp.write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n")
        os.replace(temp, state_path)
        if hidact_lines:
            evidence = [
                "NWOAS overnight physical HID evidence",
                f"source_log={log}",
                f"verdict={verdict}",
                "",
                *hidact_lines,
                "",
            ]
            evtemp = evidence_path.with_suffix(".txt.tmp")
            evtemp.write_text("\n".join(evidence))
            os.replace(evtemp, evidence_path)
        if not alive:
            return 0
        time.sleep(30)


if __name__ == "__main__":
    raise SystemExit(main())
