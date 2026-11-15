# S232: FL1100 PCIe Device Control coherent policy (USB-A read-corruption experiment)

**Status: BUILT, HARDWARE-UNVERIFIED. A successful build is not a fix.** Nothing in this
directory has been booted on the Mac mini yet. Whether the USB-A zero-chunk corruption is
gone can only be decided by the hardware validation below.

## Problem (from S231)

Windows (S231 HV) reading a USB3 stick on the USB-A Fresco FL1100 xHCI (Apple PCIe + DART
stream 1) sometimes receives a whole read chunk as zeros: one region at file offset
0x7E300000-0x7E3EFFFF (960 KiB of 0x00, followed by 64 KiB of correct data) in a 3.5 GB file,
held in the Windows file cache. CopyFile / DISM are affected; sequential unbuffered reads
(robocopy /J, NWREAD) are fine. See `../layout-s231/USB-READ-CORRUPTION-20261115.md`.

Facts used: FL1100 PCIe Device Control (ECAM 0x690200000 + 0x78) reads 0x2810 in every
logged boot (`HVLOG: FL-DEVCTL 0x2810 nosnoop=1 relaxord=1`): No Snoop Enable (bit 11) = 1,
Relaxed Ordering Enable (bit 4) = 1. DSDT has `_CCA=0` for XHC0/SCB0. AC64 is masked so
Windows uses 32-bit DMA (bounce buffers for high RAM). September S113 (older HV) cleared No
Snoop alone (hardware log `S113 SNOOP before=2810 after=2010 verified=1`) and did not fix a
related buffered-read corruption; S113 left Relaxed Ordering enabled.

## Hypothesis (unverified)

With Relaxed Ordering enabled, the FL1100 may let a later posted write (event TRB /
completion) overtake earlier data writes, so Windows can consume a completion before the
chunk's data is globally visible; with No Snoop, DMA writes may bypass coherency. Either
could leave the zero-filled cache/bounce pages that Windows had before the transfer. S232
clears **both** bits for the whole Windows session, which S113 never tested. If corruption
persists with both bits verified clear, this mechanism is ruled out for the current symptom.

## What changed (and how guest config writes reach the HV)

FL1100 config space (ECAM 0x690200000, bus 2 dev 0 fn 0) is **not trapped**: the
proxyclient maps every `/arm-io` range as hardware passthrough (`TraceMode.OFF`, `hv/__init__.py`
init), and no module loaded by the S231 launcher adds a tracer there (`usb-s174/guest_map.py`
traps only the FL1100 BAR 0x6c0000000 and DART; `layout-s231/*module.py` trap only the
emulated NVMe ECAM at 0x700000000). So guest writes to DEVCTL are invisible to the HV.

Choice: instead of adding a new stage-2 trap on the ECAM page at runtime (would change the
stage-2 map while the guest runs and make every config access to fn 0-3 trap), the policy
**re-asserts the clear on the already-trapped FL1100 BAR write path**, i.e. before every
Windows-kernel write to USBCMD (BAR+0x80) and to the doorbell array (DBOFF..DBOFF+0x100),
before that write is forwarded to the card. The controller cannot start (RS=1) or pick up
newly queued TRBs without one of those writes, so DMA for new work always starts with RO/NS
clear. Remaining gap (documented, not closed): a guest DEVCTL write or device reset that
lands between two doorbells affects transfers already queued until the next doorbell.

The ECAM access pattern is the S113 one that ran on hardware: identity 0x11001b73 and PCIe
capability ID 0x10 at 0x70 checked first, 16-bit write to 0x78 (never writes the adjacent W1C
Device Status), `dsb sy`, readback, all under `GUARD_SKIP|GUARD_SILENT`.

Deviation from the requested "before guest start": the C HV has no pre-guest FL1100 hook
(pre-guest PCIe bring-up is `p.pcie_init()` in Python), and UEFI's own PCIe/USB init runs
after guest start and may reset the device to defaults, so the first clear is done at the
first Windows-kernel USBCMD/doorbell write (the "existing FL1100 setup point" used by S113).
UEFI-phase FL1100 DMA is not changed.

