# S220 documentation audit — publish-s217 companion and evidence set

> Historical review snapshot. Main subsequently completed S215/S216 short
> hardware tests and S221 DTB reconstruction; see `claude-s220-resolution.md`
> and the individual result files for the current qualification state.

Date: 2026-09-10. Scope: read-only review of the files listed below. No builds,
hardware, network, or edits to existing files. Raw logs, .env, credentials, ADT
and NVRAM were not inspected.

Reviewed: `publish-s217/companion/README.md`, `publish-s217/companion/manifest.json`,
`publish-s217/audit-output.txt`, `loader-s215/manifest.json` + `README.md` +
`reproduce-result.json` + `out/tools.txt`, `firmware-s216/manifest.json` + `README.md` +
`reproduce-result.json` + `review-verification.json` + `components-compat.json`,
`native-s209/README.md` + `manifest.json` + `trace-result.json`,
`bench-s196/result.json` + `cinebench-evidence.txt`, `bench-s218/wrapper-manifest.json` +
`run-s218.ps1` + `CBRUN-V4.CMD`, `nvme-s214/soak-result.json` + `active-result.json` +
`README.md` + `integration-evidence.txt`.

## Status summary

| Stage | Claimed status | Evidence on disk | Assessment |
|---|---|---|---|
| S214 nvme control soak | completed, PASS | 31 valid samples over 1807 s, exit 0 | Supported as a control run on the S192 payload |
| S215 guest prefix | hardware PENDING | byte-identical rebuild, range tests | Build reproducible; boot unproven |
| S216 candidate payload | hardware pending | byte-identical rebuild, source review | Build reproducible; boot unproven; `hardware-result.json` absent as stated |
| S196 Cinebench | PASS, 1905.281 | one sample, stdout hash | Single sample on S192 + S163 gap50; not parity evidence |
| S218 bench wrapper | prepared, not installed | hash lineage verified | No result yet |
| S209 C control model | source-only, not integrated | host differential + trace replay | No runtime claim; correctly labelled |

Hash consistency checks that passed during this audit: S215 prefix
`d91700cc…` matches `loader-s215/out/m1n1.bin`, `loader-s215/manifest.json`,
`firmware-s216/components-compat.json` and `companion/manifest.json`. S216 payload
`0ae1cb75…` matches `firmware-s216/m1n1-payload-s216-compat.bin`,
`firmware-s216/manifest.json`, `review-verification.json` and `companion/manifest.json`.
`bench-s196/CBRUN-V4.CMD` hashes to the `previous_wrapper_sha256` in
`bench-s218/wrapper-manifest.json`, and `bench-s218/CBRUN-V4.CMD` hashes to
`wrapper_sha256`; the only diff is the `cb-s196` to `cb-s218` output name.

## Missing reproducibility inputs (companion)

1. **Toolchain versions are not in the manifest.** `companion/README.md` says the
   S215 recipe validates toolchain versions, but `companion/manifest.json` records
   none. The pins exist only in `loader-s215/out/tools.txt` (clang/LLD 20.1.8,
   rustc 1.88.0) and in prose in `loader-s215/README.md`. The Project Mu build
   toolchain (compiler, edk2 BaseTools/stuart versions, Python) is recorded nowhere
   in the reviewed set, including `firmware-s216/README.md`.
2. **The S215 build script is not shipped.** `loader-s215/README.md` and
   `companion/README.md` describe `build.sh`, but the companion contains only
   patches. A third party cannot run "the S215 recipe" from the companion alone.
3. **DTB input is unpinned in the companion.** `firmware-s216/components-compat.json`
   pins a padded J274 DTB (`ecc93b24…`) and the intermediate FD (`b008dcfc…`), but
   `companion/manifest.json` carries neither hash nor the DTB's provenance or padding
   rule. The assembled payload hash is therefore not reconstructible from the companion.
4. **`NwoasGuestRam.h` origin is outside the companion.** `audit-output.txt` line 36
   compares it against `memory-s161/Include/Library/NwoasGuestRam.h`. It is listed in
   the `apple_silicon_platforms_mu` patch file list, so it may be delivered by the
   patch, but the companion does not say so and the S161 dependency is undocumented.
