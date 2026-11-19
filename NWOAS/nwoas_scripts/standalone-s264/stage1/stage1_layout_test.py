#!/usr/bin/env python3
"""NWOAS 4-1 host test: does the C chainload of the first-stage m1n1 (src/chainload.c chainload_image)
lay out the next image exactly like proxyclient tools/chainload.py does today (MacBook -> HV)?

The real chainload.c is compiled on the MacBook against small stand-in headers (ADT = one fake
/chosen/memory-map node with the SEPFW / preoslog / BootArgs values of a real J274 ADT); then for each
case the result (next_stage args, ADT props, boot args top_of_kernel_data, bytes in the new region) is
compared with the chainload.py formula (lines "image_size = align(len(image)) ... bootargs_size = 0x4000",
"tba.top_of_kernel_data = new_base + image_size", entry = new_base + 0x800).
Host only: no device, nothing installed.
usage: stage1_layout_test.py --src <tree>/src [--workdir DIR]
"""
import argparse
import ctypes as ct
import os
import subprocess
import tempfile
from pathlib import Path

SHIM = {
"types.h": """#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int64_t s64;
""",
"utils.h": """#pragma once
#include <stdio.h>
#include "types.h"
#define SZ_16K (1 << 14)
#define ALIGN_UP(x, a) (((x) + ((a)-1)) & ~((a)-1))
#define UNUSED(x) (void)(x)
extern char _base[];
static inline void dc_cvau_range(void *p, size_t n) { (void)p; (void)n; }
static inline void ic_ivau_range(void *p, size_t n) { (void)p; (void)n; }
typedef u64(generic_func)(u64, u64, u64, u64, u64);
struct vector_args { generic_func *entry; u64 args[5]; bool restore_logo; };
extern struct vector_args next_stage;
""",
"adt.h": """#pragma once
#include "types.h"
extern void *adt;
int adt_path_offset(const void *adt, const char *path);
int adt_setprop(void *adt, int nodeoffset, const char *name, void *value, size_t len);
int adt_getprop_copy(const void *adt, int nodeoffset, const char *name, void *out, size_t len);
#define ADT_GETPROP_ARRAY(adt, nodeoffset, name, arr) adt_getprop_copy(adt, nodeoffset, name, (arr), sizeof(arr))
""",
"malloc.h": "#pragma once\n#include <stdlib.h>\n",
"string.h": "#pragma once\n#include <string.h>\n",
"memory.h": "#pragma once\n",
"nvme.h": "#pragma once\n",
"chainload.h": "#pragma once\n#include \"types.h\"\nint chainload_image(void *image, size_t size, char **vars, size_t var_cnt);\n",
"xnuboot.h": """#pragma once
#include "types.h"
struct boot_video { u64 base, display, stride, width, height, depth; };
struct boot_args { u16 revision; u16 version; u64 virt_base; u64 phys_base; u64 mem_size;
    u64 top_of_kernel_data; struct boot_video video; u32 machine_type; void *devtree; u32 devtree_size;
    char rest[1024 + 16]; };
extern struct boot_args cur_boot_args;
""",
"harness.c": """#include <string.h>
#include "types.h"
#include "utils.h"
#include "xnuboot.h"
char _base[16];
u8 _chainload_stub_start[64] = {0xAA};
u8 _chainload_stub_end[1];
struct vector_args next_stage;
struct boot_args cur_boot_args;
void *adt = (void *)1;
static u64 props[3][2]; static int have[3];
static const char *names[3] = {"SEPFW", "preoslog", "BootArgs"};
static int idx(const char *n) { for (int i = 0; i < 3; i++) if (!strcmp(n, names[i])) return i; return -1; }
int adt_path_offset(const void *a, const char *p) { (void)a; return strcmp(p, "/chosen/memory-map") ? -1 : 8; }
int adt_getprop_copy(const void *a, int node, const char *n, void *out, size_t len) {
    (void)a; (void)node; int i = idx(n); if (i < 0 || !have[i] || len != 16) return -1;
    memcpy(out, props[i], 16); return 16; }
int adt_setprop(void *a, int node, const char *n, void *v, size_t len) {
    (void)a; (void)node; int i = idx(n); if (i < 0 || len != 16) return -1;
    memcpy(props[i], v, 16); have[i] = 1; return 0; }
void t_set(int i, u64 a, u64 s, int h) { props[i][0] = a; props[i][1] = s; have[i] = h; }
u64 t_get(int i, int j) { return props[i][j]; }
int t_have(int i) { return have[i]; }
u64 t_base(void) { return (u64)_base; }
u64 t_next(int i) { return i < 0 ? (u64)next_stage.entry : next_stage.args[i]; }
void t_ba_set(u64 phys, u64 mem, u64 top) { cur_boot_args.phys_base = phys; cur_boot_args.mem_size = mem;
    cur_boot_args.top_of_kernel_data = top; cur_boot_args.revision = 3; }
u64 t_stub_size(void) { return (u64)(_chainload_stub_end - _chainload_stub_start); }
""",
}

SEPFW, PREOSLOG = 0x59c000, 0x40000        # nwoas_scripts/logs/s264d-compare-20261120-021139/py-adt.bin
align = lambda v, a=0x4000: (v + a - 1) & ~(a - 1)


