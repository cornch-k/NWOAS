# S205: independent clean rebuild of the S163 gap50 host HV

Offline reproducibility check only. Nothing was booted; no hardware, USB,
launcher, Mini, secrets or UI was touched. The worktrees `m1n1_windows`,
`m1n1_windows-s159` and `m1n1-guest-s197` were read but not written. The
stable artifact was compared read-only and not replaced. Native Windows boot
remains the goal; this rebuild is of the EL2 host hypervisor image used for
guest runs and says nothing new about standalone native drivers.

## Result

**IDENTICAL.** A clean build from an empty build directory, in a new detached
worktree, with the recorded base commit plus the exact companion patch and the
recorded flags, reproduces the tested stable artifact byte for byte. Two
consecutive runs gave the same four output hashes.

| Artifact | Size | SHA-256 |
|---|---|---|
| `m1n1_windows-s159/build/m1n1-s163-eoi-gap50.bin` (stable, tested) | 2162688 | `bd8f16f286c8d1141df4a17d9b85eafdd4c2d39d5680c6ed75b63429f061d166` |
| `out/m1n1.bin` (this rebuild, run 1 and run 2) | 2162688 | `bd8f16f286c8d1141df4a17d9b85eafdd4c2d39d5680c6ed75b63429f061d166` |
| `out/m1n1-raw.elf` | 4347816 | `a16d4c354b75ac8d52ff30259ac113fe4e012cb413816667fbccde397ba7862b` |
| `out/m1n1.macho` | 884736 | `223798fca37043b7e2e34a6e2cf8ce25f51998edff953d1bb91480418c0c93a0` |
| `out/m1n1.elf` | 3070072 | `afbf70cde8596a2c7aedf4ea2681427f7eaa7ffad9e054255c5140a6a245996b` |

Build tag embedded in both binaries: `v1.0.2-1474-gbddf7f06-dirty`. No
section, symbol, tag or flag diagnosis was needed because no byte differs.

## Recipe (`build.sh`)

| Item | Value |
|---|---|
| Worktree | `/Volumes/X31/NWOAS/m1n1-rebuild-s205`, detached at `bddf7f06f033a7411834ac61381d8c997034f532` (created with `git worktree add --detach` from `m1n1_windows`) |
| Companion patch | `nwoas_scripts/publish-s198/companion/m1n1_windows.patch`, sha256 `a3f6c5741f1d96db30ee88d3e2148763214d582ddcb50b67c080834e70b65851`, 345 lines, files `proxyclient/tools/run_guest.py`, `src/hv_exc.c`, `src/hv_vm.c`, `src/nwoas_stage8.inc`. Applied with `git apply` for the build only and reverted on exit; the worktree is clean afterwards. |
| Build tag | Not overridden (`M1N1_VERSION_TAG` unset). `version.sh` runs `git describe --tags --always --dirty` on the patched worktree and yields `v1.0.2-1474-gbddf7f06-dirty`, the same string the stable build produced from the dirty s159 worktree. |
| Flags | `make TOOLCHAIN=/opt/homebrew/opt/llvm/bin/ LLDDIR=/opt/homebrew/opt/lld/bin/ USE_CLANG=1 EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50' -j2` under `nice -n 19`. `RELEASE`, `CHAINLOADING`, `BUILDSTD`, `LOGO` unset, so `build_cfg.h` is empty. |
| Toolchain | Homebrew clang 20.1.8, LLD 20.1.8, llvm-objcopy 20.1.8 (explicit paths, no brew lookup); rustc/cargo 1.88.0 via `RUSTUP_TOOLCHAIN=1.88.0`, target `aarch64-unknown-none-softfloat`; `CARGO_NET_OFFLINE=true`, `CARGO_BUILD_JOBS=2`; GNU Make 4.4.1. Crates from the local registry cache: uuid 1.17.0, bitflags 2.9.3, log 0.4.27. |
| rust-fatfs | Gitlink `4eccb50d011146fbed20e133d33b22f3c27292e7`, populated by `git archive` from `m1n1_windows/.git/modules/rust/vendor/rust-fatfs` (no network, nothing written to any repository, tree modes preserved). Verified by `nwoas_scripts/loader-s199/verify_vendor_tree.sh`: 33 entries, 33 files, 0 symlinks, no extras, tree `0a4c434a`. |
| artwork | Gitlink `80d14f8b` not populated; not needed, `data/*.bin` and `font/*.bin` are tracked and `LOGO` is unset. |
| Build dir | `rm -rf build` before every run. |

Run: `nwoas_scripts/rebuild-s205/build.sh`. It refuses to run if the worktree
is not at `bddf7f06`, has tracked modifications, the patch or vendored tree
hash differs, or a tool version differs. Output goes to `out/`
(`build.log`, `tools.txt`, `sha256.txt`, `compare.txt`, `manifest.json` and
the four artifacts). Build log: 43 warnings, all pre-existing in tracked
source (C23 label declarations, unused `irq_num`, `-Wlogical-op-parentheses`,
`-Wformat`, `-Wreturn-type`); make exit 0; the only `error` string in the log
is the file name `fdt_strerror.o`.

## Provenance observations (read-only, informational)

- The stable artifact was **not** produced by a clean build. Per
  `nwoas_scripts/nvme-s163/build-gap50.log` and `manifest-gap50.json`, it was
  an incremental relink that recompiled only `hv_exc.o` with
  `NWOAS_NVME_REASSERT_US=50` on top of the gap200 build, which itself had
  recompiled only `hv_vm.o` and `hv_exc.o`. The other objects date from the
  s159 base build (2026-09-09 23:33 KST), except `nvme.o` (23:47 KST, the S159
  256-block rebuild). This rebuild shows those objects are the same as a
  clean compile of `bddf7f06` + patch with the gap50 flags, so the
  incremental history did not leak any stale or differently-flagged object
  into the tested binary. In particular `nvme.o` really carries
  `NWOAS_NVME_MAX_BLOCKS=256`.
- The s159 worktree's tracked diff (`src/hv_exc.c`, `src/hv_vm.c`,
  `src/nwoas_stage8.inc`) is byte-identical to the corresponding hunks of the
  companion patch. The patch's fourth file, `run_guest.py`, is host tooling
  and not a build input; s159 does not carry that hunk, `m1n1_windows` does.
- No absolute path from any worktree is embedded in `m1n1.bin` (`strings`
  finds no `/Volumes/X31`), which is why a build from a differently named
  worktree can match.
- The s159 build directory has been relinked since the stable artifact was
  written (its `m1n1.bin` and `hv_*.o` are from 05:21 KST today); that
  belongs to main and was not touched or used here.

## Not done

- No boot test. Whether a rebuilt candidate needs one is main's call; since
  it is byte-identical there is nothing new to test.
- The publish-s198 `apple_silicon_platforms_mu`, `MU_BASECORE` and
  `ARM_TIANO` components were not rebuilt; only the m1n1 host HV was in scope.
