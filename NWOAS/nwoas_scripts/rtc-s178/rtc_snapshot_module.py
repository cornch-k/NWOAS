"""DRAFT (S178) — NOT WIRED INTO ANY LAUNCHER. Do not pass to run_guest.py until reviewed.

Read-only pre-boot RTC snapshot for the m1n1 hypervisor guest (run_guest.py -m module).

What it does when NWOAS_RTC_SNAPSHOT=1:
  1. Locates the NUB SPMI controller and primary PMU from the *host* ADT (hv.u.adt):
       /arm-io/nub-spmi        reg[0]  -> controller MMIO base (J274: 0x23d0d9300, len 0x100)
       /arm-io/nub-spmi/spmi-pmu reg[0] -> slave id (J274: 0xf), plus
         info-rtc          (J274: 0xd002)  48-bit 32768 Hz counter   [hypothesis: == SMC key CLKM]
         info-rtc_scrpad   (J274: 0xd100)  48-bit offset (Linux nvmem cell "rtc_offset" @0xd100 len 6)
  2. Reads CNTPCT_EL0 (host, EL2) -> SPMI EXT_READL 6 bytes @info-rtc -> 6 bytes @info-rtc_scrpad
     -> CNTPCT_EL0 again.  SPMI reads are bus READ transactions; the only MMIO writes are the
     controller CMD-FIFO words that issue those reads (m1n1.hw.spmi.SPMI.read). No PMU register
     is written. No SMC traffic.
  3. Converts with the Linux rtc-macsmc formula (rtc_math.rtc_to_epoch), applies a plausibility
     window, and if valid stores a 32-byte blob in the *guest* ADT: /chosen "nwoas,rtc-snapshot".
     The guest ADT is serialized by hv.start(), copied by guest m1n1 into boot_args.devtree, then
     by UEFI PrePi/AdtParser.c into PcdAdtPointer memory, where AppleDTLib dt_get_prop() can read it.
  4. Always writes a JSON record (raw bytes, both CNTPCT samples, computed UTC, host wall clock)
     to $NWOAS_LINK_DIR/rtc-snapshot.json or ./rtc-snapshot.json for offline comparison.

Default (env unset or 0) = DRY RUN: no MMIO access, no ADT change; only prints what it would do.
Consumer side (UEFI RealTimeClockLib reading this property) does not exist yet — see PLAN.md.
"""
import json
import os
import struct
import sys
import time

_here = os.path.dirname(os.path.abspath(__file__))
if _here not in sys.path:
    sys.path.insert(0, _here)
import rtc_math  # noqa: E402

PROP = "nwoas,rtc-snapshot"
_enabled = os.environ.get("NWOAS_RTC_SNAPSHOT", "0") == "1"


def _pmu_info(adt):
    ctrl = adt["/arm-io/nub-spmi"]
    pmu = adt["/arm-io/nub-spmi/spmi-pmu"]
    base, size = ctrl.get_reg(0)
    sid = int(pmu.reg[0]) if not isinstance(pmu.reg[0], int) else pmu.reg[0]
    rtc = int(pmu.getprop("info-rtc"))
    scr = int(pmu.getprop("info-rtc_scrpad"))
    return base, size, sid, rtc, scr


def _record(rec):
    out_dir = os.environ.get("NWOAS_LINK_DIR") or os.getcwd()
    path = os.path.join(out_dir, "rtc-snapshot.json")
    with open(path, "w") as f:
        json.dump(rec, f, indent=2)
    hv.log(f"[rtc-s178] record written: {path}")


def rtc_snapshot():
    adt_host = hv.u.adt
    base, size, sid, reg_ctr, reg_off = _pmu_info(adt_host)
    hv.log(f"[rtc-s178] nub-spmi base={base:#x} len={size:#x} pmu sid={sid:#x} "
           f"ctr@{reg_ctr:#x} off@{reg_off:#x} enabled={_enabled}")
    rec = {
        "host_wallclock_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "host_wallclock_epoch": int(time.time()),
        "spmi_base": base, "pmu_sid": sid, "reg_ctr": reg_ctr, "reg_off": reg_off,
        "enabled": _enabled,
    }
    if not _enabled:
        hv.log("[rtc-s178] DRY RUN: no SPMI access, guest ADT untouched")
        _record(rec)
        return None

    from m1n1.hw.spmi import SPMI  # noqa: WPS433
    spmi = SPMI(hv.u, "/arm-io/nub-spmi")
    cnt0 = hv.u.mrs("CNTPCT_EL0")
    ctr_b = spmi.read(sid, reg_ctr, rtc_math.RTC_BYTES)
    off_b = spmi.read(sid, reg_off, rtc_math.RTC_BYTES)
    cnt1 = hv.u.mrs("CNTPCT_EL0")
    cntfrq = hv.u.mrs("CNTFRQ_EL0")

    ctr = rtc_math.le48(ctr_b)
    off = rtc_math.le48(off_b)
    epoch, ticks = rtc_math.rtc_to_epoch_ticks(ctr, off)
    ok = rtc_math.plausible(epoch)
    rec.update({
        "ctr_hex": ctr_b.hex(), "off_hex": off_b.hex(), "ctr": ctr, "off": off,
        "epoch": epoch, "sub_ticks_32768": ticks, "utc": rtc_math.epoch_to_utc_str(epoch),
        "cntpct0": cnt0, "cntpct1": cnt1, "cntfrq": cntfrq,
        "delta_vs_host_s": epoch - int(time.time()), "plausible": ok,
    })
    _record(rec)
    hv.log(f"[rtc-s178] PMU ctr={ctr:#x} off={off:#x} -> {rec['utc']} "
           f"(host delta {rec['delta_vs_host_s']:+d}s) plausible={ok} "
           f"read window {cnt1 - cnt0} ticks @ {cntfrq} Hz")
    if not ok:
        hv.log("[rtc-s178] implausible value: guest ADT untouched")
        return None

    cnt_mid = cnt0 + (cnt1 - cnt0) // 2
    blob = rtc_math.pack_snapshot(epoch, cnt_mid, cntfrq,
                                  rtc_math.SNAP_FLAG_VALID | rtc_math.SNAP_SRC_SPMI)
    hv.adt["/chosen"]._properties[PROP] = blob
    hv.log(f"[rtc-s178] guest ADT /chosen {PROP} = {blob.hex()}")
    return rec


rtc_snapshot()
