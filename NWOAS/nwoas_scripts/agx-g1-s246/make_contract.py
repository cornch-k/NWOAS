#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the strict J274 AGX contract (+ review record) from an ADT blob. Offline.

  python3 make_contract.py --adt DUMPDIR/adt.bin --out-dir OUTDIR [--d4 m1n1-bare.bin]

Writes OUTDIR/j274-agx.json (same schema as paulsmir config/j313-agx.json,
`source` replaced by NWOAS identities) and OUTDIR/j274-agx-review.json.
Refuses (exit 1, nothing written) unless the ADT is J274 / T8103 with the
macOS 13.5 OS firmware (V13_5/G13) and the D4 image has MD5 7bec0225....
OUTDIR must not exist.
"""

import argparse
import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import j274_agx_common as C  # noqa: E402

# Values seen on this Mac mini so far, for a human-readable comparison only.
# FACT: Tahoe-firmware ADTs of 2026-09-06; gpu/handoff also in the J274 13.5
# log (port-j274/gpu/hv/README.md:151-152).  Not enforced: the live ADT is.
SEEN = {
    "sgx_mmio": (0x204000000, 0x4000000), "asc_mmio": (0x206400000, 0x6C000),
    "handoff": (0xBFFF70000, 0x4000), "shared": (0xBFFF78000, 0x40000),
    "gpu": (0xBFFFB8000, 0x4000), "rtkit_private": (0xFFFFFF8000000000, 0x2000000000),
}
SEEN_IRQS = [563, 564, 565, 566, 579, 576, 575, 578, 577]


def build(adt_blob: bytes, d4_image: bytes):
    from m1n1.adt import load_adt
    if hashlib.md5(d4_image).hexdigest() != C.D4_MD5:
        raise C.ContractError("D4 image MD5 mismatch; this is not the installed m1n1")
    adt = load_adt(adt_blob)
    contract = C.extract_contract(adt, m1n1_build=C.m1n1_build_string(d4_image),
                                  host_stack=C.host_stack_sha256())
    review = C.review_record(adt, d4_image)
    C.segment_ranges(adt["/arm-io/gfx-asc"])        # must decode (fail closed)
    return contract, review


def compare_seen(contract) -> list:
    notes = []
    for name, (b, s) in SEEN.items():
        r = contract.regions[name]
        if (r.base, r.size) != (b, s):
            notes.append(f"NOTE {name}: {r.base:#x}+{r.size:#x} differs from "
                         f"previously seen {b:#x}+{s:#x}")
    if list(contract.interrupts) != SEEN_IRQS:
        notes.append(f"NOTE interrupts {list(contract.interrupts)} differ from {SEEN_IRQS}")
    return notes


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--adt", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--d4", type=Path, default=C.D4_FILE)
    args = ap.parse_args(argv)
    if args.out_dir.exists():
        print(f"STOP: {args.out_dir} already exists", file=sys.stderr)
        return 2
    try:
        contract, review = build(args.adt.read_bytes(), args.d4.read_bytes())
    except C.ContractError as exc:
        print(f"REFUSED: {exc}", file=sys.stderr)
        return 1
    args.out_dir.mkdir(parents=True)
    (args.out_dir / "j274-agx.json").write_bytes(C.canonical_bytes(contract))
    (args.out_dir / "j274-agx-review.json").write_bytes(C.review_bytes(review))
    for line in compare_seen(contract):
        print(line)
    print(f"contract sha256 {C.contract_sha256(contract)}")
    print(f"wrote {args.out_dir}/j274-agx.json and j274-agx-review.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
