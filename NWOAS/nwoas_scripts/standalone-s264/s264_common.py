# SPDX-License-Identifier: MIT
"""NWOAS S264 shared host code: result-blob parser, stage-2 page-table model, comparator.

Both sides are reduced to the same "launch record":
  maps     list of raw hv_map calls (from, to, size, incr) plus C hook / VUART records,
           replayed by a model of m1n1 hv_vm.c hv_map() (incl. HW->SW page demotion when a
           sub-page mapping lands in a 16 KiB HW page) -> canonical interval list
  adt      bytes written at adt_base
  ba       bytes written at bootargs_base, then every write64 inside it applied
  msr      {encoding: value} last write wins
  w64      writes outside boot args (secondary RVBARs)
  iodev    {iodev: usage} final + ordered list
  copies   (dst, src, len)
  agx      512-byte nwoas_agx_config, audio 0x4c-byte S256 header
  start    (entry, x0)
Nothing here talks to hardware.
"""
import json
import os
import re
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = Path(os.environ.get("S264_SRC", ROOT / "m1n1-standalone-s264" / "src"))
PLAN_H = SRC / "nwoas_s264_plan.h"

S264_RESULT_MAGIC = 0x52343653
OP_MAP, OP_HOOK, OP_VUART, OP_W64, OP_MSR, OP_IODEV, OP_COPY, OP_STEP = range(1, 9)
OP_NAMES = {1: "MAP", 2: "HOOK", 3: "VUART", 4: "W64", 5: "MSR", 6: "IODEV", 7: "COPY", 8: "STEP"}
STEP_NAMES = {1: "HV_INIT", 2: "WRITE_BOOTARGS", 3: "PMU_RESET", 4: "PCIE_INIT", 5: "NVME_CHECKS",
              6: "NVME_INIT", 7: "GPT_CHECK", 8: "NVME_TAKEOVER", 9: "AGX_CONFIG", 10: "AUDIO_A",
              11: "WIFI", 12: "WRITE_ADT", 13: "TLBI", 14: "FB", 15: "ARM_TRAPS", 16: "HV_START"}
HOOK_NAMES = {1: "PMGR", 2: "PG_OVR", 3: "CPUSTART", 4: "CPUSTATE", 5: "NVME", 6: "WIFI_CTL"}
HOOK_CLASS = {1: "R", 2: "W", 3: "RW"}
ST_NAMES = {1: "OK", 2: "DRY", 3: "FAIL", 4: "SKIP"}


# struct s264_config (nwoas_s264_plan.h), packed little endian
CFG_FMT = "<IIII" + "QQQQQ" + "QQQ" + "II" + "II" + "QQ" + "III" + "I" + "8Q"
CFG_SIZE = struct.calcsize(CFG_FMT)
S264_MAGIC = 0x34363253
CFG_F_DEFAULT = 0x7f
S264_QUERY_MAGIC = 0x5332363400000001
P_HV_NWOAS_S264 = 0xc3e


def pack_config(payload_ptr=0, payload_size=33669120, flags=CFG_F_DEFAULT):
    """Defaults = s264_default_config() = the S259 launcher environment."""
    return struct.pack(CFG_FMT, S264_MAGIC, 1, CFG_SIZE, flags,
                       payload_ptr, payload_size, 0x800, 768 << 20, 128 << 20,
                       0, 0x100000000, 0x34000,
                       1, 1048576,
                       2, 889,
                       0xa00000000, 0,
                       3, 0x2, 0,
                       0,
                       *([0] * 8))


def layout_fields():
    """Field order of struct s264_layout, read from the header (single source of truth)."""
    text = PLAN_H.read_text()
    m = re.search(r"#define S264_LAYOUT_FIELDS\(X\)(.*?)\n#define", text, re.S)
    return re.findall(r"X\((\w+)\)", m.group(1))


def sysreg_enc(t):
    op0, op1, crn, crm, op2 = t
    return (op0 << 16) | (op1 << 12) | (crn << 8) | (crm << 4) | op2


