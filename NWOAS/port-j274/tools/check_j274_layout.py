#!/usr/bin/env python3
"""Offline checker for the draft J274 guest layout (NWOAS port step 0-1).

Independent re-implementation, written for NWOAS. It does NOT import or execute
anything from external/windows-on-m1 (treated as untrusted read-only data).
The rules mirror, by reading only:
  - E/guest_layout.py:15-37   (field set), :43-104 (validate()), :9 (ALIGNMENT)
  - E/m1n1_windows/src/hv_assisted_layout.{h,c}  (address arithmetic)
  - E/m1n1_windows/src/hv_autonomous_runtime.c:216-229 (pinned-layout equality)
  - E/mu/Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dec:26-34 (PCD widths)
E/ = /Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1

Passing this checker is an offline arithmetic result only. It says nothing
about whether anything boots on the Mac mini.

Exit code (fail-closed):
  0  no finding blocks: every reference-rule violation, PCD-width problem and
     carveout overlap is either absent or explicitly accepted with --allow L-n
  1  something blocks: a MISMATCH / missing or extra key / other reference rule
     (never acceptable), or an L-n blocker that was not accepted with --allow
  2  usage error

Usage: python3 -I -B check_j274_layout.py [layout.json] [--fw-size N] [--json]
           [--carveouts FACTS.json | --no-carveouts] [--allow L-1 --allow L-2 ...]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ALIGNMENT = 0x4000                 # E/guest_layout.py:9
GUEST_OFFSET = 16 * 1024 * 1024    # E/.../hv_assisted_layout.h:10
BOOTARGS_SIZE = 0x4000             # E/.../hv_assisted_layout.h:12

# Field order of E/guest_layout.py:15-37 (the loader rejects missing/extra keys, :120-128).
FIELDS = (
    "layout_version", "phys_base", "ram_end", "firmware_base", "firmware_max_size",
    "adt_base", "adt_max_size", "boot_args_base", "boot_args_size", "ramdisk_base",
    "ramdisk_max_size", "low_mem_ipa", "low_mem_pa", "low_mem_size", "virtual_fb_base",
    "virtual_fb_width", "virtual_fb_height", "virtual_fb_stride", "pci_ecam",
    "pci_bar_window", "nvme_vintid", "xhci_base", "cpu_count",
)

# J274 iBoot-provided sizes. FACT, see J274-BOARD-PROFILE.md rows SZ-1..SZ-4
# (nwoas_scripts/logs/layout-s231-20261115-191038.Vc0lr2:147,329-331 and the
# Sep-06 ADT dump experiments/tahoe-afk-20260906/d6-adt.bin memory-map).
J274_SIZES = {"adt": 0x5c000, "trust_cache": 0x60000, "sepfw": 0x59c000, "preoslog": 0x40000}
# Our Mu FD as shipped in the S223 payload, padded to its Image-header size
# (nwoas_scripts/loader-s223/manifest.json: original_fd_bytes 0x1d88000, image_size 0x1e00000).
J274_FW_SIZE_PADDED = 0x1e00000
J274_FW_SIZE_UNPADDED = 0x1d88000
J274_PLATFORM_RAM_END = 0xbe1000000  # FACT: boot_args phys_base+mem_size, every S2xx log
J274_REAL_APCIE_ECAM = (0x690000000, 0x10000000)  # FACT: ADT /arm-io/apcie reg[0] (config/j274-adt-facts.json)

# Blockers that a human may accept explicitly (J274-GUEST-LAYOUT.md section 3). Anything else that
# fails (MISMATCH, missing/extra keys, other reference rules) can never be accepted.
ALLOWABLE = {
    "L-1": "low window at IPA 0 (reference requires low_mem_ipa >= 0x1000)",
    "L-2": "low_mem_size does not fit the UINT32 PcdLowMemoryWindowSize",
    "L-6": "pci_ecam inside the real J274 apcie ECAM",
    "L-8": "iBoot carveout(s) inside guest RAM, or carveout check not run",
}


def align_up(value: int, alignment: int = ALIGNMENT) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def decode(raw: dict) -> dict:
    out = {}
    for name in FIELDS:
        value = raw[name]
        if isinstance(value, bool):
            raise ValueError(f"{name} must be an integer")
        out[name] = int(value, 0) if isinstance(value, str) else int(value)
    return out


def assisted_layout(phys_base, adt_size, tc_size, fw_size, sepfw_size, preoslog_size):
    """Same arithmetic as hv_assisted_layout_compute() and HV.load_raw()."""
    adt_base = phys_base + GUEST_OFFSET
    tc_base = align_up(adt_base + adt_size)
    fw_base = align_up(tc_base + tc_size)
    sepfw_base = align_up(fw_base + fw_size)
    preoslog_base = align_up(sepfw_base + sepfw_size)
    boot_args_base = preoslog_base + preoslog_size     # no alignment here in the C code
    return {
        "adt_base": adt_base, "trust_cache_base": tc_base, "firmware_base": fw_base,
        "sepfw_base": sepfw_base, "preoslog_base": preoslog_base,
        "boot_args_base": boot_args_base, "top_of_kernel_data": boot_args_base + BOOTARGS_SIZE,
    }


def reference_rule_violations(lay: dict) -> list[str]:
    """Rules of E/guest_layout.py:43-104, re-stated. Returns human-readable failures."""
    bad = []
    if lay["layout_version"] != 1:
        bad.append("layout_version != 1")
    if lay["cpu_count"] != 8:
        bad.append("cpu_count != 8 (reference hard rule, guest_layout.py:46-47)")
    if lay["phys_base"] >= lay["ram_end"]:
        bad.append("guest RAM range is empty")
    if lay["virtual_fb_stride"] < lay["virtual_fb_width"] * 4:
        bad.append("virtual framebuffer stride is too small")
    for name in ("phys_base", "ram_end", "firmware_base", "adt_base", "boot_args_base",
                 "ramdisk_base", "low_mem_pa", "virtual_fb_base", "pci_ecam",
                 "pci_bar_window", "xhci_base"):
        if lay[name] % ALIGNMENT:
            bad.append(f"{name} is not 0x4000-aligned")
    fb_size = lay["virtual_fb_stride"] * lay["virtual_fb_height"]
    ranges = {
        "ADT": (lay["adt_base"], lay["adt_base"] + lay["adt_max_size"]),
        "firmware": (lay["firmware_base"], lay["firmware_base"] + lay["firmware_max_size"]),
        "boot args": (lay["boot_args_base"], lay["boot_args_base"] + lay["boot_args_size"]),
        "virtual framebuffer": (lay["virtual_fb_base"], lay["virtual_fb_base"] + fb_size),
        "RAM disk": (lay["ramdisk_base"], lay["ramdisk_base"] + lay["ramdisk_max_size"]),
        "low-memory backing": (lay["low_mem_pa"], lay["low_mem_pa"] + lay["low_mem_size"]),
    }
    for name, (start, end) in ranges.items():
        if start < lay["phys_base"] or end > lay["ram_end"] or start >= end:
            bad.append(f"{name} is outside guest RAM")
    items = list(ranges.items())
    for i, (ln, (ls, le)) in enumerate(items):
        for rn, (rs, re_) in items[i + 1:]:
            if ls < re_ and rs < le:
                bad.append(f"{ln} overlaps {rn}")
    if lay["low_mem_ipa"] < 0x1000:
        bad.append("low_mem_ipa < 0x1000 (reference requires a null guard page, guest_layout.py:103-104)")
    return bad


def load_carveouts(path: Path) -> list[dict]:
    """Carveout list from tools/extract_adt_facts.py output: [{id, base, size}, ...] as ints."""
    data = json.loads(path.read_text())
    return [{"id": int(c["id"]), "base": int(c["base"], 16), "size": int(c["size"], 16)}
            for c in data["carveouts"]]


def layout_ranges(lay: dict) -> dict:
    fb_size = lay["virtual_fb_stride"] * lay["virtual_fb_height"]
    return {
        "ADT": (lay["adt_base"], lay["adt_base"] + lay["adt_max_size"]),
        "firmware": (lay["firmware_base"], lay["firmware_base"] + lay["firmware_max_size"]),
        "boot args": (lay["boot_args_base"], lay["boot_args_base"] + lay["boot_args_size"]),
        "virtual framebuffer": (lay["virtual_fb_base"], lay["virtual_fb_base"] + fb_size),
        "RAM disk": (lay["ramdisk_base"], lay["ramdisk_base"] + lay["ramdisk_max_size"]),
        "low-memory backing": (lay["low_mem_pa"], lay["low_mem_pa"] + lay["low_mem_size"]),
    }


def carveout_findings(lay: dict, carveouts: list[dict]) -> list[str]:
    """CONFLICT for every carveout that overlaps guest RAM [phys_base, ram_end), naming the
    layout sub-ranges it hits. A region that contains ALL of guest RAM is the RAM region itself
    (on J274 region-id-24 = boot_args phys_base..phys_base+mem_size) and is reported as INFO."""
    lo, hi = lay["phys_base"], lay["ram_end"]
    sub = layout_ranges(lay)
    out = []
    for c in sorted(carveouts, key=lambda c: c["base"]):
        b, e = c["base"], c["base"] + c["size"]
        if not (b < hi and lo < e):
            continue
        if b <= lo and e >= hi:
            out.append(f"INFO carveout region-id-{c['id']} [{b:#x},{e:#x}) contains all of guest RAM "
                       "(the RAM region itself, not a reservation inside it)")
            continue
        hits = [n for n, (s, t) in sub.items() if b < t and s < e]
        out.append(f"CONFLICT L-8 carveout region-id-{c['id']} [{b:#x},{e:#x}) inside guest RAM"
                   + (f"; overlaps {', '.join(hits)}" if hits else ""))
    return out


def j274_extra_findings(lay: dict, fw_size: int) -> list[str]:
    """Port-specific checks that the reference validator does not make."""
    notes = []
    derived = assisted_layout(lay["phys_base"], J274_SIZES["adt"], J274_SIZES["trust_cache"],
                              fw_size, J274_SIZES["sepfw"], J274_SIZES["preoslog"])
    # E/m1n1_windows/src/hv_autonomous_runtime.c:222-228 requires exact equality.
    for key in ("adt_base", "firmware_base", "boot_args_base"):
        if derived[key] != lay[key]:
            notes.append(f"MISMATCH {key}: layout {lay[key]:#x} != derived {derived[key]:#x}")
    if derived["top_of_kernel_data"] > lay["ramdisk_base"]:
        notes.append("MISMATCH top_of_kernel_data above ramdisk_base")
    if J274_SIZES["adt"] > lay["adt_max_size"]:
        notes.append("MISMATCH devtree_size exceeds adt_max_size")
    if fw_size > lay["firmware_max_size"]:
        notes.append("MISMATCH firmware size exceeds firmware_max_size")
    if lay["ram_end"] > J274_PLATFORM_RAM_END:
        notes.append("INFO ram_end above platform RAM end; runtime clamps it (hv_autonomous_memory.c:5-22)")
    if lay["low_mem_size"] > 0xFFFFFFFF:
        notes.append("PCD L-2 PcdLowMemoryWindowSize is UINT32 (AppleSiliconPkg.dec:33); "
                     f"{lay['low_mem_size']:#x} does not fit")
    ecam_lo, ecam_len = J274_REAL_APCIE_ECAM
    if ecam_lo <= lay["pci_ecam"] < ecam_lo + ecam_len:
        notes.append("CONFLICT L-6 pci_ecam lies in the real J274 apcie ECAM (FL1100/Wi-Fi/Ethernet)")
    return notes


def classify(text: str, source: str) -> dict:
    """-> {"id", "level", "text"}; level FAIL (never acceptable), BLOCK (acceptable via --allow
    of its id) or INFO."""
    if source == "reference":
        fid = "L-1" if text.startswith("low_mem_ipa < 0x1000") else "REF"
        return {"id": fid, "level": "BLOCK" if fid in ALLOWABLE else "FAIL", "text": text}
    if text.startswith("INFO"):
        return {"id": "INFO", "level": "INFO", "text": text}
    if text.startswith("MISMATCH"):
        return {"id": "MISMATCH", "level": "FAIL", "text": text}
    for fid in ALLOWABLE:
        if f" {fid} " in f" {text} ":
            return {"id": fid, "level": "BLOCK", "text": text}
    return {"id": "UNKNOWN", "level": "FAIL", "text": text}


def evaluate(lay: dict, fw_size: int, carveouts: list[dict] | None) -> list[dict]:
    found = [classify(t, "reference") for t in reference_rule_violations(lay)]
    found += [classify(t, "j274") for t in j274_extra_findings(lay, fw_size)]
    if carveouts is None:
        found.append({"id": "L-8", "level": "BLOCK",
                      "text": "UNCHECKED L-8 no carveout list given (--carveouts); overlap with iBoot "
                              "carveouts not checked"})
    else:
        found += [classify(t, "carveout") for t in carveout_findings(lay, carveouts)]
    return found


def verdict(found: list[dict], allow: set[str]) -> tuple[int, list[dict], list[dict]]:
    blocking = [f for f in found if f["level"] == "FAIL"
                or (f["level"] == "BLOCK" and f["id"] not in allow)]
    accepted = [f for f in found if f["level"] == "BLOCK" and f["id"] in allow]
    return (1 if blocking else 0), blocking, accepted


def main(argv: list[str] | None = None, out=None) -> int:
    out = out or sys.stdout
    here = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser()
    ap.add_argument("layout", nargs="?", type=Path, default=here / "config/j274-guest-layout.json")
    ap.add_argument("--fw-size", type=lambda s: int(s, 0), default=J274_FW_SIZE_PADDED)
    ap.add_argument("--json", action="store_true")
    cv = ap.add_mutually_exclusive_group()
    cv.add_argument("--carveouts", type=Path, default=here / "config/j274-adt-facts.json",
                    help="carveout list (tools/extract_adt_facts.py output)")
    cv.add_argument("--no-carveouts", action="store_true",
                    help="skip the carveout check (reported as an unaccepted L-8 blocker)")
    ap.add_argument("--allow", action="append", default=[], choices=sorted(ALLOWABLE),
                    help="explicitly accept one L-n blocker (repeatable)")
    args = ap.parse_args(argv)
    raw = json.loads(args.layout.read_text())
    missing = sorted(set(FIELDS) - raw.keys())
    extra = sorted(raw.keys() - set(FIELDS))
    lay = decode(raw) if not missing else {}
    carveouts = None
    if not args.no_carveouts and args.carveouts.exists():
        carveouts = load_carveouts(args.carveouts)
    found = evaluate(lay, args.fw_size, carveouts) if lay else []
    for k in missing + extra:
        found.append({"id": "KEYS", "level": "FAIL", "text": f"{'missing' if k in missing else 'extra'} key {k}"})
    allow = set(args.allow)
    code, blocking, accepted = verdict(found, allow)
    result = {
        "layout": str(args.layout),
        "missing_keys": missing,
        "extra_keys": extra,
        "reference_rule_violations": reference_rule_violations(lay) if lay else ["not decoded"],
        "j274_findings": j274_extra_findings(lay, args.fw_size) if lay else [],
        "carveouts": (str(args.carveouts) if carveouts is not None else None),
        "carveout_findings": carveout_findings(lay, carveouts) if (lay and carveouts is not None) else [],
        "allowed": sorted(allow),
        "accepted": [f"{f['id']}: {f['text']}" for f in accepted],
        "blocking": [f"{f['id']}: {f['text']}" for f in blocking],
        "exit_code": code,
        "derived": {k: hex(v) for k, v in assisted_layout(
            lay.get("phys_base", 0), J274_SIZES["adt"], J274_SIZES["trust_cache"], args.fw_size,
            J274_SIZES["sepfw"], J274_SIZES["preoslog"]).items()} if lay else {},
    }
    if args.json:
        print(json.dumps(result, indent=1), file=out)
    else:
        for key, value in result.items():
            print(f"{key}: {value}", file=out)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
