#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264F hardware step (b) (copy of s264e_launch.py): + power_module.py in C (--no-pwr: off;
every NWOAS_PWR_* switch off as in the S268 launcher).
NWOAS S264E hardware step (b) (copy of s264d_launch.py): s264E config = s264D + the S261 module set in C
(--no-bt: NWOAS_BT=0, --s259: wifi_module.py set = needs the S259 payload, --no-fence-on-wifi-reset),
sound protection default moderate.
NWOAS S264D hardware step (b) (copy of s264c_launch.py): s264D config = s264C + sound stage B/S in C
(--no-audio-b: off, --audio-prot sense|moderate|full) + NWOAS_WIFI=1 in C (--no-wifi: control page DISABLED).
NWOAS S264C hardware step (b) (copy of s264_launch.py): s264C config (exception policy LOCAL, S262 QD=32),
every forwarded event is logged with its ESR class and the totals are printed even if the link drops.
NWOAS S264 hardware step (b): the C launcher starts the guest; the MacBook only watches.

Run ONLY through s264-launch-watch.sh.  The MacBook
  1. checks the hypervisor is S264 (QUERY),
  2. uploads the payload into the Python heap range (below guest RAM),
  3. sends ONE request: P_HV_NWOAS_S264 LAUNCH (cfg, out).  The C code then does what
     run_guest.py + the S259 modules did (hv_init, maps, ADT, boot args, NVMe, AGX, ...)
     and enters the guest; the request returns only when the guest exits or on an error.
