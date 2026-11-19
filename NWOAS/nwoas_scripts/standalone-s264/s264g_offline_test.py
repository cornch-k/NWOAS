#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264G offline test (build host only, no Mac mini).

1. The whole s264F offline test on the s264G source (config unchanged; the auto-start is armed only
   by the "nwoas_autostart=" image variable, so every s264F item must stay SAME).
2. s264g_auto_test.py: the auto-start decision logic and the ESP read of the real m1n1 Rust code on a
   GPT + FAT32 image file.
usage: s264g_offline_test.py [--workdir DIR]
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
import s264f_offline_test as TF  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workdir", type=Path)
    a = ap.parse_args()
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264g-offline-"))
    work.mkdir(parents=True, exist_ok=True)
    rows, ok = [], True
    try:
        sys.argv = [sys.argv[0], "--workdir", str(work / "s264f")]
        rc = TF.main()
        rows.append(("s264F offline test on s264G", "SAME" if rc == 0 else "DIFF", f"rc {rc}"))
        ok &= rc == 0
        r = subprocess.run([sys.executable, "-B", str(HERE / "s264g_auto_test.py"), "--workdir", str(work / "auto")],
                           capture_output=True, text=True, env=dict(os.environ))
        print(r.stdout[-6000:])
        last = [l for l in r.stdout.splitlines() if l.startswith("[s264G-auto]")]
        good = r.returncode == 0 and "S264G AUTO: PASS" in r.stdout
        rows.append(("auto-start decision + ESP read on an image file", "SAME" if good else "DIFF",
                     f"rc {r.returncode} {last[-1] if last else ''}"))
        ok &= good
        print("| item | result | detail |\n|---|---|---|")
        for row in rows:
            print(f"| {row[0]} | {row[1]} | {row[2]} |")
        print(f"[s264G] {sum(1 for r in rows if r[1] == 'SAME')}/{len(rows)} SAME")
        print("S264G OFFLINE:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        if not a.workdir:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
