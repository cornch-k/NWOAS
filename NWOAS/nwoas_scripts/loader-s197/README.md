# S197 guest m1n1 prefix rebuild (offline, not boot tested)

Follow-up to the S193 provenance finding: the 1376256-byte prefix in front of
every UEFI payload since July is a guest m1n1 tagged
`v1.0.2-1471-g59fb544-dirty` whose exact source was never recorded. S197
rebuilds that prefix from tracked source and assembles a candidate payload with
the same DTB and the S192 UEFI FD. **Nothing was booted.** Every statement about
runtime behaviour below is from source and bytes; boot compatibility is
established only by the hardware main test.

## Deliverables

| File | Purpose |
|---|---|
| `build.sh [head\|compat]` | Pinned, offline, `nice -n 19`, `-j2` build in the S197 worktree. Refuses to run if the worktree is not at `bddf7f06`, is dirty, if the vendored `rust-fatfs` tree is not byte-identical to `4eccb50d` (per-file blob-hash guard, S199), or if the toolchain versions differ. |
| `assemble_validate.py [head\|compat]` | Assembles `m1n1.bin + DTB + FD`, runs 26 layout/provenance checks from the ELF symbols and the input bytes, writes the candidate only if all pass, and writes `manifest.json`. |
| `guest-compat-s197.patch` | `git diff bddf7f06 59fb544 -- src/main.c src/i2c.c src/usb.c` (82 lines). Used only by the `compat` variant, applied during the build and reverted afterwards. |
| `out/head/` | Primary candidate: tracked source exactly as committed at `bddf7f06`. |
| `out/compat/` | Secondary candidate: same source with the three post-July guest-visible hunks reverted (section 4). |

Each `out/<variant>/` contains `m1n1.bin`, `m1n1-raw.elf`, `m1n1.elf`,
`m1n1.macho`, `build_tag.h`, `build_cfg.h`, `build.log`, `tools.txt`,
`sha256.txt`, `manifest.json` and the assembled payload.

| Candidate | Size | SHA-256 |
|---|---|---|
| `out/head/m1n1-payload-s197-head.bin` | 33177600 | `ebaf8b90fb80913c465d916be4597d15940a3339a5edb97a8b9e7d545bf0e831` |
| `out/compat/m1n1-payload-s197-compat.bin` | 33177600 | `9a0b96af3e09cbd4450590ac4ff59e5e2aa4949bd70723ad9b8240996635020a` |
| `out/head/m1n1.bin` (guest m1n1, tag `v1.0.2-1474-gbddf7f06`) | 2146304 | `b2515b77d5e3cc5d5031823ce88bd0b25fdf1f4c41a9183ce92ef12c29a0b007` |
| `out/compat/m1n1.bin` (tag `v1.0.2-1474-gbddf7f06-s197compat`) | 2146304 | `1c255a7f325a125040ed7aa5fcbc8bfda8b82c84d727698d4bf14e97ab162078` |

The `head` build was run twice from an empty build directory; all four
artifacts (`m1n1.bin`, `m1n1-raw.elf`, `m1n1.macho`, `m1n1.elf`) were
byte-identical between runs.

## 1. Source and tool pinning

