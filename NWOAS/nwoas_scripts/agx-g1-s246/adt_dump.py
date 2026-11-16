#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read-only J274 ADT dump for the AGX gate (G0 input).

Live:    M1N1DEVICE=/dev/cu.usbmodem...NY1 M1N1_KEEP_BAUD=1 python3 adt_dump.py --out DIR
Offline: python3 adt_dump.py --from-blob saved.adt --out DIR

Touches on the target (live mode only): proxy NOP/feature/bootargs/base
queries, heapblock_alloc(0) (returns the pointer, allocates nothing) and
memory READS of the ADT (~0x5c000 bytes).  It does not import m1n1.setup, so
no PMU/I2C write, no clocks, no MMIO writes, no code execution on the target.

Writes DIR/adt.bin (exact blob), DIR/adt-decoded.json and DIR/SHA256SUMS.
DIR must not exist yet.
"""

import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import j274_agx_common as C  # noqa: E402


def decode(adt, blob: bytes) -> dict:
    sgx, asc = adt["/arm-io/sgx"], adt["/arm-io/gfx-asc"]
    fws = C.os_firmware_strings(adt)
    out = {
        "dump_version": 1,
        "adt_sha256": hashlib.sha256(blob).hexdigest(),
        "adt_size": len(blob),
        "target_type": C.text(adt.target_type),
        "chip_id": int(adt["/chosen"].chip_id),
        "board_id": int(adt["/chosen"].board_id),
        "chosen_firmware": fws,
        "agx_schema_for_firmware": C.agx_schema_version(fws["firmware_version"]),
        "soc_generation": C.text(adt["/arm-io"].getprop("soc-generation")),
        "adt_identity": C.adt_identity(adt),
        "random_seed_sha256": C.random_seed_sha256(adt),
        "nodes": {p: C.node_record(adt[p]) for p in C.REQUIRED_PATHS},
        "aic_reg0": [int(v) for v in adt["/arm-io/aic"].get_reg(0)],
        "pmgr_chain": {p: C.pmgr_chain(adt, p) for p in C.CLOCK_PATHS},
        "uat_geometry_host": C.uat_geometry(),
    }
    try:
        out["gfx_asc_segment_ranges"] = C.segment_ranges(asc)
    except C.ContractError as exc:
        out["gfx_asc_segment_ranges_error"] = str(exc)
    regions = {}
    for name, (b, s) in C.SGX_REGION_PROPS.items():
        regions[name] = {"base": sgx._properties.get(b), "size": sgx._properties.get(s)}
    out["sgx_regions"] = regions
    out["hex"] = {
        "sgx_reg": [[hex(b), hex(s)] for b, s in out["nodes"]["/arm-io/sgx"]["reg"]],
        "asc_reg": [[hex(b), hex(s)] for b, s in out["nodes"]["/arm-io/gfx-asc"]["reg"]],
        "sgx_regions": {k: [hex(v["base"]) if isinstance(v["base"], int) else None,
                            hex(v["size"]) if isinstance(v["size"], int) else None]
                        for k, v in regions.items()},
    }
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--from-blob", type=Path, help="decode a saved ADT blob (no hardware)")
    args = ap.parse_args(argv)
    if args.out.exists():
        print(f"STOP: {args.out} already exists (evidence is never reused)", file=sys.stderr)
        return 2

    from m1n1.adt import load_adt
    if args.from_blob:
        blob = args.from_blob.read_bytes()
        source = {"mode": "offline", "blob": str(args.from_blob)}
    else:
        with C.deadline(90, "connect + ADT read"):
            iface, p, u = C.connect_proxy()
            try:
                blob = bytes(u.get_adt())
                source = {"mode": "live", "m1n1_base": int(u.base),
                          "proxy": str(iface.devpath)}
            finally:
                iface.dev.close()
    adt = load_adt(blob)
    data = decode(adt, blob)
    data["source"] = source

    args.out.mkdir(parents=True)
    (args.out / "adt.bin").write_bytes(blob)
    C.atomic_json(args.out / "adt-decoded.json", data)
    sums = "".join(f"{hashlib.sha256((args.out / n).read_bytes()).hexdigest()}  {n}\n"
                   for n in ("adt.bin", "adt-decoded.json"))
    (args.out / "SHA256SUMS").write_text(sums)
    print(json.dumps({k: data[k] for k in ("target_type", "chosen_firmware",
                                           "agx_schema_for_firmware", "adt_sha256")},
                     indent=2))
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
