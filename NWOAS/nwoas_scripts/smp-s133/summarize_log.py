#!/usr/bin/env python3
"""Summarize one S133 hardware log without touching the running target."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


FATAL = re.compile(r"WHEA|BugCheck|SError|panic|Traceback|CPU exit", re.I)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=pathlib.Path)
    args = parser.parse_args()
    text = args.log.read_text(errors="replace")

    entered = {
        cpu: len(re.findall(rf"Entering guest secondary {cpu}\b", text))
        for cpu in range(1, 8)
    }
    nvme_activity = {
        cpu: len(re.findall(rf"\[cpu{cpu}\] \[S(?:93|96|97|130)\]", text))
        for cpu in range(8)
    }
    gated = len(re.findall(r"NWOAS-S133 gate PMGR AP start", text))
    alive = [int(v) for v in re.findall(r"\[evtdump\] alive t=(\d+)s", text)]
    stats = re.findall(
        r"\[S97\] STAT iodb=(\d+) traps=(\d+) "
        r"host_ms/db=([0-9.]+) guest_ms/db=([0-9.]+)",
        text,
    )
    fatal = sorted(set(m.group(0) for m in FATAL.finditer(text)))

    print(f"log={args.log}")
    print(f"pmgr_gated={gated}")
    print("secondary_entries=" + ",".join(f"cpu{k}:{v}" for k, v in entered.items()))
    print("nvme_activity=" + ",".join(f"cpu{k}:{v}" for k, v in nvme_activity.items()))
    print(f"last_heartbeat_s={max(alive) if alive else 0}")
    if stats:
        iodb, traps, host_ms, guest_ms = stats[-1]
        print(
            f"last_nvme=iodb:{iodb},traps:{traps},"
            f"host_ms_per_db:{host_ms},guest_ms_per_db:{guest_ms}"
        )
    else:
        print("last_nvme=absent")
    print("fatal_markers=" + (",".join(fatal) if fatal else "none"))

    good_entries = all(count == 1 for count in entered.values())
    all_cpus_active = all(count > 0 for count in nvme_activity.values())
    return 0 if gated == 7 and good_entries and all_cpus_active and not fatal else 1


if __name__ == "__main__":
    sys.exit(main())
