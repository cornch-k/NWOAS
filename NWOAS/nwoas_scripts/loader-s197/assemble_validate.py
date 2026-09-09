#!/usr/bin/env python3
"""S197: assemble and validate a guest payload from a rebuilt m1n1 prefix.

payload = out/<variant>/m1n1.bin            (rebuilt guest m1n1, raw layout)
        + m1n1_windows/apple-j274-padded.dtb (hash-pinned, 65536 B)
        + S192 FD                            (bytes 1376256.. of the S192 payload)

Nothing here boots, touches hardware, or modifies m1n1_windows. The layout is
derived from the m1n1-raw.ld linker script and the ELF symbols of the actual
build, not from the old 0x140000/0x150000 offsets.
"""
import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path("/Volumes/X31/NWOAS")
HERE = ROOT / "nwoas_scripts/loader-s197"
NM = "/opt/homebrew/opt/llvm/bin/llvm-nm"

# Pinned inputs (from S193 review section 2 and this session's measurements).
DTB_PATH = ROOT / "m1n1_windows/apple-j274-padded.dtb"
DTB_SHA = "ecc93b24740d80007d31986a43983c36eef47dd8b0b105b663c25a7b51190f76"
S192_PATH = ROOT / "nwoas_scripts/firmware-s192/m1n1-payload-s192-native-rtc-p12.bin"
S192_SHA = "b67313c48e057e3c1e608521b69a611e129328c04554785e429f4a4211d4c7c9"
S192_SIZE = 32342016
OLD_PREFIX_LEN = 1376256          # old m1n1 (0x140000) + dtb (0x10000)
OLD_M1N1_LEN = 0x140000
FD_SHA = "ae917fef199f29c2168550ae3af3dbe9f012c54140d36700e568258c3c3c382b"
FD_SIZE = 0x8000 + 0x1D80000      # MacMini2020.fdf: DATA region + FVMAIN_COMPACT
OLD_PREFIX_M1N1_SHA = "adefb605b567e473cd8b11b0b324724c0a9e89ed1a5151c239288f4109bb0154"

# S189 hardware log: "Jumping to entrypoint at 0x83e7dc800" => guest_base
# (informational only; guest_base depends on heap_top/devtree/trustcache sizes
# and is recomputed by hv.load_raw on every boot).
S189_GUEST_BASE = 0x83E7DC000
KERNEL_ALIGN = 2 << 20            # src/payload.c KERNEL_ALIGN

KERNEL_PATH_STRINGS = [
    b"##m1n1_ver##", b"Found a devicetree for %s at %p", b"Found a kernel at %p",
    b"cpufreq: Initializing clusters", b"Preparing to boot kernel at %p with fdt at %p",
    b"Setting SMP mode to WFE", b"Failed to prepare FDT", b"FDT prepared at %p",
    b"Devicetree compatible value: %s", b"Preparing to run next stage at %p",
    b"Vectoring to next stage",
]
STACK_CANARY = struct.pack("<Q", 0x544F424B43415453)  # m1n1-raw.ld .stack tail


def sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def fdt_root_compatible(dtb: bytes):
    magic, totalsize, off_struct, off_strings = struct.unpack(">IIII", dtb[:16])
    assert magic == 0xD00DFEED
    p = off_struct
    assert struct.unpack(">I", dtb[p:p + 4])[0] == 1  # FDT_BEGIN_NODE (root)
    p += 4
    while dtb[p] != 0:
        p += 1
    p = (p + 4) & ~3
    while True:
        tok = struct.unpack(">I", dtb[p:p + 4])[0]
        p += 4
        if tok == 3:  # FDT_PROP
            length, nameoff = struct.unpack(">II", dtb[p:p + 8])
            p += 8
            name = dtb[off_strings + nameoff:dtb.index(b"\0", off_strings + nameoff)]
            val = dtb[p:p + length]
            p = (p + length + 3) & ~3
            if name == b"compatible":
                return [s.decode() for s in val.split(b"\0") if s], totalsize
        elif tok == 4:  # FDT_NOP
            continue
        else:
            return [], totalsize


