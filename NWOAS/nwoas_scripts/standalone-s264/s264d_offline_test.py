#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264D offline test (build host only, no Mac mini).

1. The whole s264C offline test (s264B equivalence on 2 ADT states, NVME_QD, exception policy,
   BRK reflection, negative controls) on the s264D source with the s264D default config
   (flags 0xeff = + AUDIO_B + WIFI_ON, audio_prot 0 = sense).
2. AUDIO_B in the plan: one step, after AUDIO_A and before WIFI (audio_module.py runs after the
   AGX module and before the Wi-Fi module), a = the stage-A buffer, b = a + 0x40000 (L1, L2 page
   tables), c = audio_prot, d = 0 (all four contract resources are plain passthrough in the plan);
   the page tables stay inside the Python heap range and off the NVMe buffer; absent when
   AUDIO_B or AUDIO_A is off; audio_prot 3 refused.
3. s264d_audio_test.py: the C stage B/S against the real audio_module.py (10 cases).
4. s264d_wifi_test.py: the C Wi-Fi side against the real S259/S257/S240 Python (facts + scenarios).
usage: s264d_offline_test.py [--workdir DIR]
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
import s264_common as C  # noqa: E402
import s264c as X  # noqa: E402
import s264d as D  # noqa: E402
import s264c_offline_test as TC  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workdir", type=Path)
    a = ap.parse_args()
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264d-offline-"))
    work.mkdir(parents=True, exist_ok=True)
    ok = True
    rows = []
    try:
        # 1. s264C test with the s264D defaults
        X.CFG_F_DEFAULT_C = D.CFG_F_DEFAULT_D
        X.pack_config_c = lambda **k: D.pack_config_d(**k)
        sys.argv = [sys.argv[0], "--workdir", str(work / "s264c")]
        rc = TC.main()
        rows.append(("s264C offline test on s264D (defaults 0xeff, audio_prot 0 = sense)",
                     "SAME" if rc == 0 else "DIFF", f"rc {rc}"))
        ok &= rc == 0
        # 2. AUDIO_B plan step
        exe = TC.T.build_host_tool(work)
        res = TC.c_plan(exe, work / "d-default", [])
        st = D.steps_full(res)
        ids = [s[0] for s in st]
        lay = res["layout"]
        ab = [s for s in st if s[0] == D.STEP_AUDIO_B]
        good = (res["err"] == 0 and len(ab) == 1 and
                ids.index(D.STEP_AUDIO_A) < ids.index(D.STEP_AUDIO_B) < ids.index(X.STEP_WIFI) and
                ab[0][1:] == (lay["audio_buf"], lay["audio_buf"] + 0x40000, D.AUDIO_PROT_DEFAULT, 0))
        rows.append(("AUDIO_B step: AUDIO_A < AUDIO_B < WIFI, (buf, buf+0x40000, prot 0, passthrough ok)",
                     "SAME" if good else "DIFF",
                     f"{[(hex(x) if i < 3 else x) for i, x in enumerate(ab[0][1:])] if ab else '-'} "
                     f"audio_buf {lay['audio_buf']:#x} order {[C.STEP_NAMES.get(i, i) for i in ids]}"))
        ok &= good
        good = (lay["py_heap_base"] <= lay["audio_buf"] and lay["audio_buf"] + 0x48000 <= lay["py_heap_top"]
                and lay["audio_buf"] + 0x48000 <= lay["phys_base"]
                and not (lay["nvme_buf"] < lay["audio_buf"] + 0x48000 and lay["audio_buf"] < lay["nvme_buf"] + 0x4000))
        rows.append(("page tables (buf+0x40000..+0x48000) in the Python heap, below guest RAM, off nvme_buf",
                     "SAME" if good else "DIFF",
                     f"heap {lay['py_heap_base']:#x}..{lay['py_heap_top']:#x} phys_base {lay['phys_base']:#x}"))
        ok &= good
        cfg = res.get("cfg")
        good = cfg is not None and bytes(cfg) == D.pack_config_d(payload_size=TC.T.PAYLOAD)
        rows.append(("host pack_config_d == C s264_default_config bytes", "SAME" if good else "DIFF",
                     f"{len(cfg) if cfg is not None else '-'} B"))
        ok &= good
        for name, extra, want in (
                ("prot 1 (moderate)", ["--audio-prot", "1"], ("c", 1)),
                ("prot 2 (full)", ["--audio-prot", "2"], ("c", 2)),
                ("AUDIO_B off", ["--flags", hex(D.CFG_F_DEFAULT_D & ~D.CFG_F_AUDIO_B)], ("absent",)),
                ("AUDIO_A off", ["--flags", hex(D.CFG_F_DEFAULT_D & ~(1 << 2))], ("absent",)),
                ("prot 3 refused", ["--audio-prot", "3"], ("err", 1, 0xa0))):
            try:
                r = TC.c_plan(exe, work / ("d-" + name.split()[0] + name.split()[1]), extra)
            except SystemExit:           # the host planner exits 1 on a plan error; the result is written
                r = C.parse_result((work / ("d-" + name.split()[0] + name.split()[1]) / "c-result.bin").read_bytes())
            ab = [s for s in D.steps_full(r) if s[0] == D.STEP_AUDIO_B]
            if want[0] == "c":
                good = (r["err"] == 0 and len(ab) == 1 and ab[0][3] == want[1] and   # stage-A header too
                        (r["audio"][0x44], r["audio"][0x48]) == D.PROT_VALUES[want[1]])
            elif want[0] == "absent":
                good = r["err"] == 0 and not ab
            else:
                good = (r["err"], r["err_arg"]) == (want[1], want[2])
            rows.append((f"AUDIO_B {name}", "SAME" if good else "DIFF",
                         f"err {r['err']}/{r['err_arg']:#x} steps {[(s[3], s[4]) for s in ab]} "
                         f"stage-A header prot {r['audio'][0x44]:#x}/{r['audio'][0x48]:#x}"))
            ok &= good
        # 2b. Wi-Fi: STEP_WIFI d = 1 by default (NWOAS_WIFI=1), 0 with WIFI_ON off
        wd = [s for s in st if s[0] == D.STEP_WIFI]
        r_off = TC.c_plan(exe, work / "d-wifi0", ["--flags", hex(D.CFG_F_DEFAULT_D & ~D.CFG_F_WIFI_ON)])
        w0 = [s for s in D.steps_full(r_off) if s[0] == D.STEP_WIFI]
        good = (len(wd) == 1 and wd[0][4] == 1 and len(w0) == 1 and w0[0][4] == 0 and r_off["err"] == 0 and
                [o for o in res["ops"] if o["kind"] != 8] == [o for o in r_off["ops"] if o["kind"] != 8])
        rows.append(("WIFI step d (NWOAS_WIFI): default 1, 0 with WIFI_ON off, same maps/hooks",
                     "SAME" if good else "DIFF", f"default {wd} / off {w0}"))
        ok &= good
        # 3. stage B/S against the real Python module
        r = subprocess.run([sys.executable, "-B", str(HERE / "s264d_audio_test.py"),
                            "--workdir", str(work / "audio")], capture_output=True, text=True,
                           env=dict(os.environ))
        print(r.stdout[-4000:])
        good = r.returncode == 0 and "S264D AUDIO: PASS" in r.stdout
        rows.append(("C stage B/S vs real audio_module.py (10 cases: 3 protection stages + 7 failures)",
                     "SAME" if good else "DIFF", f"rc {r.returncode}"))
        ok &= good
        r = subprocess.run([sys.executable, "-B", str(HERE / "s264d_wifi_test.py"),
                            "--workdir", str(work / "wifi")], capture_output=True, text=True,
                           env=dict(os.environ))
        print(r.stdout[-6000:])
        good = r.returncode == 0 and "S264D WIFI: PASS" in r.stdout
        rows.append(("C Wi-Fi side vs real nwoas_wifi_s259.py / wifi_up / wifi_probe (facts + 14 scenarios)",
                     "SAME" if good else "DIFF", f"rc {r.returncode}"))
        ok &= good
        print("| item | result | detail |\n|---|---|---|")
        for row in rows:
            print(f"| {row[0]} | {row[1]} | {row[2]} |")
        print(f"[s264D] {sum(1 for r in rows if r[1] == 'SAME')}/{len(rows)} SAME")
        print("S264D OFFLINE:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        if not a.workdir:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