New define `NWOAS_FL_COHERENT_POLICY` (default 1 in this tree). `NWOAS_FL_SNOOP_TEST` stays 0.
No change to AC64 masking, DART, NVMe, USB-C, cache-sweep or any other path.

### Source diff (`diff -ru m1n1-layout-s231/src m1n1-usb-s232/src`, also `out/src-s231-vs-s232.diff`)

```diff
diff -ru m1n1-layout-s231/src/hv_vm.c m1n1-usb-s232/src/hv_vm.c
--- m1n1-layout-s231/src/hv_vm.c	2026-11-15 17:43:20
+++ m1n1-usb-s232/src/hv_vm.c	2026-11-15 19:35:38
@@ -114,6 +114,17 @@
 #define NWOAS_FL_SKIP_LEGACY_CLEAN 0
 #endif
 
+/* S232 USB-A read-corruption experiment (hypothesis, hardware-unverified):
+ * keep FL1100 PCIe Device Control Relaxed Ordering (bit 4) and No Snoop
+ * (bit 11) cleared while Windows drives the controller. The FL1100 ECAM
+ * window is a plain stage-2 passthrough (no trap), so guest config writes
+ * are not visible to the HV; the policy re-asserts the clear before every
+ * Windows-kernel FL1100 USBCMD write and doorbell write (before the write
+ * reaches the card), with readback verification and capped logging. */
+#ifndef NWOAS_FL_COHERENT_POLICY
+#define NWOAS_FL_COHERENT_POLICY 1
+#endif
+
 #define PAGE_SIZE       0x4000
 #define CACHE_LINE_SIZE 64
 #define CACHE_LINE_LOG2 6
@@ -3181,6 +3192,63 @@
 }
 #endif // NWOAS_COH_KEEP
 
+#if NWOAS_FL_COHERENT_POLICY
+/* S232: same guarded ECAM access pattern as the hardware-proven S113 test
+ * (identity 0x11001b73 + PCIe capability ID 0x10 at 0x70, 16-bit write to
+ * Device Control at 0x78 so the adjacent W1C Device Status is never written,
+ * readback after dsb). Clears Relaxed Ordering (bit 4) and No Snoop (bit 11).
+ * The first clear logs one INIT line; any later finding of either bit set
+ * again (guest config write or device reset restoring defaults; the HV cannot
+ * tell which) is cleared and logged as REWRITE (first 8, then every 256th). */
+static void nwoas_fl_coherent_policy(const char *site)
+{
+    static bool init_logged = false;
+    static u32 rewrites = 0;
+    static u32 skips = 0;
+    const u64 ecam = 0x690200000UL;
+    const u16 mask = 0x0810;
+    enum exc_guard_t saved = exc_guard;
+    exc_count = 0;
+    exc_guard = GUARD_SKIP | GUARD_SILENT;
+    u32 id = read32(ecam);
+    u16 cap = read16(ecam + 0x70);
+    if (exc_count || id != 0x11001b73u || (cap & 0xff) != 0x10) {
+        bool fault = exc_count != 0;
+        exc_guard = saved;
+        if (skips++ < 4)
+            printf("HVLOG: S232 COH SKIP site=%s id=%x cap=%x fault=%d\n", site, id, cap, fault);
+        return;
+    }
+    u16 before = read16(ecam + 0x78);
+    if (exc_count || !(before & mask)) {
+        bool fault = exc_count != 0;
+        exc_guard = saved;
+        if (!init_logged) {
+            init_logged = true;
+            printf("HVLOG: S232 COH INIT site=%s before=%x already-clear=%d fault=%d\n", site,
+                   before, !fault, fault);
+        }
+        return;
+    }
+    write16(ecam + 0x78, before & ~mask);
+    sysop("dsb sy");
+    u16 after = read16(ecam + 0x78);
+    bool fault = exc_count != 0;
+    exc_guard = saved;
+    bool verified = !fault && after == (u16)(before & ~mask);
+    if (!init_logged) {
+        init_logged = true;
+        printf("HVLOG: S232 COH INIT site=%s before=%x after=%x ro=%d ns=%d verified=%d fault=%d\n",
+               site, before, after, (after >> 4) & 1, (after >> 11) & 1, verified, fault);
+    } else {
+        rewrites++;
+        if (rewrites <= 8 || (rewrites & 0xff) == 0)
+            printf("HVLOG: S232 COH REWRITE n=%u site=%s before=%x after=%x verified=%d fault=%d\n",
+                   rewrites, site, before, after, verified, fault);
+    }
+}
+#endif
+
 /* ==== NWOAS §3.2 FIX (2026-07-13, session4) — hv GENERATES the Port Status Change Event ==========
  * ROOT CAUSE (5-agent analysis + FORK diagnostic, real HW): the wall is a poll-vs-event MECHANISM
  * mismatch, NOT coherence/addressing. UEFI XhciDxe POLLS PORTSC MMIO (XhcPollPortStatusChange) to
@@ -3568,7 +3636,14 @@
                         if (hv_pa_read(ctx, 0x6c0000014UL, &db, 2))
                             nwoas_fl_dboff = db & ~0x3UL;
                     }
+#if NWOAS_FL_COHERENT_POLICY
+                    if (woff == 0x80)
+                        nwoas_fl_coherent_policy("usbcmd");
+#endif
                     if (nwoas_fl_dboff && woff >= nwoas_fl_dboff && woff < nwoas_fl_dboff + 0x100) {
+#if NWOAS_FL_COHERENT_POLICY
+                        nwoas_fl_coherent_policy("db");
+#endif
 #if NWOAS_FL_SNOOP_TEST
                         nwoas_fl_snoop_test();
 #endif
```

