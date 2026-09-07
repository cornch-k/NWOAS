# pcie_emul-exp5-fldiag3.py (NWOAS §3.2 diagnostic v3) — DECISIVE root-cause disambiguation.
# Superset of fldiag2: FL1100 kernel-phase dump + HCCPARAMS1.AC64 + dwc3 GCTL, PLUS the decisive
# LOW-WINDOW ADOPTION SWEEP the root-cause analysis flagged:
#   Root cause is either (i) Windows never adopts the aliased low [0,4GB) window as usable RAM
#   (=> AllocateCommonBuffer has nowhere sub-4GB to land => NULL => DCBAAP=0), or (ii) the window IS
#   usable but the DMA path bails. This module reads the WINDOW BACKING physical RAM (the hv map_hw
#   target = top win_size of RAM) and scans for Windows kernel POOL TAGS + nonzero footprint:
#     - ~zero footprint / 0 pool tags => Windows NEVER adopted the window (root = low-window-adoption).
#     - pool tags present            => window IS usable RAM to Windows (root = DMA-path bail; check AC64).
# All reads are host-side EL2 proxy reads (hv.p / hv.iface). READ-ONLY. No reboot needed beyond boot.
import os, time, threading
from m1n1.utils import irange
from m1n1.hv.types import TraceMode
from m1n1.proxy import EXC_RET

print("[fldiag3] pcie_init ->", p.pcie_init())
hv.add_tracer(irange(0x6c0000000, 0x100000), "pcie-fl1100-emul", TraceMode.BYPASS)
hv.add_tracer(irange(0x681000000, 0x3000000), "apcie-dart-emul", TraceMode.BYPASS)
_wb = int(os.environ.get("NWOAS_WIN_BASE") or "0x80000000", 0)
_ws = int(os.environ.get("NWOAS_WIN_SIZE") or "0x20000000", 0)
_sk = int(os.environ.get("NWOAS_WIN_SKEW") or "0x34000", 0)
_backing = hv.u.ba.phys_base + hv.u.ba.mem_size - _ws - _sk
hv.map_hw(_wb, _backing, _ws)
hv.log(f"[fldiag3] map_hw IPA {_wb:#x}+{_ws:#x} -> backing {_backing:#x} (phys_base={hv.u.ba.phys_base:#x} mem_size={hv.u.ba.mem_size:#x})")

FL_BAR = 0x6c0000000
FL_CFG = 0x690200000
DWC3_GCTL = 0x50228C110

_KNOWN_TAGS = {
    'MmSt','Mm  ','MmCa','File','Ntf ','Ntfx','Even','EtwB','Toke','Thre','Proc','PsWs','Vad ','VadS',
    'Se  ','ObNm','Obtb','Dire','CM  ','CM25','CM31','CcSc','MDL ','Irp ','IrpM','Devi','Driv','Gpnp',
    'PnpD','PnpF','Wait','Cont','Ttfd','Usbx','Ucx ','usbp','UsbP','Uhub','Ubhb','Ubhc','PciB','Pool',
    'ViMm','Wdf ','FSfm','LStr','usbc','DxgK','Sysm','KSem','VmMp',
}
def _is_tag(b4):
    c0 = b4[0]
    if not (65 <= c0 <= 90 or 97 <= c0 <= 122):
        return False
    for c in (b4[1], b4[2], b4[3]):
        if not (65 <= c <= 90 or 97 <= c <= 122 or 48 <= c <= 57 or c == 32):
            return False
    return True
def _pool_scan(buf):
    hits = {}; total = 0; n = len(buf); off = 4
    while off + 4 <= n:
        cand = bytes((buf[off], buf[off+1], buf[off+2], buf[off+3] & 0x7f))
        if _is_tag(cand):
            t = cand.decode('latin1'); hits[t] = hits.get(t, 0) + 1; total += 1
        off += 16
    known = {t: c for t, c in hits.items() if t in _KNOWN_TAGS}
    return total, known

def _rd_phys(pa, n):
    try:
        b = hv.iface.readmem(pa, n)
        return bytes(b) if (b is not None and len(b) == n) else None
    except Exception as e:
        hv.log(f"[fldiag3] readmem({pa:#x},{n}) err: {e}"); return None

