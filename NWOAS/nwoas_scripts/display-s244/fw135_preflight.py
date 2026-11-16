"""S244 HV pre-init check for macOS 13.5 OS firmware.  Replaces display-s241/tahoe_dcp_guest_hook.py.

Loaded by run_guest.py --pre-init-script (runs before hv.init(); an exception aborts run_guest
before any guest memory is touched).  It does NOT talk to DCP, does NOT set
hv.preserve_framebuffer or hv.pre_guest_start, and writes nothing to the Mac mini.

Why no DCP hook on 13.5 (see README.md): D4, the chainloaded S233b HV and the guest m1n1 prefix
each run m1n1's own C display_init() on 13.5 (main.c:169 defers only on mBoot-18000.121.3; the
S215 guest prefix has no deferral at all, loader-s197/guest-compat-s197.patch).  The m1n1 C DCP
path supports 13.5 (firmware.c V13_5, dcp_iboot.c v13_3 layer API), picks a <=1920x1200 <=60 Hz
mode that fits /vram, swaps it to /vram, and Project Mu's SimpleFbDxe takes base/width/height
from BootArgs (SimpleFbDxe.c:176-179).  With the defaults restored (no preserve_framebuffer), the
HV start path does fb_improve_logo + fb_shutdown(True), which is upstream m1n1 behaviour.

What this script checks here (u = the S233b proxy, i.e. the boot args/ADT the guest will get):
  G-1 firmware 13.5 / Tahoe system firmware / J274; G-2 framebuffer geometry, /vram outside guest
  RAM, S233b FBPROBE dc-ivac range inside /vram, guest m1n1 mem_size reduction == pinned 0x34000.
Result: $NWOAS_LOG.fw135-preflight.json and one log line.
"""
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, "/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/display-s244")
import fw135_model as _fw135  # noqa: E402

_r = _fw135.evaluate(u.adt, u.ba, headless_ok=os.environ.get("NWOAS_FW135_HEADLESS_OK") == "1")
_r["stage"] = "HV pre-init (S233b proxy, before hv.init)"
if os.environ.get("NWOAS_LOG"):
    Path(os.environ["NWOAS_LOG"] + ".fw135-preflight.json").write_text(
        json.dumps(_r, indent=1, default=str) + "\n")
print("[S244] preflight", json.dumps({"ok": _r["ok"], "facts": _r["facts"],
                                       "predictions": _r["predictions"]}, default=str))
for _line in _fw135.failed(_r):
    print("[S244] FAIL", _line)
if not _r["ok"]:
    raise SystemExit("[S244] STOP before hv.init: 13.5 preflight failed (nothing loaded into the guest)")
print("[S244] PASS: 13.5 OS firmware, m1n1-owned framebuffer "
      f"{_r['predictions']['resolution']}, guest skew {_r['predictions']['guest_skew']}; "
      "no DCP hook, default HV framebuffer path")