def parse_result(blob):
    h = struct.unpack_from("<24I", blob, 0)
    if h[0] != S264_RESULT_MAGIC:
        raise ValueError(f"not a S264 result blob (magic {h[0]:#x})")
    (magic, ver, total, err, err_arg, n_lay, n_ops, adt_len, ba_len, n_pm, n_st, mode,
     o_lay, o_ops, o_adt, o_ba, o_agx, o_aud, o_pm, o_st, o_cfg, opsz, _r1, _r2) = h
    inputs = struct.unpack_from("<8Q", blob, 96)
    names = layout_fields()
    if len(names) != n_lay:
        raise ValueError(f"layout field count {n_lay} != header list {len(names)}")
    lay = dict(zip(names, struct.unpack_from(f"<{n_lay}Q", blob, o_lay)))
    ops = []
    for i in range(n_ops):
        kind, oid, a, b, c, d = struct.unpack_from("<IIQQQQ", blob, o_ops + i * opsz)
        ops.append(dict(kind=kind, id=oid, a=a, b=b, c=c, d=d))
    steps = [dict(zip(("id", "status", "val"), struct.unpack_from("<IIQ", blob, o_st + 16 * i)))
             for i in range(n_st)]
    pm = list(struct.unpack_from(f"<{n_pm}Q", blob, o_pm)) if n_pm else []
    return dict(err=err, err_arg=err_arg, mode=mode, layout=lay, ops=ops, steps=steps, pmgr=pm,
                adt=bytes(blob[o_adt:o_adt + adt_len]), ba=bytes(blob[o_ba:o_ba + ba_len]),
                agx=bytes(blob[o_agx:o_agx + 512]), audio=bytes(blob[o_aud:o_aud + 0x4c]),
                inputs=dict(zip(("heapblock_top", "iodev", "midr", "amx_cur", "actlr_cur", "ba_rev",
                                 "adt_size", "n_carveouts"), inputs)),
                cfg=bytes(blob[o_cfg:o_cfg + CFG_SIZE]))


def c_record(res):
    """Launch record of the C plan."""
    L = res["layout"]
    rec = dict(maps=[], adt=None, ba=None, ba_base=L["bootargs_base"], msr={}, w64=[], iodev=[],
               copies=[], agx=None, audio=None, start=None, steps=[], spmi=None, hooks_info=[])
    for o in res["ops"]:
        k = o["kind"]
        if k == OP_MAP:
            rec["maps"].append(("map", o["a"], o["b"], o["c"], o["d"]))
        elif k == OP_HOOK:
            rec["maps"].append(("hook", o["a"], HOOK_CLASS[o["d"]], o["c"], HOOK_NAMES[o["id"]]))
        elif k == OP_VUART:
            rec["maps"].append(("vuart", o["a"], o["b"], o["c"]))
        elif k == OP_MSR:
            rec["msr"][o["a"]] = o["b"]
        elif k == OP_W64:
            rec["w64"].append((o["a"], o["b"]))
        elif k == OP_IODEV:
            rec["iodev"].append((o["a"], o["b"]))
        elif k == OP_COPY:
            rec["copies"].append((o["a"], o["b"] if o["id"] else "payload", o["c"]))
        elif k == OP_STEP:
            name = STEP_NAMES.get(o["id"], o["id"])
            rec["steps"].append(name)
            if name == "WRITE_BOOTARGS":
                rec["ba"] = res["ba"]
            elif name == "WRITE_ADT":
                rec["adt"] = res["adt"]
            elif name == "AGX_CONFIG":
                rec["agx"] = res["agx"]
            elif name == "AUDIO_A":
                rec["audio"] = res["audio"]
            elif name == "HV_START":
                rec["start"] = (o["a"], o["b"])
            elif name == "PMU_RESET":
                rec["spmi"] = (o["a"], o["b"], o["c"])
    return rec


# ---------------------------------------------------------------- stage-2 model
PAGE = 0x4000
TARGET_MASK = ((1 << 50) - 1) & ~0x3
PTE_VALID = 1


