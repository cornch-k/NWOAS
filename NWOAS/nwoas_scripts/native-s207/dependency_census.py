#!/usr/bin/env python3
"""Count observed category log lines in a finished run_guest launcher log.

Pure text analysis. Opens nothing but the log file given on the command line.
Do not point it at a log that is still being written by a live run; the
phase boundaries below assume the run has at least reached the Windows
storage-driver initialisation.

What this tool reports and what it does NOT report
--------------------------------------------------
This is a count of matching log lines per category per phase. It is an
*observed lower bound on the paths that were exercised*, not a census of
round trips and not a proof that any category saw zero traffic. Specifically:

  - A logged line is not necessarily one host round trip. The S133 gate line
    is a nested log emitted from inside a single CPUSTART write trap, so a
    gate line and its CPUSTART-write line describe the same trap. An admin
    CMD line and the controller-register (MMIO) read/write lines around it
    can belong to the same doorbell trap.
  - Quiet-logging caps mean not every proxy event prints a line. A zero in a
    cell means "no matching line was seen", not "this path was never taken".
  - NS2 link requests (HV_NWOAS_NVME_LINK) are not logged per call and do not
    appear in any category here.

Treat the numbers as observed category-line counts. Do not read a zero as a
proof of no callbacks, and do not sum the columns into a round-trip total.

Phase markers (approximate, not exact boundaries)
--------------------------------------------------
Phases are split on markers the S200/S202 launcher family emits. The split is
approximate: a line's phase is decided by which markers have been seen so far
on the same host log, which can differ slightly from the guest's own ordering.

  pre-entry : before "Jumping to entrypoint" (host-only preparation; the
              Python namespace probe reads appear here)
  entry-arm1: guest entry to the first "[S149] target NVMe fastpath armed"
              (guest m1n1 prefix: chicken registers, PMGR reads, CPUSTART;
              then UEFI NvmExpressDxe bring-up with admin depth 2)
  arm1-arm2 : first arm to second arm. On the observed logs UEFI registers the
              NVMe BAR as a non-discoverable device and does not touch ECAM,
              so the PCI config lines here are Windows enumeration plus
              stornvme admin bring-up (depth 256)
  after-arm2: Windows runtime (periodic admin commands, CC/CSTS/ASQ/ACQ reads,
              shutdown flush)
"""
import argparse
import collections
import re
import sys
from pathlib import Path

ENTRY = re.compile(r"Jumping to entrypoint at 0x[0-9a-f]+")
ARMED = re.compile(r"\[S149\] target NVMe fastpath armed SQ=[0-9a-f]+/(\d+)")

CATEGORIES = [
    ("pmgr_read", re.compile(r"PMGR R [0-9a-f]+\+")),
    ("pmgr_write", re.compile(r"PMGR W [0-9a-f]+\+")),
    ("cpustart_write", re.compile(r"CPUSTART W ")),
    ("s133_gate", re.compile(r"NWOAS-S133 gate PMGR AP start")),
    ("cpu_state_read", re.compile(r"CPU STATE R ")),
    ("pci_cfg_read", re.compile(r"\[S130\] PCI R ")),
    ("pci_cfg_write", re.compile(r"\[S130\] PCI W ")),
    ("nvme_reg_read", re.compile(r"\[S130\] MMIO R ")),
    ("nvme_reg_write", re.compile(r"\[S130\] MMIO W ")),
    ("nvme_admin_cmd", re.compile(r"\[S130\] CMD q=0 ")),
    ("nvme_io_cmd_python", re.compile(r"\[S130\] CMD q=[1-9]")),
    ("ans_read_python", re.compile(r"\[S93\] ANS READ ")),
    ("ans_write_python", re.compile(r"\[S96\] ANS WRITE ")),
    ("ans_flush_python", re.compile(r"\[S96\] ANS FLUSH ")),
    ("direct_dma", re.compile(r"\[S101\] DIRECT ")),
    ("guest_exception_shell", re.compile(r"Guest exception: |Entering hypervisor shell")),
    ("python_traceback", re.compile(r"^Traceback \(most recent call last\)")),
]


def census(text):
    phases = collections.OrderedDict(
        (name, collections.Counter()) for name in ("pre-entry", "entry-arm1", "arm1-arm2", "after-arm2")
    )
    phase = "pre-entry"
    arms = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if ENTRY.search(line):
            phase = "entry-arm1"
            phases[phase]["_start_line"] = lineno
            continue
        m = ARMED.search(line)
        if m:
            arms.append((lineno, int(m.group(1))))
            phase = "arm1-arm2" if len(arms) == 1 else "after-arm2"
            phases[phase]["_start_line"] = lineno
            continue
        for name, rx in CATEGORIES:
            if rx.search(line):
                phases[phase][name] += 1
                break
    return phases, arms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path)
    args = ap.parse_args()
    text = args.log.read_text(errors="replace")
    phases, arms = census(text)
    print(f"log={args.log}")
    print("# observed category-line counts; not round trips; a zero is not a proof of no callbacks")
    print("fastpath_arms=" + ",".join(f"line{l}:depth{d}" for l, d in arms))
    names = [n for n, _ in CATEGORIES]
    width = max(len(n) for n in names)
    print(f"{'category (observed lines)':<{width}} " + " ".join(f"{p:>11}" for p in phases))
    for name in names:
        row = [phases[p].get(name, 0) for p in phases]
        if any(row):
            print(f"{name:<{width}} " + " ".join(f"{v:>11}" for v in row))
    if len(arms) < 2:
        print("WARNING: fewer than two fastpath arms; later phases are not established")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
