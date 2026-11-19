#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264C offline test (build host only, no Mac mini).

1. The s264B equivalence test (s264_offline_test.run_case, both ADT input states) on the s264C
   source with the s264C default config (flags 0x2ff: + EXC_LOCAL + NVME_QD): every s264B item
   must stay SAME (the new actions are STEP ops and C trap handlers only).
2. New actions against the real Python code:
   a. NVME_QD: run the real nvmeqd-s262/nvmeqd_module.py with the S268 launcher environment
      (NWOAS_NVME_QD=32, the other NWOAS_NVME_QD_* unset) on a fake proxy; its
      p.nwoas_nvme_fastpath(24, depth, flags) must equal the C STEP_NVME_QD (a, b); the step must be
      after WIFI and before TLBI (nvmeqd runs last of the -m modules, before hv.start); absent when
      the flag is off.
   b. Exception policy: STEP_ARM_TRAPS a = 1 by default, 3 with EXC_FATAL, 0 with neither.
   c. BRK reflection: s264_lower_calc (host build of the same C) against the real
      proxyclient HV._lower (hv/__init__.py:1571-1600) for every SPSR.M and DAIF/NZCV pattern.
   d. Negative controls: a wrong QD depth, a wrong QD flag, a wrong lowered vector and a wrong
      lowered SPSR must each be reported.
