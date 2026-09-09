# S199: S197 reproducibility guard fix, and the FD `image_size` question

Offline source work only. Nothing was booted, no hardware, launcher, USB,
Mini, secrets or MacBook UI was touched, and no ADT/NVRAM dump was read.
Both S197 candidates keep their hashes (verified below). Native Windows boot
is the goal; nothing here claims native completion. The S192 EL2-guest boot
and Cinebench exit 0 are guest results, and the S197 candidate is still not
booted.

## 1. Vendored-tree guard in `loader-s197/build.sh`

**Problem.** `build.sh` said the vendored `rust/vendor/rust-fatfs` tree was
"byte-identical to the pinned commit", but the check was
`diff -rq <(find . -type f | sort) <(git ls-tree -r --name-only ...)`: a
comparison of *path lists*. A file edited in place (same name, same size,
same mtime) passed. Demonstrated on a copy with one byte of `src/lib.rs`
flipped: the old guard reports no difference.

**Fix.** New read-only verifier `verify_vendor_tree.sh <dir> <git-repo>
<tree-ish>` (bash 3.2 compatible, this host's `/bin/bash`):

- For every `git ls-tree -r -z` entry of the pinned tree:
  - `100644`/`100755`: path must be a regular file and not a symlink; raw
    bytes hashed with `git hash-object --no-filters` (no `-w`; nothing is
    written to either repository) must equal the tree blob id; executable bit
    must match the mode.
  - `120000`: path must be a symlink and its target string must hash to the
    blob id.
  - `160000` gitlink or any other mode: fail (not verifiable here).
- Every directory component of a tree path must be a real directory, not a
  symlink.
- Every file or symlink on disk that is not in the tree fails as `EXTRA`
  (only a top-level `.git` gitfile/dir is ignored, since a real submodule
  checkout carries one and it is not a build input).
- Exit 0 only when all checks pass, 1 on mismatch (each mismatch printed),
  2 on usage/environment error. `GIT_OPTIONAL_LOCKS=0` is set so git takes
  no locks.

`build.sh` now calls this verifier in its source-pinning block and stops on
failure; the pinned commit, tool pins and everything after the guard are
unchanged. `loader-s197/README.md` records the correction.

**Tests** (`test_verify_vendor_tree.sh`, all in `mktemp -d`, 17 cases,
17 pass):

| Case | Expect | Result |
|---|---|---|
| A0 unmodified copy of the S197 vendored tree | pass | ok |
| A1 one byte flipped in `src/lib.rs`, same name/size/mtime | fail | ok |
| A2 trailing newline appended to `Cargo.toml` | fail | ok |
| A3 exec bit added to a `100644` file | fail | ok |
| A4 exec bit removed from `build-nostd.sh` (`100755`) | fail | ok |
| A5 file replaced by a symlink to identical content | fail | ok |
| A6 extra file, A7 missing file, A9 stray `target/` build dir | fail | ok |
| A8 `src/` replaced by a symlink to a moved copy | fail | ok |
| A10 top-level `.git` gitfile present | pass | ok |
| A11 unknown tree id | usage error (2) | ok |
| A12 the real S197 vendored tree, after all of the above | pass | ok |
| B0 throwaway repo with a `120000` symlink entry, `git archive`d | pass | ok |
| B1 symlink target changed, B2 symlink replaced by file, B3 exec bit removed | fail | ok |

The pinned tree has no symlinks (33 blobs: 31 `100644`, 2 `100755`), so
Part B exists to exercise the symlink path. No sanitizer applies to shell;
`bash -n` passes for all three scripts and `shellcheck` is not installed on
this host.

**Reproducibility re-check.** `build.sh head` and `build.sh compat` were
re-run through the corrected script (`nice -n 19`, `make -j2`,
`CARGO_BUILD_JOBS=2`, sequentially). Guard output:

```
OK: .../m1n1-guest-s197/rust/vendor/rust-fatfs matches 4eccb50d011146fbed20e133d33b22f3c27292e7
    (tree 0a4c434a256bc7b0e6e7594aa9ff1dde0d4f875b): 33 entries, 33 files, 0 symlinks, no extras
```

`out/head/sha256.txt` and `out/compat/sha256.txt` are byte-identical to the
values before the change (`m1n1.bin` head `b2515b77…b007`, compat
`1c255a7f…2078`), the two payloads still hash to `ebaf8b90…0e831` and
`9a0b96af…5020a` as listed in `loader-s197/README.md`, and the worktree is
clean after the compat run.

## 2. Is FD `image_size = 0x100e0000` a required footprint or a wrong length?

**Finding: it is a mis-encoded 30 MiB, not a footprint requirement.** Nothing
in the UEFI side needs memory beyond `PcdFdSize` (0x1E00000, 30 MiB) after
the FD, and m1n1 uses the value only as an allocation size and a `memcpy`
length. The current value merely makes m1n1 reserve and copy 269 MiB instead
of 30 MiB. It is harmless in the sense that the copy source is mapped RAM and
the copy destination is a private heap block, and S192 boots with it, so no
change is made now.

### Evidence, FDF side

- `Platform/MacMini2020Pkg/MacMini2020.fdf:31`: `Size = 0x00001E00000`
  (`PcdFdSize`, 30 MiB). Line 63: `0x00, 0x00, 0x0e, 0x10, 0x00, 0x00, 0x00,
  0x00, # image_size: 30 MB`. Little-endian that is `0x100E0000`
  (269,221,888). 30 MiB would be `0x00, 0x00, 0xe0, 0x01`. The two bytes are
  nibble-swapped relative to the intended value, and the comment states the
  intent. Line 73: the FV region is `0x8000|0x1D80000`, so the FD file is
  0x1D88000 bytes (30,965,760, matches the built
  `J274MACMINI2020_EFI.fd`) — smaller than `PcdFdSize`, not larger.
- All five platform FDFs (`MacBookAirMid2020`, `MacBookProLate2020`,
  `MacBookProEarly2023`, `MacStudio2022`, `MacMini2020`) carry the same
  bytes; the MacMini line is unchanged since the initial commit `35034be`
  ("Add M1 Windows UEFI platform support"). The local
  `apple_silicon_platforms_mu` checkout has no modification to that file.

### Evidence, m1n1 side (`m1n1-guest-s197`, commit `bddf7f06`)

- `src/payload.c:306-308`: `payload_run` calls `load_one_payload(p, 0)`, so
  `size == 0` for every inline payload.
- `src/payload.c:134-159` `load_kernel`: `assert(size <= image_size)`;
  if the FD is not 2 MiB aligned (`KERNEL_ALIGN`, line 23) it does
  `heapblock_alloc_aligned(kernel->image_size, KERNEL_ALIGN)` and
  `memcpy(new_addr, kernel, size ? size : kernel->image_size)` (lines
  142-143), then returns NULL (line 157) so the payload loop ends. The
  comment at 147-153 says the header has no accurate file size for inline
  payloads. `image_size` is therefore both the heap reservation and the copy
  length.
- `src/heapblock.c:35-41`: bump allocator; `heap_base` advances by
  `image_size`. Later heap users (DT buffer, malloc arena) are placed
  after the block. `src/kboot.h:11` documents the field as "Effective Image
  size".
- No other user: `grep image_size src/` matches only `payload.c` and the
  unrelated `chainload.c` local variable.
- `proxyclient/m1n1/hv/__init__.py:2186-2224` (`load_raw`): the guest region
  is `image + SEPFW + preoslog + boot-args` at `guest_base`, and the tracer
  `RAM-HIGH` maps `phys_base .. mem_size_actual`, so the 269 MiB read that
  starts at `guest_base + 0x21c000` (S197) or `+ 0x150000` (S192) covers the
  rest of the payload, SEPFW, preoslog, boot-args, then free mapped RAM.
  Over-read source is mapped; destination is the fresh heap block; nothing is
  overwritten. This is a code-reading conclusion, not a measured one.

### Evidence, UEFI PrePi/PEI side (`apple_silicon_platforms_mu`)

- `PrePi/AArch64/ModuleEntryPoint.S:159`: the only header field read is the
  magic at `+0x38`. Lines 186-187: the PE/COFF image is relocated in place
  with the FD's own first 0x8000 bytes as a temporary stack. `image_size` is
  never read.
- `ModuleEntryPoint.S:43-60,82`: `FdTop = PcdFdBaseAddress + PcdFdSize`; the
  UEFI memory region (`PcdSystemMemoryUefiRegionSize` = 0x8000000, 128 MiB,
  `AppleSiliconPkg.dsc.inc:63`) holding stacks and HOBs is placed at the top
  of system memory, or directly *below* `FdBase` if the gap above the FD is
  smaller than 128 MiB. It is never placed after the FD. Primary stack is
  0x20000 below that top (line 89).
- `PrePi/AdtParser.c:35,48`: system memory is `phys_base .. phys_base +
  mem_size - 4 GiB` from boot-args; `top_of_kernel_data` is not consumed, so
  UEFI's view of RAM does not depend on where m1n1's heap ended.
- `PrePi/PrePi.c:46,77,82`: `UefiMemoryLength = PcdSystemMemoryUefiRegionSize`;
  cache invalidation covers exactly `PcdFdSize`; then `MemoryPeim`.
- `T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c:206-207,
  212-320`: `FdTop = FdBase + PcdFdSize`; the FD is split out of the system
  memory resource and marked with `BuildMemoryAllocationHob(FdBase,
  PcdFdSize, EfiBootServicesData)` (316-320). Only `PcdFdSize` bytes are
  protected; the space after `FdTop` is ordinary system memory.
- `AppleSiliconPkg/Library/PlatformPeiLib/PlatformPeiLib.c:22`:
  `BuildFvHob(PcdFvBaseAddress, PcdFvSize)`.

So the UEFI image is position-independent, relocated in place, has no BSS
outside the FV, and its scratch memory lives elsewhere. The only true
footprint constraint is `image_size >= PcdFdSize` (0x1E00000): UEFI treats
`[FdBase, FdBase + 0x1E00000)` as its own, which is 0x78000 bytes past the
FD file end, and m1n1's bump allocator must not hand that tail to anyone
else. `0x1E00000` satisfies it; `0x100E0000` satisfies it with 239 MiB to
spare.

### Why not change it now

- S192 (same header) boots as an EL2 guest through Cinebench; the
  over-allocation is not on the failure path of anything under
  investigation.
- Shrinking `image_size` moves every later m1n1 heap allocation (the
  `kboot_prepare_dt` buffers, malloc arena) down by 0xE2E0000 bytes. That is
  a real layout change with no offline way to prove it is neutral.
- The task rule: no boot-image patch or length reduction without proof.

### Concrete future candidate plan (not executed)

1. Source fix, one line: `MacMini2020.fdf:63` →
   `0x00, 0x00, 0xe0, 0x01, 0x00, 0x00, 0x00, 0x00, # image_size: 30 MB
   (0x01E00000 == PcdFdSize)`. Optionally the same line in the other four
   platform FDFs for consistency; only MacMini matters here.
2. Rebuild the FD with the existing Mu recipe (`-j2`), then prove offline
   that the new FD differs from the S192 FD in exactly bytes `0x10..0x17`
   (`cmp -l`, expect two differing bytes at offsets 19 and 20, 1-based).
   Because GenFds copies `DATA = {}` verbatim, that is the expected result;
   if anything else differs, stop and explain it before continuing.
3. Assemble with `loader-s197/assemble_validate.py` after relaxing its pin at
   lines 147-149 to accept `image_size == 0x01E00000` and adding two checks:
   `image_size >= PcdFdSize (0x1E00000)` and `image_size >= len(fd)`.
   Keep the S197 prefix and DTB unchanged so the candidate differs from
   S197-head in 8 bytes only.
4. Hardware A/B (user-run, new launcher copy, size/sha asserts updated):
   S197-head vs S197-head+30MiB header. Success signal is boot-stage parity
   on UART, plus the m1n1 `Heap base:` line (`heapblock.c:27`) and the PrePi
   `FD Base Address` line (`PrePi.c:151`) showing the FD at the same place
   and the DT buffer 0xE2E0000 lower. A regression isolates to heap layout,
   which is then a finding about something else relying on the gap.
5. Only after step 4 passes should any launcher or the stable candidate be
   pointed at the new FD. No byte-patching of an already assembled payload
   is planned; if ever used as a cross-check, it must reproduce the step-2
   FD bit-for-bit.

## Files

| File | Purpose |
|---|---|
| `verify_vendor_tree.sh` | Read-only per-file tree verifier (content, mode, type, extras). |
| `test_verify_vendor_tree.sh` | 17 fixture cases in a temp dir; exit 0 iff all pass. |
| `README.md` | This report. |
| `../loader-s197/build.sh` | Now calls the verifier; everything else unchanged. |
| `../loader-s197/README.md` | Records the correction and the unchanged hashes. |
