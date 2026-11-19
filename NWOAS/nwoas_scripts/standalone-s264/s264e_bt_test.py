#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264E: the C WifiBtS261 (src/nwoas_s264_wifi.c, Bluetooth fn1 + fence on top of the S259 Wi-Fi)
against the REAL bt-s261/nwoas_bt_s261.py, build host only.  Nothing here touches hardware; passing is
not a hardware result.

Both sides run on the S261 fake hardware (bt-s261/test_bt_hv.FakeHW3 = S257 FakeHW2 + fn1 config model,
UEFI state from wifi-s259/test_wifi_hv), one fresh copy per side, the same integer-us clock, the same
fake hypervisor for the fence (add_tracer / del_tracer of bt-s261-bar-fence over both fn1 BARs,
pt_update).  Compared: every MMIO read/write, exception guard, nop, sleep, SMC call, fence change and
pt_update, plus every Wi-Fi and BT control-page read and fence read.  Poll loops that only repeat until
a time limit are compared once (s264d_wifi_test.collapse).
usage: s264e_bt_test.py [--src DIR] [--workdir DIR]
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
sys.path.insert(0, str(HERE))
import s264d_wifi_test as W  # noqa: E402  (also sets sys.path for the Wi-Fi/proxyclient code)
ROOT = W.ROOT
sys.path.insert(0, str(ROOT / "nwoas_scripts" / "bt-s261"))
import nwoas_bt_s261 as b  # noqa: E402
import test_bt_hv as tb    # noqa: E402
w, wp, wu, ws, th, tu, tp = W.w, W.wp, W.wu, W.ws, W.th, W.tu, W.tp


class BtPage(ct.Structure):
    _fields_ = [(n, ct.c_uint32) for n in ("bdaddr_lo", "bdaddr_hi", "cal_len", "calbf_len")] + \
               [("board", ct.c_uint32 * 4), ("cal", ct.c_uint32 * 0x600), ("calbf", ct.c_uint32 * 0x600)] + \
               [(n, ct.c_uint32) for n in ("bd_ok", "raw_cal_len", "raw_calbf_len")]


class Io(ct.Structure):
    _fields_ = W.Io._fields_


BT_INFO = {"bdaddr": tb.FAKE_BD, "cal": tb.FAKE_CAL, "calbf": tb.FAKE_CALBF}


