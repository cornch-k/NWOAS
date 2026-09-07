# NWOAS EXP-5 5c — FL1100 boot-device xHCI status diag at the mid-read STALL (kmutil-free).
#
# exp5c-v7 broke the DCBAAP=0 wall: DCBAA lands in the low DMA window, Enable-Slot succeeds, and
# the FL1100 DMA-reads boot.wim to ~201MB (LBA 0x64820) via DART Maps. It then STALLS mid-read
# (~34% of a ~590MB boot.wim) while bootmgfw spins in a timer-poll loop (VGATE flood). Every
# DMA-mechanism hypothesis is ruled out (window 4GB verified, IOVA 1.06/3.75GB not exhausted,
# DART L2 flat 4GB, ERR=0, coherency). The open fork:
#   FL1100 USBSTS.HCH|HSE|HCE set at the stall  => controller HALTED (device-side: event-ring
#                                                  overflow vs slow tethered drain, or a real HSE)
#   FL1100 USBSTS running (HCH=0)               => the stall is guest/bootmgfw-side (poll wedge)
#
# The guest (EL1) cannot touch the FL1100 BAR (L2C bus error, pcie_emul-exp5.py:3-8); the hv (EL2)
# can. So read the controller's OWN operational/runtime registers over the proxy (hv.p.read32/64,
# executed at EL2 on the mini). Register-read template lifted verbatim from nwoas_capture_hook.py
# :109 xhci_dump, retargeted from the Apple dwc3 (0x502280000) to the FL1100 BAR (0x6c0000000).
# All reads are non-destructive status; this hook never writes device state.
#
# Load AFTER pcie_emul-exp5.py:
#   run_guest.py -r m1n1-payload-exp5c-v7.bin -m pcie_emul-exp5.py -m nwoas_fl1100_diag.py
# It chains pcie_emul's run_shell so the early one-shot backing fix still applies on break-in.

import threading, time, traceback
from m1n1.proxy import EXC_RET   # break-in handler returns EXC_RET.HANDLED (resume guest)

FL_BAR = 0x6c0000000   # FL1100 xHCI BAR0 (PCIe MMIO32 window base; pcie_emul BYPASS/SPTE_MAP target)


def _fl_dump(hv, tag):
    """READ-ONLY dump of the FL1100 xHCI halt/error + event-ring state, via EL2 proxy reads."""
    try:
        rd32 = lambda pa: (hv.p.read32(pa) & 0xffffffff)
        rd64 = lambda pa: (hv.p.read64(pa) & 0xffffffffffffffff)
        cap0 = rd32(FL_BAR)
        caplen = cap0 & 0xff
        if caplen == 0 or caplen == 0xff:
            print(f"NWOAS-FLDIAG {tag}: FL1100 BAR not up yet (cap0={cap0:#x}) -- skip")
            return
        op = FL_BAR + caplen
        hcsp1 = rd32(FL_BAR + 0x04); nports = (hcsp1 >> 24) & 0xff
        rtsoff = rd32(FL_BAR + 0x18) & ~0x1f
        rt = FL_BAR + rtsoff
        usbcmd = rd32(op + 0x00); usbsts = rd32(op + 0x04)
        crcr   = rd64(op + 0x18); dcbaap = rd64(op + 0x30); config = rd32(op + 0x38)
        iman   = rd32(rt + 0x20); imod = rd32(rt + 0x24); erstsz = rd32(rt + 0x28)
        erstba = rd64(rt + 0x30); erdp = rd64(rt + 0x38)
        # USBSTS: HCH(0) HSE(2) EINT(3) PCD(4) SSS(8) RSS(9) SRE(10) CNR(11) HCE(12)
        hch=(usbsts&1); hse=(usbsts>>2)&1; eint=(usbsts>>3)&1; pcd=(usbsts>>4)&1
        sre=(usbsts>>10)&1; cnr=(usbsts>>11)&1; hce=(usbsts>>12)&1
        verdict = ("HALTED(device-side)" if (hch or hse or hce)
                   else "RUNNING(stall is guest/bootmgfw-side)")
        print(f"NWOAS-FLDIAG {tag}: ★USBSTS={usbsts:#x} HCH={hch} HSE={hse} HCE={hce} "
              f"SRE={sre} CNR={cnr} EINT={eint} PCD={pcd} => {verdict}")
        print(f"NWOAS-FLDIAG {tag}: USBCMD={usbcmd:#x}(RS={usbcmd&1} HCRST={(usbcmd>>1)&1} "
              f"INTE={(usbcmd>>2)&1} HSEE={(usbcmd>>3)&1}) CONFIG={config:#x} "
              f"DCBAAP={dcbaap:#x} CRCR={crcr:#x}")
        print(f"NWOAS-FLDIAG {tag}: IMAN={iman:#x}(IP={iman&1} IE={(iman>>1)&1}) IMOD={imod:#x} "
              f"ERSTSZ={erstsz:#x} ERSTBA={erstba:#x} ERDP={erdp:#x}(EHB={(erdp>>3)&1})")
        for n in range(max(1, min(nports, 4))):
            ps = rd32(op + 0x400 + n*0x10)
            print(f"NWOAS-FLDIAG {tag}: PORTSC[{n}]={ps:#x}(CCS={ps&1} PED={(ps>>1)&1} "
                  f"PLS={(ps>>5)&0xf} PP={(ps>>9)&1} CSC={(ps>>17)&1})")
        # Event ring dequeue TRB: an unconsumed event (guest not draining) or a Transfer-Event with
        # a non-success completion code points at event-ring overflow / a failed transfer at the stall.
        if erstba and (erstba >> 40) == 0:
            seg0 = rd64(erstba)
            if seg0:
                dq = erdp & ~0xf
                trb = [rd32(dq), rd32(dq+4), rd32(dq+8), rd32(dq+12)]
                cc = (trb[2] >> 24) & 0xff          # completion code (1=Success, 21=EventRingFull...)
                ty = (trb[3] >> 10) & 0x3f           # TRB type (32=TransferEvent, 33=CmdCompletion)
                print(f"NWOAS-FLDIAG {tag}: EVT base={seg0:#x} DQ@{dq:#x} "
                      f"TRB={[hex(x) for x in trb]} type={ty} cyc={trb[3]&1} compcode={cc}")
    except Exception:
        traceback.print_exc()