def _window_sweep():
    # Read the window BACKING physical RAM at spread offsets across [0, win_size); a chunk that
    # holds Windows pool tags proves Windows wrote kernel pool there => it adopted the window as RAM.
    hv.log(f"[fldiag3] ===== LOW-WINDOW ADOPTION SWEEP (backing {_backing:#x}, size {_ws:#x}) =====")
    SCAN = 16 * 1024   # 16KB per probe (serial-drop budget)
    fracs = (0.0, 0.05, 0.12, 0.25, 0.4, 0.55, 0.7, 0.85, 0.97)
    tot_nz = 0; tot_bytes = 0; tot_tags = 0; tot_known = 0; probes = 0
    known_agg = {}
    for fr in fracs:
        off = int(_ws * fr) & ~0x3fff
        buf = _rd_phys(_backing + off, SCAN)
        if buf is None:
            hv.log(f"[fldiag3]  sweep +{off:#x} READ FAILED (link drop) -- region invalid"); continue
        nz = sum(1 for c in buf if c != 0)
        total, known = _pool_scan(buf)
        for t, c in known.items(): known_agg[t] = known_agg.get(t, 0) + c
        probes += 1; tot_nz += nz; tot_bytes += len(buf); tot_tags += total; tot_known += sum(known.values())
        ks = ", ".join(f"'{t}'x{c}" for t, c in sorted(known.items(), key=lambda kv:-kv[1])[:6]) or "(none)"
        hv.log(f"[fldiag3]  sweep +{off:#010x} nz={nz}/{len(buf)} pooltag_cand={total} known={sum(known.values())} [{ks}]")
    if probes == 0:
        hv.log("[fldiag3]  ★SWEEP INVALID (all reads failed)"); return
    nzpct = (100 * tot_nz) // max(tot_bytes, 1)
    ktop = dict(sorted(known_agg.items(), key=lambda kv:-kv[1])[:12])
    hv.log(f"[fldiag3]  ★SWEEP TOTAL: probes={probes} nonzero={nzpct}% pooltag_cands={tot_tags} KNOWN_WINDOWS_TAGS={tot_known} {ktop}")
    if tot_known >= 8:
        hv.log("[fldiag3]  ★VERDICT: window ADOPTED by Windows (kernel pool tags present) -> root=DMA-path bail (check AC64/scratchpad), NOT window-adoption.")
    elif nzpct < 3 and tot_known == 0:
        hv.log("[fldiag3]  ★VERDICT: window NOT ADOPTED (near-zero, no pool tags) -> root=low-window-adoption failure. Fix must make window real kernel RAM (or give Windows real sub-4GB backing).")
    else:
        hv.log("[fldiag3]  ★VERDICT: AMBIGUOUS (some nonzero, few/no known tags) -> UEFI leftovers likely; not conclusively adopted. Widen scan / re-run.")

def _dwc3_gctl():
    try:
        g = hv.p.read32(DWC3_GCTL) & 0xffffffff
        m = (g >> 12) & 0x3
        name = {0:"reserved",1:"HOST",2:"DEVICE",3:"OTG"}.get(m, "?")
        hv.log(f"[fldiag3] ★DWC3(USB-C) GCTL={g:#x} PRTCAPDIR={m}({name}) Track2:{'need Host switch' if name=='DEVICE' else name}")
    except Exception as e:
        hv.log(f"[fldiag3] dwc3 gctl err: {e}")

