#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264D: sound stage B/S in C against the REAL sound-s256/audio_module.py (build host only).

Both sides drive the same register model of the J274 sound hardware (I2C1 + TAS amp at 0x31,
DART-sio, ADMAC, MCA, NCO, PMGR, GPIO181):
  * Python: the whole audio_module.py (stage A, B, S, final header) with the real proxyclient
    drivers (m1n1/hw/i2c, admac, mca, nco, dart, dart8020, pmgr) on a fake p/u/iface/hv;
    p.set32 counts as one read + one write (what the proxy does on the Mac mini).
  * C: src/nwoas_s264_audio.c built for the host (-DS264_HOST) and called through ctypes with
    the same model behind its io callbacks; its context comes from the same ADT dump.
Recorded and compared: every MMIO read (address, value) and write (address, value) in order,
clock enables, dc_civac, sleeps, memory writes (content hash) and reads, the final header bytes
and the returned status.  Cases: normal + 7 failure injections (each must give the same trace).
usage: s264d_audio_test.py [--src DIR] [--workdir DIR]
"""
import argparse
import contextlib
import ctypes as ct
import hashlib
import io
import os
import struct
import subprocess
import sys
import tempfile
import time
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import s264_pyref as R  # noqa: E402  (sets sys.path for the proxyclient)
import s264d as D  # noqa: E402

ROOT = HERE.parents[1]
AUDIO = ROOT / "nwoas_scripts" / "sound-s256" / "audio_module.py"
ADT = ROOT / "nwoas_scripts" / "agx-g1-s246" / "evidence" / "g0-adt-20261116-live1" / "adt.bin"
HEAP_BASE = 0x900000000
HEAP_SIZE = 768 << 20
I2C1, ADMAC, MCA0 = 0x235014000, 0x238200000, 0x238400000
GPIO181 = 0x23c1002d4

PROTS = {"sense": 0, "moderate": 1, "full": 2}
CASES = [("normal", "sense"), ("normal", "moderate"), ("normal", "full")] + \
    [(c, "moderate") for c in ("b0", "vol", "tcr", "i2c_timeout", "txa_en", "sense_bw", "tdm")]


# ------------------------------------------------------------------ shared register model
class Model:
    def __init__(self, inject, dart_base, nco_base, clk_base, pmgr_ps):
        self.inject = inject
        self.r = {}
        self.dart, self.nco, self.clk, self.ps = dart_base, nco_base, clk_base, pmgr_ps
        self.amp = bytearray(256)
        self.amp[0x02] = 0x0e
        self.amp[0x31] = 0x07
        self.ptr, self.state, self.xen, self.rx = 0, None, False, []
        self.i2c_addr = None
        d = dart_base
        self.r.update({d + 0xfc: 0x3, d + 0x100: 0x80, d + 0x104: 0x80, d + 0x200: 0x80001234,
                       d + 0x210: 0x80005678, d + 0x40: 0x80000001, d + 0x60: 0x1f})
        self.r.update({clk_base + 4 * i: (0x07000000 if i % 2 else 0x05000003) for i in range(8)})
        self.r[nco_base] = 0x80000000
        self.r[pmgr_ps] = 0x0f
        self.r[GPIO181] = 0x76a00

    def read(self, a):
        b = a - I2C1
        if 0 <= b < 0x4000:
            if b == 0x04:
                return self.rx.pop(0) if self.rx else 0x100
            if b == 0x14:
                return (1 << 27) if self.xen else 0
            if b == 0x00:
                return 0
            return self.r.get(a, 0)
        if a == self.dart + 0x20:
            return 0
        if ADMAC + 0x8074 <= a < ADMAC + 0x8074 + 0x200 * 32 and (a - ADMAC - 0x8074) % 0x200 == 0:
            return 0x100 | self.r.get(a, 0)
        if self.inject == "txa_en" and a == MCA0 + 0x300:
            return self.r.get(a, 0) | 1
        return self.r.get(a, 0)

    def write(self, a, v):
        b = a - I2C1
        if 0 <= b < 0x4000:
            if b == 0x14:
                if v == 0xffffffff:
                    self.xen = False
                return
            if b == 0x00:
                self._mtx(v)
                return
            self.r[a] = v
            return
        if a == self.dart + 0x108 and self.inject == "tcr":
            return
        if a == self.dart + 0x40 and v == 0xffffffff:
            self.r[a] = 0
            return
        if a == ADMAC + 0x8040 + 0x200 * 3 and self.inject == "sense_bw":
            v &= ~0x7
        self.r[a] = v

    def _mtx(self, v):
        byte, stop = v & 0xff, bool(v & (1 << 9))
        if v & (1 << 8):                                  # START + address
            self.i2c_addr = byte >> 1
            self.state = "rd" if byte & 1 else "reg"
            return
        if v & (1 << 10):                                 # READ n
            for _ in range(byte):
                self.rx.append(self.amp[self.ptr & 0xff])
                self.ptr += 1
        elif self.state == "reg":
            self.ptr, self.state = byte, "data"
        else:
            ign = ((self.inject == "vol" and self.ptr == 0x05) or
                   (self.inject == "tdm" and self.ptr == 0x0e))
            if self.i2c_addr == 0x31 and not ign:
                self.amp[self.ptr & 0xff] = byte
            self.ptr += 1
        if stop and self.inject != "i2c_timeout":
            self.xen = True


class Trace(list):
    def norm(self):
        """Runs of consecutive memory writes compare as sets (Python flushes the dirty page
        tables in set order, C writes L1 then L2; both are plain RAM)."""
        out, run = [], []
        for e in self:
            if e[0] == "mw":
                run.append(e)
                continue
            if run:
                out += sorted(run)
                run = []
            out.append(e)
        return out + sorted(run)


# ------------------------------------------------------------------ Python side
def run_python(adt_bytes, inject, model, trace, prot):
    from m1n1.adt import load_adt
    mem = R.FakeMem()
    iface = R.FakeIface(mem)
    on = [False]

    class P:
        def mcc_get_carveouts(self):
            return 0x10000

        def read64(self, a):
            return struct.unpack("<Q", mem.read(a, 8))[0]

        def read32(self, a):
            if 0x200000000 <= a < 0x300000000:
                v = model.read(a) & 0xffffffff
                if on[0]:
                    trace.append(("r", a, v))
                return v
            return struct.unpack("<I", mem.read(a, 4))[0]

        def write32(self, a, v):
            v &= 0xffffffff
            if 0x200000000 <= a < 0x300000000:
                if on[0]:
                    trace.append(("w", a, v))
                model.write(a, v)
                return
            mem.write(a, struct.pack("<I", v))

        def set32(self, a, m):
            self.write32(a, self.read32(a) | m)

        def clear32(self, a, m):
            self.write32(a, self.read32(a) & ~m)

        def memset8(self, d, v, n):
            mem.write_fast(d, bytes([v]) * n)

        def dc_civac(self, a, n):
            if on[0]:
                trace.append(("civac", a, n))

        def pmgr_adt_clocks_enable(self, path):
            if on[0]:
                trace.append(("clk", path))
            return 0

    p = P()

    class If(R.FakeIface):
        def writemem(self, addr, data, progress=None):
            if on[0]:
                trace.append(("mw", addr, len(data), hashlib.sha256(bytes(data)).hexdigest()[:16]))
            mem.write_fast(addr, bytes(data))

        def readmem(self, addr, n):
            if on[0]:
                trace.append(("mr", addr, n))
            return mem.read(addr, n)

    iface = If(mem)
    p.iface = iface                      # ADMACChannel: parent.p.iface
    mem.write(0x10000, bytes(16))

    class U:
        def __init__(self):
            self.proxy, self.iface = p, iface
            self.adt = load_adt(adt_bytes)
            self.ba = types.SimpleNamespace(phys_base=0x800000000, mem_size_actual=16 << 30)
            self.heap_base, self.heap_top = HEAP_BASE, HEAP_BASE + HEAP_SIZE
            self._brk = HEAP_BASE + 0x10000

        def memalign(self, align, size):
            self._brk = (self._brk + align - 1) & ~(align - 1)
            a = self._brk
            self._brk += size
            return a

        def malloc(self, size):
            return self.memalign(0x40, size)

        def read(self, addr, width):
            assert width == 32
            return p.read32(addr)

        def write(self, addr, data, width):
            assert width == 32
            return p.write32(addr, data)

    u = U()

    class Maps:
        def overlaps(self, rng):
            return []

        def items(self):
            return []

        def lookup(self, addr):
            return {"HW": None, "X": None} if inject == "b0" else {"HW": None}

    hv = types.SimpleNamespace(mmio_maps=Maps(), phys_base=u.heap_top, map_hw=lambda *a: None)
    src = AUDIO.read_text()
    ns = dict(p=p, u=u, iface=iface, hv=hv, __name__="audio_module")
    cut = src.index("_s256_buf = _s256_stage_a()")
    box = {}

    def stage_a_then_record():
        buf = box["stage_a"]()
        box["buf"] = buf
        on[0] = True
        return buf

    sleep = time.sleep
    out = io.StringIO()
    env = os.environ.pop("NWOAS_AUDIO_PROT", None)
    os.environ["NWOAS_AUDIO_PROT"] = prot
    try:
        time.sleep = lambda s: trace.append(("sleep", int(round(s * 1e6)))) if on[0] else None
        with contextlib.redirect_stdout(out):
            # the protection stage is read at import time: re-run the top part with the env set
            exec(compile(src[:cut], str(AUDIO), "exec"), ns)
            box["stage_a"] = ns["_s256_stage_a"]
            ns["_s256_stage_a"] = stage_a_then_record
            exec(compile("\n" * src[:cut].count("\n") + src[cut:], str(AUDIO), "exec"), ns)
    finally:
        time.sleep = sleep
        os.environ.pop("NWOAS_AUDIO_PROT", None)
        if env is not None:
            os.environ["NWOAS_AUDIO_PROT"] = env
    buf = box["buf"]
    return buf, ns["_s256_status"], mem.read(buf, 0x4c), out.getvalue()


# ------------------------------------------------------------------ C side
class Ctx(ct.Structure):
    _fields_ = [(n, ct.c_uint64) for n in ("i2c1", "admac", "mca0", "switch1", "clk_base", "clk_size",
                                           "dart", "gpio", "pmgr0", "nco")] + \
               [("nco_fin", ct.c_uint32), ("admac_nchans", ct.c_uint32), ("buf", ct.c_uint64),
                ("pt", ct.c_uint64), ("prot_gain", ct.c_uint32), ("prot_vol_floor", ct.c_uint32),
                ("err", ct.c_uint32)]


RD = ct.CFUNCTYPE(ct.c_uint32, ct.c_uint64)
WR = ct.CFUNCTYPE(None, ct.c_uint64, ct.c_uint32)
SL = ct.CFUNCTYPE(None, ct.c_uint32)
CK = ct.CFUNCTYPE(ct.c_int, ct.c_char_p)
CV = ct.CFUNCTYPE(None, ct.c_uint64, ct.c_uint64)
MW = ct.CFUNCTYPE(None, ct.c_uint64, ct.c_void_p, ct.c_uint32)
LG = ct.CFUNCTYPE(None, ct.c_char_p)


class Io(ct.Structure):
    _fields_ = [("rd32", RD), ("wr32", WR), ("sleep_us", SL), ("clocks_enable", CK), ("civac", CV),
                ("mem_write", MW), ("mem_read", MW), ("log", LG), ("pace_us", SL)]


def build_lib(src, work):
    lib = work / "libs264audio.dylib"
    cmd = ["nice", "-n", "19", "clang", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-DS264_HOST", "-shared", "-fPIC", f"-I{src}", str(src / "nwoas_s264_audio.c"),
           str(src / "nwoas_s264_plan.c"), "-o", str(lib)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or r.stderr.strip():
        print(r.stderr)
        raise SystemExit("host audio library build failed or warned")
    return ct.CDLL(str(lib))


def run_c(lib, adt_bytes, buf, inject, model, trace, stage_a_hdr, prot):
    mem = R.FakeMem()
    mem.write(buf, stage_a_hdr)
    logs, pace = [], []

    def rd(a):
        v = model.read(a) & 0xffffffff
        trace.append(("r", a, v))
        return v

    def wr(a, v):
        trace.append(("w", a, v))
        model.write(a, v)

    def mw(a, p, n):
        data = ct.string_at(p, n)
        trace.append(("mw", a, n, hashlib.sha256(data).hexdigest()[:16]))
        mem.write_fast(a, data)

    def mr(a, p, n):
        trace.append(("mr", a, n))
        ct.memmove(p, mem.read(a, n), n)

    def ck(path):
        trace.append(("clk", path.decode()))
        return 0

    cbs = Io(RD(rd), WR(wr), SL(lambda us: trace.append(("sleep", us))), CK(ck),
             CV(lambda a, n: trace.append(("civac", a, n))), MW(mw), MW(mr), LG(lambda m: logs.append(m.decode())),
             SL(lambda us: pace.append(us)))   # pacing: deliberate deviation, not in the trace
    ctx = Ctx()
    adt = ct.create_string_buffer(adt_bytes, len(adt_bytes))
    lib.s264_audio_ctx_from_adt.argtypes = [ct.c_void_p, ct.c_uint32, ct.c_uint64, ct.c_uint64, ct.POINTER(Ctx)]
    lib.s264_audio_run.argtypes = [ct.POINTER(Io), ct.POINTER(Ctx)]
    lib.s264_audio_run.restype = ct.c_uint32
    lib.s264_audio_ctx_from_adt(adt, len(adt_bytes), buf, buf + 0x40000, ct.byref(ctx))
    if inject == "b0" and not ctx.err:
        ctx.err = 1                      # what the executor does with the planner's d = 1
    g, f = D.PROT_VALUES[PROTS[prot]]
    ctx.prot_gain, ctx.prot_vol_floor = g, f
    st = lib.s264_audio_run(ct.byref(cbs), ct.byref(ctx))
    return st, mem.read(buf, 0x4c), logs, ctx


def model_for(adt_bytes, inject):
    from m1n1.adt import load_adt
    a = load_adt(adt_bytes)
    dart = a["/arm-io/dart-sio"].get_reg(0)[0]
    nco = a["/arm-io/nco"].get_reg(0)[0]
    clk = a["/arm-io/mca-switch"].get_reg(2)[0]
    ps = a["/arm-io/pmgr"].get_reg(0)[0] + 0x228
    return Model(inject, dart, nco, clk, ps)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, default=Path(os.environ.get(
        "S264_SRC", ROOT / "m1n1-standalone-s264" / "src")))
    ap.add_argument("--workdir", type=Path)
    a = ap.parse_args()
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264d-audio-"))
    work.mkdir(parents=True, exist_ok=True)
    lib = build_lib(a.src, work)
    adt_bytes = ADT.read_bytes()
    rows, ok = [], True
    for case, prot in CASES:
        pt, ctr = Trace(), Trace()
        pbuf, pstat, phdr, pout = run_python(adt_bytes, case, model_for(adt_bytes, case), pt, prot)
        hdr_a = struct.pack("<IIQIIIIIIIIQIIIII", 0x5541574e, 2, 0x80001000, 0x20000, 48000, 1, 0, 0, 0x31,
                            0x0c, 0, 0x80021000, 0x10000, 3, 2, *D.PROT_VALUES[PROTS[prot]])
        cst, chdr, clogs, ctx = run_c(lib, adt_bytes, pbuf, case, model_for(adt_bytes, case), ctr, hdr_a, prot)
        pn, cn = pt.norm(), ctr.norm()
        same = pn == cn and phdr == chdr and (cst & 0x7fffffff) == pstat
        first = next((i for i, (x, y) in enumerate(zip(pn, cn)) if x != y), min(len(pn), len(cn)))
        nmmio = sum(1 for e in pn if e[0] in "rw")
        detail = (f"status Python {pstat:#x} / C {cst:#x}; {len(pn)} events ({nmmio} MMIO) vs {len(cn)}"
                  + ("" if pn == cn else f"; first difference at #{first}: Python "
                     f"{pn[first] if first < len(pn) else '-'} / C {cn[first] if first < len(cn) else '-'}")
                  + ("" if phdr == chdr else "; header bytes differ"))
        rows.append((f"{case} ({prot})", "SAME" if same else "DIFF", detail + f"; header prot {phdr[0x44]:#x}/{phdr[0x48]:#x}"))
        ok &= same
        fails = [ln for ln in pout.splitlines() if "FAIL" in ln]
        print(f"[s264D-audio] {case}/{prot}: Python {fails[0][:110] if fails else 'no FAIL line'}")
        print(f"[s264D-audio] {case}/{prot}: C      {[m for m in clogs if 'FAIL' in m][:2]}")
        if (case, prot) == ("normal", "moderate"):
            (work / "trace-normal.txt").write_text("\n".join(map(str, pn)) + "\n")
    # negative controls: the comparison must notice a one-value change and a dropped event
    pt, ctr = Trace(), Trace()
    pbuf, pstat, phdr, _ = run_python(adt_bytes, "normal", model_for(adt_bytes, "normal"), pt, "sense")
    run_c(lib, adt_bytes, pbuf, "normal", model_for(adt_bytes, "normal"), ctr, bytes(0x4c), "sense")
    cn = ctr.norm()
    k = next(i for i, e in enumerate(cn) if e[0] == "w" and e[1] == GPIO181)
    neg = {"GPIO181 value +1": cn[:k] + [("w", GPIO181, cn[k][2] + 1)] + cn[k + 1:] != pt.norm(),
           "one MMIO read dropped": cn[:5] + cn[6:] != pt.norm()}
    for n, v in neg.items():
        print(f"[s264D-audio] negative control '{n}': {'detected' if v else 'NOT DETECTED'}")
        ok &= v
    print("| case | result | detail |\n|---|---|---|")
    for r in rows:
        print(f"| {r[0]} | {r[1]} | {r[2]} |")
    print(f"[s264D-audio] {sum(1 for r in rows if r[1] == 'SAME')}/{len(rows)} SAME")
    print("S264D AUDIO:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
