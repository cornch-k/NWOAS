#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264D: the C Wi-Fi hypervisor side (src/nwoas_s264_wifi.c) against the REAL Python
(wifi-s259/nwoas_wifi_s259.py + wifi-s257/wifi_up.py + wifi-s240/wifi_probe.py + wifi_scan.py),
build host only.  Nothing here touches hardware; passing is not a hardware result.

1. Facts: wp.adt_facts + wu.validate_facts + the two DART checks of wifi_module._s259_checks on the
   real ADT dumps, against s264w_facts_from_adt + s264w_validate; then 9 mutations of the facts.
2. Driver: the S240/S257 fake hardware (wifi-s240/test_wifi_probe.FakeHW, wifi-s257/test_wifi_up.FakeHW2,
   UEFI state from wifi-s259/test_wifi_hv.uefi_state), one fresh copy per side, the same integer-us
   clock.  Python WifiS259 and the C driver get the same control-page operations; recorded and
   compared: every MMIO read (addr, value) / write, set_exc_guard, get_exc_count, nop, sleeps, SMC
   start/key_info/write32/stop, boot_force_off result and every control-page read value.
   Poll loops that only repeat until a time limit are compared once (the iteration count of a
   wall-clock timeout is not a behaviour).
usage: s264d_wifi_test.py [--src DIR] [--workdir DIR]
"""
import argparse
import contextlib
import ctypes as ct
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SCRIPTS = ROOT / "nwoas_scripts"
for p in (SCRIPTS / "wifi-s259", SCRIPTS / "wifi-s257", SCRIPTS / "wifi-s240", ROOT / "m1n1_windows" / "proxyclient"):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))
import nwoas_wifi_s259 as w   # noqa: E402
import test_wifi_hv as th     # noqa: E402
import test_wifi_up as tu     # noqa: E402
import test_wifi_probe as tp  # noqa: E402
wp, wu, ws = w.wp, w.wu, w.ws

ADTS = [ROOT / "nwoas_scripts" / "agx-g1-s246" / "evidence" / "g0-adt-20261116-live1" / "adt.bin",
        ROOT / "experiments" / "tahoe-afk-20260906" / "current.adt"]


class IntClock:
    """Integer-microsecond clock shared by the fake hardware and both drivers."""

    def __init__(self):
        self.us = 0

    def __call__(self):
        return self.us / 1e6

    def sleep(self, s):
        self.us += max(int(round(s * 1e6)), 0)

    @property
    def t(self):                        # test_wifi_probe.FakeHW: each read32 does clock.t += 0.0001
        return self.us / 1e6

    @t.setter
    def t(self, v):
        self.us = int(round(v * 1e6))


# ------------------------------------------------------------------ C structures
class Facts(ct.Structure):
    _fields_ = [(n, ct.c_uint64) for n in ("ecam", "rc", "port0", "port0_phy", "port1", "gpio", "smc")] + \
               [(n, ct.c_uint32) for n in ("gpio_phandle", "perst_phandle", "perst_pin", "perst_polarity",
                                           "port1_perst_phandle", "port1_perst_pin", "perst_to_config_ms",
                                           "t_refclk_to_perst_us", "j274", "wlan_bcm4378")] + \
               [("dart0_base", ct.c_uint64), ("dart1_base", ct.c_uint64)] + \
               [(n, ct.c_uint32) for n in ("dart0_phandle", "bridge0_dart_phandle", "bridge0_dart_ok")]


class Page(ct.Structure):
    _fields_ = [(n, ct.c_uint32) for n in ("state", "error", "last_cmd", "seq", "cmd_count", "cfg_off", "cfg_err",
                                           "cfg_reads", "cfg_writes", "mac_lo", "mac_hi", "cal_len", "antenna")] + \
               [("board", ct.c_uint32 * 4), ("cal", ct.c_uint32 * 1024)]


RD = ct.CFUNCTYPE(ct.c_uint32, ct.c_uint64)
WR = ct.CFUNCTYPE(None, ct.c_uint64, ct.c_uint32)
EC = ct.CFUNCTYPE(ct.c_uint32)
SG = ct.CFUNCTYPE(None, ct.c_uint32)
SL = ct.CFUNCTYPE(None, ct.c_uint64)
NW = ct.CFUNCTYPE(ct.c_uint64)
NP = ct.CFUNCTYPE(None)
SS = ct.CFUNCTYPE(ct.c_int)
SK = ct.CFUNCTYPE(ct.c_int, ct.c_uint32, ct.POINTER(ct.c_uint32), ct.POINTER(ct.c_uint32))
SW = ct.CFUNCTYPE(ct.c_int, ct.c_uint32, ct.c_uint32)
LG = ct.CFUNCTYPE(None, ct.c_char_p)
FN = ct.CFUNCTYPE(None, ct.c_int)        # s264E bar_fence (never called without the S261 fence)


class Io(ct.Structure):
    _fields_ = [("rd32", RD), ("wr32", WR), ("exc_count", EC), ("set_guard", SG), ("sleep_us", SL),
                ("now_us", NW), ("nop", NP), ("smc_start", SS), ("smc_key_info", SK), ("smc_write32", SW),
                ("smc_stop", SS), ("log", LG), ("pace_us", SG), ("bar_fence", FN), ("pt_update", NP)]


def build_lib(src, work):
    lib = work / "libs264wifi.dylib"
    cmd = ["nice", "-n", "19", "clang", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-DS264_HOST", "-shared", "-fPIC", f"-I{src}", str(src / "nwoas_s264_wifi.c"),
           str(src / "nwoas_s264_plan.c"), "-o", str(lib)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or r.stderr.strip():
        print(r.stderr)
        raise SystemExit("host Wi-Fi library build failed or warned")
    L = ct.CDLL(str(lib))
    L.s264w_facts_from_adt.argtypes = [ct.c_void_p, ct.c_uint32, ct.POINTER(Facts)]
    L.s264w_validate.argtypes = [ct.POINTER(Facts)]
    L.s264w_validate.restype = ct.c_uint32
    L.s264w_sizeof.restype = ct.c_ulong
    L.s264w_page_sizeof.restype = ct.c_ulong
    L.s264w_init.argtypes = [ct.c_void_p, ct.POINTER(Io), ct.POINTER(Facts), ct.POINTER(Page), ct.c_bool]
    L.s264w_boot_force_off.argtypes = [ct.c_void_p]
    L.s264w_hook_read.argtypes = [ct.c_void_p, ct.c_uint32, ct.c_int]
    L.s264w_hook_read.restype = ct.c_uint64
    L.s264w_hook_write.argtypes = [ct.c_void_p, ct.c_uint32, ct.c_uint64, ct.c_int]
    assert L.s264w_page_sizeof() == ct.sizeof(Page), (L.s264w_page_sizeof(), ct.sizeof(Page))
    return L


# ------------------------------------------------------------------ 1. facts
EXPECT_KEYS = list(wp.EXPECT)


def py_verdict(f, d0, d1):
    try:
        with contextlib.redirect_stdout(open(os.devnull, "w")):
            wu.validate_facts(f)
    except wp.Refusal as e:
        m = str(e)
        if "wrong target" in m:
            return 22
        if "differs from recorded J274 value" in m:
            k = m.split("ADT ", 1)[1].split("=", 1)[0]
            return 23 | (EXPECT_KEYS.index(k) << 8)
        if "function-perst" in m:
            return 24
        if "bcm4378" in m:
            return 25
        if "reset timing" in m:
            return 26
        return ("?", m)
    if d0["base"] != w.DART0_BASE or d0.get("bridge0_dart_phandle") != d0.get("phandle"):
        return 27
    if d1 != w.DART1_BASE:
        return 28
    return 0


def c_facts(L, adt_bytes):
    f = Facts()
    buf = ct.create_string_buffer(adt_bytes, len(adt_bytes))
    r = L.s264w_facts_from_adt(buf, len(adt_bytes), ct.byref(f))
    return r, f


def facts_rows(L):
    from m1n1.adt import load_adt
    rows, ok = [], True
    for path in ADTS:
        if not path.is_file():
            rows.append((f"facts {path.name}", "SKIP", "dump missing"))
            continue
        raw = path.read_bytes()
        adt = load_adt(raw)
        pf = wp.adt_facts(adt)
        d0 = ws.dart_info(adt)
        d1 = adt["/arm-io/dart-apcie1"].get_reg(0)[0]
        r, cf = c_facts(L, raw)
        fields = {k: pf[k] for k in ("ecam", "rc", "port0", "port0_phy", "port1", "gpio", "smc", "gpio_phandle",
                                     "perst_phandle", "perst_pin", "perst_polarity", "port1_perst_phandle",
                                     "port1_perst_pin", "perst_to_config_ms", "t_refclk_to_perst_us")}
        diff = [k for k, v in fields.items() if getattr(cf, k) != v]
        if bool(cf.j274) != any("j274" in c.lower() for c in pf["compatible"]):
            diff.append("j274")
        if bool(cf.wlan_bcm4378) != any("bcm4378" in c for c in pf["wlan_compatible"]):
            diff.append("wlan")
        if (cf.dart0_base, cf.dart0_phandle, cf.bridge0_dart_phandle, cf.dart1_base) != \
                (d0["base"], d0["phandle"], d0["bridge0_dart_phandle"], d1):
            diff.append("dart")
        pv, cv = py_verdict(pf, d0, d1), L.s264w_validate(ct.byref(cf))
        good = r == 0 and not diff and pv == cv
        rows.append((f"facts {path.parent.name}/{path.name}", "SAME" if good else "DIFF",
                     f"C rc {r} diff {diff} verdict Python {pv} / C {cv} (gpio phandle {pf['gpio_phandle']})"))
        ok &= good
        if path == ADTS[0]:
            base_py, base_c, base_d0, base_d1 = pf, cf, d0, d1
    # mutations (both sides get the same change)
    muts = [("ecam +0x1000", {"ecam": base_py["ecam"] + 0x1000}),
            ("perst pin 0x97", {"perst_pin": 0x97}),
            ("perst phandle != gpio", {"perst_phandle": base_py["gpio_phandle"] + 1}),
            ("gpio phandle 102 (13.5 iBoot)", {"gpio_phandle": 102, "perst_phandle": 102, "port1_perst_phandle": 102}),
            ("perst-to-config 2000 ms", {"perst_to_config_ms": 2000}),
            ("smc base", {"smc": base_py["smc"] + 0x4000}),
            ("not j274", {"compatible": ["J313AP"], "j274": 0}),
            ("wlan not bcm4378", {"wlan_compatible": ["wlan-pcie,bcm4387"], "wlan_bcm4378": 0}),
            ("dart-apcie0 base", {"_d0": base_d0["base"] + 0x4000}),
            ("dart-apcie1 base", {"_d1": base_d1 + 0x4000})]
    for name, ch in muts:
        pf = dict(base_py)
        d0, d1 = dict(base_d0), base_d1
        cf = Facts.from_buffer_copy(base_c)
        for k, v in ch.items():
            if k == "_d0":
                d0["base"] = v
                cf.dart0_base = v
            elif k == "_d1":
                d1 = v
                cf.dart1_base = v
            elif k in ("j274", "wlan_bcm4378"):
                setattr(cf, k, v)
            elif k in ("compatible", "wlan_compatible"):
                pf[k] = v
            else:
                pf[k] = v
                setattr(cf, k, v)
        pv, cv = py_verdict(pf, d0, d1), L.s264w_validate(ct.byref(cf))
        good = pv == cv
        rows.append((f"facts mutation: {name}", "SAME" if good else "DIFF", f"Python {pv} / C {cv}"))
        ok &= good
    return rows, ok


# ------------------------------------------------------------------ 2. driver
def make_hw(clock, hw_kw=None, setup=None, **ukw):
    hw = tu.FakeHW2(clock, **(hw_kw or {}))
    th.uefi_state(hw, **ukw)
    th.wrap_intmsk(hw)
    if setup:
        setup(hw)
    return hw


class Rec:
    """Wraps the fake hardware's proxy surface; every call goes to the trace."""

    def __init__(self, hw, trace):
        self.hw, self.t = hw, trace

    def read32(self, a):
        v = self.hw.read32(a) & 0xFFFFFFFF
        self.t.append(("r", a, v))
        return v

    def write32(self, a, v):
        self.t.append(("w", a, v & 0xFFFFFFFF))
        self.hw.write32(a, v)

    def set_exc_guard(self, m):
        self.t.append(("guard", m))
        self.hw.set_exc_guard(m)

    def get_exc_count(self):
        v = self.hw.get_exc_count()
        self.t.append(("exc", v))
        return v

    def nop(self):
        self.t.append(("nop",))
        self.hw.nop()

    def __getattr__(self, k):
        return getattr(self.hw, k)