def _fldump():
    rd32 = lambda a: hv.p.read32(a) & 0xffffffff
    rd64 = lambda a: rd32(a) | (rd32(a + 4) << 32)
    cmd = rd32(FL_CFG + 0x04) & 0xffff
    bar0 = rd32(FL_CFG + 0x10)
    hv.log(f"[fldiag3] ★FL1100 CFG: CMD={cmd:#06x}(MEM={(cmd>>1)&1} BM={(cmd>>2)&1}) BAR0={bar0:#x}")
    if not ((cmd >> 1) & 1):
        hv.log("[fldiag3] ★CMD.MEM=0 -> FL1100 MMIO decode OFF. §3.2=controller-not-started.")
        return
    cap0 = rd32(FL_BAR); caplen = cap0 & 0xff
    hccp1 = rd32(FL_BAR + 0x10); ac64 = hccp1 & 1
    hv.log(f"[fldiag3] ★HCCPARAMS1={hccp1:#x} AC64={ac64}({'64bit-capable' if ac64 else '32bit-only'})")
    op = FL_BAR + caplen
    rtsoff = rd32(FL_BAR + 0x18) & ~0x1f; rt = FL_BAR + rtsoff
    hcs1 = rd32(FL_BAR + 0x04); nports = (hcs1 >> 24) & 0xff
    usbcmd = rd32(op + 0x00); usbsts = rd32(op + 0x04)
    dcbaap = rd64(op + 0x30); crcr = rd64(op + 0x18); config = rd32(op + 0x38)
    erstsz = rd32(rt + 0x28); erstba = rd64(rt + 0x30); erdp = rd64(rt + 0x38); iman = rd32(rt + 0x20)
    hv.log(f"[fldiag3] ★OP: USBCMD={usbcmd:#x}(RS={usbcmd&1} INTE={(usbcmd>>2)&1}) USBSTS={usbsts:#x}(HCH={usbsts&1} HSE={(usbsts>>2)&1} EINT={(usbsts>>3)&1}) caplen={caplen:#x} nports={nports}")
    hv.log(f"[fldiag3] ★DCBAAP={dcbaap:#x} CRCR={crcr:#x} CONFIG={config:#x}")
    hv.log(f"[fldiag3] ★ERST: sz={erstsz:#x} ERSTBA={erstba:#x} ERDP={erdp:#x} IMAN={iman:#x}(IP={iman&1} IE={(iman>>1)&1})")
    for n in range(min(max(nports, 1), 4)):
        ps = rd32(op + 0x400 + n * 0x10)
        hv.log(f"[fldiag3] ★PORTSC[{n}]={ps:#x}(CCS={ps&1} PED={(ps>>1)&1} PLS={(ps>>5)&0xf} PP={(ps>>9)&1} CSC={(ps>>17)&1})")
    for nm, v in (("DCBAAP", dcbaap), ("ERSTBA", erstba)):
        loc = "HIGH>4GB(UNREACHABLE by 32bit FL1100!)" if v >= 0x100000000 else ("WINDOW<4GB(reachable)" if v else "UNSET")
        hv.log(f"[fldiag3] ★{nm} placement: {v:#x} = {loc}")

hv._fldiag_n = 0
hv._fldiag_dumps = 0
def _diag_run_shell(entry_msg="", exit_msg="", **kw):
    hv._fldiag_n += 1
    if hv._fldiag_dumps < 5 and (hv._fldiag_n == 1 or (hv._fldiag_n % 20) == 0):
        hv._fldiag_dumps += 1
        hv.log(f"[fldiag3] === DUMP #{hv._fldiag_dumps} (break-in {hv._fldiag_n}) ===")
        try:
            if hv._fldiag_dumps == 1:
                _dwc3_gctl()
            _fldump()
            if hv._fldiag_dumps in (2, 4):    # sweep on later dumps (Windows has run longer)
                _window_sweep()
        except Exception as e:
            try: hv.log(f"[fldiag3] dump err: {e}")
            except Exception: pass
        hv.log(f"[fldiag3] === dump #{hv._fldiag_dumps} done ===")
    return EXC_RET.HANDLED
hv.run_shell = _diag_run_shell

def _log_has(pat):
    lp = os.environ.get("NWOAS_LOG")
    if not lp: return False
    try:
        with open(lp, "r", errors="ignore") as f:
            return pat in f.read()
    except Exception:
        return False

def _breaker():
    t = 0
    while not _log_has("elr=0xfffff8"):
        time.sleep(5); t += 5
        if t > 1500:
            hv.log("[fldiag3] kernel marker not seen in 25min; bursting anyway"); break
    hv.log("[fldiag3] kernel detected; settling 180s then bursting")
    time.sleep(180)
    for i in range(160):
        if hv._fldiag_dumps >= 5: break
        try: hv.interrupt()
        except Exception: pass
        time.sleep(3)
    hv.log(f"[fldiag3] breaker done ({hv._fldiag_dumps} dumps, {hv._fldiag_n} break-ins)")

threading.Thread(target=_breaker, daemon=True).start()
print("[fldiag3] FL1100 kernel-phase dump (v3: +AC64 +GCTL +WINDOW-ADOPTION-SWEEP) armed")