While the request is blocked, any exception or event the hypervisor still forwards to the
MacBook is served by the stock proxyclient HV handlers AND counted: the pass criterion is
"0 forwarded events up to the Windows desktop" (NS2 transport is absent in this mode).
HARDWARE-UNVERIFIED tool.
"""
import argparse
import collections
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import s264_common as C  # noqa: E402
import s264_pyref as R  # noqa: E402
import s264c as X  # noqa: E402
import s264d as D  # noqa: E402
import s264e as E  # noqa: E402
import s264f as F  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--payload", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--fatal", action="store_true", help="also S264_POL_FATAL (log + reboot instead of forwarding)")
    ap.add_argument("--no-qd", action="store_true", help="S262 concurrent NVMe off (s264B synchronous NS1 path)")
    ap.add_argument("--no-local", action="store_true", help="exception policy off (s264B: forward to the MacBook)")
    ap.add_argument("--no-wifi", action="store_true", help="NWOAS_WIFI=0 (control page DISABLED, no SMC access)")
    ap.add_argument("--no-audio-b", action="store_true", help="sound stage B/S off (header status 0, s264C)")
    ap.add_argument("--no-bt", action="store_true", help="NWOAS_BT=0 (Wi-Fi exactly as S259, BT page DISABLED)")
    ap.add_argument("--s259", action="store_true", help="wifi_module.py module set (no BT page/fence): S259 payload")
    ap.add_argument("--no-fence-on-wifi-reset", action="store_true", help="NWOAS_BT_FENCE_WIFI_RESET=0")
    ap.add_argument("--no-pwr", action="store_true", help="power_module.py step off (s264E)")
    ap.add_argument("--audio-prot", choices=("sense", "moderate", "full"), default="moderate",
                    help="NWOAS_AUDIO_PROT (default moderate = what the delivery wrappers pass)")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=False)
    payload = a.payload.read_bytes()

    from m1n1.proxy import UartInterface, M1N1Proxy, START, EXC, EVENT
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    from m1n1.hv import HV
    from m1n1.hv.types import HV_EVENT
    from m1n1.adt import load_adt
    from m1n1 import trace

    iface = UartInterface()
    p = M1N1Proxy(iface, debug=False)
    bootstrap_port(iface, p)
    u = ProxyUtils(p, heap_size=768 * 1024 * 1024)
    q = p.request(C.P_HV_NWOAS_S264, 0)
    if q != C.S264_QUERY_MAGIC:
        raise SystemExit(f"STOP: running hypervisor is not S264 (query {q:#x}); nothing done")

    # Python heap allocations grow from heap_base; the C launcher's scratch (ADT assembly,
    # NVMe/audio buffers) is in the top 8 MiB of the same range: keep the payload far below.
    size = (len(payload) + 0x3fff) & ~0x3fff
    buf = u.memalign(0x4000, size)
    assert buf + size < u.heap_top - 0x1000000, "payload buffer too close to the C scratch area"
    print(f"[S264-watch] uploading payload {len(payload)} B to {buf:#x}", flush=True)
    u.compressed_writemem(buf, payload, True)
    for off in (0, len(payload) // 2 & ~0xfff, (len(payload) - 0x1000) & ~0xfff):
        if iface.readmem(buf + off, 0x1000) != payload[off:off + 0x1000]:
            raise SystemExit(f"STOP: payload read-back mismatch at +{off:#x}")
    flags = F.CFG_F_DEFAULT_F | (X.CFG_F_EXC_FATAL if a.fatal else 0)
    if a.no_pwr:
        flags &= ~F.CFG_F_PWR
    if a.no_bt:
        flags &= ~E.CFG_F_BT_ON
    if a.s259:
        flags &= ~(E.CFG_F_S261 | E.CFG_F_BT_ON)
    if a.no_fence_on_wifi_reset:
        flags |= E.CFG_F_BT_NOFENCE_WR
    if a.no_wifi:
        flags &= ~D.CFG_F_WIFI_ON
    if a.no_audio_b:
        flags &= ~D.CFG_F_AUDIO_B
    prot = {"sense": 0, "moderate": 1, "full": 2}[a.audio_prot]
    if a.no_qd:
        flags &= ~X.CFG_F_NVME_QD
    if a.no_local:
        flags &= ~X.CFG_F_EXC_LOCAL
    qd = 0 if a.no_qd else X.NVME_QD_DEFAULT
    cfg = F.pack_config_f(payload_ptr=buf, payload_size=len(payload), flags=flags, nvme_qd=qd, audio_prot=prot)
    print(f"[S264F-watch] config flags {flags:#x} nvme_qd {qd:#x} (QD {'off' if a.no_qd else 'on'}, "
          f"local policy {'off' if a.no_local else 'on'}, fatal {'on' if a.fatal else 'off'}, "
          f"Wi-Fi {'off' if a.no_wifi else 'on'}, sound B {'off' if a.no_audio_b else 'on'} prot {a.audio_prot}, "
          f"module set {'S259' if a.s259 else 'S261'}, BT {'off' if (a.no_bt or a.s259) else 'on'}, "
          f"power step {'off' if a.no_pwr else 'on'})",
          flush=True)
    cfg_buf = u.memalign(0x4000, 0x4000)
    out_size = 0x600000
    out_buf = u.memalign(0x4000, out_size)
    iface.writemem(cfg_buf, cfg)

    # Proxyclient HV object WITHOUT HV.init() (the C launcher does hv_init and all setup):
    # only the host-side state the stock handlers read.
    hv = HV(iface, p, u)
    hv.adt = load_adt(u.get_adt())
    hv.iodev = p.iodev_whoami()
    hv.tba = u.ba.copy()
    hv.device_addr_tbl = hv.adt.build_addr_lookup()
    hv.print_tracer = trace.PrintTracer(hv, hv.device_addr_tbl)
    hv._split_console = True
    hv.started = True
    for cpu in hv.adt["cpus"]:
        if cpu.state == "running":
            hv.started_cpus[cpu.cpu_id] = (getattr(cpu, "die_id", 0), cpu.cluster_id, cpu.cpu_id)
            hv.sysreg[cpu.cpu_id] = {}
            break
    seen = collections.Counter()

    ec_seen = collections.Counter()

    def counted(name, fn):
        def h(*args, **kw):
            seen[name] += 1
            detail = ""
            try:   # (reason, code, info) handlers: read the ExcInfo the HV passed (esr, elr, far)
                if len(args) >= 3 and isinstance(args[2], int) and args[2]:
                    raw = iface.readmem(args[2], 8 * 36)
                    spsr, elr, esr, far = struct.unpack_from("<4Q", raw, 8 * 32)
                    ec = (esr >> 26) & 0x3f
                    ec_seen[(name, ec)] += 1
                    detail = f" esr={esr:#x} (EC {ec:#x} ISS {esr & 0x1ffffff:#x}) elr={elr:#x} far={far:#x} spsr={spsr:#x}"
            except Exception as e:  # noqa: BLE001
                detail = f" (ExcInfo read failed: {e})"
            if seen[name] <= 200:
                print(f"[S264F-watch] forwarded to the MacBook: {name} #{seen[name]}{detail}", flush=True)
            return fn(*args, **kw)
        return h

    for (start, kind, name, fn) in (
            (START.EXCEPTION_LOWER, EXC.SYNC, "EXC_SYNC", hv.handle_exception),
            (START.EXCEPTION_LOWER, EXC.IRQ, "EXC_IRQ", hv.handle_exception),
            (START.EXCEPTION_LOWER, EXC.FIQ, "EXC_FIQ", hv.handle_exception),
            (START.EXCEPTION_LOWER, EXC.SERROR, "EXC_SERROR", hv.handle_exception),
            (START.EXCEPTION, EXC.FIQ, "HV_FIQ", hv.handle_exception),
            (START.HV, HV_EVENT.USER_INTERRUPT, "USER_INTERRUPT", hv.handle_exception),
            (START.HV, HV_EVENT.HOOK_VM, "HOOK_VM", hv.handle_exception),
            (START.HV, HV_EVENT.VTIMER, "VTIMER", hv.handle_exception),
            (START.HV, HV_EVENT.WDT_BARK, "WDT_BARK", hv.handle_bark),
            (START.HV, HV_EVENT.CPU_SWITCH, "CPU_SWITCH", hv.handle_exception),
            (START.HV, HV_EVENT.VIRTIO, "VIRTIO", hv.handle_virtio),
            (START.HV, HV_EVENT.PANIC, "PANIC", hv.handle_bark),
            (START.HV, HV_EVENT.NWOAS_NVME_LINK, "NVME_LINK", hv.handle_nwoas_nvme_link)):
        iface.set_handler(start, kind, counted(name, fn))
    iface.set_event_handler(EVENT.MMIOTRACE, counted("MMIOTRACE", hv.handle_mmiotrace))
    iface.set_event_handler(EVENT.IRQTRACE, counted("IRQTRACE", hv.handle_irqtrace))

    print("[S264-watch] sending LAUNCH; from here the C code owns the boot", flush=True)
    iface.dev.timeout = None
    try:
        ret = p.request(C.P_HV_NWOAS_S264, 2, cfg_buf, len(cfg), out_buf, out_size)
    finally:
        print(f"[S264F-watch] forwarded totals: {dict(seen) or 'none'}; by ESR class: "
              f"{ {f'{k[0]}/EC{k[1]:#x}': v for k, v in ec_seen.items()} or 'none'}", flush=True)
    sret = ret - (1 << 64) if ret >> 63 else ret
    print(f"[S264-watch] LAUNCH returned {sret} (1 = guest exited, <0 = failed before entry)", flush=True)
    try:
        hdr = iface.readmem(out_buf, C.S264_RESULT_HDR if hasattr(C, "S264_RESULT_HDR") else 160)
        if struct.unpack_from("<I", hdr)[0] == C.S264_RESULT_MAGIC:
            n = struct.unpack_from("<I", hdr, 8)[0]
            blob = iface.readmem(out_buf, n)
            (a.out / "c-result.bin").write_bytes(blob)
            res = C.parse_result(blob)
            for s in res["steps"]:
                print(f"[S264-watch] step {C.STEP_NAMES.get(s['id'], s['id'])}: "
                      f"{C.ST_NAMES.get(s['status'], s['status'])} {s['val']:#x}")
    except Exception as e:  # noqa: BLE001
        print(f"[S264-watch] could not read the result blob: {e}")
    print("[S264-watch] forwarded events:", dict(seen) or "none")
    return 0 if sret == 1 and not seen else 1


if __name__ == "__main__":
    sys.exit(main())
