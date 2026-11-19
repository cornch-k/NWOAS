#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264F: the C power step (src/nwoas_s264_pwr.c + the planner's S264_STEP_PWR) against the REAL
power-s260/power_module.py, build host only.  No hardware; passing is not a hardware result.

For every NWOAS_PWR_* environment case the real module runs on a fake proxy that answers like the
S260 HV (nwoas_pwr.c); its P_HV_NWOAS_PWR requests (op, arguments), memory writes and the CONFIG
arguments are recorded.  The CONFIG arguments (+ NWOAS_PWR_SLC) are encoded into the s264F config
(s264f.pwr_field), the host planner must give the same STEP_PWR arguments, and the C run with those
arguments must issue the same request sequence.  Environments Python refuses (assert) must be plan
errors.  Not compared (not ported): the NWSTAT-MARK / NWSTAT-SET job-output hook and hvstats.jsonl.
usage: s264f_pwr_test.py [--src DIR] [--workdir DIR]
"""
import argparse
import ctypes as ct
import os
import runpy
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import s264_common as C  # noqa: E402
import s264d as D  # noqa: E402
import s264f as F  # noqa: E402
import s264c_offline_test as TC  # noqa: E402

ROOT = HERE.parents[1]
PWR_DIR = ROOT / "nwoas_scripts" / "power-s260"
sys.path.insert(0, str(PWR_DIR))
import nwoas_pwr_layout as PL  # noqa: E402

BUF = 0x9F000000
CASES = [("default (S268: nothing set)", {}),
         ("WFI", {"NWOAS_PWR_WFI": "1"}),
         ("SECTICK 100", {"NWOAS_PWR_SECTICK": "100"}),
         ("TGAP_US 0", {"NWOAS_PWR_TGAP_US": "0"}),
         ("TGAP_US 500", {"NWOAS_PWR_TGAP_US": "500"}),
         ("NVME_SHUT", {"NWOAS_PWR_NVME_SHUT": "1"}),
         ("OFF", {"NWOAS_PWR_OFF": "1"}),
         ("SMC sensors", {"NWOAS_PWR_SMC": "1"}),
         ("PRINT 60", {"NWOAS_PWR_PRINT": "60"}),
         ("SLC", {"NWOAS_PWR_SLC": "1"}),
         ("all together", {"NWOAS_PWR_WFI": "1", "NWOAS_PWR_SECTICK": "250", "NWOAS_PWR_TGAP_US": "2000",
                           "NWOAS_PWR_NVME_SHUT": "1", "NWOAS_PWR_OFF": "1", "NWOAS_PWR_SMC": "1",
                           "NWOAS_PWR_PRINT": "30", "NWOAS_PWR_SLC": "1"})]
BAD = [("SECTICK 5 (Python asserts)", {"NWOAS_PWR_SECTICK": "5"}, F.pwr_field(2, 5)),
       ("TGAP_US 70000", {"NWOAS_PWR_TGAP_US": "70000"}, F.pwr_field(4, 0, 70000)),
       ("PRINT 4000", {"NWOAS_PWR_PRINT": "4000"}, F.pwr_field(0x40, 0, 0, 4000))]


class Machine:
    """What the S260 HV answers (nwoas_pwr.c nwoas_pwr_proxy) + memory."""

    def __init__(self, trace):
        self.t, self.mem = trace, {}

    def pwr(self, op, a0, a1, a2, a3):
        self.t.append(("req", op, a0, a1, a2, a3))
        if op == PL.OP_QUERY:
            return (PL.MAGIC << 32) | PL.VERSION
        if op == PL.OP_CONFIG:
            return (1 << 32) | a0 if not a0 & ~PL.F_ALL else 0
        if op == PL.OP_MCC_STAT:
            self.write(a0, struct.pack("<4I", 0x1, 0x3, 0x1, 0x3))
            return 4
        if op == PL.OP_SLC:
            return 0
        if op == PL.OP_SNAPSHOT:
            blob = bytearray(PL.SIZE)
            struct.pack_into("<IIII", blob, 0, PL.MAGIC, PL.VERSION, PL.SIZE, PL.NCPU)
            self.write(a0, bytes(blob))
            return PL.SIZE
        if op == PL.OP_SMC_READ:
            self.write(a2, b"".join(struct.pack("<Q", 0x42c80000) for _ in range(a1)))
            return a1
        return (1 << 64) - 1

    def write(self, a, data):
        for i, x in enumerate(data):
            self.mem[a + i] = x

    def read(self, a, n):
        return bytes(self.mem.get(a + i, 0) for i in range(n))


def run_python(env, work):
    trace = []
    m = Machine(trace)

    class P:
        def request(self, cmd, op, a0=0, a1=0, a2=0, a3=0):
            assert cmd == PL.P_HV_NWOAS_PWR
            return m.pwr(op, a0, a1, a2, a3)

    class U:
        def malloc(self, n):
            return BUF

    class I:
        def readmem(self, a, n):
            return m.read(a, n)

        def writemem(self, a, data):
            trace.append(("mw", a, bytes(data)))
            m.write(a, data)

    class Link:
        ack = 0

        def receive(self, data):
            return True

    class Hv:
        _nwoas_link = Link()

        def log(self, s):
            pass

    keep = dict(os.environ)
    try:
        for k in list(os.environ):
            if k.startswith("NWOAS_PWR_"):
                del os.environ[k]
        os.environ.update(env)
        os.environ["NWOAS_LINK_DIR"] = str(work)
        runpy.run_path(str(PWR_DIR / "power_module.py"), init_globals=dict(p=P(), u=U(), iface=I(), hv=Hv()))
        return trace, None
    except AssertionError as e:
        return trace, f"assert {e}"
    finally:
        os.environ.clear()
        os.environ.update(keep)


RQ = ct.CFUNCTYPE(ct.c_uint64, ct.c_uint64, ct.c_uint64, ct.c_uint64, ct.c_uint64, ct.c_uint64)
MW = ct.CFUNCTYPE(None, ct.c_uint64, ct.c_void_p, ct.c_uint32)
LG = ct.CFUNCTYPE(None, ct.c_char_p)


class Io(ct.Structure):
    _fields_ = [("pwr", RQ), ("mem_write", MW), ("mem_read", MW), ("log", LG)]


def build_lib(src, work):
    lib = work / "libs264pwr.dylib"
    cmd = ["nice", "-n", "19", "clang", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-DS264_HOST", "-shared", "-fPIC", f"-I{src}", str(src / "nwoas_s264_pwr.c"), "-o", str(lib)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or r.stderr.strip():
        print(r.stderr)
        raise SystemExit("host power library build failed or warned")
    L = ct.CDLL(str(lib))
    L.s264_pwr_run.argtypes = [ct.POINTER(Io), ct.c_uint64, ct.c_uint32, ct.c_uint32, ct.c_uint32, ct.c_uint32,
                               ct.c_bool]
    return L


def run_c(L, step):
    trace = []
    m = Machine(trace)

    def mw(a, p, n):
        data = ct.string_at(p, n)
        trace.append(("mw", a, data))
        m.write(a, data)

    def mr(a, p, n):
        ct.memmove(p, m.read(a, n), n)

    io = Io(RQ(m.pwr), MW(mw), MW(mr), LG(lambda s: None))
    _, fl, hz, gap, d = step
    rc = L.s264_pwr_run(ct.byref(io), BUF, fl, hz, gap, d & 0xffffffff, bool(d >> 32 & 1))
    return trace, rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, default=Path(os.environ.get(
        "S264_SRC", ROOT / "m1n1-standalone-s264" / "src")))
    ap.add_argument("--workdir", type=Path)
    a = ap.parse_args()
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264f-pwr-"))
    work.mkdir(parents=True, exist_ok=True)
    L = build_lib(a.src, work)
    exe = TC.T.build_host_tool(work)
    rows, ok = [], True
    for name, env in CASES:
        pt, perr = run_python(env, work)
        cfgs = [e for e in pt if e[0] == "req" and e[1] == PL.OP_CONFIG]
        if perr or len(cfgs) != 1:
            rows.append((f"pwr: {name}", "DIFF", f"Python {perr} configs {len(cfgs)}"))
            ok = False
            continue
        _, _, fl, hz, gap, prs = cfgs[0]
        slc = env.get("NWOAS_PWR_SLC") == "1"
        res = TC.c_plan(exe, work / ("p-" + str(len(rows))), ["--pwr", hex(F.pwr_field(fl, hz, gap, prs, slc))])
        st = [s for s in D.steps_full(res) if s[0] == F.STEP_PWR]
        ids = [s[0] for s in D.steps_full(res)]
        order_ok = len(st) == 1 and ids.index(D.STEP_WIFI) < ids.index(F.STEP_PWR) < ids.index(17)
        step_ok = order_ok and st[0][1:] == (fl, hz, gap, prs | (int(slc) << 32))
        ct_, rc = run_c(L, st[0]) if st else ([], -1)
        same = res["err"] == 0 and step_ok and rc == 0 and ct_ == pt
        first = next((i for i, (x, y) in enumerate(zip(pt, ct_)) if x != y), min(len(pt), len(ct_)))
        rows.append((f"pwr: {name}", "SAME" if same else "DIFF",
                     f"CONFIG({fl:#x}, {hz}, {gap}, {prs}) slc={int(slc)}; step {st[0][1:] if st else '-'} "
                     f"order WIFI < PWR < NVME_QD {order_ok}; {len(pt)} requests/writes"
                     + ("" if ct_ == pt else f"; first difference #{first}: Python "
                        f"{pt[first] if first < len(pt) else '-'} / C {ct_[first] if first < len(ct_) else '-'}")))
        ok &= same
    for name, env, field in BAD:
        _, perr = run_python(env, work)
        try:
            res = TC.c_plan(exe, work / ("b-" + str(len(rows))), ["--pwr", hex(field)])
        except SystemExit:
            res = C.parse_result((work / ("b-" + str(len(rows))) / "c-result.bin").read_bytes())
        good = bool(perr) and (res["err"], res["err_arg"]) == (1, 0xb0)
        rows.append((f"pwr refused: {name}", "SAME" if good else "DIFF",
                     f"Python {perr} / C err {res['err']}/{res['err_arg']:#x}"))
        ok &= good
    res = TC.c_plan(exe, work / "p-off", ["--flags", hex(F.CFG_F_DEFAULT_F & ~F.CFG_F_PWR)])
    good = res["err"] == 0 and not [s for s in D.steps_full(res) if s[0] == F.STEP_PWR]
    rows.append(("pwr step absent with CFG_F_PWR off", "SAME" if good else "DIFF", ""))
    ok &= good
    pt, _ = run_python({}, work)
    ct_, _ = run_c(L, (19, 0, 0, 0, 0))
    neg = ct_[:-1] + [ct_[-1][:-1] + (ct_[-1][-1] + 1,)] != pt
    rows.append(("negative control: one request argument changed", "SAME" if neg else "DIFF",
                 "detected" if neg else "NOT detected"))
    ok &= neg
    print("| item | result | detail |\n|---|---|---|")
    for r in rows:
        print(f"| {r[0]} | {r[1]} | {r[2]} |")
    print(f"[s264F-pwr] {sum(1 for r in rows if r[1] == 'SAME')}/{len(rows)} SAME")
    print("S264F PWR:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