class Stage2:
    """Interval model of m1n1 hv_vm.c hv_map() as both launchers use it.

    entry tuples: ("HW", pa_minus_ipa, attrs) | ("SW", pa_minus_ipa, trace_bits)
                  | ("PHOOK", cls, index) (Python proxy hook) | ("CHOOK", cls, name) | ("VUART",)
    Unmapped space has no interval."""

    def __init__(self):
        self.iv = []  # sorted, non-overlapping [start, end, entry]

    def _cut(self, a, b):
        out, mid = [], []
        for s, e, ent in self.iv:
            if e <= a or s >= b:
                out.append([s, e, ent])
                continue
            if s < a:
                out.append([s, a, ent])
            if e > b:
                out.append([b, e, ent])
            mid.append([max(s, a), min(e, b), ent])
        return out, mid

    def _set(self, a, b, ent):
        out, _ = self._cut(a, b)
        if ent is not None:
            out.append([a, b, ent])
        out.sort()
        self.iv = out

    def _demote(self, page):
        """hv_pt_get_l4(): a HW 16 KiB page that receives a sub-page entry becomes SW MAP."""
        out, mid = self._cut(page, page + PAGE)
        for s, e, ent in mid:
            if ent[0] == "HW":
                ent = ("SW", ent[1], 0)
            out.append([s, e, ent])
        out.sort()
        self.iv = out

    def hv_map(self, frm, to, size, incr):
        if size <= 0:
            return
        hw = bool(to) and bool(to & PTE_VALID)
        if hw:
            assert frm % PAGE == 0 and size % PAGE == 0, f"HW map not 16K aligned {frm:#x}+{size:#x}"
            ent = ("HW", (to & TARGET_MASK & ~0x3fff) - frm if incr else None, to & 0xfff & ~1)
        elif not to:
            ent = None
        else:
            typ = (to >> 50) & 7
            if typ == 0:
                ent = ("SW", (to & TARGET_MASK) - frm if incr else None, to >> 61)
            elif typ in (2, 3, 4):
                ent = ("PHOOK", {2: "R", 3: "W", 4: "RW"}[typ], (to & TARGET_MASK) >> 2)
            else:
                ent = ("RAW", to)
        end = frm + size
        # hv_map(): an unaligned head or tail goes through hv_pt_map_l4 -> hv_pt_get_l4
        if frm % PAGE:
            self._demote(frm & ~(PAGE - 1))
        if end % PAGE:
            self._demote(end & ~(PAGE - 1))
        self._set(frm, end, ent)

    def hook(self, frm, size, cls, name):
        end = frm + size
        if frm % PAGE:
            self._demote(frm & ~(PAGE - 1))
        if end % PAGE:
            self._demote(end & ~(PAGE - 1))
        self._set(frm, end, ("CHOOK", cls, name))

    def vuart(self, base):
        self._set(base, base + PAGE, ("VUART",))

    def canonical(self, heap=None, labels=None):
        """Merge adjacent equal intervals.  Hooks become ("HOOK", cls) so a Python proxy hook
        and a C hook of the same R/W class compare equal; the label is kept aside.
        HW/SW targets inside the Python heap become ("HEAPBUF", kind) (the audio buffer PA is
        chosen differently by each launcher)."""
        out = []
        for s, e, ent in self.iv:
            if ent[0] in ("PHOOK", "CHOOK"):
                ent = ("HOOK", ent[1])
            elif ent[0] in ("HW", "SW") and heap and ent[1] is not None and heap[0] <= s + ent[1] < heap[1]:
                ent = ("HEAPBUF", ent[0])
            if out and out[-1][1] == s and out[-1][2] == ent:
                out[-1][1] = e
            else:
                out.append([s, e, ent])
        return out


def replay(maps):
    st = Stage2()
    for m in maps:
        if m[0] == "map":
            st.hv_map(*m[1:])
        elif m[0] == "hook":
            st.hook(m[1], m[3], m[2], m[4])
        elif m[0] == "vuart":
            st.vuart(m[1])
    return st


def final_ba(rec):
    if rec["ba"] is None:
        return None
    ba = bytearray(rec["ba"])
    base = rec["ba_base"]
    for addr, val in rec["w64"]:
        if base <= addr < base + len(ba):
            struct.pack_into("<Q", ba, addr - base, val)
    return bytes(ba)


def hexdiff(a, b, limit=8):
    out = []
    if a is None or b is None:
        return [f"one side missing (C {a is not None}, Python {b is not None})"]
    if len(a) != len(b):
        out.append(f"length C {len(a)} != Python {len(b)}")
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            out.append(f"first difference at +{i:#x}: C {a[i:i+16].hex()} Python {b[i:i+16].hex()}")
            break
    return out[:limit]


