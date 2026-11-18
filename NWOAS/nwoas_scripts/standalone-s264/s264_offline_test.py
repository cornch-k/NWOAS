#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264 offline equivalence test (build host only, no Mac mini).

Runs the real Python launcher path (s264_pyref.run_python_offline) and the host build of the
C planner (host/s264_plan_host, same nwoas_s264_plan.c as the hypervisor) on the same inputs:
  ADT  = nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin (J274, 13.5 iBoot)
  boot args / heap / carveouts = values printed by the S259 launcher log of 2026-11-19 01:00
and compares the launch records.  Also compares the Wi-Fi control page register values and the
PMGR hook addresses with that log.  Exit 0 = every item SAME.

usage: s264_offline_test.py [--workdir DIR] [--keep]
"""
import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import s264_common as C  # noqa: E402
import s264_pyref as R  # noqa: E402

ROOT = HERE.parents[1]
SRC = C.SRC   # $S264_SRC or m1n1-standalone-s264/src
ADT = ROOT / "nwoas_scripts" / "agx-g1-s246" / "evidence" / "g0-adt-20261116-live1" / "adt.bin"
ADT_SHA = "32e63706ee8816b9a731324340ea2206c683ce441c7c2beda09ab20c4e285c23"

# nwoas_scripts/logs/agxmed1g-s249-20261119-010040.QNzidN (S259 launcher, real hardware):
#   phys_base 0x83c4b4000 = heapblock_alloc(0) + 128 MiB + 768 MiB; carveouts lines 400-402;
#   HV boot args lines 145 (preflight facts), MIDR line 16; payload 0x201c000 line 403.
BA = dict(version=2, virt_base=0x19224000, phys_base=0x801224000, mem_size=0x3dfddc000,
          top_of_kernel_data=0x803060000,
          video=dict(base=0xbe3f60000, display=1, stride=7680, width=1920, height=1080, depth=32),
          machine_type=0, devtree=0x1a7cc000, devtree_size=0x58000, cmdline="", boot_flags=0,
          mem_size_actual=0x400000000)
HEAPBLOCK_TOP = 0x83c4b4000 - (896 << 20)
CARVEOUTS = [(0xbe1008000, 0xbe2c60000 - 0xbe1008000), (0xbe1000000, 0x8000),
             (0xbe6e20000, 0xbffe20000 - 0xbe6e20000)]
PAYLOAD = 33669120
MIDR, AMX, ACTLR = 0x611f0221, 0x1, 0x40   # amx/actlr: arbitrary non-zero to test the RMW
LOG_PMGR = {0x23b7001c0, 0x23b7001c8, 0x23b700220, 0x23b700270, 0x23b700420, 0x23d280088, 0x23d280098}
LOG_LAYOUT = dict(phys_base=0x83c4b4000, guest_base=0x83d564000, bootargs_base=0x83fb5c000,
                  entry=0x83d564800, low_backing=0xae0fcc000, mem_size=0x3a4b4c000,
                  mem_size_patched=0x2a4b4c000, image_size=0x25fc000)


def build_host_tool(work):
    exe = work / "s264_plan_host"
    cmd = ["clang", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter", "-DS264_HOST",
           f"-I{SRC}", str(HERE / "host" / "s264_plan_host.c"), str(SRC / "nwoas_s264_plan.c"), "-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or r.stderr.strip():
        print(r.stderr)
        raise SystemExit("host tool build failed or warned")
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workdir", type=Path)
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    adt = ADT.read_bytes()
    assert hashlib.sha256(adt).hexdigest() == ADT_SHA, "ADT dump changed"
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264-offline-"))
    work.mkdir(parents=True, exist_ok=True)
    try:
        exe = build_host_tool(work)
        prec, hv, wifi, ba_raw = R.run_python_offline(adt, 2, BA, HEAPBLOCK_TOP, CARVEOUTS, PAYLOAD,
                                                      MIDR, AMX, ACTLR)
        (work / "ba.bin").write_bytes(ba_raw + bytes(1152 - len(ba_raw)))
        args = [str(exe), "--adt", str(ADT), "--ba", str(work / "ba.bin"), "--ba-rev", "2",
                "--heapblock-top", hex(HEAPBLOCK_TOP), "--iodev", "3", "--midr", hex(MIDR),
                "--amx", hex(AMX), "--actlr", hex(ACTLR), "--payload-size", str(PAYLOAD),
                "--out", str(work / "c-result.bin"), "--wifi-dump", str(work / "c-wifi.bin")]
        for b, n in CARVEOUTS:
            args += ["--carveout", f"{b:#x}:{n:#x}"]
        r = subprocess.run(args, capture_output=True, text=True)
        print(r.stdout.strip())
        if r.returncode:
            print(r.stderr)
            raise SystemExit(f"host planner failed rc={r.returncode}")
        res = C.parse_result((work / "c-result.bin").read_bytes())
        crec = C.c_record(res)
        L = res["layout"]
        heap = (L["py_heap_base"], L["py_heap_top"])
        ok, _, rows = C.compare(crec, prec, heap)
        # Wi-Fi page
        cw = struct.unpack("<2048I", (work / "c-wifi.bin").read_bytes())
        pw = tuple(wifi.reg_read(off) & 0xffffffff for off in range(0, 0x2000, 4))
        rows.append(("Wi-Fi control page 0x0-0x1ffc (NWOAS_WIFI=0)", "SAME" if cw == pw else "DIFF",
                     "" if cw == pw else str([(hex(i * 4), hex(x), hex(y)) for i, (x, y) in enumerate(zip(cw, pw)) if x != y][:4])))
        ok &= cw == pw
        pm = set(res["pmgr"])
        rows.append(("PMGR hook addresses vs S259 log", "SAME" if pm == LOG_PMGR else "DIFF",
                     ", ".join(hex(x) for x in sorted(pm))))
        ok &= pm == LOG_PMGR
        bad = {k: (hex(L[k]), hex(v)) for k, v in LOG_LAYOUT.items() if L[k] != v}
        rows.append(("layout vs S259 log (phys_base, guest_base, bootargs, entry, low_backing, mem_size)",
                     "SAME" if not bad else "DIFF", str(bad) if bad else f"{len(LOG_LAYOUT)} values"))
        ok &= not bad
        # Python hv object agrees with its own record
        rows.append(("Python hv.entry/guest_base", "SAME" if (hv.entry, hv.guest_base) == (L["entry"], L["guest_base"]) else "DIFF",
                     f"{hv.entry:#x} {hv.guest_base:#x}"))
        print("| item | result | detail |\n|---|---|---|")
        for name, res_, det in rows:
            print(f"| {name} | {res_} | {det} |")
        sha = hashlib.sha256((work / "c-result.bin").read_bytes()).hexdigest()
        print(f"C result blob sha256 {sha} ({(work / 'c-result.bin').stat().st_size} B), "
              f"ADT {len(crec['adt'])} B sha256 {hashlib.sha256(crec['adt']).hexdigest()}")
        # negative controls: each mutation of the Python record must be reported as DIFF
        import copy
        muts = {
            "ADT byte": lambda r: r.__setitem__("adt", r["adt"][:100] + bytes([r["adt"][100] ^ 1]) + r["adt"][101:]),
            "boot args mem_size patch": lambda r: r["w64"].__setitem__(
                next(i for i, w in enumerate(r["w64"]) if w[0] == r["ba_base"] + 24),
                (r["ba_base"] + 24, 0x2a4b48000)),
            "one PMGR hook dropped": lambda r: r["maps"].remove(next(m for m in r["maps"] if m[0] == "map" and m[1] == 0x23b700220)),
            "HACR value": lambda r: r["msr"].__setitem__(C.sysreg_enc((3, 4, 1, 1, 7)), 0x317000000004000),
            "BYPASS range as HW": lambda r: r["maps"].append(("map", 0x6e0000000, 0x6e0000000 | 0x7fd, 0x900000, 1)),
        }
        neg_ok = True
        for name, fn in muts.items():
            m = copy.deepcopy(prec)
            fn(m)
            mok, _, _ = C.compare(crec, m, heap)
            neg_ok &= not mok
            print(f"negative control '{name}': {'detected' if not mok else 'NOT DETECTED'}")
        ok &= neg_ok
        print("OFFLINE EQUIVALENCE:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        if not a.keep and not a.workdir:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
