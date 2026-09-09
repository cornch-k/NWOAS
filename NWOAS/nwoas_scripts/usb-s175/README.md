# S175: bounded DART1 SID1 translate helper (host-tested only)

HARDWARE SETUP TESTED on S187 same-payload comparison; DART1/SID1 TCR changed1100->1080, readbackOK. Both attached input devices are on USB-A, so USB-C command/data transfer and hotplug remain UNVERIFIED. Source repository restored after isolated builds.

## Artifacts

| File | Purpose |
|---|---|
| `s175_dart1_translate.h` | API, register constants, status enums, snapshot/result structs |
| `s175_dart1_translate.c` | helper, no libc, no heap, callback MMIO only |
| `test_s175.c` | fake-MMIO tests that execute the real helper |
| `build.sh` | freestanding compile check + ASan/UBSan test build and run |
| `build/` | objects, test binary, `test_s175.log`, `manifest.sha256` (generated) |

Build and run: `bash build.sh`. Flags: `-std=c11 -Wall -Wextra -Wpedantic
-Wshadow -Wconversion -Wsign-conversion -Werror`, tests with
`-fsanitize=address,undefined -fno-sanitize-recover=all`. Result on host:
`PASSED: 0 failure(s)`, warning-clean, no libc/heap symbols in the
freestanding object.

## What the helper does

`s175_dart1_sid1_translate(ops, src_ttbr[4], result)`:

1. Reads CAPLENGTH of the fixed non-debug xHCI 0x502280000; rejects 0/0xff.
2. Requires USBCMD.RS=0 and USBSTS.HCH=1, else `ERR_XHC_NOT_HALTED`. No writes.
3. Validates the 4 caller-supplied TTBR register values: TTBR0 must carry
   the VALID bit; TTBR1..3 must be 0 or VALID. Every VALID entry's 16 KB L1
   table (`(ttbr & 0x7fffffff) << 12`) must pass the caller's
   `table_in_range` predicate. Else `ERR_SOURCE_INVALID`. No writes.
4. Snapshots DART1 (0x502f80000) CONFIG, ENABLED_STREAMS, SID1 TCR,
   SID1 TTBR0..3, ERROR/ADDR, plus USBCMD/USBSTS. CONFIG bit 15 set gives
   `ERR_LOCKED`. No writes.
5. Writes, SID1 only: TTBR0..3, ENABLED_STREAMS bit 1 (only if clear),
   then TCR = `(baseline & ~BIT(8)) | BIT(7)`. Every other TCR bit,
   including DAPF bypass bit 12, is preserved. D78 wrote 0x80 outright and
   therefore also cleared bit 12; with the observed baseline 0x1100 this
   helper programs 0x1080.
6. STREAM_SELECT = BIT(1), STREAM_COMMAND = INVALIDATE, then polls BUSY
   for at most 100 us in 1 us steps. Busy at timeout gives `ERR_FLUSH_BUSY`.
7. Reads back TTBR0..3, TCR, ENABLED bit 1. Any mismatch gives
   `ERR_READBACK` with the register index and both values.
8. On step 6 or 7 failure: if the xHC is still halted, writes the saved
   SID1 registers back, flushes again with the same bound, and verifies.
   `restore` is `RESTORE_OK` only when readback matches and the flush
   finished. A busy flush is reported as `RESTORE_FLUSH_BUSY`, never as
   restored. If the xHC started meanwhile: `RESTORE_SKIPPED_XHC_RUNNING`.
9. `result.after` is captured whenever any write happened, so the caller
   always has the honest final register state.

Never touched: DART0 0x502f00000, the debug USB0 DARTs 0x382f00000 and
0x382f80000, SID0 of DART1, any xHCI register (read-only use). The tests
assert this from the access log.

## Tests (all in `test_s175.c`)

valid success, busy that clears within bound, busy forever, CONFIG locked,
four source-invalid variants plus a two-valid-TTBR success, xHC not halted
and CAPLENGTH invalid, readback mismatch with successful rollback, rollback
failure (TCR stuck) and xHC-started-before-rollback, DAPF/unrelated-bit
preservation across five baselines, other-SID/other-DART no-access, bad
arguments.

## Integration notes for main

- Callbacks map to m1n1 `read32`/`write32`, `sysop("dsb sy")`, `udelay`,
  and a predicate built on `nwoas_in_dram` or stricter.
- Source TTBRs are the FL1100 DART SID1 values at 0x682008000 + 0x210..0x21c,
  exactly what D78 copied, or an EL2-built table. Read them at the call site;
  the helper never reads that DART.
- Call site: the D80 gate (Windows kernel write of USBCMD with RS=1 while
  halted), or earlier. The helper checks halted/RS itself.
- Both D83 in-place rewrites (payload and Link TRB) and `NWOAS_DART_LOWALIAS`
  must be disabled in the same build; event seeding stays at 0. Main owns
  those edits.
- Log `result.before`/`after`, `status`, `restore`, `flush_wait_us`,
  `mismatch_*` verbatim.

## Interpretation limits

- This helper programs one variant: translate on DART1 SID1 only, DART0
  left as found. One failed variant cannot prove translate mode unviable
  on dwc3_1; it only removes that variant. D78 through D80 tested a
  different variant (both DARTs) and saw zero events without a latched
  fault; the cause was never isolated.
- The earlier review phrase "no rewrite changes yet" would confound a
  translate run: a TRB pointer rewritten to a backing PA above
  0xAE0FCC000 is an unmapped IOVA under translate. Both rewrites must be
  off from a fresh boot; there is no safe mid-run switch because rewrites
  persist in guest rings.
- Success criterion for a hardware run is the first command-completion
  event and ERDP advance with the cable present at boot, then EP7 events.
  Hotplug, CCS loss, and the later host-system-error are lifecycle matters
  outside this helper.

## Main integration and limited hardware evidence

`build_candidate.py` produced HV SHA6758060953a01faa65f435c3dcaaaced7c5868d554048a2c3fde794205d5cfbe,2162688B. S16350us+1MiBNVMe unchanged. EntireD83aliasfunction removed; LOWALIAS=0,eventseedalready0. BeforekernelRun risingedge, validate2lowIOVAs+currentCRCR/DCBAAP/ERSTagainstsourceDARTandstage2. HelperprogramsonlyDART1SID1; failedsetup suppressesRuninstead of starting untranslatedDMA withrewritesabsent. Eight boundedfirstpendingIRQsummarylinesadded. SourcefilesandMakefilerestored. Nativeintegrationtestexecutesactualmappingcheck:56casespassed. ExistingCbuildwarningsarelegacyunusedfunctions/variablesandoldformatstrings; helperwarningclean.

S174controlpayloadSHAa5c4007a97a89e15ee443ab85c897364a806be4b8b2dc1878f54acd8459dc53f is sharedwithcontrol. Preparedlauncher`../usb-s175-dart1-guest-test.sh`. No hardwaretrialyet; firstcommandcompletion/descriptortransferwillbe initialacceptance, notfullhotplug.

## 2026-09-10 05:10 hardware update

Run usb-s187-translate-20260910-050757.UwMq91: setup=OK, no rollback needed; Windows worker/inventory/CPU8threads/SSDsample passed. Present-device parent paths place trackpad05ac0324 and keyboard3434d030 under XHC0/USB-A; XHC1/USB-C contains only root hub. Empty USB-C event rings therefore do not diagnose DMA failure. Earlier S174 hardware-pending paragraphs above describe preparation history. No successful USB-C transfer or hotplug claim.
