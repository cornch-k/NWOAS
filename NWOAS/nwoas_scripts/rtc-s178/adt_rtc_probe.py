#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Read-only Apple Device Tree (ADT) probe for RTC-related nodes.

Standalone parser (no `construct` dependency) for the ADT binary format:
  node  := u32 prop_count, u32 child_count, prop[prop_count], node[child_count]
  prop  := char name[32], u32 size (bit31 = placeholder flag), u8 value[size] padded to 4
Prints /arm-io/nub-spmi* (PMU children incl. info-* props), /arm-io/smc, and
any property anywhere whose name contains "rtc".

Usage: python3 adt_rtc_probe.py <adt.bin> [...]
"""
import struct
import sys


class Node:
    def __init__(self, name, props, children, path):
        self.name = name
        self.props = props          # dict name -> bytes
        self.children = children
        self.path = path


def parse(buf, off=0, parent_path=""):
    pcount, ccount = struct.unpack_from("<II", buf, off)
    off += 8
    props = {}
    for _ in range(pcount):
        name = buf[off:off + 32].split(b"\0")[0].decode("ascii", "replace")
        size = struct.unpack_from("<I", buf, off + 32)[0]
        placeholder = bool(size & 0x80000000)
        size &= 0x7FFFFFFF
        val = bytes(buf[off + 36:off + 36 + size])
        props[name] = (val, placeholder)
        off += 36 + ((size + 3) & ~3)
    name = props.get("name", (b"?", False))[0].split(b"\0")[0].decode("ascii", "replace")
    path = parent_path + "/" + name if parent_path or name != "device-tree" else ""
    if name == "device-tree":
        path = ""
    children = []
    for _ in range(ccount):
        child, off = parse(buf, off, path)
        children.append(child)
    return Node(name, props, children, path or "/"), off


def fmt(val):
    if len(val) == 0:
        return "<empty>"
    if len(val) <= 8 and len(val) in (1, 2, 4, 8):
        return "0x%x (%d bytes)" % (int.from_bytes(val, "little"), len(val))
    printable = all(32 <= b < 127 or b == 0 for b in val)
    if printable:
        return repr(val.rstrip(b"\0").replace(b"\0", b"|"))
    return val.hex() + " (%d bytes)" % len(val)


def dump(node, depth=0, filt=None):
    ind = "  " * depth
    print(f"{ind}{node.path or '/'}")
    for k, (v, ph) in node.props.items():
        if k == "name":
            continue
        if filt is None or filt(k):
            print(f"{ind}  {k}{' [placeholder]' if ph else ''} = {fmt(v)}")


def walk(node, fn):
    fn(node)
    for c in node.children:
        walk(c, fn)


def find(root, path):
    hits = []

    def f(n):
        if n.path.startswith(path):
            hits.append(n)
    walk(root, f)
    return hits


def main():
    for fn in sys.argv[1:]:
        buf = open(fn, "rb").read()
        root, used = parse(buf)
        print(f"##### {fn}  parsed_bytes={used} file_bytes={len(buf)}")
        print("compatible:", fmt(root.props.get("compatible", (b"", 0))[0]),
              "model:", fmt(root.props.get("model", (b"", 0))[0]))

        print("\n--- /arm-io/*spmi* (full property dump) ---")
        for n in find(root, "/arm-io/"):
            if "spmi" in n.name and n.path.count("/") == 2:
                dump(n)
                for c in n.children:
                    dump(c, 1)

        print("\n--- /arm-io/smc (selected) ---")
        for n in find(root, "/arm-io/smc"):
            if n.path.count("/") == 2:
                dump(n, 0, lambda k: k in ("compatible", "reg", "interrupts",
                                           "clock-gates", "device_type", "AAPL,phandle"))
                for c in n.children:
                    print("   child:", c.path)

        print("\n--- any property with 'rtc' in its name (whole tree) ---")

        def f(n):
            for k, (v, ph) in n.props.items():
                if "rtc" in k.lower():
                    print(f"{n.path}: {k} = {fmt(v)}")
        walk(root, f)

        print("\n--- /arm-io/pmgr device names containing spmi/smc/nub (power domains) ---")
        for n in find(root, "/arm-io/pmgr"):
            if n.path.count("/") == 2:
                v = n.props.get("ps-regs", (b"", 0))[0]
                for k in ("devices",):
                    pass
        # Devices are encoded inside pmgr node's "devices" prop in newer ADTs; just list names.
        pm = [n for n in find(root, "/arm-io/pmgr") if n.path.count("/") == 2]
        if pm:
            v = pm[0].props.get("devices", (b"", 0))[0]
            names = []
            off = 0
            # device entry: 0x30 bytes, name at +0x10..+0x30 (m1n1 pmgr layout)
            while off + 0x30 <= len(v):
                nm = v[off + 0x10:off + 0x30].split(b"\0")[0].decode("ascii", "replace")
                if nm:
                    names.append(nm)
                off += 0x30
            print([n for n in names if any(s in n.lower() for s in ("spmi", "smc", "nub"))])


if __name__ == "__main__":
    main()