def build_lib(src, work):
    lib = work / "libs264bt.dylib"
    cmd = ["nice", "-n", "19", "clang", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-DS264_HOST", "-shared", "-fPIC", f"-I{src}", str(src / "nwoas_s264_wifi.c"),
           str(src / "nwoas_s264_plan.c"), "-o", str(lib)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or r.stderr.strip():
        print(r.stderr)
        raise SystemExit("host BT library build failed or warned")
    L = ct.CDLL(str(lib))
    L.s264w_sizeof.restype = ct.c_ulong
    L.s264w_bt_page_sizeof.restype = ct.c_ulong
    L.s264w_init.argtypes = [ct.c_void_p, ct.POINTER(Io), ct.POINTER(W.Facts), ct.POINTER(W.Page), ct.c_bool]
    L.s264w_bt_init.argtypes = [ct.c_void_p, ct.POINTER(BtPage), ct.c_bool]
    L.s264w_fence_install.argtypes = [ct.c_void_p, ct.c_bool]
    L.s264w_boot_force_off.argtypes = [ct.c_void_p]
    for fn in ("s264w_hook_read", "s264w_bt_hook_read"):
        getattr(L, fn).argtypes = [ct.c_void_p, ct.c_uint32, ct.c_int]
        getattr(L, fn).restype = ct.c_uint64
    for fn in ("s264w_hook_write", "s264w_bt_hook_write"):
        getattr(L, fn).argtypes = [ct.c_void_p, ct.c_uint32, ct.c_uint64, ct.c_int]
    L.s264w_fence_read.argtypes = [ct.c_void_p, ct.c_uint64, ct.c_int]
    L.s264w_fence_read.restype = ct.c_uint64
    L.s264w_fence_write.argtypes = [ct.c_void_p, ct.c_uint64, ct.c_int]
    assert L.s264w_bt_page_sizeof() == ct.sizeof(BtPage), (L.s264w_bt_page_sizeof(), ct.sizeof(BtPage))
    assert L.s264w_page_sizeof() == ct.sizeof(W.Page)
    return L


def make_hw(clock, sc):
    hw = tb.FakeHW3(clock, **(sc.get("hw_kw") or {}))
    th.uefi_state(hw, **sc.get("ukw", {}))
    th.wrap_intmsk(hw)
    return hw


class FenceHv:
    def __init__(self, trace):
        self.t = trace

    def add_tracer(self, zone, ident, mode, read=None, write=None, **kw):
        assert ident == b.FENCE_ID
        self.t.append(("fence", 1, zone.start))

    def del_tracer(self, zone, ident):
        assert ident == b.FENCE_ID
        self.t.append(("fence", 0, zone.start))

    def pt_update(self):
        self.t.append(("pt",))


def run_py(sc, ops):
    clock, trace = W.IntClock(), []
    hw = make_hw(clock, sc)
    rec = W.Rec(hw, trace)
    logs = []
    obj = b.WifiBtS261(rec, tp.good_facts(), lambda base: W.RecSmc(hw, trace, sc.get("smc_info")),
                       enabled=sc.get("enabled", True), bt_enabled=sc.get("bt_enabled", True),
                       bt=sc.get("bt", BT_INFO),
                       sleep=lambda s: (trace.append(("sleep", int(round(s * 1e6)))), clock.sleep(s)),
                       clock=clock, phase=lambda n, s: contextlib.nullcontext(), log=logs.append, run_dir=None,
                       wlan=tu.fake_info())
    out = []
    for op in ops:
        k = op[0]
        if k == "boot":
            try:
                obj.boot_force_off()
                out.append(("boot", 0))
            except RuntimeError as e:
                out.append(("boot", 1 if "link still up" in str(e) else 2))
        elif k == "fi":
            obj.fence_install(FenceHv(trace), fence_on_wifi_reset=op[1])
        elif k == "w":
            obj.hook_write(w.CTRL_IPA + op[1], op[2], 8 << op[3])
        elif k == "r":
            out.append(("r", op[1], obj.hook_read(w.CTRL_IPA + op[1], 8 << op[2])))
        elif k == "bw":
            obj.bt_hook_write(b.BT_CTRL_IPA + op[1], op[2], 8 << op[3])
        elif k == "br":
            out.append(("br", op[1], obj.bt_hook_read(b.BT_CTRL_IPA + op[1], 8 << op[2])))
        elif k == "bgen":                                    # write ARM = current (or stale) generation
            g = obj.bt_hook_read(b.BT_CTRL_IPA + b.R_GEN, 32)
            obj.bt_hook_write(b.BT_CTRL_IPA + b.R_ARM, g + op[1], 32)
        elif k == "fr":
            out.append(("fr", obj.fence_read(op[1], 8 << op[2])))
        elif k == "fw":
            obj.fence_write(op[1], 0, 8 << op[2])
        elif k == "hw":
            op[1](hw)
    return trace, out, logs


def run_c(L, sc, ops):
    clock, trace = W.IntClock(), []
    hw = make_hw(clock, sc)
    rec = W.Rec(hw, trace)
    smc = {"cur": None}
    logs = []

    def key_info(key, lp, tp_):
        try:
            info = smc["cur"].key_info("gP0d")
        except Exception:
            return 1
        lp[0] = info[0]
        tp_[0] = int.from_bytes(str(info[1]).encode("ascii", "replace")[:4].ljust(4, b"\0"), "little")
        return 0

    def call(fn):
        try:
            fn()
            return 0
        except Exception:
            return 1

    def smc_start():
        smc["cur"] = W.RecSmc(hw, trace, sc.get("smc_info"))
        return call(smc["cur"].start)

    def bar_fence(on):
        for base, _ in b.BT_BAR_EMUL:
            trace.append(("fence", on, base))

    cbs = Io(W.RD(rec.read32), W.WR(rec.write32), W.EC(rec.get_exc_count), W.SG(rec.set_exc_guard),
             W.SL(lambda us: (trace.append(("sleep", us)), clock.sleep(us / 1e6))), W.NW(lambda: clock.us),
             W.NP(rec.nop), W.SS(smc_start), W.SK(key_info),
             W.SW(lambda k, v: call(lambda: smc["cur"].write32("gP0d", v))),
             W.SS(lambda: call(smc["cur"].stop)), W.LG(lambda m: logs.append(m.decode())),
             W.SG(lambda us: None), W.FN(bar_fence), W.NP(lambda: trace.append(("pt",))))
    f = tp.good_facts()
    cf = W.Facts(ecam=f["ecam"], rc=f["rc"], port0=f["port0"], port0_phy=f["port0_phy"], port1=f["port1"],
                 gpio=f["gpio"], smc=f["smc"], gpio_phandle=f["gpio_phandle"], perst_phandle=f["perst_phandle"],
                 perst_pin=f["perst_pin"], perst_polarity=f["perst_polarity"],
                 port1_perst_phandle=f["port1_perst_phandle"], port1_perst_pin=f["port1_perst_pin"],
                 perst_to_config_ms=f["perst_to_config_ms"], t_refclk_to_perst_us=f["t_refclk_to_perst_us"],
                 j274=1, wlan_bcm4378=1, dart0_base=w.DART0_BASE, dart1_base=w.DART1_BASE)
    pg = W.Page()
    regs = w.WifiS259._wlan_regs(tu.fake_info())
    pg.mac_lo, pg.mac_hi, pg.cal_len, pg.antenna = (regs[w.R_MAC_LO], regs[w.R_MAC_HI], regs[w.R_CAL_LEN],
                                                    regs[w.R_ANTENNA])
    for i in range(4):
        pg.board[i] = regs[w.R_BOARD + 4 * i]
    for off, v in regs.items():
        if off >= w.CAL_OFF:
            pg.cal[(off - w.CAL_OFF) // 4] = v
    bp = BtPage()
    br = b.WifiBtS261._bt_regs(sc.get("bt", BT_INFO), tu.fake_info())
    bp.bdaddr_lo, bp.bdaddr_hi = br[b.R_BDADDR_LO], br[b.R_BDADDR_HI]
    bp.cal_len, bp.calbf_len = br[b.R_CAL_LEN], br[b.R_CALBF_LEN]
    for i in range(4):
        bp.board[i] = br[b.R_BOARD + 4 * i]
    for off, v in br.items():
        if b.CAL_OFF <= off < b.CALBF_OFF:
            bp.cal[(off - b.CAL_OFF) // 4] = v
        elif off >= b.CALBF_OFF:
            bp.calbf[(off - b.CALBF_OFF) // 4] = v
    st = ct.create_string_buffer(L.s264w_sizeof())
    L.s264w_init(st, ct.byref(cbs), ct.byref(cf), ct.byref(pg), sc.get("enabled", True))
    L.s264w_bt_init(st, ct.byref(bp), sc.get("bt_enabled", True))
    out = []
    for op in ops:
        k = op[0]
        if k == "boot":
            out.append(("boot", L.s264w_boot_force_off(st)))
        elif k == "fi":
            L.s264w_fence_install(st, op[1])
        elif k == "w":
            L.s264w_hook_write(st, op[1], op[2], op[3])
        elif k == "r":
            out.append(("r", op[1], L.s264w_hook_read(st, op[1], op[2])))
        elif k == "bw":
            L.s264w_bt_hook_write(st, op[1], op[2], op[3])
        elif k == "br":
            out.append(("br", op[1], L.s264w_bt_hook_read(st, op[1], op[2])))
        elif k == "bgen":
            g = L.s264w_bt_hook_read(st, b.R_GEN, 2)
            L.s264w_bt_hook_write(st, b.R_ARM, (g + op[1]) & 0xFFFFFFFF, 2)
        elif k == "fr":
            out.append(("fr", L.s264w_fence_read(st, op[1], {8: 0, 16: 1, 32: 2, 64: 3}[8 << op[2]])))
        elif k == "fw":
            L.s264w_fence_write(st, op[1], op[2])
        elif k == "hw":
            op[1](hw)
    return trace, out, logs


# ------------------------------------------------------------------ scenarios
WDUMP = [("r", o, 2) for o in (w.R_STATE, w.R_ERROR, w.R_AVAIL, w.R_SEQ, w.R_IDS, w.R_BAR0, w.R_EPCMD, w.R_DEVCTL,
                               w.R_DART_CHECK, w.R_CHIPID, w.R_CFG_ERR)]
BDUMP = [("br", o, 2) for o in range(0, 0x58 + 4, 4)] + [("br", o, 2) for o in (0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70,
                                                                               0x74, 0x80, 0x84, 0x1000, 0x2800,
                                                                               0x2800 + 5660, 0x3FFC)]
DUMP = WDUMP + BDUMP
BOOT = [("boot",), ("fi", True)]
WUP = [("w", w.R_CMD, w.CMD_POWER_UP, 2)]
WDOWN = [("w", w.R_CMD, w.CMD_POWER_DOWN, 2)]
ENS = [("bw", b.R_CMD, b.BT_CMD_ENSURE, 2)]
QUI = [("bw", b.R_CMD, b.BT_CMD_QUIET, 2)]
FN1 = tb.FN1_MSI
FENCEP = [("fr", b.BT_BAR_EMUL[1][0] + 0x20045C, 2), ("fr", b.BT_BAR_EMUL[0][0], 3), ("fw", b.BT_BAR_EMUL[0][0], 2)]


def bt_cfg(off, val):
    return [("bw", b.R_CFG_OFF, off, 2), ("bw", b.R_CFG_DATA, val, 2), ("br", b.R_CFG_ERR, 2)]


CFG = ([("bw", b.R_CFG_OFF, 0, 2), ("br", b.R_CFG_DATA, 2)]
       + bt_cfg(0x80, 0x18002000) + bt_cfg(0x70, 0x1810A000) + bt_cfg(0x74, 0x18011000) + bt_cfg(0x78, 0x18107000)
       + bt_cfg(0x84, 0x19000000) + bt_cfg(0x80, 0x18002004)
       + [("bw", b.R_CFG_OFF, 0x88, 2), ("br", b.R_CFG_DATA, 2)] + bt_cfg(0x88, 0x00090000)
       + bt_cfg(0x18, 0) + bt_cfg(FN1, b.MSI_ENABLE) + bt_cfg(FN1 + 4, 0x00200000) + bt_cfg(FN1 + 8, 1)
       + bt_cfg(FN1 + 8, 0) + bt_cfg(FN1 + 0xC, 0x12345678) + bt_cfg(FN1, b.MSI_ENABLE)
       + bt_cfg(0x48 + 8, 0xFFFF) + bt_cfg(0x48 + 0x10, 0xFFFFFFFF) + bt_cfg(0x04, wu.CMD_BME | wu.CMD_INTX_DIS)
       + bt_cfg(0x81, 0) + [("bw", b.R_CFG_OFF, 0x1000, 2), ("br", b.R_CFG_DATA, 2), ("br", b.R_CFG_ERR, 2)])


def chip_reset(hw):
    for fn in (0, 1):
        for k in (0x10, 0x14, 0x18, 0x1C, 0x04):
            hw.fn[fn]["cfg"][k] = hw.bar_orig[fn][k]


def fn1_reset(hw):
    for k in (0x10, 0x14, 0x18, 0x1C):
        hw.fn[1]["cfg"][k] = hw.bar_orig[1][k]
    hw.fn[1]["cfg"][0x04] = 0x00100000


SCENARIOS = [
    ("boot fenced, Wi-Fi up assigns fn1, config access, quiet/ensure, arm", {},
     BOOT + DUMP + FENCEP + WUP + DUMP + [("bgen", -1), ("bgen", 0)] + DUMP + FENCEP + CFG + DUMP + QUI + DUMP
     + bt_cfg(0x04, wu.CMD_BME) + ENS + DUMP + [("bgen", 0)] + DUMP),
    ("fence on the Wi-Fi chip reset bracket (fn0 0xBC)", {},
     BOOT + WUP + [("bgen", 0)] + [("w", w.R_CFG_OFF, wu.EP_LINKCTL_CFG, 2), ("w", w.R_CFG_DATA, 0, 2)] + DUMP + FENCEP),
    ("no fence on the Wi-Fi chip reset (NWOAS_BT_FENCE_WIFI_RESET=0)", {},
     [("boot",), ("fi", False)] + WUP + [("bgen", 0)]
     + [("w", w.R_CFG_OFF, wu.EP_LINKCTL_CFG, 2), ("w", w.R_CFG_DATA, 0, 2)] + DUMP),
    ("power down refences before power off, stale arm, BT ensure powers again", {},
     BOOT + WUP + [("bgen", 0)] + WDOWN + DUMP + [("bgen", -1), ("hw", chip_reset)] + ENS + DUMP + [("bgen", 0)] + DUMP),
    ("quiet fences, chip reset cleared fn1, arm restores BARs", {},
     BOOT + WUP + QUI + ENS + [("hw", fn1_reset), ("bgen", 0)] + DUMP),
    ("BT _INI before Wi-Fi _INI powers the chip", {}, BOOT + ENS + DUMP + WUP + DUMP),
    ("BT disabled (NWOAS_BT=0) = S259", {"bt_enabled": False}, BOOT + WUP + ENS + QUI + DUMP + FENCEP),
    ("Wi-Fi disabled hides both", {"enabled": False}, [("fi", True)] + ENS + WUP + DUMP + CFG[:2]),
    ("fn1 BAR2 assignment does not stick -> fn1 FAILED, Wi-Fi up", {"hw_kw": {"fn1_bar_stuck": True}},
     BOOT + WUP + DUMP + ENS + DUMP),
    ("chip failed: ensure does not retry", {"hw_kw": {"device_present": False}}, BOOT + WUP + ENS + DUMP),
    ("FLR with capability", {}, BOOT + WUP + bt_cfg(0x04, wu.CMD_BME | wu.CMD_INTX_DIS)
     + [("bw", b.R_RESET, 1, 2), ("bw", b.R_RESET, 2, 2)] + DUMP),
    ("FLR without capability", {"hw_kw": {"flr_cap": False}}, BOOT + WUP + [("bw", b.R_RESET, 1, 2)] + DUMP),
    ("no BD address / calibration in the ADT", {"bt": {"bdaddr": None, "cal": b"", "calbf": bytes(0x1900)}},
     BOOT + WUP + DUMP),
    ("64-bit and unaligned BT writes ignored, 8/16/64-bit reads", {},
     BOOT + [("bw", b.R_CMD, b.BT_CMD_ENSURE, 3), ("bw", b.R_CMD + 1, 1, 0)] + DUMP
     + [("br", b.R_MAGIC + 1, 0), ("br", b.R_MAGIC + 2, 1), ("br", b.R_MAGIC, 3), ("br", 0x1000, 3)]),
]


def page_rows(L):
    """s264_bt_page_init (C, plan + executor) vs bt_info + _bt_regs + the bt_module.py refusal, on both
    ADT dumps (the BD address is compared, never printed)."""
    from m1n1.adt import load_adt
    L.s264_wifi_page_init.argtypes = [ct.c_void_p, ct.c_uint32, ct.POINTER(W.Page)]
    L.s264_bt_page_init.argtypes = [ct.c_void_p, ct.c_uint32, ct.POINTER(W.Page), ct.POINTER(BtPage), ct.c_bool]
    rows, ok = [], True
    for path in W.ADTS:
        if not path.is_file():
            continue
        raw = path.read_bytes()
        adt = load_adt(raw)
        info = b.bt_info(adt)
        wlan = w.wu.wlan_info(adt)
        regs = b.WifiBtS261._bt_regs(info, wlan)
        py_ref = 0 if not (info["bdaddr"] is None or not 0 < len(info["calbf"]) <= b.CAL_MAX
                           or not 0 < len(info["cal"]) <= b.CAL_MAX) else "refused"
        buf = ct.create_string_buffer(raw, len(raw))
        pg, bp = W.Page(), BtPage()
        L.s264_wifi_page_init(buf, len(raw), ct.byref(pg))
        rc_on = L.s264_bt_page_init(buf, len(raw), ct.byref(pg), ct.byref(bp), True)
        rc_off = L.s264_bt_page_init(buf, len(raw), ct.byref(pg), ct.byref(bp), False)
        c = {b.R_BDADDR_LO: bp.bdaddr_lo, b.R_BDADDR_HI: bp.bdaddr_hi, b.R_CAL_LEN: bp.cal_len,
             b.R_CALBF_LEN: bp.calbf_len}
        for i in range(4):
            c[b.R_BOARD + 4 * i] = bp.board[i]
        for i in range((bp.cal_len + 3) // 4):
            c[b.CAL_OFF + 4 * i] = bp.cal[i]
        for i in range((bp.calbf_len + 3) // 4):
            c[b.CALBF_OFF + 4 * i] = bp.calbf[i]
        diff = sorted(k for k in set(regs) | set(c) if regs.get(k) != c.get(k))
        good = not diff and (rc_on == 0) == (py_ref == 0) and rc_off == 0
        rows.append((f"BT page from the ADT {path.parent.name}/{path.name}", "SAME" if good else "DIFF",
                     f"{len(regs)} registers, cal {bp.cal_len} B, cal-bf {bp.calbf_len} B, BD address "
                     f"{'present' if info['bdaddr'] else 'missing'}; NWOAS_BT=1 check C rc {rc_on} / Python "
                     f"{py_ref}; differing offsets {[hex(x) for x in diff[:4]]}"))
        ok &= good
    return rows, ok


def rows_for(L):
    rows, ok = page_rows(L)
    for name, sc, ops in SCENARIOS:
        pt, pout, _ = run_py(sc, ops)
        ct_, cout, _ = run_c(L, sc, ops)
        pn, cn = W.collapse(pt), W.collapse(ct_)
        first = next((i for i, (x, y) in enumerate(zip(pn, cn)) if x != y), min(len(pn), len(cn)))
        same = pn == cn and pout == cout
        nm = sum(1 for e in pn if e[0] in "rw")
        nf = sum(1 for e in pn if e[0] == "fence")
        st = [v for k, o, v in [x for x in pout if x[0] == "br"] if o == b.R_STATE]
        det = (f"{len(pt)}/{len(ct_)} events ({nm} MMIO, {nf} fence changes); last BT state "
               f"{st[-1] if st else '-'}"
               + ("" if pn == cn else f"; first trace difference #{first}: Python "
                  f"{pn[first] if first < len(pn) else '-'} / C {cn[first] if first < len(cn) else '-'}")
               + ("" if pout == cout else f"; reads differ: {[(x, y) for x, y in zip(pout, cout) if x != y][:3]}"))
        rows.append((f"bt: {name}", "SAME" if same else "DIFF", det))
        ok &= same
    pt, pout, _ = run_py(*SCENARIOS[0][1:])
    ct_, cout, _ = run_c(L, *SCENARIOS[0][1:])
    cn = W.collapse(ct_)
    k = next(i for i, e in enumerate(cn) if e[0] == "fence")
    neg = {"one fence change dropped": cn[:k] + cn[k + 1:] != W.collapse(pt),
           "one BT page value changed": cout[:-1] + [(cout[-1][0], cout[-1][1], cout[-1][2] ^ 1)] != pout}
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
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264e-bt-"))
    work.mkdir(parents=True, exist_ok=True)
    L = build_lib(a.src, work)
    rows, ok = rows_for(L)
    print("| item | result | detail |\n|---|---|---|")
    for r in rows:
        print(f"| {r[0]} | {r[1]} | {r[2]} |")
    print(f"[s264E-bt] {sum(1 for r in rows if r[1] == 'SAME')}/{len(rows)} SAME")
    print("S264E BT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
