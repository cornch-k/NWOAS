# S167: bounded high-RAM trial (offline built, not deployed)

This candidate follows S161 normalization; first boot and validate S161 alone.
It keeps Conventional RAM whose extent is wholly inside
`[boot_args.phys_base, boot_args.phys_base + 4 GiB)`, plus the existing low DMA
window. It does not promise exactly 8 GiB Windows RAM: reserved/ACPI/loader
regions and later reclaim alter the available total. No physical backing alias
is duplicated. `NWOAS_HIDE_KEEP_HIGH_BYTES=0` retains the previous cap.

`prepare_wrapper.py` generates an isolated wrapper snapshot. `build_candidate.py`
temporarily applies S161, adds the split header, enables 4 GiB high keep, keeps
USB-C XHC1 hidden, builds with two low-priority workers, and restores original
UEFI source bytes. `manifest.json` identifies the built payload.

The split helper preserves full descriptor size, including extension bytes,
and inserts an upper-tail descriptor. It never truncates a descriptor to fit a
buffer. A short buffer returns EFI_BUFFER_TOO_SMALL with required length; the
caller retries. It changes only the caller snapshot, performs no allocation,
and preserves the original live MapKey. The original 2 GiB minimum low
Conventional-pool guard remains in effect. Only Conventional type is retained;
runtime and ACPI descriptors are untouched. A descriptor beginning before
KeepStart is intentionally not partially retained (conservative all-hidden).

Validation:
- Fable actual-header C tests: 365 checks, ASan/UBSan, zero failures.
- Main actual-wrapper extraction tests: default-off and 4 GiB on, probe/retry,
  split/metadata, MapKey unchanged, low-DMA guard untouched; ASan/UBSan pass.
- UEFI build passed with PE/COFF validation, source restoration checked.
- No hardware acceptance, high-buffer USB DMA proof or benchmark result yet.

Hardware acceptance must compare S102/early/final backing and dynamic wide-DART
range; confirm Windows memory and CPU count; then exercise checksum-correct
I/O, memory allocation and USB-A PnP/HSE. FL1100 AC64 is zero: retain its low
DCBAAP/CRCR/ERST requirement and observe unexpected LOWALIAS remaps. High Windows
client-buffer DMA bounce behavior is still an open question. If USB/boot
regresses, restore the S161 or S139 cap using preserved payloads.

The original S163 50us HV reports an invalid `last_eoi_pc` diagnostic because
its IRQ path does not populate ctx->elr. Ignore that field; the timestamp and
IRQ-spacing logic are independent of it.

### Integration review adjudication

The separate CLI integration review found no code blocker. Correct its prose:
our wrapper harness uses 48-byte descriptors (40-byte base + 8 extension), and
these snapshots are uncommitted. Total Windows memory can exceed the simple
8GiB nominal sum because BootServices/Loader regions outside the kept interval
also remain reclaimable in conservative mode. Measure actual availability.
The old `first_hidden_PA` field still names the first eligible descriptor before
splitting; use the new S167 kept/hidden-page fields for the trial.

CLI was requested as `claude-fable-5-1`, but this review's result modelUsage lists
`claude-opus-5` and `claude-opus-4-8` (plus auxiliary Haiku), not Fable. Preserve
that provenance; do not label it a confirmed Fable execution. Earlier split-test
review did report Fable5.1 usage. No additional CLI privilege was requested.

## First hardware control, 2026-09-10 ~03:05 KST

Run `memory-s167-download-p12-20260910-025625.HdLLoE`: 8,639,746,048 Windows physical bytes, eight cores/logicals, ~6GiB initially free. CPU+SSD checksum and USB-A/root Code0 checks pass. S171 private4096MiB allocation written/read in full3passes, all words matched; elapsed6668ms. Does not identify physicalPFNs. Official783373346B Cinebench ZIP transferred and fullSHA verified on Windows; extraction in progress.

The legacy `DARTDIAG ... MAPPED-WRONG-TARGET` label is not valid evidence of a mapping defect for these rows: for example IOVA0xfffde000 has16KiB page0xfffdc000. Actual backing0xae0fcc000 + page =0xbe0fa8000, exactly the PTE address in the log; adding offset0x2000 yields0xbe0faa000, also matching LATEUSB ERST translatedPA. The label must be interpreted against current backing, not treated as a failed mapping on its own. The DART error shown has FLAG=0 and is explicitly stale.
