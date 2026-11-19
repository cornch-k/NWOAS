#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264F offline test (build host only, no Mac mini).

1. The whole s264E offline test on the s264F source with the s264F default config (0xbeff = + PWR,
   pwr field 0 = every NWOAS_PWR_* switch off, as the S268 launcher runs power_module.py).
2. s264f_pwr_test.py: the C power step against the real power-s260/power_module.py (11 environment
   cases, 3 refused environments, step absent when off, negative control).
usage: s264f_offline_test.py [--workdir DIR]
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
import s264e as E  # noqa: E402
import s264f as F  # noqa: E402
import s264e_offline_test as TE  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workdir", type=Path)
    a = ap.parse_args()
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264f-offline-"))
    work.mkdir(parents=True, exist_ok=True)
    rows, ok = [], True
    try:
        E.CFG_F_DEFAULT_E = F.CFG_F_DEFAULT_F
        E.pack_config_e = lambda **k: F.pack_config_f(**k)
        sys.argv = [sys.argv[0], "--workdir", str(work / "s264e")]
        rc = TE.main()
        rows.append(("s264E offline test on s264F (defaults 0xbeff, pwr 0)", "SAME" if rc == 0 else "DIFF", f"rc {rc}"))
        ok &= rc == 0
        r = subprocess.run([sys.executable, "-B", str(HERE / "s264f_pwr_test.py"), "--workdir", str(work / "pwr")],
                           capture_output=True, text=True, env=dict(os.environ))
        print(r.stdout[-5000:])
        last = [l for l in r.stdout.splitlines() if l.startswith("[s264F-pwr]")]
        good = r.returncode == 0 and "S264F PWR: PASS" in r.stdout
        rows.append(("C power step vs power_module.py", "SAME" if good else "DIFF",
                     f"rc {r.returncode} {last[-1] if last else ''}"))
        ok &= good
        print("| item | result | detail |\n|---|---|---|")
        for row in rows:
            print(f"| {row[0]} | {row[1]} | {row[2]} |")
        print(f"[s264F] {sum(1 for r in rows if r[1] == 'SAME')}/{len(rows)} SAME")
        print("S264F OFFLINE:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        if not a.workdir:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