## Build (identical to S231 except the version tag)

```
cd /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-usb-s232
env RUSTUP_TOOLCHAIN=1.88.0 CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=2 \
    M1N1_VERSION_TAG=v1.0.2-1474-gbddf7f06-s232U \
    RUSTFLAGS="--remap-path-prefix=$HOME/.rustup/toolchains/1.88.0-aarch64-apple-darwin/lib/rustlib/src/rust=/rustc/6b00bc3880198600130e1cf62b8f8a93494488cc" \
    bash -c "rm -rf build && nice -n 19 make TOOLCHAIN=/opt/homebrew/opt/llvm@20/bin/ LLDDIR=/opt/homebrew/opt/lld@20/bin/ USE_CLANG=1 EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50' -j4"
cp build/m1n1.bin ../nwoas_scripts/usb-s232/out/m1n1-s232-usbcoherent.bin
```

Build log: `out/build.log` (59 warnings, same count as S231; no errors).

## Hashes

| Binary | Size (B) | sha256 |
|---|---|---|
| S232 `out/m1n1-s232-usbcoherent.bin` | 2162688 | `f000aae15f0f75d14a0f2900ddfa738d0401e6c1bb18ece5c90f3a4a300a1c97` |
| S231 `../layout-s231/out/m1n1-s231-layout80.bin` (baseline) | 2162688 | `02c0bd9ea9d036d0e9d0bae13e9998f8da12f58ae5c54242f6ca22812f7dd0d6` |

Size is unchanged because every segment is 16 KiB aligned and .text/.rodata had slack.

## Binary diff summary (capstone, `tools/bindiff.py` + `tools/hvrw.py`, run with `m1n1_windows/.venv-hv/bin/python3 -I`)

Compared `build/m1n1-raw.elf` of both trees (each verified byte-identical to its shipped .bin)
with PC-relative targets resolved to symbol+offset / string contents. Reports:
`out/bindiff-s231-vs-s232.txt`, `out/hv_emulate_rw-masked-diff.txt`.

- Functions: 838 -> 839; only new function `nwoas_fl_coherent_policy` (0x1c4 B, 113 insns):
  reads 0x690200000 (id) and 0x690200070 (cap), `ldrh` 0x690200078, `tst` with 0x810,
  `strh (v & 0xf7ef)` to 0x690200078, `dsb sy`, `ldrh` readback, capped printf.