5. **Base commits are validated against private remote-tracking branches.**
   `audit-output.txt` lines 2 and 4 reach `bddf7f06` and `35034bea` via
   `fork/nwoas-stage8-instrumentation` and `fork/nwoas-native-windows`. No fork URL is
   recorded in `companion/manifest.json`; only the MU_BASECORE/ARM_TIANO bases resolve
   to public `release/202502` branches.
6. **Host-side runtime inputs are absent.** The companion states it "requires host EL2
   orchestration/runtime services" but does not pin the gap50 HV binary hash (it lives
   in `nvme-s163/manifest-gap50.json`), the `run_guest.py` invocation used for S214,
   or the launcher environment `WINDOW_AFTER_RAM=1` that
   `firmware-s216/review-verification.json` notes as a tested precondition.
7. **`hv_build_flags` is ambiguous.** The value ends "(or25 separate candidate)";
   the README calls it "Gap25". State explicitly which flag value produces the Gap25
   binary and which manifest pins its hash.

## Performance claims

- **S196 is one sample and is not a macOS parity result.** `bench-s196/result.json`
  already lists this in `limitations`. Note additionally that it was measured with
  `hv: S163 gap50` and `payload: S192 native RTC/P12`, so it says nothing about the
  S215/S216 candidates. The 15.66 % change against S180 is a two-sample delta with
  a changed wrapper and firmware, as the file itself states. Any public summary
  should quote the score with "single run, host-EL2-mediated, no macOS baseline".
- **`CPU Speed (MHz) : 30.000`** in `cinebench-evidence.txt` is wrong metadata, not
  a measured clock. Keep that caveat next to the score wherever it is quoted.
- **S218 has no result.** `bench-s218/wrapper-manifest.json` is "prepared, not
  installed". Do not cite S218 until a `result.json` exists.
- **S214 is a bounded control run, not throughput.** `soak-result.json` and
  `active-result.json` both say 64 MiB logical reads on a compressed filesystem.
  `active-result.json` shows a `disk_us` max of 993139 vs median 125372; the soak
  file has no such outlier. The outlier is unexplained in the reviewed files.
- **`nvme-s214/README.md` is stale.** It says the thirty-minute soak "is still running;
  do not infer completion", but `soak-result.json` (mtime 07:29) records a completed
  31-sample run with `completed_exit0: true`. Update the README when main records
  final status.
- **`soak-result.json` does not pin its configuration.** Unlike `active-result.json`,
  it has no `hv`, `payload` or binary-hash fields; provenance rests on the session
  name alone.

## Native-driver claims

- **S209 is correctly labelled as unintegrated.** `native-s209/README.md` and
  `manifest.json` say source-only, not installed, `runtime_host_dependencies_removed: 0`.
  No public text should describe it as a Windows driver or as removing host
  mediation.
- **Manifest test list has a duplicate.** `native-s209/manifest.json` `tests` lines 7
  and 8 are identical. The README claims separate plain and ASan+UBSan harness
  passes; the manifest should list them as two distinct entries or the sanitizer
  claim is not reflected in the manifest.
- **Trace replay is partial.** `trace-result.json` covers 267 reads / 48 control
  writes and explicitly omits admin command bodies, completions and the fastpath.
  Do not describe it as a controller replay.
- **Neither S215 nor S216 introduces native USB-C, DCP, or storage drivers.**
  `companion/README.md` and `firmware-s216/README.md` say so; keep that language.

## S215/S216 pending vs S214 completed

S215 and S216 have build-level evidence only: byte-identical repeat builds
(`loader-s215/reproduce-result.json`, `firmware-s216/reproduce-result.json`),
host ASan/UBSan range tests, and a source review with no blocking issue
(`firmware-s216/review-verification.json`, referencing `loader-s213/review-s216.md`).
`firmware-s216/hardware-result.json` does not exist. Both must stay "candidate"
until main records hardware results. S214 is the only completed hardware run in
this set and it exercised the S192 payload with the S208 HV, not the candidates.

## Recommended additions before publication (no code changes)

1. Add `toolchain`, `dtb_sha256`, `fd_sha256`, and fork URLs to
   `companion/manifest.json`; ship or reference `loader-s215/build.sh`.
2. Add `hv_binary_sha256` and the launcher environment to the companion or a
   sibling `runtime-manifest.json`.
3. Refresh `nvme-s214/README.md` and add `hv`/`payload` fields to `soak-result.json`.
4. De-duplicate the `tests` array in `native-s209/manifest.json`.
5. Publish S196 only with its `limitations` block attached verbatim.
