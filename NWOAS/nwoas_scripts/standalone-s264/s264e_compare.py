#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264E hardware step (a) (copy of s264d_compare.py): s264E config (0x3eff, sound protection
moderate) and the S268 module set on the Python side: bt-s261/bt_module.py instead of wifi_module.py
(NWOAS_WIFI=0 as before, no SMC), NWOAS_AUDIO_PROT=moderate; payload = the S261 Bluetooth payload.
NWOAS S264D hardware step (a) (copy of s264c_compare.py with the s264D config: + AUDIO_B, + WIFI_ON,
audio_prot 0 = sense = the Python side's environment; a dry-run executes no step, so neither changes a
compared item).
NWOAS S264C hardware step (a) (copy of s264_compare.py with the s264C config).
NWOAS S264 hardware step (a): C dry-run vs Python launcher, same boot, guest NOT started.

Run ONLY through s264-dryrun-compare.sh (which chainloads the pinned S264 hypervisor first).
Order inside one proxy session:
  1. P_HV_NWOAS_S264 QUERY  (refuses any other hypervisor)
  2. P_HV_NWOAS_S264 DRYRUN (C plan; no MMIO, no sysreg writes, no stage-2 changes)
  3. the real Python launcher path (HV.init, load_raw, PMU, the S259 modules, hv.start) with
     a recorder that SUPPRESSES hv.start's iodev changes, fb shutdown, SPRR/GXF and hv_start.
     The payload bytes are not uploaded (only their length matters for the record).
     sound-s256/audio_module.py runs stage A only (the S264 milestone has no sound);
     NWOAS_WIFI=0 (no SMC).
  4. compare -> $OUT/compare.md, compare.json, c-result.bin, c-adt.bin, py-adt.bin
Afterwards the Mac mini sits in the proxy with an initialised but never-entered hypervisor:
it must be rebooted before the next test.  HARDWARE-UNVERIFIED tool.
"""
import argparse
import hashlib
import os
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import s264_common as C  # noqa: E402
import s264c as X  # noqa: E402,F401  (s264C step names)
import s264d as D  # noqa: E402,F401
import s264e as E  # noqa: E402  (s264E config, hook names)
import s264_pyref as R  # noqa: E402

SCRIPTS = R.SCRIPTS
S259_MODULES = ["cpufreq-s193/capture.py", "usb-s174/guest_map.py", "smp-s133/pmgr_gate.py",
                "layout-s231/guest_module.py", "memory-s180/link_module.py",
                "file-delivery-s243/module.py", "layout-s231/module.py", "AGX", "native-s230/query_module.py",
                "cpufreq-s189/observe.py", "AUDIO_A", "bt-s261/bt_module.py"]
CORE_MODULES = ["usb-s174/guest_map.py", "smp-s133/pmgr_gate.py", "layout-s231/guest_module.py",
                "LAYOUT_EFFECT", "AGX", "AUDIO_A", "bt-s261/bt_module.py"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--payload", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--modules", choices=("s259", "core"), default="s259")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=False)
    os.environ.update(R.S259_ENV)
    os.environ["NWOAS_AUDIO_PROT"] = "moderate"          # s264E HV default (user decision)
    os.environ.setdefault("NWOAS_LINK_DIR", str(a.out / "link"))
    os.environ.setdefault("NWOAS_LOG", str(a.out / "launcher"))
    payload_len = a.payload.stat().st_size

    from m1n1.proxy import UartInterface, M1N1Proxy
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    from m1n1.hv import HV
    from m1n1.hw.pmu import PMU

    iface = UartInterface()
    p = M1N1Proxy(iface, debug=False)
    bootstrap_port(iface, p)
    u = ProxyUtils(p, heap_size=768 * 1024 * 1024)   # run_guest.py:47

    q = p.request(C.P_HV_NWOAS_S264, 0)
    if q != C.S264_QUERY_MAGIC:
        raise SystemExit(f"STOP: running hypervisor is not S264 (query {q:#x}); nothing done")

    # ---- 2. C dry-run
    cfg = E.pack_config_e(payload_ptr=0, payload_size=payload_len)   # s264E default (0x3eff, QD 32, moderate)
    cfg_buf = u.memalign(0x4000, 0x4000)
    out_size = 0x600000
    out_buf = u.memalign(0x4000, out_size)
    iface.writemem(cfg_buf, cfg)
    t0 = time.time()
    n = p.request(C.P_HV_NWOAS_S264, 1, cfg_buf, len(cfg), out_buf, out_size)
    if n >> 63 or n == 0 or n > out_size:
        raise SystemExit(f"STOP: C dry-run returned {n:#x} (negative = plan error); Python part not run")
    blob = iface.readmem(out_buf, n)
    (a.out / "c-result.bin").write_bytes(blob)
    res = C.parse_result(blob)
    crec = C.c_record(res)
    print(f"[S264] C dry-run: {len(res['ops'])} ops, ADT {len(res['adt'])} B, err {res['err']}, "
          f"{time.time() - t0:.1f}s", flush=True)

    # ---- 3. Python launcher path, recorded
    hv = HV(iface, p, u)
    rec = R.Recorder(p, u, iface, suppress_start=True)
    rec.skip_payload_upload = True
    hv.run_script(str(SCRIPTS / "display-s244" / "fw135_preflight.py"))   # --pre-init-script
    hv.init()
    hv.load_raw(bytes(payload_len), 0x800)
    path, bus = PMU.find_primary_pmu(u.adt)
    base = u.adt[path.rpartition('/')[0]].get_reg(0)[0]
    rec.spmi_range = (base, base + 0x100)
    PMU(u).reset_panic_counter()                                        # run_guest.py:103
    audio_buf = None
    for m in (S259_MODULES if a.modules == "s259" else CORE_MODULES):
        glb = hv.shell_locals
        if m == "AGX":
            hv.run_script(str(R.ROOT / "port-j274" / "gpu" / "hv" / "tools-hv2" / "agx_hv2_module.py"))
        elif m == "AUDIO_A":
            audio_buf = R.run_audio_stage_a(glb)
        elif m == "LAYOUT_EFFECT":
            R.layout_module_effect(hv)
        else:
            hv.run_script(str(SCRIPTS / m))
    rec.phase = "start"
    hv.start()            # recorder suppresses iodev/fb/SPRR/GXF/hv_start
    prec = rec.record(hv, audio_buf)

    # ---- 4. compare (both guest ADTs are kept so a DIFF can be traced without another run)
    (a.out / "c-adt.bin").write_bytes(crec["adt"] or b"")
    (a.out / "py-adt.bin").write_bytes(prec["adt"] or b"")
    L = res["layout"]
    ok, _, rows = C.compare(crec, prec, (L["py_heap_base"], L["py_heap_top"]))
    md = C.write_report(a.out / "compare.json", ok, rows,
                        {"c_layout": L, "c_steps": res["steps"], "python_entry": hv.entry,
                         "python_guest_base": hv.guest_base, "modules": a.modules,
                         "c_result_sha256": hashlib.sha256(blob).hexdigest()})
    (a.out / "compare.md").write_text(md + "\n")
    print(md)
    print("[S264] DRY-RUN COMPARE:", "PASS (every item SAME)" if ok else "FAIL (see DIFF rows)")
    print("[S264] guest NOT started; reboot the Mac mini before the next test")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