def py_layout(file_len, sepfw_len, preoslog_len):
    """proxyclient tools/chainload.py, raw mode (image = file + 4 zero bytes)."""
    image_size = align(file_len + 4)
    sepfw_off = image_size
    image_size += align(sepfw_len)
    preoslog_off = image_size
    image_size += align(preoslog_len)
    bootargs_off = image_size
    image_size += 0x4000
    return dict(sepfw_off=sepfw_off, preoslog_off=preoslog_off, bootargs_off=bootargs_off,
                image_size=image_size, entry_off=0x800, top_off=image_size)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, required=True)
    ap.add_argument("--workdir", type=Path)
    a = ap.parse_args()
    work = a.workdir or Path(tempfile.mkdtemp(prefix="st1-layout-"))
    shim = work / "shim"
    shim.mkdir(parents=True, exist_ok=True)
    (work / "build").mkdir(exist_ok=True)
    (work / "build" / "build_cfg.h").write_text("#pragma once\n")      # no CHAINLOADING: chainload_image only
    for n, t in SHIM.items():
        (shim / n).write_text(t)
    (shim / "chainload.c").write_bytes((a.src / "chainload.c").read_bytes())
    lib = work / "libst1layout.dylib"
    cmd = ["nice", "-n", "19", "clang", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-shared", "-fPIC", f"-iquote{shim}", str(shim / "chainload.c"), str(shim / "harness.c"), "-o", str(lib)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or r.stderr.strip():
        print(r.stderr)
        raise SystemExit("host chainload build failed or warned")
    L = ct.CDLL(str(lib))
    for f in ("t_get", "t_base", "t_next", "t_stub_size"):
        getattr(L, f).restype = ct.c_uint64
    L.t_get.argtypes = [ct.c_int, ct.c_int]
    L.t_next.argtypes = [ct.c_int]
    L.t_set.argtypes = [ct.c_int, ct.c_uint64, ct.c_uint64, ct.c_int]
    L.t_ba_set.argtypes = [ct.c_uint64, ct.c_uint64, ct.c_uint64]
    L.chainload_image.argtypes = [ct.c_void_p, ct.c_size_t, ct.c_void_p, ct.c_size_t]

    cases = [("stage1 -> ESP boot.bin = D4 (2,097,152 B)", 2097152, True),
             ("stage1-42 -> ESP nwoas-s264g.bin = s264G + nwoas_autostart=3", 2605056 + len(b"nwoas_autostart=3\n") + 4, True),
             ("D4-size image, ADT without preoslog", 2097152, False)]
    rows, ok = [], True
    for name, flen, with_pre in cases:
        img = bytes((i * 7 + 3) & 0xff for i in range(flen))
        sep = bytes((i * 13 + 1) & 0xff for i in range(SEPFW))
        pre = bytes((i * 5 + 9) & 0xff for i in range(PREOSLOG))
        sep_buf = ct.create_string_buffer(sep, SEPFW)
        pre_buf = ct.create_string_buffer(pre, PREOSLOG)
        img_buf = ct.create_string_buffer(img, flen)
        L.t_set(0, ct.addressof(sep_buf), SEPFW, 1)
        L.t_set(1, ct.addressof(pre_buf), PREOSLOG, 1 if with_pre else 0)
        L.t_set(2, 0x111110000, 0x4000, 1)
        L.t_ba_set(0x801224000, 0x3dfddc000, 0x1234)
        rc = L.chainload_image(img_buf, flen, None, 0)
        want = py_layout(flen, SEPFW, PREOSLOG if with_pre else 0)
        base = L.t_base()
        new_image = L.t_next(1)
        got = dict(sepfw_off=L.t_get(0, 0) - base, bootargs_off=L.t_get(2, 0) - base,
                   image_size=L.t_next(3), entry_off=L.t_next(4) - base)
        if with_pre:
            got["preoslog_off"] = L.t_get(1, 0) - base
        bad = [k for k in got if got[k] != want[k]]
        good = rc == 0 and not bad and L.t_next(2) == base and L.t_get(0, 1) == SEPFW and L.t_get(2, 1) == 0x4000
        good &= L.t_next(0) == base + want["bootargs_off"]          # boot args pointer handed to the next stage
        good &= L.t_next(-1) == new_image + want["image_size"]      # stub right after the region
        region = ct.string_at(new_image, want["image_size"])
        good &= region[:flen] == img and region[flen:flen + 4] == b"\0\0\0\0"
        good &= region[want["sepfw_off"]:want["sepfw_off"] + SEPFW] == sep
        if with_pre:
            good &= region[want["preoslog_off"]:want["preoslog_off"] + PREOSLOG] == pre
        ba = region[want["bootargs_off"]:want["bootargs_off"] + 64]
        top = int.from_bytes(ba[32:40], "little")                   # struct boot_args.top_of_kernel_data
        phys = int.from_bytes(ba[16:24], "little")
        mem = int.from_bytes(ba[24:32], "little")
        good &= top == base + want["top_off"] and phys == 0x801224000 and mem == 0x3dfddc000
        det = (f"rc {rc}; C: sepfw +{got['sepfw_off']:#x}"
               + (f" preoslog +{got['preoslog_off']:#x}" if with_pre else "")
               + f" bootargs +{got['bootargs_off']:#x} size {got['image_size']:#x} entry +{got['entry_off']:#x}"
               + f" top +{top - base:#x}; chainload.py: size {want['image_size']:#x}"
               + (f"; differs: {bad}" if bad else ""))
        rows.append((name, "SAME" if good else "DIFF", det))
        ok &= good
    print("| case | result | detail |")
    print("|---|---|---|")
    for r_ in rows:
        print("| " + " | ".join(r_) + " |")
    print(f"[stage1-layout] {sum(r_[1] == 'SAME' for r_ in rows)}/{len(rows)} SAME")
    print("STAGE1 LAYOUT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