usage: s264c_offline_test.py [--workdir DIR] [--keep]
"""
import argparse
import os
import runpy
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import s264_common as C  # noqa: E402
import s264_offline_test as T  # noqa: E402
import s264c as X  # noqa: E402

ROOT = HERE.parents[1]
NVMEQD = ROOT / "nwoas_scripts" / "nvmeqd-s262" / "nvmeqd_module.py"


def python_nvmeqd(workdir):
    """Execute the real module; return the (depth, flags) of its action-24 request."""
    calls = []

    class FakeP:
        def nwoas_nvme_fastpath(self, action, *a):
            calls.append((action,) + a)
            if action == 26:
                return (0x53323632 << 32) | (3 << 24)          # g262R signature, version 3
            if action == 24:   # engine ON answer: bit 63, irq 590, cell 4 of 5, depth, flags
                return (1 << 63) | (590 << 32) | (4 << 24) | (5 << 16) | (a[0] << 8) | (a[1] & 0xff)
            if action == 25:
                return 8 * 57
            return 0

    class FakeU:
        def malloc(self, n):
            return 0x10000

    class FakeIface:
        def readmem(self, addr, n):
            return bytes(n)

    class FakeHv:
        def log(self, s):
            pass

    pwr_ns = {"_pwr_mark": lambda label, job: None}
    exec("def _pwr_receive(data):\n    return _pwr_mark('x', 0)\n", pwr_ns)
    env_keep = dict(os.environ)
    try:
        for k in list(os.environ):
            if k.startswith("NWOAS_NVME_QD"):
                del os.environ[k]
        os.environ["NWOAS_NVME_QD"] = "32"                 # agxdcp-s259/s268 launcher default
        os.environ["NWOAS_LINK_DIR"] = str(workdir)
        runpy.run_path(str(NVMEQD), init_globals=dict(p=FakeP(), u=FakeU(), iface=FakeIface(),
                                                      hv=FakeHv(), _pwr_receive=pwr_ns["_pwr_receive"]))
    finally:
        os.environ.clear()
        os.environ.update(env_keep)
    req = [c for c in calls if c[0] == 24]
    assert len(req) == 1, calls
    return req[0][1], req[0][2]


def c_plan(exe, work, extra):
    adt = T.ADT.read_bytes()
    work.mkdir(parents=True, exist_ok=True)
    (work / "adt-in.bin").write_bytes(adt)
    _, _, _, ba_raw = T.R.run_python_offline(adt, 2, T.BA, T.HEAPBLOCK_TOP, T.CARVEOUTS, T.PAYLOAD,
                                             T.MIDR, T.AMX, T.ACTLR)
    (work / "ba.bin").write_bytes(ba_raw + bytes(1152 - len(ba_raw)))
    args = [str(exe), "--adt", str(work / "adt-in.bin"), "--ba", str(work / "ba.bin"), "--ba-rev", "2",
            "--heapblock-top", hex(T.HEAPBLOCK_TOP), "--iodev", "3", "--midr", hex(T.MIDR),
            "--amx", hex(T.AMX), "--actlr", hex(T.ACTLR), "--payload-size", str(T.PAYLOAD),
            "--out", str(work / "c-result.bin"), "--wifi-dump", str(work / "c-wifi.bin")] + extra
    for b, n in T.CARVEOUTS:
        args += ["--carveout", f"{b:#x}:{n:#x}"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"host planner failed rc={r.returncode}: {r.stderr}")
    return C.parse_result((work / "c-result.bin").read_bytes())


def python_lower(spsr, vbar, code):
    """The real HV._lower on a minimal fake self (only what it touches)."""
    from m1n1.hv import HV
    from m1n1.sysreg import SPSR, ESR

    class Ctx:
        pass

    class U:
        def __init__(self):
            self.msrs = []

        def msr(self, reg, val):
            self.msrs.append((reg, val))

        def mrs(self, reg):
            return vbar

    self = type("FakeHV", (), {})()
    self.is_fault = True
    self.exc_code = code
    self.u = U()
    self.ctx = Ctx()
    self.ctx.elr, self.ctx.far = 0x1234, 0x5678
    self.ctx.spsr = SPSR(spsr)
    self.ctx.esr = ESR(0xf2000000)
    ok = HV._lower(self)
    if not ok:
        return None
    return self.ctx.spsr.value, self.ctx.elr


def c_lower(exe, spsr, vbar, code):
    out = subprocess.run([str(exe), "--lower", hex(spsr), hex(vbar), str(code)],
                         capture_output=True, text=True).stdout.split()
    if out[0] == "refused":
        return None
    return int(out[1], 16), int(out[2], 16)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workdir", type=Path)
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264c-offline-"))
    work.mkdir(parents=True, exist_ok=True)
    rows = []
    try:
        exe = T.build_host_tool(work)
        adt = T.ADT.read_bytes()
        ok = True
        # 1. s264B items unchanged with the s264C default config
        for label, data in (("iboot", adt), ("chainload", T.chainload_state(adt))):
            ok &= T.run_case(label, data, exe, work / label)
        rows.append(("s264B equivalence items with the s264C default config (2 ADT states)",
                     "SAME" if ok else "DIFF", "see the per-state tables above"))
        # 2a. NVME_QD
        pdepth, pflags = python_nvmeqd(work)
        res = c_plan(exe, work / "c-default", [])
        st = X.steps_of(res)
        ids = [s[0] for s in st]
        qd = [s for s in st if s[0] == X.STEP_NVME_QD]
        good = (len(qd) == 1 and (qd[0][1], qd[0][2]) == (pdepth, pflags)
                and ids.index(X.STEP_WIFI) < ids.index(X.STEP_NVME_QD) < ids.index(X.STEP_TLBI))
        rows.append(("NVME_QD step vs real nvmeqd_module.py (S268 env) + order WIFI < QD < TLBI",
                     "SAME" if good else "DIFF",
                     f"Python 24({pdepth}, {pflags:#x}) / C {qd} order {[C.STEP_NAMES.get(i, i) for i in ids]}"))
        ok &= good
        cfg_c = res.get("cfg") if isinstance(res, dict) else None
        good = cfg_c is not None and bytes(cfg_c) == X.pack_config_c(payload_size=T.PAYLOAD)
        rows.append(("host pack_config_c == C s264_default_config bytes (what the launch sends)",
                     "SAME" if good else "DIFF", f"{len(cfg_c) if cfg_c is not None else '-'} B"))
        ok &= good
        res_off = c_plan(exe, work / "c-noqd", ["--flags", hex(X.CFG_F_DEFAULT_C & ~X.CFG_F_NVME_QD)])
        noqd = not any(s[0] == X.STEP_NVME_QD for s in X.steps_of(res_off))
        rows.append(("NVME_QD absent when CFG_F_NVME_QD is off", "SAME" if noqd else "DIFF", ""))
        ok &= noqd
        # 2b. policy argument
        pol = {}
        for name, fl in (("default", X.CFG_F_DEFAULT_C), ("fatal", X.CFG_F_DEFAULT_C | X.CFG_F_EXC_FATAL),
                         ("none", X.CFG_F_DEFAULT_C & ~X.CFG_F_EXC_LOCAL)):
            r = c_plan(exe, work / f"c-pol-{name}", ["--flags", hex(fl)])
            pol[name] = [s[1] for s in X.steps_of(r) if s[0] == X.STEP_ARM_TRAPS]
        good = pol == {"default": [1], "fatal": [3], "none": [0]}
        rows.append(("ARM_TRAPS policy argument (default / +fatal / none)", "SAME" if good else "DIFF", str(pol)))
        ok &= good
        # 2c. BRK reflection vs HV._lower
        nz, n_same, bad = 0, 0, []
        vbar = 0xfffff80012340000
        for m in (0x0, 0x4, 0x5, 0x8, 0x9):   # every SPSR_M the Python enum accepts (sysreg.py:178-183)
            for hi in (0x0, 0x3c0, 0x200, 0xf0000000, 0x400000 | 0x80):
                for code in (0, 1, 2, 3):
                    sp = hi | m
                    pr, cr = python_lower(sp, vbar, code), c_lower(exe, sp, vbar, code)
                    nz += 1
                    if pr == cr:
                        n_same += 1
                    else:
                        bad.append((hex(sp), code, pr, cr))
        good = not bad
        rows.append(("BRK reflection s264_lower_calc vs HV._lower (5 modes x 5 bit patterns x 4 codes)",
                     "SAME" if good else "DIFF", f"{n_same}/{nz} same" + (f", first {bad[:3]}" if bad else "")))
        ok &= good
        # 2d. negative controls
        neg = {
            "QD depth 31": not ((31, pflags) == (qd[0][1], qd[0][2])),
            "QD flags 0x3": not ((pdepth, 0x3) == (qd[0][1], qd[0][2])),
            "lowered vector +0x80": python_lower(0x5, vbar, 0)[1] + 0x80 != c_lower(exe, 0x5, vbar, 0)[1],
            "lowered SPSR without F": (python_lower(0x5, vbar, 0)[0] & ~0x40) != c_lower(exe, 0x5, vbar, 0)[0],
        }
        for k, v in neg.items():
            print(f"[s264C] negative control '{k}': {'detected' if v else 'NOT DETECTED'}")
        ok &= all(neg.values())
        print("| item | result | detail |\n|---|---|---|")
        for r in rows:
            print(f"| {r[0]} | {r[1]} | {r[2]} |")
        print(f"[s264C] {sum(1 for r in rows if r[1] == 'SAME')}/{len(rows)} SAME")
        print("S264C OFFLINE:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        if not a.keep and not a.workdir:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