- `hv_emulate_rw` (hv_emulate_rw_aligned is inlined): 0x24e8 -> 0x2500 B. After masking branch
  offsets and assert line numbers, the only semantic change is in the FL1100 BAR write block:
  inserted `cmp woff,#0x80 -> bl nwoas_fl_coherent_policy("usbcmd")` and
  `bl nwoas_fl_coherent_policy("db")` at the top of the doorbell branch, plus register
  reallocation around the `nwoas_fl_dboff` reload. The other masked hunks were checked
  instruction-by-instruction and are identical raw instructions (artifact of the
  normalizer's adrp register tracking).
- `hv_handle_dabort`, `hv_map`, `hv_pt_get_l3`, `hv_pt_map_l3`, `hv_pt_map_l4`, `hv_pt_walk`:
  only `__assert_fail` line-number immediates differ (hv_vm.c lines shifted); zero diff when masked.
- `.init`: 16 raw bytes differ, all PC-relative call/adr immediates into the shifted .text;
  normalized identical. `.data` and the bss-region bytes are identical.
- Relocations: 685 vs 685, identical after symbolization.
- `.rodata` strings: version tag `...-s231L` -> `...-s232U`, plus the four new `S232 COH` format
  strings and the site names.

## Launcher

`../usb-s232-guest-test.sh` = `../layout-s231-guest-test.sh` with only the HV path, HV sha256
pin, log prefix (`logs/usb-s232-*`) and banner changed (HV size pin is the same 2162688).
`bash -n` passes; all 23 stat/sha pin lines were run in a subshell and pass. Not executed.

## What to look for in the hardware log

| Line | Meaning |
|---|---|
| `HVLOG: S232 COH INIT site=usbcmd before=2810 after=2000 ro=0 ns=0 verified=1 fault=0` | Expected: first clear at Windows takeover; both bits now 0 (other DEVCTL bits unchanged). Site may be `db`. |
| `HVLOG: S232 COH INIT site=... before=... already-clear=1 fault=0` | Bits were already 0 at the first check (unexpected vs 0x2810 history; record it). |
| `HVLOG: S232 COH REWRITE n=... before=2810 ...` | RO/NS came back (guest config write or device reset; HV cannot tell which) and were cleared again. First 8, then every 256th. Frequent REWRITE means the inter-doorbell gap matters. |
| `HVLOG: S232 COH SKIP ...` / `verified=0` / `fault=1` | Policy did not apply: the run is NOT a valid test of the hypothesis. |
| `HVLOG: FL-DEVCTL 0x... nosnoop=. relaxord=.` | Existing once/boot read on the USBSTS poll; may print 0x2810 if it fires before the first USBCMD write, otherwise 0x2000. |
| No `S232 COH` line at all | Policy never ran (no kernel USBCMD/doorbell write seen): invalid test. |

## Validation plan (hardware, user)

1. Cold/fresh boot with `nwoas_scripts/usb-s232-guest-test.sh` (same WinPE/USB setup as S231).
2. Confirm `S232 COH INIT ... verified=1` (and note any REWRITE/SKIP lines).
3. In WinPE, three times:
   `copy /b E:\sources\install2.swm C:\S232T<n>.swm`, then
   `fc /b C:\S232T<n>.swm C:\S231RC\install2.swm` and
   `certutil -hashfile C:\S232T<n>.swm SHA256`.
   Expected sha256 `c19b48920f6232d4f0cc6d9617d4ba493d6a56182e5c44dcf97b36487982f0f4`, fc "no differences".
   Caveat: copies 2 and 3 may be served from the Windows file cache filled by copy 1; only a
   read that actually goes to the USB stick tests the fix. Rebooting between copies (or copying
   a different large file such as install.swm) gives independent USB reads.
4. Optional: DISM /Apply-Image directly from E: (failed with 0x80070570 under S231).

Interpretation: PASS on all copies with `verified=1` supports the hypothesis but does not prove
it (the S231 failure was intermittent; compare against an S231 control run). Any mismatch with
RO/NS verified clear rules out this mechanism; next candidates per the S231 analysis are the
32-bit bounce / DMA address translation path, scatter-gather TRB chains and concurrent ring
handling.

## Rollback

Use `nwoas_scripts/layout-s231-guest-test.sh` (S231 HV, sha 02c0bd9e...). DEVCTL is volatile
PCIe config state; a reboot / PCIe reset restores the device default. m1n1-layout-s231 was not
modified.
