"""S244 read-only probe: is the Mac mini running D4 m1n1 on macOS 13.5 OS firmware, and is the
S243 stack's memory layout still valid there?  Run on the MacBook with D4 waiting in proxy mode.

Live (what fw135-s244-guest-test.sh runs before chainload; same as stub-135 step 7 plus more):
  cd /Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows
  M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONPATH=proxyclient \
    .venv-hv/bin/python3 -B /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/display-s244/probe_fw135_s244.py

Offline (no device; a saved ADT + explicit boot-args numbers; used by the tests):
  ... probe_fw135_s244.py --adt-file X.adt --ba-json ba.json

Reads only: proxy base, boot args, ADT, and len(marker) bytes of the running m1n1 image (D4 identity).
No memory writes beyond ProxyUtils' host-side heap bookkeeping, no DCP/NVMe/PCIe, no chainload.
Exit: 0 all gates pass | 1 G-1 (firmware/model) not met | 3 running m1n1 is not D4 | 4 G-2 layout
check failed | 2 proxy error.  NWOAS_FW135_HEADLESS_OK=1 downgrades only the two video-geometry
checks (no HDMI sink); the memory-safety checks are never downgraded.

Fixes vs stub-135/tool/probe_fw135.py: that script does bytes(ch.system_firmware_version), but the
m1n1 ADT parser returns that property as str (TypeError on both saved J274 ADTs, see README).
"""
import argparse
import hashlib
import json
import os
import signal
import sys
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fw135_model as model  # noqa: E402

ROOT = Path("/Users/angigyeom/Desktop/DEV/NWOAS")
D4 = ROOT / "experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin"
D4_MD5 = "7bec0225edc101857be3b7f195dd7357"
MARKER = b"NWOAS-DCP-DEFER: automatic display init deferred to host"


def report(result, d4_ok, label):
    result["facts"]["running_m1n1_is_d4"] = d4_ok
    print(json.dumps(result, indent=1, default=str))
    for line in model.failed(result):
        print(f"[{label}] FAIL {line}")
    if not model.gate_g1(result):
        print(f"[{label}] G-1 NOT MET: not 13.5 OS firmware on Tahoe system firmware", flush=True)
        return 1
    print(f"[{label}] G-1 PASS: 13.5 OS firmware ({result['facts']['os_firmware']}) on system "
          f"firmware {result['facts']['system_firmware']}")
    if d4_ok is False:
        print(f"[{label}] STOP: the m1n1 in proxy is not D4 (reboot the Mac mini into the stub entry)")
        return 3
    if not result["ok"]:
        print(f"[{label}] G-2 NOT MET: S243 layout assumptions do not hold on this boot", flush=True)
        return 4
    print(f"[{label}] G-2 PASS: m1n1 framebuffer {result['predictions']['resolution']} in /vram, "
          f"guest skew {result['predictions']['guest_skew']} == pin, FBPROBE range inside /vram",
          flush=True)
    return 0


def offline(args):
    from m1n1.adt import load_adt
    adt = load_adt(Path(args.adt_file).read_bytes())
    raw = json.loads(Path(args.ba_json).read_text())
    ba = SimpleNamespace(phys_base=int(raw["phys_base"], 0), mem_size=int(raw["mem_size"], 0),
                         video=SimpleNamespace(**{k: int(raw["video"][k], 0) for k in
                                                  ("base", "display", "stride", "width", "height", "depth")}))
    result = model.evaluate(adt, ba, headless_ok=os.environ.get("NWOAS_FW135_HEADLESS_OK") == "1")
    return report(result, None, "S244-offline")


def live():
    from m1n1.proxy import UartInterface, M1N1Proxy
    from m1n1.proxyutils import ProxyUtils, bootstrap_port

    def deadline(*_):
        raise TimeoutError("no proxy answer within 30 s")

    signal.signal(signal.SIGALRM, deadline)
    signal.alarm(30)
    iface = UartInterface()
    try:
        p = M1N1Proxy(iface, debug=False)
        bootstrap_port(iface, p)
        u = ProxyUtils(p)
        binary = D4.read_bytes()
        assert hashlib.md5(binary).hexdigest() == D4_MD5, "local D4 copy changed"
        base = p.get_base()
        d4_ok = iface.readmem(base + binary.index(MARKER), len(MARKER)) == MARKER
        result = model.evaluate(u.adt, u.ba, headless_ok=os.environ.get("NWOAS_FW135_HEADLESS_OK") == "1")
        result["facts"]["m1n1_base"] = hex(base)
        return report(result, d4_ok, "S244-probe")
    finally:
        signal.alarm(0)
        iface.dev.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adt-file")
    ap.add_argument("--ba-json")
    args = ap.parse_args()
    if args.adt_file:
        return offline(args)
    try:
        return live()
    except Exception as error:  # proxy/UART errors must not look like "G-1 not met" (exit 1)
        print("[S244-probe] STOP: proxy error:", type(error).__name__, error, flush=True)
        return 2


if __name__ == "__main__":
    sys.exit(main())