class RecSmc:
    def __init__(self, hw, trace, info=None):
        self.s = tp.FakeSmc(hw) if info is None else tp.FakeSmc(hw, info=info)
        self.t = trace

    def start(self):
        self.t.append(("smc", "start"))
        self.s.start()

    def key_info(self, key):
        self.t.append(("smc", "key_info", key))
        return self.s.key_info(key)

    def write32(self, key, v):
        self.t.append(("smc", "write32", key, v))
        self.s.write32(key, v)

    def stop(self):
        self.t.append(("smc", "stop"))
        self.s.stop()


def run_py(sc, ops):
    clock, trace = IntClock(), []
    hw = make_hw(clock, sc.get("hw_kw"), sc.get("setup"), **sc.get("ukw", {}))
    rec = Rec(hw, trace)
    smc_info = sc.get("smc_info")
    logs = []
    obj = w.WifiS259(rec, tp.good_facts(), lambda base: RecSmc(hw, trace, smc_info), enabled=sc.get("enabled", True),
                     sleep=lambda s: (trace.append(("sleep", int(round(s * 1e6)))), clock.sleep(s)),
                     clock=clock, phase=lambda n, s: contextlib.nullcontext(), log=logs.append, run_dir=None,
                     wlan=tu.fake_info())
    out = []
    for op in ops:
        if op[0] == "boot":
            try:
                obj.boot_force_off()
                out.append(("boot", 0))
            except RuntimeError as e:
                out.append(("boot", 1 if "link still up" in str(e) else 2))
        elif op[0] == "w":
            obj.hook_write(w.CTRL_IPA + op[1], op[2], 8 << op[3])
        elif op[0] == "r":
            out.append(("r", op[1], obj.hook_read(w.CTRL_IPA + op[1], 8 << op[2])))
        elif op[0] == "hw":
            op[1](hw)
    return trace, out, logs, obj


