#!/usr/bin/env python3
"""Extract the J274 ADT values port-j274 relies on into a JSON file (offline, read-only).

Reads a saved ADT dump with OUR proxyclient parser (m1n1_windows/proxyclient/m1n1/adt.py).
Nothing from external/windows-on-m1 is imported. No serial port, no proxy, no hardware.
Needs `construct`, so run it with the hv venv and -I -B (no __pycache__ is written):

  cd /Users/angigyeom/Desktop/DEV/NWOAS
  m1n1_windows/.venv-hv/bin/python3 -I -B port-j274/tools/extract_adt_facts.py \
      experiments/tahoe-afk-20260906/d6-adt.bin port-j274/config/j274-adt-facts.json

Only addresses/sizes are written. Identifiers, keys, MAC addresses, nonces etc. in /chosen are
never read into the output.

It refuses (exit 2, nothing imported from the proxyclient, nothing written) unless the running
interpreter really has -I (sys.flags.isolated) and -B (sys.flags.dont_write_bytecode).  The
record stores those flags and sys.executable as read at run time, not a typed-in string.
"""
from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

N = Path(__file__).resolve().parents[2]
PROXYCLIENT = N / "m1n1_windows" / "proxyclient"


def _ints(v):
    return [int(x) for x in v]


def extract(adt) -> dict:
    cm = adt["/chosen/carveout-memory-map"]
    carveouts = []
    for key, value in cm._properties.items():
        if not key.startswith("region-id-"):
            continue
        base, size = _ints(value)[:2]
        carveouts.append({"id": int(key[len("region-id-"):]), "base": hex(base), "size": hex(size),
                          "end": hex(base + size)})
    carveouts.sort(key=lambda c: int(c["base"], 16))
    apcie = adt["/arm-io/apcie"]
    ecam_base, ecam_size = apcie.get_reg(0)
    disp0 = adt["/arm-io/disp0"]
    ext = disp0.getprop("external", None)
    return {
        "compatible": [str(c) for c in adt.compatible],
        "apcie_reg0": {"base": hex(ecam_base), "size": hex(ecam_size)},
        "disp0_external": int(ext) if ext is not None else None,
        "carveouts": carveouts,
    }


def interpreter_record(flags=None) -> dict:
    """What actually runs this script (read from sys at run time)."""
    flags = sys.flags if flags is None else flags
    return {"executable": sys.executable, "isolated": bool(flags.isolated),
            "dont_write_bytecode": bool(flags.dont_write_bytecode),
            "version": sys.version.split()[0]}


def main(argv=None, flags=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 2:
        print(__doc__)
        return 2
    interp = interpreter_record(flags)
    if not (interp["isolated"] and interp["dont_write_bytecode"]):
        print(f"REFUSED: run with -I -B (isolated={interp['isolated']}, "
              f"dont_write_bytecode={interp['dont_write_bytecode']}); nothing written", file=sys.stderr)
        return 2
    dump, out = Path(argv[0]), Path(argv[1])
    sys.path.insert(0, str(PROXYCLIENT))
    from m1n1.adt import load_adt  # our own parser
    data = dump.read_bytes()
    facts = extract(load_adt(data))
    rel = lambda p: str(p.resolve().relative_to(N)) if p.resolve().is_relative_to(N) else str(p)
    # not resolved: a venv's bin/python3 is a symlink to the base interpreter
    exe = interp["executable"]
    exe = exe[len(str(N)) + 1:] if exe.startswith(str(N) + "/") else exe
    record = {
        "source_dump": rel(dump),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "parser": "m1n1_windows/proxyclient/m1n1/adt.py (load_adt)",
        "interpreter": interp,
        "command": f"{exe} -I -B {rel(Path(__file__))} {rel(dump)} {rel(out)}",
        "python_version": interp["version"],
        "note": "offline extraction from a saved dump; the carveout table of the CURRENT boot may differ",
        **facts,
    }
    out.write_text(json.dumps(record, indent=1) + "\n")
    print(f"wrote {out}: {len(facts['carveouts'])} carveouts, sha256 {record['source_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