def compare(crec, prec, heap):
    """Returns (ok, report lines, table rows)."""
    rows, lines = [], []

    def row(name, ok, detail=""):
        rows.append((name, "SAME" if ok else "DIFF", detail))
        return ok

    ok = True
    ok &= row("guest ADT bytes", crec["adt"] == prec["adt"],
              f"{len(crec['adt'] or b'')} B" if crec["adt"] == prec["adt"] else "; ".join(hexdiff(crec["adt"], prec["adt"])))
    cba, pba = final_ba(crec), final_ba(prec)
    ok &= row("boot args bytes (after S102 mem_size patch)", cba == pba,
              f"{len(cba or b'')} B" if cba == pba else "; ".join(hexdiff(cba, pba)))
    cm = replay(crec["maps"]).canonical(heap)
    pm = replay(prec["maps"]).canonical(heap)
    same = cm == pm
    detail = f"{len(cm)} intervals"
    if not same:
        cs = {(s, e, ent) for s, e, ent in [(a, b, c) for a, b, c in cm]}
        ps = {(s, e, ent) for s, e, ent in [(a, b, c) for a, b, c in pm]}
        only_c = sorted(cs - ps)[:6]
        only_p = sorted(ps - cs)[:6]
        detail = (f"C {len(cm)} / Python {len(pm)} intervals; only C: "
                  + ", ".join(f"{s:#x}-{e:#x} {ent}" for s, e, ent in only_c)
                  + "; only Python: " + ", ".join(f"{s:#x}-{e:#x} {ent}" for s, e, ent in only_p))
    ok &= row("stage-2 map (canonical)", same, detail)
    ok &= row("sysreg writes (last value per register)", crec["msr"] == prec["msr"],
              f"{len(crec['msr'])} registers" if crec["msr"] == prec["msr"] else
              f"C {sorted((hex(k), hex(v)) for k, v in crec['msr'].items())} Python "
              f"{sorted((hex(k), hex(v)) for k, v in prec['msr'].items())}")
    base = crec["ba_base"]
    cw = sorted(w for w in crec["w64"] if not base <= w[0] < base + 0x4000)
    pw = sorted(w for w in prec["w64"] if not base <= w[0] < base + 0x4000)
    ok &= row("write64 outside boot args (RVBARs)", cw == pw, f"{len(cw)} writes" if cw == pw else f"C {cw} Python {pw}")
    ok &= row("iodev usage sequence", crec["iodev"] == prec["iodev"],
              f"{len(crec['iodev'])} calls" if crec["iodev"] == prec["iodev"] else f"C {crec['iodev']} Python {prec['iodev']}")
    ok &= row("copies (SEPFW, TrustCache, preoslog, payload)", sorted(map(str, crec["copies"])) == sorted(map(str, prec["copies"])),
              f"C {crec['copies']} Python {prec['copies']}")
    ok &= row("AGX config 512 B", crec["agx"] == prec["agx"],
              "" if crec["agx"] == prec["agx"] else "; ".join(hexdiff(crec["agx"], prec["agx"])))
    ok &= row("S256 audio header 0x4c B", crec["audio"] == prec["audio"],
              "" if crec["audio"] == prec["audio"] else "; ".join(hexdiff(crec["audio"], prec["audio"])))
    ok &= row("guest entry / x0", crec["start"] == prec["start"],
              f"C {crec['start'] and tuple(map(hex, crec['start']))} Python {prec['start'] and tuple(map(hex, prec['start']))}")
    if prec.get("spmi") is not None or crec.get("spmi") is not None:
        cs = crec.get("spmi")
        ps = prec.get("spmi")
        # Python records the two SPMI CMD writes; C records (base, slave, reg)
        exp = None
        if cs:
            b, slave, reg = cs
            exp = [(b + 4, (reg << 16) | (1 << 15) | (slave << 8) | 0x30), (b + 4, 0)]
        row("PMU panic counter SPMI writes", ps is None or exp == ps, f"C {exp} Python {ps}")
    return ok, lines, rows


def jsonable(x):
    if isinstance(x, bytes):
        return {"len": len(x), "sha256": __import__("hashlib").sha256(x).hexdigest()}
    if isinstance(x, dict):
        return {str(hex(k) if isinstance(k, int) else k): jsonable(v) for k, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [jsonable(i) for i in x]
    return x


def write_report(path, ok, rows, extra):
    lines = ["| 항목 | 결과 | 내용 |", "|---|---|---|"]
    for name, res, detail in rows:
        lines.append(f"| {name} | {res} | {detail} |")
    Path(path).write_text(json.dumps({"ok": ok, "rows": rows, **jsonable(extra)}, indent=1) + "\n")
    return "\n".join(lines)
S264_RESULT_HDR = 160