def run_c(L, sc, ops):
    clock, trace = IntClock(), []
    hw = make_hw(clock, sc.get("hw_kw"), sc.get("setup"), **sc.get("ukw", {}))
    rec = Rec(hw, trace)
    smc = {"cur": None}
    logs = []

    def key_info(key, lp, tp_):
        s = smc["cur"]
        try:
            info = s.key_info("gP0d")
        except Exception:
            return 1
        if not isinstance(info, (tuple, list)) or len(info) < 2:
            lp[0], tp_[0] = 0, 0
            return 0
        lp[0] = info[0]
        tp_[0] = int.from_bytes(str(info[1]).encode("ascii", "replace")[:4].ljust(4, b"\0"), "little")
        return 0

    def guarded_call(fn):
        try:
            fn()
            return 0
        except Exception:
            return 1

    def smc_start():
        smc["cur"] = RecSmc(hw, trace, sc.get("smc_info"))
        return guarded_call(smc["cur"].start)

    cbs = Io(RD(rec.read32), WR(rec.write32), EC(rec.get_exc_count), SG(rec.set_exc_guard),
             SL(lambda us: (trace.append(("sleep", us)), clock.sleep(us / 1e6))), NW(lambda: clock.us), NP(rec.nop),
             SS(smc_start), SK(key_info), SW(lambda k, v: guarded_call(lambda: smc["cur"].write32("gP0d", v))),
             SS(lambda: guarded_call(smc["cur"].stop)), LG(lambda m: logs.append(m.decode())),
             SG(lambda us: None),                 # pacing: deliberate deviation, not in the trace
             FN(lambda on: trace.append(("fence", on))), NP(lambda: trace.append(("pt",))))
    f = tp.good_facts()
    cf = Facts(ecam=f["ecam"], rc=f["rc"], port0=f["port0"], port0_phy=f["port0_phy"], port1=f["port1"],
               gpio=f["gpio"], smc=f["smc"], gpio_phandle=f["gpio_phandle"], perst_phandle=f["perst_phandle"],
               perst_pin=f["perst_pin"], perst_polarity=f["perst_polarity"],
               port1_perst_phandle=f["port1_perst_phandle"], port1_perst_pin=f["port1_perst_pin"],
               perst_to_config_ms=f["perst_to_config_ms"], t_refclk_to_perst_us=f["t_refclk_to_perst_us"],
               j274=1, wlan_bcm4378=1, dart0_base=w.DART0_BASE, dart1_base=w.DART1_BASE)
    pg = Page()
    regs = w.WifiS259._wlan_regs(tu.fake_info())
    pg.mac_lo, pg.mac_hi, pg.cal_len, pg.antenna = (regs[w.R_MAC_LO], regs[w.R_MAC_HI], regs[w.R_CAL_LEN],
                                                    regs[w.R_ANTENNA])
    for i in range(4):
        pg.board[i] = regs[w.R_BOARD + 4 * i]
    for off, v in regs.items():
        if off >= w.CAL_OFF:
            pg.cal[(off - w.CAL_OFF) // 4] = v
    st = ct.create_string_buffer(L.s264w_sizeof())
    L.s264w_init(st, ct.byref(cbs), ct.byref(cf), ct.byref(pg), sc.get("enabled", True))
    out = []
    for op in ops:
        if op[0] == "boot":
            out.append(("boot", L.s264w_boot_force_off(st)))
        elif op[0] == "w":
            L.s264w_hook_write(st, op[1], op[2], op[3])
        elif op[0] == "r":
            out.append(("r", op[1], L.s264w_hook_read(st, op[1], op[2])))
        elif op[0] == "hw":
            op[1](hw)
    return trace, out, logs


def collapse(seq):
    """Drop a window of 2..8 events that repeats right after itself (poll / retry loops)."""
    out = list(seq)
    changed = True
    while changed:
        changed = False
        for L in range(2, 9):
            i = 0
            res = []
            while i < len(out):
                if i + 2 * L <= len(out) and out[i:i + L] == out[i + L:i + 2 * L] and \
                        any(e[0] == "sleep" for e in out[i:i + L]):
                    i += L
                    changed = True
                    continue
                res.append(out[i])
                i += 1
            out = res
    return out


DUMP = [("r", o, 2) for o in range(0, 0x58, 4)] + [("r", o, 2) for o in (0x60, 0x64, 0x68, 0x6c, 0x70, 0x74, 0x1000, 0x1004)]
UP = [("w", w.R_CMD, w.CMD_POWER_UP, 2)]
DOWN = [("w", w.R_CMD, w.CMD_POWER_DOWN, 2)]


def reset_ep_for_second_up(hw):
    # test_wifi_hv.test_power_down_then_up_again: a fresh chip after PERST#/power
    for k in (0x10, 0x14, 0x18, 0x1C):
        hw.fn[0]['cfg'][k] = hw.bar_orig[0][k]
    hw.fn[0]['cfg'][0x04] = 0x00100000


def window_mismatch(hw):
    orig = hw.write32
    win = wp.ecam_addr(hw.f["ecam"], 0, 0, 0, 0x20)

    def w32(addr, val):
        if addr == win:
            val = 0x0000FFF0
        return orig(addr, val)
    hw.write32 = w32


def dart1_bad(hw):
    hw.regs[w.DART1_BASE + ws.DART_TTBR] = 0


CFGWIN = [("w", w.R_CFG_OFF, 0x0, 2), ("r", w.R_CFG_DATA, 2), ("r", w.R_CFG_ERR, 2),
          ("w", w.R_BAR0WIN, 0x18001000, 2), ("r", w.R_BAR0WIN, 2), ("w", w.R_BAR0WIN, 0x18001123, 2),
          ("w", w.R_CFG_OFF, 0x10, 2), ("w", w.R_CFG_DATA, 0, 2), ("r", w.R_CFG_ERR, 2),
          ("w", w.R_CFG_OFF, 0x04, 2), ("w", w.R_CFG_DATA, 0x0404, 2), ("r", w.R_EPCMD, 2),
          ("w", w.R_CFG_OFF, 0x50, 2), ("w", w.R_CFG_DATA, 0x2810, 2), ("r", w.R_DEVCTL, 2),
          ("w", w.R_CFG_OFF, 0xBC, 2), ("w", w.R_CFG_DATA, 0xFFFFFFFF, 2), ("r", w.R_CFG_DATA, 2),
          ("w", w.R_CFG_OFF, 0x81, 2), ("r", w.R_CFG_ERR, 2), ("r", w.R_CFG_OFF + 1, 0), ("r", w.R_STATE, 3)]

SCENARIOS = [
    ("boot + up + config window + down + up again + idempotent", {},
     [("boot",)] + [("w", w.R_CFG_OFF, 0, 2), ("r", w.R_CFG_DATA, 2)] + UP + DUMP + CFGWIN + DOWN + DUMP + [("hw", reset_ep_for_second_up)] + UP + UP + DUMP),
    ("second up without the chip reset (BARs still set)", {}, UP + DOWN + UP + DUMP),
    ("boot: chip left on (link up before)", {"hw_kw": {"link_already_up": True}}, [("boot",)] + DUMP),
    ("boot: link stuck up -> STOP", {"hw_kw": {"link_already_up": True, "link_stuck_up": True},
                                     "setup": lambda hw: setattr(hw, "was_up", True)}, [("boot",)] + DUMP),
    ("up: dart-apcie0 differs -> copied", {"ukw": {"dart0_like_dart1": False}}, UP + DUMP),
    ("up: dart-apcie1 not UEFI shape", {"setup": dart1_bad}, UP + DUMP),
    ("up: INTx masked -> unmasked", {"ukw": {"intx_masked": True}}, UP + DUMP),
    ("up: device absent (link down)", {"hw_kw": {"device_present": False}}, UP + DUMP),
    ("up: link already up -> refused", {"hw_kw": {"link_already_up": True}}, UP + DUMP),
    ("up: root port window read-back mismatch", {"setup": window_mismatch}, UP + DUMP),
    ("up: SMC key type wrong", {"smc_info": (4, "flt ", 0)}, UP + DUMP),
    ("disabled (NWOAS_WIFI=0 object)", {"enabled": False}, UP + CFGWIN[:3] + DUMP),
    ("64-bit control writes + unaligned", {}, [("w", w.R_CMD, w.CMD_POWER_UP | (5 << 32), 3), ("w", 0x22, 1, 1)] + DUMP
     + [("w", w.R_CFG_OFF, 0x04 | (0x406 << 32), 3)] + DUMP),
]


def driver_rows(L):
    rows, ok = [], True
    for name, sc, ops in SCENARIOS:
        pt, pout, plogs, obj = run_py(sc, ops)
        ct_, cout, clogs = run_c(L, sc, ops)
        pn, cn = collapse(pt), collapse(ct_)
        first = next((i for i, (x, y) in enumerate(zip(pn, cn)) if x != y), min(len(pn), len(cn)))
        same = pn == cn and pout == cout
        nm = sum(1 for e in pn if e[0] in "rw")
        det = (f"{len(pt)}/{len(ct_)} events ({nm} MMIO after loop folding)"
               + ("" if pn == cn else f"; first trace difference #{first}: Python "
                  f"{pn[first] if first < len(pn) else '-'} / C {cn[first] if first < len(cn) else '-'}")
               + ("" if pout == cout else f"; reads differ: {[(a, b) for a, b in zip(pout, cout) if a != b][:3]}"))
        st = [v for k, o, v in [x for x in pout if x[0] == "r"] if o == w.R_STATE]
        er = [v for k, o, v in [x for x in pout if x[0] == "r"] if o == w.R_ERROR]
        det += f"; state/error Python {st[-1] if st else '-'}/{w.ERRORS.get(er[-1]) if er else '-'}"
        boot = [x for x in pout if x[0] == "boot"]
        if boot:
            det += f"; boot {boot[0][1]}"
        rows.append((f"driver: {name}", "SAME" if same else "DIFF", det))
        ok &= same
    # negative controls: a changed value and a dropped read must be seen
    pt, pout, _, _ = run_py(*SCENARIOS[0][1:])
    ct_, cout, _ = run_c(L, *SCENARIOS[0][1:])
    cn = collapse(ct_)
    k = next(i for i, e in enumerate(cn) if e[0] == "w")
    neg = {"one written value +1": cn[:k] + [("w", cn[k][1], cn[k][2] + 1)] + cn[k + 1:] != collapse(pt),
           "one MMIO read dropped": [e for i, e in enumerate(cn) if i != 3] != collapse(pt),
           "one control-page value changed": cout[:-1] + [("r", cout[-1][1], cout[-1][2] ^ 1)] != pout}
    for n, v in neg.items():
        rows.append((f"negative control: {n}", "SAME" if v else "DIFF", "detected" if v else "NOT detected"))
        ok &= v
    return rows, ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, default=Path(os.environ.get(
        "S264_SRC", ROOT / "m1n1-standalone-s264" / "src")))
    ap.add_argument("--workdir", type=Path)
    a = ap.parse_args()
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264d-wifi-"))
    work.mkdir(parents=True, exist_ok=True)
    L = build_lib(a.src, work)
    rows, ok = facts_rows(L)
    r2, ok2 = driver_rows(L)
    rows += r2
    ok &= ok2
    print("| item | result | detail |\n|---|---|---|")
    for r in rows:
        print(f"| {r[0]} | {r[1]} | {r[2]} |")
    print(f"[s264D-wifi] {sum(1 for r in rows if r[1] == 'SAME')}/{len(rows)} SAME "
          f"({sum(1 for r in rows if r[1] == 'SKIP')} skipped)")
    print("S264D WIFI:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