# --- chain run_shell so the FL1100 dump runs in break-in (main-thread) context ---
# CRITICAL: chain pcie_emul-exp5.py's prior run_shell FIRST. Its early one-shot breaker may break
# in before ours (during DXE) to apply the backing fix; if we replaced run_shell without chaining,
# that early fix would never run and the boot would corrupt. Calling it after the fix is a guarded
# no-op, so chaining is always safe.
_fld_prior = hv.run_shell            # noqa: F821  (hv injected by run_guest.py -m scope)
_fld_n = {"n": 0}

def _fld_run_shell(entry_msg="", exit_msg="", **kw):
    try:
        _fld_prior(entry_msg=entry_msg, exit_msg=exit_msg, **kw)   # backing fix (guarded) + chain
    except Exception:
        traceback.print_exc()
    _fld_n["n"] += 1
    _fl_dump(hv, f"BK#{_fld_n['n']}")                              # noqa: F821
    return EXC_RET.HANDLED

hv.run_shell = _fld_run_shell        # noqa: F821


# --- breaker: burst-interrupt at several late offsets to catch the persistent ~201MB stall ---
# Prior runs reach the stall ~3 min after launch and then spin indefinitely, so 240/330/420/540s
# offsets straddle the transition (running -> halted?) and confirm persistence. Burst pattern
# copied from pcie_emul-exp5.py:_exp5_breaker: hv.interrupt() only writes "!" when the host is not
# _in_handler, and on the slow tethered hv the host is mid-handler most of the boot, so a single
# interrupt almost never lands -- spin at ~0.4s calling it only in idle windows until it lands.
def _fld_breaker():
    t0 = time.time()
    for target in (240, 330, 420, 540, 720):
        while time.time() - t0 < target:
            time.sleep(2)
        landed = {"before": _fld_n["n"]}
        busy = idle = 0
        for _ in range(30):                                       # ~12s burst
            if _fld_n["n"] > landed["before"]:
                break                                             # a break-in landed
            if getattr(hv, "_in_handler", False):                 # noqa: F821
                busy += 1
            else:
                idle += 1
                try:
                    hv.interrupt()                                # noqa: F821
                except Exception:
                    pass
            time.sleep(0.4)
        try:
            hv.log(f"NWOAS-FLDIAG breaker t={int(time.time()-t0)}s target={target}s "
                   f"landed={_fld_n['n'] > landed['before']} idle_writes={idle} busy_hits={busy}")
        except Exception:
            pass

if not getattr(hv, "_nwoas_fld_started", False):                  # noqa: F821
    hv._nwoas_fld_started = True                                  # noqa: F821
    threading.Thread(target=_fld_breaker, daemon=True, name="nwoas-fld-breaker").start()
    print("[emul] NWOAS-FLDIAG hook installed (FL1100 USBSTS at stall; run_shell chained, "
          "breaker started)")