def nm_symbols(elf: Path):
    out = subprocess.run([NM, str(elf)], check=True, capture_output=True, text=True).stdout
    want = {"_start", "_base", "_end", "_payload_start", "_stack_bot", "_bss_end", "_file_end"}
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in want:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def main(variant: str):
    out = HERE / "out" / variant
    checks = []

    def check(name, ok, detail=""):
        checks.append({"check": name, "ok": bool(ok), "detail": detail})
        print(("PASS " if ok else "FAIL ") + name + (": " + detail if detail else ""))
        return ok

    m1n1 = (out / "m1n1.bin").read_bytes()
    syms = nm_symbols(out / "m1n1-raw.elf")
    tag = re.search(rb'"([^"]+)"', (out / "build_tag.h").read_bytes()).group(1).decode()

    # --- m1n1 prefix layout (from linker script + ELF, not assumed) ---
    check("m1n1.bin length == _payload_start (ELF)", len(m1n1) == syms["_payload_start"],
          f"len=0x{len(m1n1):x} _payload_start=0x{syms['_payload_start']:x}")
    check("_payload_start == _end (raw ld)", syms["_payload_start"] == syms["_end"])
    check("_payload_start 16 KiB aligned", syms["_payload_start"] % 0x4000 == 0)
    check("_start at +0x800 (launcher entryoffset=0x800)", syms["_start"] == 0x800,
          f"_start=0x{syms['_start']:x}")
    check("stack canary STACKBOT at end of m1n1.bin", m1n1[-8:] == STACK_CANARY,
          "raw image ends with .stack, so payload follows the stack exactly")
    check("_stack_bot == _payload_start", syms["_stack_bot"] == syms["_payload_start"])
    check("first instruction is vector 0 (mov x9,'0')", m1n1[:4] == bytes.fromhex("090680d2"))
    check("version tag string embedded", (b"##m1n1_ver##" + tag.encode()) in m1n1, tag)
    missing = [s.decode() for s in KERNEL_PATH_STRINGS if s not in m1n1]
    check("kernel+fdt path strings present", not missing, "missing: " + ", ".join(missing))
    check("prefix differs from old opaque prefix (expected)", sha256(m1n1[:OLD_M1N1_LEN]) != OLD_PREFIX_M1N1_SHA
          or len(m1n1) != OLD_M1N1_LEN)
    check("old prefix offset 0x150000 is NOT the new FD offset (expected change)",
          len(m1n1) + 0x10000 != OLD_PREFIX_LEN,
          f"new FD offset 0x{len(m1n1) + 0x10000:x} vs old 0x{OLD_PREFIX_LEN:x}")

    # --- DTB ---
    dtb = DTB_PATH.read_bytes()
    check("DTB sha256 pinned", sha256(dtb) == DTB_SHA, DTB_SHA[:16])
    compat, totalsize = fdt_root_compatible(dtb)
    check("DTB magic and totalsize == file size (padded 64 KiB)",
          dtb[:4] == b"\xd0\x0d\xfe\xed" and totalsize == len(dtb) == 0x10000,
          f"totalsize=0x{totalsize:x}")
    check("DTB root compatible contains apple,j274 (ADT target-type J274)", "apple,j274" in compat,
          ",".join(compat))
    check("DTB totalsize 16 KiB aligned (keeps FD 16 KiB aligned)", totalsize % 0x4000 == 0)

    # --- S192 FD extraction ---
    s192 = S192_PATH.read_bytes()
    check("S192 payload size and sha256 pinned", len(s192) == S192_SIZE and sha256(s192) == S192_SHA)
    check("S192 bytes 0x140000..0x150000 == pinned DTB", s192[OLD_M1N1_LEN:OLD_PREFIX_LEN] == dtb)
    fd = s192[OLD_PREFIX_LEN:]
    check("FD sha256 (S192 tail) pinned", sha256(fd) == FD_SHA, FD_SHA[:16])
    check("FD size == FDF regions 0x8000 + 0x1D80000", len(fd) == FD_SIZE, f"0x{len(fd):x}")
    code0, code1, text_off, image_size = struct.unpack("<IIQQ", fd[:24])
    check("FD Linux Image header: adr x1,. / b 0x8000", code0 == 0x10000001 and code1 == 0x14001FFF,
          f"{code0:08x} {code1:08x}")
    check("FD text_offset 0x80000, image_size 0x100e0000, magic ARM\\x64 at 0x38",
          text_off == 0x80000 and image_size == 0x100E0000 and fd[0x38:0x3C] == b"ARM\x64",
          f"text_offset=0x{text_off:x} image_size=0x{image_size:x}")
    fv_len = struct.unpack("<Q", fd[0x8020:0x8028])[0]
    check("FV header _FVH at 0x8028 and FvLength == 0x1D80000",
          fd[0x8028:0x802C] == b"_FVH" and fv_len == 0x1D80000, f"FvLength=0x{fv_len:x}")
    # load_one_payload() must reach the kernel-magic branch, so the FD must not
    # start with any earlier-tested magic.
    earlier = [b"\x1f\x8b", b"\xfd7zXZ\x00", b"\xd0\x0d\xfe\xed", b"07070", b"m1n1_sig",
               b"m1n1_initramfs", b"m1n1_logo_256128"]
    check("FD start matches no earlier payload magic (kernel branch is taken)",
          not any(fd.startswith(m) for m in earlier))

    # --- assemble ---
    payload = m1n1 + dtb + fd
    dtb_off = len(m1n1)
    fd_off = dtb_off + len(dtb)
    check("assembled layout: m1n1 | dtb@_payload_start | fd@_payload_start+0x10000",
          payload[dtb_off:dtb_off + 4] == b"\xd0\x0d\xfe\xed" and payload[fd_off + 0x38:fd_off + 0x3C] == b"ARM\x64",
          f"dtb@0x{dtb_off:x} fd@0x{fd_off:x} total={len(payload)} (0x{len(payload):x})")
    check("total payload 16 KiB aligned (hv.load_raw align())", len(payload) % 0x4000 == 0)
    check("total size differs from old 32342016 (launcher size asserts must change)",
          len(payload) != S192_SIZE, f"{len(payload)}")

    # --- in-place kernel alignment (informational, depends on runtime guest_base) ---
    old_fd_pa = S189_GUEST_BASE + OLD_PREFIX_LEN
    new_fd_pa = S189_GUEST_BASE + fd_off
    info = {
        "s189_guest_base": hex(S189_GUEST_BASE),
        "old_fd_pa": hex(old_fd_pa), "old_fd_2MiB_aligned": old_fd_pa % KERNEL_ALIGN == 0,
        "new_fd_pa": hex(new_fd_pa), "new_fd_2MiB_aligned": new_fd_pa % KERNEL_ALIGN == 0,
        "note": "payload.c load_kernel() memcpy()s image_size (0x100e0000) bytes to a "
                "2 MiB-aligned heap block when the FD is not 2 MiB aligned; same branch "
                "for old and new with the S189 guest_base, but guest_base is runtime-dependent.",
    }
    print("INFO kernel alignment:", json.dumps(info))

    ok = all(c["ok"] for c in checks)
    name = f"m1n1-payload-s197-{variant}.bin"
    target = out / name
    if ok:
        target.write_bytes(payload)
    tools = (out / "tools.txt").read_text()
    manifest = {
        "candidate": str(target) if ok else None,
        "variant": variant,
        "validated": ok,
        "bytes": len(payload),
        "sha256": sha256(payload) if ok else None,
        "boot_tested": False,
        "claim": "layout/provenance validated offline only; no boot compatibility claim",
        "layout": {
            "m1n1": {"offset": 0, "size": len(m1n1), "sha256": sha256(m1n1), "build_tag": tag,
                     "_start": hex(syms["_start"]), "_payload_start": hex(syms["_payload_start"])},
            "dtb": {"offset": dtb_off, "size": len(dtb), "sha256": sha256(dtb), "path": str(DTB_PATH),
                    "compatible": compat},
            "fd": {"offset": fd_off, "size": len(fd), "sha256": sha256(fd),
                   "source": f"{S192_PATH}[{OLD_PREFIX_LEN}:]", "s192_sha256": S192_SHA},
        },
        "old_prefix": {"m1n1_size": OLD_M1N1_LEN, "fd_offset": OLD_PREFIX_LEN,
                       "m1n1_sha256": OLD_PREFIX_M1N1_SHA, "tag": "v1.0.2-1471-g59fb544-dirty"},
        "source": {
            "worktree": str(ROOT / "m1n1-guest-s197"),
            "commit": "bddf7f06f033a7411834ac61381d8c997034f532",
            "rust-fatfs": "4eccb50d011146fbed20e133d33b22f3c27292e7",
            "artwork": "80d14f8b6f485b310e305a84b4b806361518ddd1 (not used by build)",
            "patch": "guest-compat-s197.patch" if variant == "compat" else None,
        },
        "tools": tools,
        "kernel_alignment_info": info,
        "checks": checks,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(("CANDIDATE " + str(target) + " " + manifest["sha256"]) if ok else "NOT WRITTEN: validation failed")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "head"))
