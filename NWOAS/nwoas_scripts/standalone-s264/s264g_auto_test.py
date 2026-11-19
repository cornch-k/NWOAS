#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS S264G offline test for the 4-2 auto-start (build host only; nothing touches the Mac mini).

1. Decision logic (src/nwoas_s264_auto.c, host build via ctypes): deadline vs host-seen, and the launch
   config composition for every ESP file situation (nowin, cfg present/wrong, payload present/missing/
   too large/empty) - expected values written out here from nwoas_s264_auto.h.
2. ESP read on an image file: the REAL m1n1 Rust code (rust/src/nvme.rs, gpt.rs, chainload.rs of the
   tree under test, included by #[path]) compiled for the host with a file-backed nvme_read(), on a
   4096-byte-sector GPT disk image whose slot 4 is a FAT32 ESP (4096-byte sectors, made with the
   vendored rust-fatfs) with the GUID of PLAN-4-1.  Checks rust_load_file_into (read / too big /
   missing / exists) and rust_load_image with the stage-1 chainload spec "…;m1n1/boot.bin".
usage: s264g_auto_test.py [--src DIR] [--workdir DIR]
"""
import argparse
import ctypes as ct
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import s264_common as C  # noqa: E402

ESP_GUID = "a3e73003-c14a-487b-9f2e-8854fe119192"
CFG_SIZE = C.CFG_SIZE


# ------------------------------------------------------------------ 1. decision logic
class Files(ct.Structure):
    _fields_ = [("nowin_rc", ct.c_int), ("cfg_rc", ct.c_int), ("cfg_size", ct.c_uint64),
                ("cfg_bytes", ct.c_void_p), ("payload_rc", ct.c_int), ("payload_size", ct.c_uint64),
                ("payload_ptr", ct.c_uint64)]


def build_c(src, work):
    lib = work / "libs264auto.dylib"
    cmd = ["nice", "-n", "19", "clang", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
           "-DS264_HOST", "-shared", "-fPIC", f"-I{src}", str(src / "nwoas_s264_auto.c"),
           str(src / "nwoas_s264_plan.c"), "-o", str(lib)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or r.stderr.strip():
        print(r.stderr)
        raise SystemExit("host auto library build failed or warned")
    L = ct.CDLL(str(lib))
    L.s264_auto_should_fire.argtypes = [ct.c_uint32, ct.c_uint64, ct.c_bool]
    L.s264_auto_should_fire.restype = ct.c_bool
    L.s264_auto_compose.argtypes = [ct.POINTER(Files), ct.c_void_p]
    L.s264_auto_payload_cap.argtypes = [ct.c_void_p]
    L.s264_auto_payload_cap.restype = ct.c_uint64
    L.s264_default_config.argtypes = [ct.c_void_p]
    return L


def decision_rows(L):
    rows, ok = [], True
    fire = [((0, 10**9, False), False, "variable absent (MacBook chainload) never fires"),
            ((3, 2_999_999, False), False, "3 s: 2.999999 s"), ((3, 3_000_000, False), True, "3 s: 3.0 s"),
            ((3, 10**9, True), False, "host byte seen"), ((5000, 599_999_999, False), False, "capped 600 s: 599.9 s"),
            ((5000, 600_000_000, False), True, "capped 600 s: 600 s")]
    for args, want, name in fire:
        got = bool(L.s264_auto_should_fire(*args))
        rows.append((f"fire: {name}", "SAME" if got == want else "DIFF", f"want {want} got {got}"))
        ok &= got == want
    dflt = ct.create_string_buffer(CFG_SIZE)
    L.s264_default_config(dflt)
    d = bytes(dflt.raw)
    flags_off = 8                                       # magic, version, size, flags
    d_flags = int.from_bytes(d[12:16], "little")
    good_cfg = bytearray(d)
    good_cfg[12:16] = (d_flags & ~(1 << 10)).to_bytes(4, "little")      # e.g. sound B off
    bad_cfg = bytearray(d)
    bad_cfg[0] ^= 1
    P = 0x80C4B4000
    cases = [("nowin present", dict(nowin_rc=0), 1, None),
             ("nowin present (non-empty)", dict(nowin_rc=1), 1, None),
             ("no cfg, payload ok -> default + FATAL", dict(payload_rc=0, payload_size=33669120), 0,
              (d_flags | (1 << 8), 33669120)),
             ("cfg file ok, payload ok -> file + FATAL", dict(cfg_rc=0, cfg=bytes(good_cfg), payload_rc=0,
                                                              payload_size=1234), 0,
              ((d_flags & ~(1 << 10)) | (1 << 8), 1234)),
             ("cfg file wrong magic", dict(cfg_rc=0, cfg=bytes(bad_cfg), payload_rc=0, payload_size=1), 4, None),
             ("cfg file too large", dict(cfg_rc=1, cfg_size=CFG_SIZE + 8, payload_rc=0, payload_size=1), 4, None),
             ("cfg file short", dict(cfg_rc=0, cfg=bytes(d[:100]), payload_rc=0, payload_size=1), 4, None),
             ("payload missing", dict(payload_rc=-1), 2, None),
             ("payload too big", dict(payload_rc=1, payload_size=1 << 31), 3, None),
             ("payload empty", dict(payload_rc=0, payload_size=0), 5, None)]
    for name, kw, want, flags_size in cases:
        f = Files(nowin_rc=kw.get("nowin_rc", -1), cfg_rc=kw.get("cfg_rc", -1), payload_rc=kw.get("payload_rc", -1),
                  payload_size=kw.get("payload_size", 0), payload_ptr=P)
        keep = None
        if "cfg" in kw:
            keep = ct.create_string_buffer(kw["cfg"], len(kw["cfg"]))
            f.cfg_bytes = ct.cast(keep, ct.c_void_p)
            f.cfg_size = len(kw["cfg"])
        elif "cfg_size" in kw:
            f.cfg_size = kw["cfg_size"]
        out = ct.create_string_buffer(CFG_SIZE)
        rc = L.s264_auto_compose(ct.byref(f), out)
        good = rc == want
        det = f"rc {rc} (want {want})"
        if flags_size and rc == 0:
            o = bytes(out.raw)
            fl = int.from_bytes(o[12:16], "little")
            pp, ps = int.from_bytes(o[16:24], "little"), int.from_bytes(o[24:32], "little")
            good &= (fl, ps) == flags_size and pp == P
            det += f" flags {fl:#x} payload {pp:#x}+{ps}"
        rows.append((f"compose: {name}", "SAME" if good else "DIFF", det))
        ok &= good
        _ = keep, flags_off
    cap = L.s264_auto_payload_cap(dflt)
    good = cap == (768 << 20) - 0x800000 and cap > 33669120
    rows.append(("payload room (default py heap 768 MiB - 8 MiB scratch)", "SAME" if good else "DIFF", f"{cap:#x}"))
    ok &= good
    return rows, ok


# ------------------------------------------------------------------ 2. ESP read on an image file
CARGO = """[package]
name = "esp_reader"
version = "0.1.0"
edition = "2021"
publish = false

[dependencies]
fatfs = {{ path = "{vendor}", default-features = false, features = ["lfn", "alloc"] }}
uuid = {{ version = "1.7.0", default-features = false }}
"""

MAIN = r'''#![feature(new_zeroed_alloc)]
#![allow(dead_code)]
extern crate alloc;
use core::ffi::c_void;
use std::io::{Read as _, Seek as _, SeekFrom as SF, Write as _};
use std::sync::Mutex;

#[allow(non_camel_case_types)]
type c_size_t = usize;

#[macro_export]
macro_rules! println { ($($t:tt)*) => { std::println!($($t)*) } }

#[path = "{rs}/nvme.rs"]
mod nvme;
#[path = "{rs}/gpt.rs"]
mod gpt;
#[path = "{rs}/chainload.rs"]
mod chainload;

static IMG: Mutex<Option<std::fs::File>> = Mutex::new(None);

#[no_mangle]
pub extern "C" fn nvme_read(nsid: u32, lba: u64, buf: *mut c_void) -> bool {
    if nsid != 1 { return false; }
    let mut g = IMG.lock().unwrap();
    let f = g.as_mut().unwrap();
    let mut b = vec![0u8; 4096];
    if f.seek(SF::Start(lba * 4096)).is_err() || f.read_exact(&mut b).is_err() { return false; }
    unsafe { core::ptr::copy_nonoverlapping(b.as_ptr(), buf as *mut u8, 4096) };
    true
}

// ---- image maker: 4 KiB-sector GPT, slot 4 = FAT32 ESP (fatfs, 4 KiB sectors)
struct Part<'a> { f: &'a mut std::fs::File, base: u64, len: u64, pos: u64 }
impl fatfs::IoBase for Part<'_> { type Error = (); }
impl fatfs::Read for Part<'_> {
    fn read(&mut self, buf: &mut [u8]) -> Result<usize, ()> {
        let n = core::cmp::min(buf.len() as u64, self.len - self.pos) as usize;
        self.f.seek(SF::Start(self.base + self.pos)).map_err(|_| ())?;
        self.f.read_exact(&mut buf[..n]).map_err(|_| ())?;
        self.pos += n as u64; Ok(n)
    }
}
impl fatfs::Write for Part<'_> {
    fn write(&mut self, buf: &[u8]) -> Result<usize, ()> {
        let n = core::cmp::min(buf.len() as u64, self.len - self.pos) as usize;
        self.f.seek(SF::Start(self.base + self.pos)).map_err(|_| ())?;
        self.f.write_all(&buf[..n]).map_err(|_| ())?;
        self.pos += n as u64; Ok(n)
    }
    fn flush(&mut self) -> Result<(), ()> { Ok(()) }
}
impl fatfs::Seek for Part<'_> {
    fn seek(&mut self, p: fatfs::SeekFrom) -> Result<u64, ()> {
        self.pos = match p { fatfs::SeekFrom::Start(n) => n, fatfs::SeekFrom::End(n) => (self.len as i64 + n) as u64,
                             fatfs::SeekFrom::Current(n) => (self.pos as i64 + n) as u64 };
        Ok(self.pos)
    }
}

fn crc32(d: &[u8]) -> u32 {
    let mut c: u32 = 0xffffffff;
    for &b in d { c ^= b as u32; for _ in 0..8 { c = if c & 1 != 0 { (c >> 1) ^ 0xedb88320 } else { c >> 1 }; } }
    !c
}

fn make_image(path: &str, files: &[(String, String)], esp_guid: uuid::Uuid, corrupt_gpt: bool) {
    let esp_start: u64 = 6; let esp_sectors: u64 = 70000; // 4 KiB sectors (~273 MiB, FAT32 needs >= 65525 clusters)
    let total = esp_start + esp_sectors + 6;
    let mut f = std::fs::OpenOptions::new().read(true).write(true).create(true).truncate(true).open(path).unwrap();
    f.set_len(total * 4096).unwrap();
    // partition entries at LBA 2: slots 1-3 dummy, slot 4 = ESP
    let mut ents = vec![0u8; 128 * 128];
    let efi_type = uuid::Uuid::parse_str("c12a7328-f81f-11d2-ba4b-00a0c93ec93b").unwrap();
    for i in 0..4u64 {
        let e = &mut ents[(i as usize) * 128..(i as usize + 1) * 128];
        let (ty, guid, s, n) = if i == 3 { (efi_type, esp_guid, esp_start, esp_sectors) }
            else { (uuid::Uuid::from_u128(0x1111 + i as u128), uuid::Uuid::from_u128(0x2222 + i as u128), 1 + i, 0) };
        if i < 3 { continue; }
        e[0..16].copy_from_slice(&ty.to_bytes_le());
        e[16..32].copy_from_slice(&guid.to_bytes_le());
        e[32..40].copy_from_slice(&s.to_le_bytes());
        e[40..48].copy_from_slice(&(s + n - 1).to_le_bytes());
    }
    let mut h = vec![0u8; 92];
    h[0..8].copy_from_slice(if corrupt_gpt { b"EFI PARX" } else { b"EFI PART" });
    h[8..12].copy_from_slice(&0x00010000u32.to_le_bytes());
    h[12..16].copy_from_slice(&92u32.to_le_bytes());
    h[24..32].copy_from_slice(&1u64.to_le_bytes());
    h[32..40].copy_from_slice(&(total - 1).to_le_bytes());
    h[72..80].copy_from_slice(&2u64.to_le_bytes());
    h[80..84].copy_from_slice(&128u32.to_le_bytes());
    h[84..88].copy_from_slice(&128u32.to_le_bytes());
    h[88..92].copy_from_slice(&crc32(&ents).to_le_bytes());
    let c = crc32(&h); h[16..20].copy_from_slice(&c.to_le_bytes());
    f.seek(SF::Start(4096)).unwrap(); f.write_all(&h).unwrap();
    f.seek(SF::Start(2 * 4096)).unwrap(); f.write_all(&ents).unwrap();
    {
        let mut p = Part { f: &mut f, base: esp_start * 4096, len: esp_sectors * 4096, pos: 0 };
        fatfs::format_volume(&mut p, fatfs::FormatVolumeOptions::new().bytes_per_sector(4096)
            .fat_type(fatfs::FatType::Fat32).volume_label(*b"SYSTEM     ")).unwrap();
    }
    let p = Part { f: &mut f, base: esp_start * 4096, len: esp_sectors * 4096, pos: 0 };
    let fs = fatfs::FileSystem::new(p, fatfs::FsOptions::new()).unwrap();
    assert!(fs.fat_type() == fatfs::FatType::Fat32);
    for (name, src) in files {
        let data = std::fs::read(src).unwrap();
        let root = fs.root_dir();
        let mut dir = root.clone();
        let parts: Vec<&str> = name.split('/').collect();
        for d in &parts[..parts.len() - 1] { dir = match dir.open_dir(d) { Ok(x) => x, Err(_) => dir.create_dir(d).unwrap() }; }
        let mut fh = dir.create_file(parts[parts.len() - 1]).unwrap();
        use fatfs::Write as _W; _W::write_all(&mut fh, &data).unwrap();
    }
    fs.unmount().unwrap();
}

fn main() {
    let a: Vec<String> = std::env::args().collect();
    // a[1] image, a[2] files manifest (name=path lines), a[3] cases json-lines, a[4] out dir, a[5] esp guid, a[6] corrupt 0/1
    let files: Vec<(String, String)> = std::fs::read_to_string(&a[2]).unwrap().lines()
        .filter(|l| !l.is_empty()).map(|l| { let (n, p) = l.split_once('=').unwrap(); (n.to_string(), p.to_string()) }).collect();
    make_image(&a[1], &files, uuid::Uuid::parse_str(&a[5]).unwrap(), &a[6] == "1");
    *IMG.lock().unwrap() = Some(std::fs::OpenOptions::new().read(true).open(&a[1]).unwrap());
    for (i, line) in std::fs::read_to_string(&a[3]).unwrap().lines().enumerate() {
        let (kind, rest) = line.split_once(' ').unwrap();
        let (cap, spec) = rest.split_once(' ').unwrap();
        let spec_c = std::ffi::CString::new(spec).unwrap();
        if kind == "into" {
            let cap: usize = cap.parse().unwrap();
            let mut buf = vec![0xa5u8; cap];
            let mut size: usize = 0;
            let rc = unsafe { chainload::rust_load_file_into(spec_c.as_ptr(), if cap == 0 { core::ptr::null_mut() } else { buf.as_mut_ptr() as *mut c_void }, cap, &mut size) };
            std::fs::write(format!("{}/out{}.bin", a[4], i), &buf[..core::cmp::min(cap, size.max(if rc == 1 { 16.min(cap) } else { 0 }))]).unwrap();
            std::println!("RESULT {} into rc={} size={}", i, rc, size);
        } else {
            let mut img: *mut c_void = core::ptr::null_mut();
            let mut size: usize = 0;
            let rc = unsafe { chainload::rust_load_image(spec_c.as_ptr(), &mut img, &mut size) };
            if rc == 0 { let s = unsafe { core::slice::from_raw_parts(img as *const u8, size) }; std::fs::write(format!("{}/out{}.bin", a[4], i), s).unwrap(); }
            std::println!("RESULT {} image rc={} size={}", i, rc, size);
        }
    }
}
'''


def esp_rows(src, work):
    rows, ok = [], True
    crate = work / "esp_reader"
    (crate / "src").mkdir(parents=True, exist_ok=True)
    rs = (src.parent / "rust" / "src").resolve()
    (crate / "Cargo.toml").write_text(CARGO.format(vendor=(rs.parent / "vendor" / "rust-fatfs")))
    shutil.copy(rs.parent / "Cargo.lock", crate / "Cargo.lock")
    (crate / "src" / "main.rs").write_text(MAIN.replace("{rs}", str(rs)))
    env = dict(os.environ, RUSTUP_TOOLCHAIN="1.88.0", CARGO_NET_OFFLINE="true", RUSTC_BOOTSTRAP="1",
               CARGO_BUILD_JOBS="2")
    r = subprocess.run(["nice", "-n", "19", "cargo", "build", "--quiet", "--release", "--offline",
                        "--target-dir", str(work / "target")], cwd=crate, capture_output=True, text=True, env=env)
    if r.returncode:
        print(r.stderr[-3000:])
        return [("ESP reader host build (real nvme.rs/gpt.rs/chainload.rs)", "DIFF", "build failed")], False
    exe = work / "target" / "release" / "esp_reader"
    files = work / "files"
    files.mkdir(exist_ok=True)
    payload = bytes((i * 131 + (i >> 12)) & 0xff for i in range(1 << 20)) * 32 + b"tail-NWOAS"   # 33.5 MB
    (files / "payload").write_bytes(payload)
    (files / "boot").write_bytes(b"\x00m1n1-stage2-test" * 4096)
    (files / "cfg").write_bytes(bytes(range(256))[:CFG_SIZE])
    (files / "empty").write_bytes(b"")

    def run(manifest, cases, guid=ESP_GUID, corrupt=False):
        img, man, cas, out = work / "disk.img", work / "man.txt", work / "cases.txt", work / "out"
        shutil.rmtree(out, ignore_errors=True)
        out.mkdir()
        man.write_text("".join(f"{n}={files / p}\n" for n, p in manifest))
        cas.write_text("".join(f"{k} {c} {s}\n" for k, c, s in cases))
        rr = subprocess.run(["nice", "-n", "19", str(exe), str(img), str(man), str(cas), str(out), guid,
                             "1" if corrupt else "0"], capture_output=True, text=True)
        res = {}
        for line in rr.stdout.splitlines():
            if line.startswith("RESULT "):
                _, i, kind, rc, size = line.split()
                res[int(i)] = (int(rc.split("=")[1]), int(size.split("=")[1]),
                               (out / f"out{i}.bin").read_bytes() if (out / f"out{i}.bin").exists() else b"")
        img.unlink(missing_ok=True)
        return res, rr
    sp = f"{ESP_GUID};"
    manifest = [("m1n1/nwoas-payload.bin", "payload"), ("m1n1/boot.bin", "boot"), ("m1n1/nwoas-s264.cfg", "cfg")]
    cases = [("into", len(payload), sp + "m1n1/nwoas-payload.bin"),
             ("into", len(payload) - 1, sp + "m1n1/nwoas-payload.bin"),
             ("into", 0, sp + "m1n1/nowin"),
             ("into", CFG_SIZE, sp + "m1n1/nwoas-s264.cfg"),
             ("image", 0, sp + "m1n1/boot.bin"),
             ("image", 0, sp + "m1n1\\boot.bin"),
             ("into", 16, "00000000-0000-0000-0000-000000000001;m1n1/boot.bin")]
    res, rr = run(manifest, cases)
    want = [("payload read into the heap buffer", 0, len(payload), payload),
            ("payload 1 byte too big: rc 1, size reported, nothing read", 1, len(payload), None),
            ("m1n1/nowin missing", -1, 0, None),
            ("cfg read (exact size)", 0, CFG_SIZE, (files / "cfg").read_bytes()),
            ("stage-1 spec <GUID>;m1n1/boot.bin (rust_load_image)", 0, len((files / "boot").read_bytes()),
             (files / "boot").read_bytes()),
            ("backslash spec <GUID>;m1n1\\boot.bin", None, None, None),
            ("other partition GUID", -1, 0, None)]
    for i, (name, rc, size, data) in enumerate(want):
        got = res.get(i)
        if got is None:
            rows.append((f"ESP read: {name}", "DIFF", f"no result; stderr {rr.stderr[-300:]}"))
            ok = False
            continue
        if rc is None:          # behaviour recorded, not required
            rows.append((f"ESP read: {name}", "SAME", f"recorded: rc {got[0]} size {got[1]}"))
            continue
        good = got[0] == rc and got[1] == size and (data is None or got[2] == data)
        det = f"rc {got[0]} size {got[1]}" + (f" sha256 {hashlib.sha256(got[2]).hexdigest()[:16]}" if got[2] else "")
        if i == 1:
            good &= set(got[2]) <= {0xa5}                # untouched
        rows.append((f"ESP read: {name}", "SAME" if good else "DIFF", det))
        ok &= good
    res, rr = run(manifest + [("m1n1/nowin", "empty")], [("into", 0, sp + "m1n1/nowin")])
    good = res.get(0, (None,))[0] == 0
    rows.append(("ESP read: m1n1/nowin present (0 bytes) -> exists", "SAME" if good else "DIFF", str(res.get(0, '-')[:2])))
    ok &= good
    # NWOAS 4-1 negative rehearsal (stage1-missing.bin): the chainload file is absent -> rc < 0 -> USB proxy
    res, rr = run(manifest, [("image", 0, sp + "m1n1/nwoas-absent.bin")])
    good = res.get(0, (0,))[0] < 0
    rows.append(("ESP read: stage-1 spec …;m1n1/nwoas-absent.bin (file missing) -> rc < 0 (fallback = USB proxy)",
                 "SAME" if good else "DIFF", str(res.get(0, '-')[:2])))
    ok &= good
    res, rr = run(manifest, [("into", 16, sp + "m1n1/boot.bin")], corrupt=True)
    good = res.get(0, (None,))[0] == -1
    rows.append(("ESP read: corrupt GPT header -> -1 (fallback = MacBook proxy)", "SAME" if good else "DIFF",
                 str(res.get(0, '-')[:2])))
    ok &= good
    return rows, ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, default=Path(os.environ.get(
        "S264_SRC", ROOT / "m1n1-standalone-s264" / "src")))
    ap.add_argument("--workdir", type=Path)
    a = ap.parse_args()
    work = a.workdir or Path(tempfile.mkdtemp(prefix="s264g-auto-"))
    work.mkdir(parents=True, exist_ok=True)
    try:
        L = build_c(a.src, work)
        rows, ok = decision_rows(L)
        r2, ok2 = esp_rows(a.src, work)
        rows += r2
        ok &= ok2
        print("| item | result | detail |\n|---|---|---|")
        for r in rows:
            print(f"| {r[0]} | {r[1]} | {r[2]} |")
        print(f"[s264G-auto] {sum(1 for r in rows if r[1] == 'SAME')}/{len(rows)} SAME")
        print("S264G AUTO:", "PASS" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        for p in ("target", "files", "out", "disk.img"):
            q = work / p
            if q.is_dir():
                shutil.rmtree(q, ignore_errors=True)
            elif q.exists():
                q.unlink()
        _ = json


if __name__ == "__main__":
    sys.exit(main())
