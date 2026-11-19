#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264E offline test (build host only, no Mac mini).

1. The whole s264C offline test on the s264E source with the s264E default config (flags 0x3eff, sound
   protection moderate), with the S268 module set on the Python side: bt-s261/bt_module.py (real file,
   NWOAS_WIFI=0 as before, NWOAS_AUDIO_PROT=moderate) instead of the wifi_module.py effects, so the
   stage-2 map / hooks / Wi-Fi page items now cover the BT control page, the fn1 BAR emulation and the
   fn1 BAR fence.
2. Plan items: AUDIO_B with prot 1 by default; STEP_WIFI d for every Wi-Fi/BT flag combination; the
   BT page hook, fn1 BYPASS maps and fence hooks present only with S261; default config bytes.
3. s264d_audio_test.py (10 cases), s264d_wifi_test.py (28 rows), s264e_bt_test.py (18 rows).
usage: s264e_offline_test.py [--workdir DIR]
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
os.environ["NWOAS_AUDIO_PROT"] = "moderate"       # S268 delivery wrappers; = the s264E HV default
import s264_common as C  # noqa: E402
import s264_pyref as R  # noqa: E402
import s264c as X  # noqa: E402
import s264d as D  # noqa: E402
import s264e as E  # noqa: E402
import s264c_offline_test as TC  # noqa: E402

BT_DIR = R.SCRIPTS / "bt-s261"


def run_bt_module(hv, glb):
    """bt-s261/bt_module.py exactly as the S268 launcher runs it (-m), NWOAS_WIFI=0 from S259_ENV:
    the ADT checks, both control page HOOKs, three BAR BYPASS ranges, the closed fn1 BAR fence."""
    if str(BT_DIR) not in sys.path:
        sys.path.insert(0, str(BT_DIR))
    import nwoas_bt_s261 as b
    keep = b.make_run_dir
    b.make_run_dir = lambda: None                   # no run directory on the build host
    try:
        hv.run_script(str(BT_DIR / "bt_module.py"))
    finally:
        b.make_run_dir = keep
    return hv._nwoas_bt_s261


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workdir", type=Path)
    a = ap.parse_args()
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264e-offline-"))
    work.mkdir(parents=True, exist_ok=True)
    ok, rows = True, []
    try:
        X.CFG_F_DEFAULT_C = E.CFG_F_DEFAULT_E
        X.pack_config_c = lambda **k: E.pack_config_e(**k)
        R.run_wifi_disabled = run_bt_module
        sys.argv = [sys.argv[0], "--workdir", str(work / "s264c")]
        rc = TC.main()
        rows.append(("s264C offline test on s264E (defaults 0x3eff, moderate, Python = bt_module.py)",
                     "SAME" if rc == 0 else "DIFF", f"rc {rc}"))
        ok &= rc == 0
        exe = TC.T.build_host_tool(work)
        res = TC.c_plan(exe, work / "e-default", [])
        st = D.steps_full(res)
        ab = [s for s in st if s[0] == D.STEP_AUDIO_B]
        good = res["err"] == 0 and len(ab) == 1 and ab[0][3] == 1 and (res["audio"][0x44], res["audio"][0x48]) == (0x14, 0x18)
        rows.append(("default AUDIO_B prot 1 (moderate), stage-A header 0x14/0x18", "SAME" if good else "DIFF",
                     f"{[(s[3], s[4]) for s in ab]}"))
        ok &= good
        good = res["cfg"] == E.pack_config_e(payload_size=TC.T.PAYLOAD)
        rows.append(("host pack_config_e == C s264_default_config bytes", "SAME" if good else "DIFF", ""))
        ok &= good
        bt_hooks = (0x348004000, 0x6e0900000, 0x6e0c00000)
        for name, fl in (("default (Wi-Fi + S261 + BT)", E.CFG_F_DEFAULT_E),
                         ("NWOAS_BT=0", E.CFG_F_DEFAULT_E & ~E.CFG_F_BT_ON),
                         ("NWOAS_WIFI=0", E.CFG_F_DEFAULT_E & ~D.CFG_F_WIFI_ON),
                         ("no fence on Wi-Fi reset", E.CFG_F_DEFAULT_E | E.CFG_F_BT_NOFENCE_WR),
                         ("S259 module set (no S261)", E.CFG_F_DEFAULT_E & ~E.CFG_F_S261)):
            r = TC.c_plan(exe, work / ("e-" + str(fl)), ["--flags", hex(fl)])
            wd = [s for s in D.steps_full(r) if s[0] == D.STEP_WIFI]
            hooks = sorted((o["a"], o["id"], o["c"]) for o in r["ops"] if o["kind"] == 2 and o["a"] in bt_hooks)
            maps = sorted((o["a"], o["c"]) for o in r["ops"] if o["kind"] == 1 and o["a"] in bt_hooks[1:])
            s261 = bool(fl & E.CFG_F_S261)
            want_h = [(0x348004000, 7, 0x4000), (0x6e0900000, 8, 0x8000), (0x6e0c00000, 8, 0x400000)] if s261 else []
            want_m = [(0x6e0900000, 0x8000), (0x6e0c00000, 0x400000)] if s261 else []
            good = (r["err"] == 0 and len(wd) == 1 and wd[0][4] == E.wifi_d(fl) and hooks == want_h and maps == want_m)
            rows.append((f"WIFI step d / BT page + fence ops: {name}", "SAME" if good else "DIFF",
                         f"d {wd[0][4] if wd else '-'} (want {E.wifi_d(fl)}) hooks {len(hooks)} maps {len(maps)}"))
            ok &= good
        for script, tag, n in (("s264d_audio_test.py", "S264D AUDIO: PASS", "sound stage B/S vs audio_module.py"),
                               ("s264d_wifi_test.py", "S264D WIFI: PASS", "Wi-Fi vs nwoas_wifi_s259.py"),
                               ("s264e_bt_test.py", "S264E BT: PASS", "Bluetooth vs nwoas_bt_s261.py")):
            r = subprocess.run([sys.executable, "-B", str(HERE / script), "--workdir", str(work / script)],
                               capture_output=True, text=True, env=dict(os.environ))
            print(r.stdout[-5000:])
            good = r.returncode == 0 and tag in r.stdout
            last = [l for l in r.stdout.splitlines() if "SAME" in l and l.startswith("[s264")]
            rows.append((f"C {n}", "SAME" if good else "DIFF", f"rc {r.returncode} {last[-1] if last else ''}"))
            ok &= good
        print("| item | result | detail |\n|---|---|---|")
        for row in rows:
            print(f"| {row[0]} | {row[1]} | {row[2]} |")
        print(f"[s264E] {sum(1 for r in rows if r[1] == 'SAME')}/{len(rows)} SAME")
        print("S264E OFFLINE:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        if not a.workdir:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
