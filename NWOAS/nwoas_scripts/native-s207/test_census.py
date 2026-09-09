#!/usr/bin/env python3
"""Pure parser tests for dependency_census.census(). No hardware, no files.

Run: python3 nwoas_scripts/native-s207/test_census.py

These tests pin the parser's behaviour on the two cases that most easily
mislead a reader of the counts:

  1. Overlapping category lines. A single log line can match more than one
     category regex, and one guest trap can emit several lines (for example a
     CPUSTART write plus its nested S133 gate line). The parser counts one
     category per line, first match wins, so the caller must not read a
     category count as a round-trip count.
  2. Truncation. A live or interrupted log can end mid-line or before the
     second fastpath arm. The parser must not crash and must signal that the
     later phases are not established.
"""
import sys
from dependency_census import census


def _flat(phases):
    return {(p, k): v for p, ctr in phases.items() for k, v in ctr.items() if not k.startswith("_")}


def test_first_match_wins_on_overlap():
    # A CPUSTART write line and its nested gate line are two lines describing
    # one trap. Each is counted once, in its own category; they are not summed
    # into a claim of two round trips by the tool.
    text = "\n".join([
        "Jumping to entrypoint at 0x800",
        "CPUSTART W 23b754008 = 1",
        "HVLOG: NWOAS-S133 gate PMGR AP start die0 cluster0 core1",
        "[S149] target NVMe fastpath armed SQ=abc/2",
        "[S149] target NVMe fastpath armed SQ=abc/256",
    ])
    phases, arms = census(text)
    flat = _flat(phases)
    assert flat.get(("entry-arm1", "cpustart_write")) == 1, flat
    assert flat.get(("entry-arm1", "s133_gate")) == 1, flat
    assert len(arms) == 2, arms


def test_no_category_double_counts_a_single_line():
    # cpu_state_read and cpustart_write regexes both begin near "CPU"; ensure a
    # single line increments exactly one category (first in CATEGORIES order).
    text = "\n".join([
        "Jumping to entrypoint at 0x800",
        "CPU STATE R 210050000",
    ])
    phases, _ = census(text)
    flat = _flat(phases)
    assert sum(flat.values()) == 1, flat
    assert flat.get(("entry-arm1", "cpu_state_read")) == 1, flat


def test_truncated_before_second_arm():
    # Log cut off after the first arm and mid-line. Must not raise; later
    # phases stay whatever was reached, and the caller checks len(arms) < 2.
    text = "\n".join([
        "Jumping to entrypoint at 0x800",
        "[S149] target NVMe fastpath armed SQ=abc/2",
        "[S130] PCI R 700000000",
        "[S130] MMIO R 700100",   # truncated line, no newline follows
    ])
    phases, arms = census(text)
    flat = _flat(phases)
    assert len(arms) == 1, arms
    assert flat.get(("arm1-arm2", "pci_cfg_read")) == 1, flat
    assert flat.get(("arm1-arm2", "nvme_reg_read")) == 1, flat


def test_empty_and_arms_only():
    phases, arms = census("")
    assert arms == [], arms
    assert _flat(phases) == {}, phases


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
        print(f"ok {t.__name__}")
    print(f"{len(tests)} passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