- Worktree: `/Volumes/X31/NWOAS/m1n1-guest-s197`, detached at
  `bddf7f06f033a7411834ac61381d8c997034f532` (m1n1_windows HEAD, branch
  `nwoas-stage8-instrumentation`, 2026-09-09 22:01 KST, "Add target NVMe fast
  path and restore USB-C root hub"). `git status --untracked-files=no` is empty.
- Submodules (gitlinks at `bddf7f06`): `rust/vendor/rust-fatfs`
  `4eccb50d011146fbed20e133d33b22f3c27292e7` (populated with
  `git archive` from the main checkout's submodule store, no network);
  `artwork` `80d14f8b6f485b310e305a84b4b806361518ddd1` (not populated, not
  needed: `data/bootlogo_*.bin` and `font/*.bin` are tracked and `LOGO` is
  unset).
- Vendored-tree guard (S199 correction): the first version of `build.sh`
  claimed the vendored `rust-fatfs` tree was byte-identical to `4eccb50d` but
  only diffed the *file list* against `git ls-tree --name-only`, so an edited
  file with the same name passed. `build.sh` now calls
  `nwoas_scripts/loader-s199/verify_vendor_tree.sh`, which hashes every file
  on disk with `git hash-object --no-filters` (read-only, no `-w`) and compares
  content, executable bit, symlink/regular type, and the absence of extra or
  missing paths against the tree object `0a4c434a` of commit `4eccb50d` in
  `m1n1_windows/.git/modules/rust/vendor/rust-fatfs`. Result on the S197
  worktree: 33 entries, 33 files, 0 symlinks, no extras, OK. Both variants
  were rebuilt through the corrected script on 2026-09-10; all `sha256.txt`
  values and both payload hashes in the table above are unchanged.
- Deliberately excluded: the three uncommitted files in `m1n1_windows`
  (`proxyclient/tools/run_guest.py`, `src/hv_vm.c`, `src/nwoas_stage8.inc`,
  63 lines, host-side NVMe latency diagnostics and `--strict-init`). The
  guest never executes the hypervisor code they touch.
- Tools: Homebrew clang 20.1.8 (`/opt/homebrew/opt/llvm/bin/`), LLD 20.1.8
  (`/opt/homebrew/opt/lld/bin/`), llvm-objcopy 20.1.8, GNU Make 4.4.1,
  rustc/cargo 1.88.0 selected with `RUSTUP_TOOLCHAIN=1.88.0` (this host has no
  default toolchain), target `aarch64-unknown-none-softfloat`,
  `CARGO_NET_OFFLINE=true` (uuid 1.17.0 from the registry cache, bitflags
  2.9.3, log 0.4.27). Makefile flags: `USE_CLANG=1`, `RELEASE`/`CHAINLOADING`/
  `BUILDSTD`/`LOGO` unset, so `build_cfg.h` is empty and the compile flags are
  the tracked `BASE_CFLAGS` (`-O2 -march=armv8.2-a -fpic -ffreestanding`,
  `-mgeneral-regs-only` for non-FP objects) with the tracked `LDFLAGS`.
- No global installs were made. One accidental side effect during the
  survey: `rustup +1.88 ...` created a duplicate `1.88-aarch64-apple-darwin`
  toolchain; it was uninstalled immediately and `rustup toolchain list` again
  shows only `1.88.0-aarch64-apple-darwin`.
- Warnings in `build.log`: 58, all pre-existing in tracked source (C23 label
  declarations, unused `irq_num`, `-Wlogical-op-parentheses`, format types).
  No errors.

## 2. What the original recipe was, and what changed

Original (`NWOAS-진행보고-2026-07-06.md` §9.3):

```
cat build/m1n1.bin apple-j274-padded.dtb J274MACMINI2020_EFI.fd > m1n1-payload.bin
```

That is exactly what S197 does, with the pieces replaced by known ones:

| Piece | Old (S192 payload) | S197 |
|---|---|---|
| Guest m1n1 | bytes 0..0x140000, `v1.0.2-1471-g59fb544-dirty`, sha256 `adefb605…bb0154`, source diff lost | `out/<variant>/m1n1.bin`, 0x20c000 bytes, from `bddf7f06` |
| DTB | bytes 0x140000..0x150000 | same file `m1n1_windows/apple-j274-padded.dtb`, sha256 `ecc93b24…190f76`, byte-identical to the S192 region |
| UEFI FD | bytes 0x150000.. (30965760 B) | the same bytes, sliced from the S192 payload (`b67313c4…4d4c7c9`) at offset 1376256; sha256 `ae917fef…c3c382b`, also byte-identical to `apple_silicon_platforms_mu/Build/MacMini2020-AARCH64/DEBUG_CLANGPDB/FV/J274MACMINI2020_EFI.fd` at the time of writing |

The old m1n1 region ends with the `.stack` canary `STACKBOT` at 0x13fff8, so the
old prefix was a raw `m1n1.bin` (linker script `m1n1-raw.ld`) with
`_payload_start = 0x140000`. The new image is 0x20c000 bytes because the
tracked tree gained ~13k lines of hypervisor code since June; the guest does
not run that code but it is linked in (same `OBJECTS` list, no config knob to
drop it).

New layout (from `llvm-nm out/head/m1n1-raw.elf`, not assumed):

| Offset | Size | Content |
|---|---|---|
| 0x000000 | 0x20c000 | guest m1n1, `_start` at +0x800, `_payload_start = _end = _stack_bot = 0x20c000` |
| 0x20c000 | 0x10000 | DTB (`apple,j274`, `apple,t8103`, `apple,arm-platform`), totalsize 0x10000 |
| 0x21c000 | 0x1d88000 | FD: 0x8000 Linux `Image` header stub (`adr x1,.` / `b 0x8000`, text_offset 0x80000, image_size 0x100e0000, `ARM\x64` at +0x38) + FVMAIN_COMPACT (`_FVH` at +0x8028, FvLength 0x1d80000) |

Total 33177600 bytes (0x1fa4000, 16 KiB aligned for `hv.load_raw`). **The old
offsets 0x140000/0x150000 and the old size 32342016 are no longer valid**; any
launcher or builder that does `old[:1376256] + fd` or asserts 32342016 must not
be pointed at this candidate without being changed.

## 3. Handoff behaviour preserved (source inspection)

The guest boot path is unchanged between `59fb544` and `bddf7f06` in the code
that matters for the handoff (`git diff 59fb544..HEAD` on `src/payload.c`,
`src/cpufreq.c`, `src/start.S`, `m1n1-raw.ld`, `src/heapblock.c` is empty;
`src/kboot.c` differs only inside `kboot_prepare_adt`, see below):

1. `load_one_payload(_payload_start)` sees `d00dfeed`, `load_fdt` accepts it
   (`apple,j274` matches ADT `target-type`), advances 0x10000.
2. At `_payload_start + 0x10000` the FD bytes `01 00 00 10` match no earlier
   magic and `+0x38 == "ARM\x64"`, so `load_kernel` runs; with size 0 it
   returns NULL and the loop ends.
3. `kernel && fdt` and no `chainload=` var: `cpufreq_init()`,
   `smp_start_secondaries()`, `mitigations_perform()`,
   `kboot_prepare_dt(fdt)` (copies the DTB into a 64 KiB + 96 KiB heap buffer,
   fills chosen/cpus/memory/… and calls `kboot_prepare_adt()`), then
   `kboot_boot(kernel)`: `mcc_enable_cache`, `tunables_apply_static`,
   `clk_init`, `usb_init`, `pcie_init`, `dapf_init_all`, WFE mode, and
   `next_stage.entry = kernel`, `x0 = dt`, `x4 = &cur_boot_args`.
4. `m1n1_main` shuts down NVMe/exceptions/USB iodev/display/fb/MMU and vectors
   to the FD stub with x0 = FDT, as `MacMini2020.fdf` expects.

`load_kernel` copies the kernel to a 2 MiB-aligned heap block when the FD is
not 2 MiB aligned in guest physical memory. With the S189 guest base
(`0x83e7dc000`) the old FD sat at `0x83e92c000` and the new one sits at
`0x83e9f8000`; neither is 2 MiB aligned, so both take the same copy branch. The
copy length is the header's `image_size` (0x100e0000, 269 MiB, the FDF comment
says "30 MB" but encodes 0x100e0000), which over-reads past the 30 MiB FD into
SEPFW/preoslog/boot-args and unmapped-but-RAM guest memory, exactly as before.
`guest_base` is recomputed by `hv.load_raw` on every boot from `heap_top`, the
ADT size and the TrustCache size, so alignment is a runtime property, not a
build property.

`kboot_prepare_adt()` at `bddf7f06` copies the ADT into the EL2 buffer and
returns before the per-CPU carveout loop (NWOAS fix in `59394e6c`). At clean
`59fb544` the buffer was uninitialised and the loop ran, which the commit
message says data-aborted. The old prefix was built from a dirty `59fb544`
tree and its strings prove it already carried at least the `smp.c` spurious-
target guard from the same September commit (`spurious target` appears in the
old bytes), so the July dirty tree was ahead of `59fb544`; the tracked HEAD is
the closest committed approximation of it, not a bit-exact match.

## 4. Guest-visible differences between `bddf7f06` and the old prefix

String tests on the old 0x140000 bytes (`NWOAS-DCP-DEFER`, `i2c: rev=`,
`HPM %s port` all absent; `spurious target` present) show the old prefix
predates three hunks that HEAD carries and that run on the guest path:

| File | HEAD behaviour in the guest | Old prefix behaviour |
|---|---|---|
| `src/main.c` | `display_init()` is skipped when `chip_id == 0x8103` and `os_firmware.iboot == "mBoot-18000.121.3"` (the Tahoe iBoot under test; the guest reads the same ADT), printing `NWOAS-DCP-DEFER`. | `display_init()` ran in the guest (July report: display.c changes take effect in the guest payload). |
| `src/usb.c` | `usb_init_i2c` indexes HPM nodes with `name[3] - '0'`, so `kboot_boot -> usb_init` now powers up and masks IRQs on the TPS6598x PD controllers `hpm0`/`hpm1` over i2c0. | `name[3] - 30` gave 18/19 ≥ `USB_IODEV_COUNT` (8), so both HPM nodes were skipped and no PD-controller i2c traffic happened. |
| `src/i2c.c` | `i2c_init` reads the controller revision and sets CTL.EN for rev ≥ 6, and prints `i2c: rev=…`. | No enable write. |

`src/display.c` (`video.depth` 30 → 32) is also in HEAD; the July reports
("pre-8bpc" payloads, "STDFMT" bare images) indicate the dirty tree already
had it, so no revert is offered for it. `src/smp.c` (spin-table guard) is
present in both.

Because the task asks to preserve the handoff behaviour, two candidates are
provided: `head` (pure tracked source, cleanest provenance) and `compat`
(`head` + `guest-compat-s197.patch`, which reverts exactly the three hunks
above to their `59fb544` form; 82-line patch, sha256 in `out/compat/tools.txt`).
Which one matches the old prefix's hardware behaviour is unknown until tested;
the string evidence favours `compat` for fidelity, the provenance argument
favours `head`. Both build tags are distinct so a UART capture can tell them
apart.

No dependency on the S163 hypervisor patches was found: the guest path
(`m1n1_main -> run_actions -> payload_run`) never enters `hv_*`, and
`nwoas_stage8.inc` is included only from `hv_exc.c`.

## 5. Validation performed (all offline)

`assemble_validate.py` results (26/26 PASS for both variants), summarised:

- m1n1: `len(m1n1.bin) == _payload_start == _end == _stack_bot == 0x20c000`,
  16 KiB aligned; `_start == 0x800` (matches `run_guest.py -r` default
  `entryoffset=0x800`); trailing `STACKBOT` canary; vector 0 opcode first;
  build tag string embedded; all eleven kernel+fdt path strings present.
- DTB: sha256 pinned; magic; totalsize == 0x10000 == file size; root
  compatible contains `apple,j274`; identical to the S192 region.
- FD: S192 payload size and sha256 pinned before slicing; slice sha256 pinned;
  size equals the FDF region sum 0x8000 + 0x1d80000; header opcodes,
  text_offset, image_size, magic; `_FVH` and FvLength; no earlier payload
  magic at offset 0.
- Assembly: DTB magic at `_payload_start`, `ARM\x64` at
  `_payload_start + 0x10000 + 0x38`, total 16 KiB aligned.

Not validated (unknowns):

- Boot. No hardware, USB, or launcher was touched.
- Whether the guest's `cpufreq_init()`, `usb_init` HPM transactions, or the
  display deferral behave acceptably under the host HV: hardware only.
- The larger prefix moves the FD by 0xcc000 bytes; the 269 MiB `load_kernel`
  over-read window moves with it. Same code path, different addresses.
- `kboot_prepare_dt` allocates from the guest heap, which starts at
  `cur_boot_args.top_of_kernel_data` set by `hv.load_raw`; the larger image
  shifts that by 0xcc000 as well.
- The FD is taken from the S192 payload, not rebuilt; S192 itself is still
  hardware-pending per the 2026-09-10 status.

## 6. How to reproduce

```
nwoas_scripts/loader-s197/build.sh head      # or: compat
python3 nwoas_scripts/loader-s197/assemble_validate.py head   # or: compat
```

Both steps are idempotent and leave the worktree clean. A hardware trial needs
a new launcher (copy of `firmware-s192-native-guest-test.sh`) that points
`PAYLOAD` at one candidate and replaces the size assert 32342016 with 33177600
and the sha256 with the value in the table above; nothing else in the launcher
depends on the prefix length. Do not promote either candidate in place.
