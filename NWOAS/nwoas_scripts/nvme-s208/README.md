# S208 — NVMe EOI-PC diagnostic correctness fix

Bounded, offline, independent. Owns only this directory and the detached
worktree `/Volumes/X31/NWOAS/m1n1-diag-s208`. Initially no hardware, live source,
launchers, secrets, UI or USB were touched. `m1n1_windows`,
`m1n1_windows-s159`, `m1n1-guest-s197` and `m1n1-rebuild-s205` were read
(worktree base, fatfs store, stable reference) but never written. No generic
main build change; no rebase onto the S194 current artifact.

## What this fixes (and what it does not)

The S163 companion patch added an NVMe EOI diagnostic in `hv_exc_irq`:

    nwoas_nvme_note_eoi(intd, ctx->elr);

In the `ENABLE_VGIC_MODULE` build the direct IRQ path never calls
`hv_get_context()`. The assembly entry `_exc_entry` saves only `x0`-`x29`; the
`elr`/`spsr`/`esr` fields of `struct exc_info` are filled solely by
`hv_get_context()` in C, which only the sync/abort paths call. So `ctx->elr`
here is uninitialized stack, and the recorded `last_eoi_pc` is stale garbage.
The S163 README already recorded this and forbade interpreting `last_eoi_pc`.

The fix reads the architectural `ELR_EL2` directly at that one site:

    nwoas_nvme_note_eoi(intd, hv_get_elr());

with a reason comment. Nothing else changes. IRQ spacing, mask, queue,
generation and delivery semantics are all preserved; `hv_bridge_nvme_irq` and
`nwoas_nvme_note_eoi` are byte-identical to S163.

This corrects evidence accuracy / an uninitialized diagnostic read only. It is
**not** a watchdog fix and makes no Windows-stability claim.

## Artifacts

| File | Purpose |
|---|---|
| `minimal-from-s163.diff` | The whole S208 change vs S163: one call-site line + reason comment |
| `s208.patch` | Full candidate diff from `bddf7f06` (companion patch + the fix) |
| `hv_exc-s208.c`, `hv_vm-s208.c` | Verbatim snapshot of the candidate worktree source |
| `build.sh` | Pinned two-variant build (gap50, gap25) |
| `out/` | Both binaries + ELF/macho, logs, `sha256.txt`, `tools.txt` |
| `harness/` | Compiled call-site harness (`harness.c`, `gen.sh`, extracted fragments, run log) |
| `test_irq_gap_s208.py` | Existing S163 real bridge test, candidate source, gaps 0/25/50/200 |
| `manifest.json` | Full hashes, sizes, toolchain, validation, hardware status |

## Binaries (MAX_BLOCKS=256, clean build, `nice -19`, `make -j2`)

| Binary | Bytes | SHA-256 | REASSERT_US |
|---|---|---|---|
| `m1n1-s208-eoipc-gap50.bin` | 2162688 | `f801c5bec4e2972a0011b594ec8fb9201406dd7a82180631f4328f6d2779fa16` | 50 |
| `m1n1-s208-eoipc-gap25.bin` | 2162688 | `1f1291dd354ab202faaceccd05635b0d1cac7666f32bf808c49807db0bec5bde` | 25 |

Build tag `v1.0.2-1474-gbddf7f06-dirty` (same describe string as the stable
S163 build). 43 pre-existing warnings, 0 errors, make exit 0. S205 proved a
clean S163 gap50 build equals the tested stable artifact byte-for-byte, so the
S208 gap50 binary is exactly that stable image plus this one-line fix; the
differing bytes versus stable are the layout-shift cascade from inserting the
`hv_get_elr()` call.

## Validation

- **Compiled call-site harness** (`harness/`): extracts the real
  `nwoas_nvme_note_eoi` and the real maintenance-EOI loop verbatim from the
  candidate source and compiles them against fakes. With a poisoned context
  value and a distinct fake architectural PC, the fixed unit records the
  architectural PC and never the poison; the pre-fix contrast unit records the
  poison (reproducing the defect). An unrelated interrupt (`intd != 900`)
  leaves every NVMe counter and `eoi_last_pc`/`last_eoi` at zero while still
  EOIing, clearing its LR and unmasking its SPI. Plain and UBSan runs pass for
  both units. Main review corrected the mock virtual interrupt ID fields to
  the actual header values (mask 0xffff, shift 0), then reran both units with
  Apple Clang: plain, UBSan, and ASan+UBSan all passed. The prior Homebrew
  ASan startup timeout had no established cause; no sandbox explanation is
  asserted. The extraction now uses the retained candidate source snapshot.
- **Existing real bridge test** (`test_irq_gap_s208.py`): the S163 harness run
  against the candidate source for gaps 0, 25, 50 and 200 us. All pass:
  affinity, disabled line, capacity, dedup, exact deadline, live-level
  cancellation, eventual redelivery, 64-bit counter wrap. Not a hardware
  stability proof.

Source-extraction limit: `hv_exc_irq` cannot be compiled standalone (it pulls
in the whole vGIC/AIC/exception stack), so the changed line is exercised by the
extracted call-site harness rather than by compiling that function. The bridge
test complements it by proving the candidate still satisfies every
gap/mask/queue/delivery invariant.

## Reproduce

    nwoas_scripts/nvme-s208/build.sh              # both binaries
    nwoas_scripts/nvme-s208/harness/gen.sh        # call-site harness
    python3 nwoas_scripts/nvme-s208/test_irq_gap_s208.py

## Hardware status

**S214 gap50 control passed:** 8-core Windows, 256MiB full write/read integrity,
2,219 continuous reads over 300 seconds, and 31 valid mixed samples over
1,807.56 seconds. See `../nvme-s214/`. Actual EOI PC is now recorded.
Native Windows boot remains the goal; this is
an EL2 host-HV diagnostic-accuracy change with no runtime behavior change beyond
the recorded EOI PC.
