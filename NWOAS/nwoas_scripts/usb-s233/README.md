# S233: FL1100 cache-maintenance A/B (USB-A read-corruption experiment)

**Status: BUILT, HARDWARE-UNVERIFIED. A successful build is not a fix.** Neither binary in this
directory has been booted on the Mac mini. Whether the USB-A corruption changes can only be
decided by the hardware validation below.

Two binaries, same source tree (`m1n1-usb-s233`, = `m1n1-usb-s232` + the `hv_vm.c` diff below),
differing only by one compile-time define:

| Binary | `NWOAS_FL_EVT_NO_INVALIDATE` | `NWOAS_FL_RING_FULL_CLEAN` | Launcher |
|---|---|---|---|
| (a) `out/m1n1-s233-usbcache.bin` | 1 | 0 | `../usb-s233-guest-test.sh` |
| (b) `out/m1n1-s233b-ringclean.bin` | 1 | 1 | `../usb-s233b-guest-test.sh` |

Both keep everything S232 has (FL1100 DEVCTL Relaxed Ordering / No Snoop clear policy,
`NWOAS_FL_COHERENT_POLICY=1`).

## Why (from S232)

S232 cleared FL1100 DEVCTL RO+NS (verified on hardware 0x2810 -> 0x2000) and the U3 fresh-copy
test still failed: `install2.swm` and `boot.wim` copied from the USB stick were wrong,
`install.swm` was right. So RO/NS is not sufficient. S231 showed the bad region as 960 KiB of zeros
at a 1 MiB boundary (`../plan-20261115/usb.md`, `../usb-s232/README.md`). Remaining HV-side
hypotheses that S233 targets (all unverified):

- **H2 (A/B via (a))**: the EINT event-ring sweep (`nwoas_fl_inval_evt`) used `dc ivac`
  (invalidate only) over 0x1000 bytes. `dc ivac` throws away a dirty CPU line without writing it
  back, so any guest write still in cache in that window reverts to older DRAM contents.
- **H4 (A/B via (b))**: on a doorbell the HV cleaned only 0x100 bytes at the endpoint's
  output-context TR Dequeue Pointer. The xHC refreshes that field only on stop/reset, so during a
  long transfer it is stale (S231 log: dci=1 stayed at 0xfffde600 for 22 lines), and newly
  enqueued TRBs elsewhere in the ring are cleaned only if they happen to fall inside the
  `[crcr, crcr+0x8000)` command-ring sweep. A dci=4 segment at 0xfffb6000 lay outside it.

Facts from the S231 Windows-phase log (`../logs/layout-s231-20261115-191038.Vc0lr2.vuart`) used for
the design: ERSTBA (IPA) 0xfffde000, event-ring segment 0 at 0xfffdd000 with 256 TRBs (= exactly
0x1000 bytes, page-aligned), command ring 0xfffde400, scratchpad array 0xfffde200, transfer rings
0xfffde600 / 0xfffdea00 / 0xfffdec00 / 0xfffdee00 / 0xfffb6000, all below 4 GiB (AC64 masked),
and the FL1100's ERSTSZ register = 4 (four event-ring segments). The HV sweeps only segment 0.
Inference, not proven: in that layout the 0x1000 window lands exactly on segment 0 and on no
neighbouring structure, and USB works through segments 1-3 with no HV maintenance at all. That
suggests the FL1100's DMA writes reach the CPU without HV help, which would make (a) mostly a
safety fix rather than a likely cure. The `S233 EVTSEG` line checks the window/segment relation
on every boot.

## What changed

### (1) `NWOAS_FL_EVT_NO_INVALIDATE=1` (both binaries)

In `nwoas_fl_inval_evt()` (USBSTS read with EINT, Windows kernel, no synthetic PSCE pending), the
0x1000-byte event-ring op is `dc civac` instead of `dc ivac`. The 16-byte ERST[0] clean before it
is unchanged.

Why this is safe:
- On a **clean** line, `dc civac` and `dc ivac` do the same thing (drop the line; the next read
  comes from DRAM = the FL1100's DMA'd events). They differ only on a **dirty** line: `civac`
  writes it back, `ivac` discards it. A dirty line can only come from a guest CPU write, so with
  `civac` the HV never destroys guest-written data.
- Remaining risk is the old reason this path used `ivac` (comment at `nwoas_cache_maint`): a dirty
  init-zero line of the ring written back over an event the FL1100 just DMA'd. With
  `NWOAS_COH_KEEP=1` (unchanged, compile-time warning if 0) the HV already does `dc civac` on this
  same 0x1000 page at every complete ERSTBA write, and usbxhci zero-inits the ring before
  programming ERSTBA (xHCI 4.2), so those lines are clean before the controller can post. If it
  still happened, the symptom would be a lost/late event (USB timeout, controller reset), not
  silently wrong file data.
- Unknown: whether the M1 core already turns a `dc ivac` issued at EL2 into clean+invalidate. If it
  does, (a) changes nothing in effect, and an unchanged failure rate is consistent with that.

### (2) `NWOAS_FL_RING_FULL_CLEAN` (0 in (a), 1 in (b))

The doorbell write that S232 already traps now passes the slot (doorbell index =
`(offset - DBOFF) / 4`) and DB Target (`val & 0xff`) to `nwoas_fl_clean_cmd`. For every
**active endpoint** (output-context EP State Running(1) or Stopped(3), plus the endpoint this
doorbell names) without streams, the 0x100 clean is replaced by `nwoas_fl_ring_full_clean`:

- starts at the output-context TR Dequeue Pointer and follows Link TRBs (type 6) through every
  segment until it returns to its start;
- max 16 segments, max 256 TRBs per segment (or up to and including the Link TRB);
- every TRB address and every Link target must be inside the guest low window [0, 4 GiB) and
  translate (stage-2 walk, cached per 16 KiB page) to mapped DRAM before it is read or cleaned;
  otherwise the walk stops and is counted (`xcap`);
- each segment run is cleaned with `dc civac` (start rounded down to the 64 B line), split at
  16 KiB IPA pages;
- a run that is not entirely inside `[crcr, crcr+0x8000)` is counted (`xout`) and logged
  (`S233 XFER-OUT`, first 16, then every 1024th).

Endpoints with MaxPStreams > 0 (TR Dequeue Pointer = Stream Context Array, not a ring) are not
walked; they keep the 0x100 clean and are counted (`xstrm`). Inactive endpoints keep the 0x100
clean. TRBs are guest-authored and device-read, so `civac` never discards guest data. Hazard (why
(b) is a separate variant, cf. D63 in the source comment): if a device-written buffer shares a
cache line with a ring segment and holds a dirty CPU line, the clean writes that line back to DRAM.
Cost: one ring walk per active endpoint per doorbell (up to 4096 TRB reads and the matching
`dc civac`), under the trap path. Watch for USB throughput drop or timeouts.

### Counters (both binaries)

`nwoas_fl_mc` counts every FL1100 maintenance path; `nwoas_fl_mc_tick()` (called from the doorbell
sweep and the EINT sweep) prints one `S233 MC` line at most every 10 s, the first immediately,
hard cap 2048 lines per boot. Plain increments under bhl; no guest or device state is written.

No change to AC64 masking, DART, NVMe, USB-C, DEVCTL policy, NC remap, the COH_KEEP cleans
themselves, or the diagnostic dump paths. Remaining `dc ivac` users in the HV (unchanged): the
capped FL1100 diagnostic dumps (`nwoas_dump_evtring` / `nwoas_dump_dart_dcbaa`, at most 16 runs
early in boot + one POST-CONSUME snapshot) and USB-C / HID diagnostic paths.

## Build

```
cd /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-usb-s233
# (a)
env RUSTUP_TOOLCHAIN=1.88.0 CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=2 \
    M1N1_VERSION_TAG=v1.0.2-1474-gbddf7f06-s233U \
    RUSTFLAGS="--remap-path-prefix=$HOME/.rustup/toolchains/1.88.0-aarch64-apple-darwin/lib/rustlib/src/rust=/rustc/6b00bc3880198600130e1cf62b8f8a93494488cc" \
    bash -c "rm -rf build && nice -n 19 make TOOLCHAIN=/opt/homebrew/opt/llvm@20/bin/ LLDDIR=/opt/homebrew/opt/lld@20/bin/ USE_CLANG=1 EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50' -j4"
cp build/m1n1.bin ../nwoas_scripts/usb-s233/out/m1n1-s233-usbcache.bin
# (b): same, with M1N1_VERSION_TAG=v1.0.2-1474-gbddf7f06-s233B and
#      EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50 -DNWOAS_FL_RING_FULL_CLEAN=1'
cp build/m1n1.bin ../nwoas_scripts/usb-s233/out/m1n1-s233b-ringclean.bin
```

Logs: `out/build.log`, `out/build-b.log` (45 `warning:` lines each, the same set as S232's
build.log; no errors). (a) was rebuilt from the final source and is byte-identical to the shipped
file; `m1n1-usb-s233/build/` holds that (a) build. Version tags keep the S232 length (`s233U`,
`s233B` vs `s232U`).

## Hashes

| Binary | Size (B) | sha256 |
|---|---|---|
| (a) `out/m1n1-s233-usbcache.bin` | 2162688 | `bbeb3dbbd413dc008720a76eadf501ac51c8793f64e1508ebe786a2917c01207` |
| (b) `out/m1n1-s233b-ringclean.bin` | 2162688 | `d2eb62b53310dbd6c5b481a37bdbbf39aea5826f2bfb8d4a9c740cba3c529316` |
| S232 `../usb-s232/out/m1n1-s232-usbcoherent.bin` (baseline) | 2162688 | `f000aae15f0f75d14a0f2900ddfa738d0401e6c1bb18ece5c90f3a4a300a1c97` |

## Binary diff (capstone, `m1n1_windows/.venv-hv/bin/python3 -I`, tools in `tools/`)

`tools/bindiff.py` and `tools/hvrw.py` are the S232 tools; `tools/effects.py` (call / cache-op /
barrier multiset per function) and `tools/dccount.py` (dc ops per function) are new. Every ELF
was checked byte-identical to its shipped .bin.

S232 -> (a) (`out/bindiff-s232-vs-s233.txt`, `out/effects-s232-vs-s233.txt`,
`out/masked-other-s232-vs-s233.txt`, `out/dc-ops-per-function.txt`):
- Functions 839 -> 840; new: `nwoas_fl_mc_tick` (0x10c B).
- `hv_emulate_rw` (the doorbell sweep and EINT sweep are inlined into it) 0x2500 -> 0x2680 B. Its
  dc ops go from 8 civac + 3 ivac to 9 civac + 2 ivac: exactly one `dc ivac` became `dc civac`.
  Side-effect ops added: 2 x `bl nwoas_fl_mc_tick`, 1 `debug_printf` (EVTSEG) and the inlined
  `hv_pa_read` of ERST[0].size (2 x `dsb sy`, `hv_get_esr`/`hv_get_far` on its fault path); one
  extra `bl nwoas_coh_keep_fresh` instruction is a compiler tail duplication (3 call sites in
  source before and after). Everything else in the linear diff is register reallocation and block
  reordering.
- `nwoas_coh_keep_fresh`: only the two counter increments.
- `hv_handle_dabort`, `hv_map`, `hv_pt_*`: only `__assert_fail` line numbers (masked diff 0).
- `.data`, bss-region bytes, relocations (685) identical; `.init` normalized identical.
- `.rodata`: version tag, the new `S233 CFG/EVTSEG/MC` strings, `civac`.

Isolation of (1) (`out/bindiff-s233ctl-vs-s233.txt`): a control build of the same source with
`-DNWOAS_FL_EVT_NO_INVALIDATE=0` differs from (a) only by `dc ivac` -> `dc civac` at the event-ring
op, `evt_no_invalidate=0` -> `1` in the CFG printf, and the `ivac`/`civac` log string.

(a) -> (b) (`out/bindiff-s233-vs-s233b.txt`, `out/effects-s233-vs-s233b.txt`): new function
`nwoas_fl_flush_run` (holds the walk's `dc civac`); `hv_emulate_rw` adds 3 x
`bl nwoas_fl_flush_run`, 2 x `bl hv_pt_walk` (TRB page and Link-target page translation), the
XFER-FULL printf; nothing removed. Strings: tag `s233B`, `S233 XFER-FULL`, `S233 XFER-OUT`.

## Launchers

`../usb-s233-guest-test.sh` and `../usb-s233b-guest-test.sh` are `../usb-s232-guest-test.sh` with
only the HV path, HV sha256 pin, log prefix (`logs/usb-s233-*` / `logs/usb-s233b-*`) and banner
changed (size pin 2162688 unchanged). `bash -n` passes; all 23 stat/sha pin lines of each were run
in a subshell and pass. Not executed.

## Log lines to expect

| Line | Meaning |
|---|---|
| `HVLOG: S232 COH INIT ... verified=1` | S232 DEVCTL policy still active (required, as in S232). |
| `HVLOG: S233 CFG evt_no_invalidate=1 ring_full_clean=0 coh_keep=1` | (a). (b) prints `ring_full_clean=1`. Missing = no doorbell/EINT sweep ran: invalid test. |
| `HVLOG: S233 EVTSEG seg0=0x... trbs=256 bytes=0x1000 window=0x1000 op=civac` | Once per new event-ring segment base (max 8). `bytes` < 0x1000 or a non-page-aligned `seg0` means the old ivac window covered neighbouring data on that boot (supports H2 for that boot). |
| `HVLOG: S233 MC t=...s db=... cmd=... dcbaa=... ctx=... x100=... xfull=... xtrbs=... xsegs=... xcap=... xout=... xstrm=... evt=... evtop=civac erst=... keep=... keepring=...` | Every <=10 s. `db`/`evt` should grow during a copy. (a): `xfull=0`. |
| `HVLOG: S233 XFER-FULL slot=.. dci=.. deq=0x... trbs=.. segs=.. complete=1` | (b) only, first 32 walks (+32 incomplete). `complete=0` = cap or invalid pointer hit; check `xcap`. |
| `HVLOG: S233 XFER-OUT n=.. slot=.. dci=.. seg=0x...-0x... crcr=0x...` | (b) only: a cleaned ring run outside the command-ring sweep window (new coverage vs S232). First 16, then every 1024th; total in `xout`. |
| `xstrm` > 0 | (b): a stream (UAS) endpoint was active; its rings are NOT walked. Record it; this would leave H4 open. |
| USB timeouts / controller resets / `NWOAS-FLC` USBCMD HCRST lines during the copy | Possible side effect of (1) (lost event) or of (2) (cost); record and compare with S232. |

## Validation (hardware, user)

Same U3 test as S232 (`../usb-s232/U3-fresh-copy.cmd`: fresh-boot `copy /b` of `install.swm`,
`install2.swm`, `boot.wim` from E: to C:, each checked with NWREAD against the known sha256), over
**3 fresh boots per binary**:

1. Boot with `../usb-s233-guest-test.sh` (a). Confirm `S232 COH INIT ... verified=1`,
   `S233 CFG ... evt_no_invalidate=1 ring_full_clean=0`, and `S233 MC` lines with growing `evt`.
2. Run U3; record `S232 U3 RESULT FAIL=` and which files mismatched; restart Windows; repeat for 3
   fresh boots. (`../usb-s232/run-u3-cycles.sh` hardcodes the S232 launcher, log prefix and result
   file, so it cannot be reused unmodified for S233.)
3. Repeat 1-2 with `../usb-s233b-guest-test.sh` (b); additionally record `xfull`, `xcap`, `xout`,
   `xstrm` and the XFER-FULL/XFER-OUT lines.

Interpretation:
- (a) fails like S232 -> discarding dirty lines in the event-ring window is not the cause (or M1
  already treats the EL2 `dc ivac` as clean+invalidate).
- (a) fails, (b) passes 3/3 with `xout` > 0 -> supports H4 (TRBs outside the old cleaned range);
  does not prove it (the failure is intermittent; S232 failed on 2 of 3 files in one run).
- Both fail -> the HV cache-maintenance hypotheses tested here are not sufficient; next candidates
  per `../plan-20261115/usb.md`: 32-bit DMA bounce buffers (`bcdedit truncatememory`, needs the
  user's approval), stream endpoints if `xstrm` > 0, AC64 unmasking.
- Any USB hang / repeated controller reset under (a) or (b) is a regression of that variant; go
  back to S232.

## Rollback

Use `../usb-s232-guest-test.sh` (S232 HV, sha `f000aae1...`) or `../layout-s231-guest-test.sh`
(S231). All S233 changes are in the HV image only; nothing is persisted on the Mac mini or in
Windows. `m1n1-usb-s232` was not modified.

## Source diff (`diff -ru m1n1-usb-s232/src m1n1-usb-s233/src`, also `out/src-s232-vs-s233.diff`)

```diff
diff -ru m1n1-usb-s232/src/hv_vm.c m1n1-usb-s233/src/hv_vm.c
--- m1n1-usb-s232/src/hv_vm.c	2026-11-15 19:35:38
+++ m1n1-usb-s233/src/hv_vm.c	2026-11-15 19:57:17
@@ -125,6 +125,26 @@
 #define NWOAS_FL_COHERENT_POLICY 1
 #endif
 
+/* S233 USB-A read-corruption experiment (hypothesis, hardware-unverified):
+ * (1) NWOAS_FL_EVT_NO_INVALIDATE: the EINT event-ring sweep in
+ *     nwoas_fl_inval_evt() used dc ivac (invalidate only) over 0x1000 bytes,
+ *     which silently DISCARDS any dirty guest CPU line in that window. Use
+ *     dc civac instead, so the HV never throws away guest-written data.
+ * (2) NWOAS_FL_RING_FULL_CLEAN: on an endpoint doorbell, clean+invalidate the
+ *     whole transfer ring of the rung endpoint (all segments, followed via
+ *     Link TRBs) instead of only 0x100 bytes at the output-context dequeue
+ *     pointer. Built but OFF by default (A/B against (1)).
+ * Both paths are counted in rate-limited "S233 MC" HVLOG lines. */
+#ifndef NWOAS_FL_EVT_NO_INVALIDATE
+#define NWOAS_FL_EVT_NO_INVALIDATE 1
+#endif
+#ifndef NWOAS_FL_RING_FULL_CLEAN
+#define NWOAS_FL_RING_FULL_CLEAN 0
+#endif
+#if NWOAS_FL_EVT_NO_INVALIDATE && !NWOAS_COH_KEEP
+#warning "NWOAS_FL_EVT_NO_INVALIDATE relies on NWOAS_COH_KEEP flushing the event-ring init zeros at ERSTBA"
+#endif
+
 #define PAGE_SIZE       0x4000
 #define CACHE_LINE_SIZE 64
 #define CACHE_LINE_LOG2 6
@@ -3023,28 +3043,209 @@
            (u64)len, pages);
 }
 
+/* S233: counters for every FL1100 cache-maintenance path, printed as one
+ * "HVLOG: S233 MC" line at most every 10 s (first line on the first call,
+ * hard cap 2048 lines per boot). All callers run in the trap path under bhl,
+ * so plain increments are sufficient. Read-only w.r.t. guest/device state. */
+static struct {
+    u64 db;          // nwoas_fl_clean_cmd() runs (one per FL1100 doorbell write)
+    u64 cmd;         // command-ring 0x8000 cleans
+    u64 dcbaa;       // DCBAA 0x4000 cleans
+    u64 ctx;         // device-context 0x1000 cleans
+    u64 x100;        // 0x100 transfer-ring cleans at the output-context dequeue pointer
+    u64 xfull;       // full transfer-ring walks (NWOAS_FL_RING_FULL_CLEAN)
+    u64 xtrbs;       // TRBs covered by full walks
+    u64 xsegs;       // ring segments covered by full walks
+    u64 xcap;        // full walks stopped by a cap / invalid pointer (partial coverage)
+    u64 xout;        // full-walk segment runs not inside [crcr, crcr+0x8000)
+    u64 xstrm;       // active endpoints with MaxPStreams>0 (not walked; 0x100 only)
+    u64 evt;         // EINT event-ring sweeps (nwoas_fl_inval_evt reaching the ring op)
+    u64 erst;        // ERST[0] 16-byte cleans in the EINT path
+    u64 keep;        // NWOAS_COH_KEEP keep-fresh calls (ERST[0] + DCBAA head 16 B cleans)
+    u64 keepring;    // keep-fresh calls that also cleaned the 0x1000 event-ring page
+} nwoas_fl_mc;
+
+static void nwoas_fl_mc_tick(void)
+{
+    static u64 last = 0;
+    static u32 lines = 0;
+    static bool cfg_logged = false;
+    if (!cfg_logged) {
+        cfg_logged = true;
+        printf("HVLOG: S233 CFG evt_no_invalidate=%d ring_full_clean=%d coh_keep=%d\n",
+               NWOAS_FL_EVT_NO_INVALIDATE, NWOAS_FL_RING_FULL_CLEAN, NWOAS_COH_KEEP);
+    }
+    if (lines >= 2048)
+        return;
+    u64 now = mrs(CNTPCT_EL0);
+    u64 frq = mrs(CNTFRQ_EL0);
+    if (last && now - last < 10 * frq)
+        return;
+    last = now;
+    lines++;
+    printf("HVLOG: S233 MC t=%lus db=%lu cmd=%lu dcbaa=%lu ctx=%lu x100=%lu xfull=%lu xtrbs=%lu "
+           "xsegs=%lu xcap=%lu xout=%lu xstrm=%lu evt=%lu evtop=%s erst=%lu keep=%lu keepring=%lu\n",
+           frq ? now / frq : 0, nwoas_fl_mc.db, nwoas_fl_mc.cmd, nwoas_fl_mc.dcbaa, nwoas_fl_mc.ctx,
+           nwoas_fl_mc.x100, nwoas_fl_mc.xfull, nwoas_fl_mc.xtrbs, nwoas_fl_mc.xsegs,
+           nwoas_fl_mc.xcap, nwoas_fl_mc.xout, nwoas_fl_mc.xstrm, nwoas_fl_mc.evt,
+           NWOAS_FL_EVT_NO_INVALIDATE ? "civac" : "ivac", nwoas_fl_mc.erst, nwoas_fl_mc.keep,
+           nwoas_fl_mc.keepring);
+}
+
+#if NWOAS_FL_RING_FULL_CLEAN
+/* S233 (2): clean+invalidate whole CPU-authored transfer rings. The walk starts
+ * at the output-context TR Dequeue Pointer (refreshed by the xHC only on
+ * stop/reset, so usually stale while the endpoint runs) and follows Link TRBs
+ * (type 6) through every segment until it returns to its start, so it covers
+ * wherever Windows has enqueued since. Bounds: max 16 segments, max 256 TRBs
+ * per segment (or up to and including the Link TRB). Every TRB address and
+ * every Link target must lie in the guest low window [0, 4 GiB) (AC64 is masked,
+ * so all FL1100 DMA addresses are 32-bit) and translate to mapped DRAM before it
+ * is read or cleaned; otherwise the walk stops (counted as xcap). Reads use the
+ * EL2 cacheable DRAM map (the CPU's latest copy), so they cannot fault.
+ * dc civac only: TRBs are guest-authored and device-read, so this never
+ * discards guest data. Hazard (why it is a separate, OFF-by-default variant,
+ * cf. D63): a dirty CPU line of a device-written buffer sharing a cache line
+ * with a ring segment would be written back to DRAM. */
+#define NWOAS_FL_LOW_WINDOW 0x100000000UL
+
+// Validate a 16-byte TRB / Link target IPA: inside [0, 4 GiB) and backed by mapped DRAM. The stage-2
+// walk result is cached per 16 KiB IPA page in *pg / *pg_pa (a TRB never straddles a page: 16 B
+// aligned), so walking a ring costs one hv_pt_walk per page, not per TRB.
+static bool nwoas_fl_low_mapped(u64 ipa, u64 *pg, u64 *pg_pa, u64 *pa_out)
+{
+    if (ipa + 16 < ipa || ipa + 16 > NWOAS_FL_LOW_WINDOW)
+        return false;
+    u64 page = ipa & ~(u64)(PAGE_SIZE - 1);
+    if (page != *pg) {
+        *pg = page;
+        *pg_pa = nwoas_ipa_to_pa(page);
+    }
+    u64 pa = *pg_pa ? *pg_pa + (ipa - page) : 0;
+    if (!pa || !nwoas_in_dram(pa, 16))
+        return false;
+    *pa_out = pa;
+    return true;
+}
+
+// Clean [from, to) of a validated segment run, per 16 KiB IPA page; also count/log a run that is
+// not fully inside the command-ring sweep window [crcr, crcr+0x8000) (i.e. new coverage).
+static void nwoas_fl_flush_run(u64 from, u64 to, u32 slot, u32 dci)
+{
+    static u32 out_log = 0;
+    if (from >= to)
+        return;
+    // Round down to the cache line: nwoas_coh_clean() masks its start to 64 B but keeps len, so an
+    // unaligned start would otherwise leave the line holding the last TRB(s) of the run uncleaned.
+    from &= ~0x3fUL;
+    u64 cr = nwoas_fl_crcr & ~0x3fUL;
+    if (!nwoas_fl_crcr || from < cr || to > cr + 0x8000) {
+        nwoas_fl_mc.xout++;
+        if (out_log < 16 || (nwoas_fl_mc.xout & 0x3ff) == 0) {
+            out_log++;
+            printf("HVLOG: S233 XFER-OUT n=%lu slot=%u dci=%u seg=0x%lx-0x%lx crcr=0x%lx\n",
+                   nwoas_fl_mc.xout, slot, dci, from, to, cr);
+        }
+    }
+    while (from < to) {
+        u64 pg_end = (from & ~(u64)(PAGE_SIZE - 1)) + PAGE_SIZE;
+        u64 end = to < pg_end ? to : pg_end;
+        nwoas_coh_clean(from, end - from); // translates + bounds-checks its own start
+        from = end;
+    }
+}
+
+static void nwoas_fl_ring_full_clean(u64 ring, u32 slot, u32 dci)
+{
+    static u32 walk_log = 0;
+    const u64 start = ring & ~0xfUL;
+    u64 t = start, run = start, pg = ~0UL, pg_pa = 0;
+    u32 trbs = 0, seg_trbs = 0, segs = 1;
+    bool done = false;
+    nwoas_fl_mc.xfull++;
+    for (;;) {
+        u64 pa = 0;
+        if (seg_trbs >= 256 || !nwoas_fl_low_mapped(t, &pg, &pg_pa, &pa))
+            break; // segment cap or invalid TRB address: stop, partial coverage
+        u32 ctrl = *(const volatile u32 *)(pa + 12);
+        trbs++;
+        seg_trbs++;
+        if (((ctrl >> 10) & 0x3f) != 6) { // not a Link TRB: next TRB in this segment
+            t += 16;
+            if (t == start) { // wrapped back to where the walk began
+                done = true;
+                break;
+            }
+            continue;
+        }
+        nwoas_fl_flush_run(run, t + 16, slot, dci); // the segment run including its Link TRB
+        u64 next = *(const volatile u64 *)pa & ~0xfUL;
+        u64 npa = 0;
+        if (!next || !nwoas_fl_low_mapped(next, &pg, &pg_pa, &npa)) {
+            run = t = 0; // bad Link target: nothing more to clean
+            break;
+        }
+        if (next == start) { // ring closed exactly at a segment start
+            run = t = 0;
+            done = true;
+            break;
+        }
+        if (segs >= 16) {
+            run = t = 0;
+            break;
+        }
+        segs++;
+        seg_trbs = 0;
+        run = t = next;
+    }
+    nwoas_fl_flush_run(run, t, slot, dci); // tail: [run, t) (empty when the last op was a Link)
+    if (!done)
+        nwoas_fl_mc.xcap++;
+    nwoas_fl_mc.xtrbs += trbs;
+    nwoas_fl_mc.xsegs += segs;
+    if (walk_log < 32 || (!done && walk_log < 64)) {
+        walk_log++;
+        printf("HVLOG: S233 XFER-FULL slot=%u dci=%u deq=0x%lx trbs=%u segs=%u complete=%d\n", slot,
+               dci, start, trbs, segs, done);
+    }
+}
+#endif
+
 // On a doorbell write: flush usbxhci's just-written command TRBs + DCBAA + device contexts to DRAM
 // so the FL1100 DMA-reads the real data instead of stale cache.  Also flush the first 16 TRBs of
 // each live endpoint transfer ring.  D63 proved that a keyboard report can reach the interrupt-IN
 // buffer while the xHC never posts its chained Event Data completion: the Normal TRB was visible to
 // the No-Snoop controller, but the transfer-ring chain was not kept coherent.  Keep this to 0x100
 // bytes so we never clean adjacent device-authored report buffers back over fresh input data.
+// S233: with NWOAS_FL_RING_FULL_CLEAN, every active endpoint (EP State Running/Stopped, plus the
+// endpoint this doorbell names: slot = doorbell index, dci = DB Target) gets a whole-ring walk
+// instead of the 0x100 clean; inactive endpoints and stream endpoints keep the 0x100 clean.
+#if NWOAS_FL_RING_FULL_CLEAN
+static void nwoas_fl_clean_cmd(struct exc_info *ctx, u32 db_slot, u32 db_target)
+#else
 static void nwoas_fl_clean_cmd(struct exc_info *ctx)
+#endif
 {
     static u32 transfer_log = 0;
-    if (nwoas_fl_crcr)
+    nwoas_fl_mc.db++;
+    nwoas_fl_mc_tick();
+    if (nwoas_fl_crcr) {
         nwoas_coh_clean(nwoas_fl_crcr, 0x8000); // command ring: 2 pages
+        nwoas_fl_mc.cmd++;
+    }
     if (!nwoas_fl_dcbaap)
         return;
     u64 dp = nwoas_ipa_to_pa(nwoas_fl_dcbaap & ~0x3fUL);
     if (!nwoas_in_dram(dp, 0x4000))
         return;
     nwoas_cache_maint(dp, 0x4000, true); // DCBAA, then read it back (fresh from DRAM) for the ctxs
+    nwoas_fl_mc.dcbaa++;
     /* DCBAA[0] is the scratchpad-buffer-array pointer, not a device slot. */
     for (int i = 1; i < 16; i++) {
         u64 ctxp = 0;
         if (hv_pa_read(ctx, dp + 8 * i, &ctxp, 3) && ctxp) {
             nwoas_coh_clean(ctxp, 0x1000); // device/input context: 1 page
+            nwoas_fl_mc.ctx++;
             u64 cp = nwoas_ipa_to_pa(ctxp & ~0x3fUL);
             if (!cp || !nwoas_in_dram(cp, 32 * 32))
                 continue;
@@ -3058,7 +3259,22 @@
                 u64 ring = deq & ~0xfUL;
                 if (!ring)
                     continue;
+#if NWOAS_FL_RING_FULL_CLEAN
+                // Active = EP State Running(1) or Stopped(3) (a doorbell restarts a Stopped
+                // endpoint), or the endpoint this doorbell names. Disabled/Halted/Error: 0x100.
+                u32 ep_state = ep[0] & 0x7;
+                if (ep_state == 1 || ep_state == 3 || ((u32)i == db_slot && dci == db_target)) {
+                    if ((ep[0] >> 10) & 0x1f) {
+                        // MaxPStreams > 0: deq points to a Stream Context Array, not a ring.
+                        nwoas_fl_mc.xstrm++;
+                    } else {
+                        nwoas_fl_ring_full_clean(ring, (u32)i, dci);
+                        continue;
+                    }
+                }
+#endif
                 nwoas_coh_clean(ring, 0x100); // guest-authored TRBs only; no report buffers
+                nwoas_fl_mc.x100++;
                 if (transfer_log < 32) {
                     transfer_log++;
                     printf("HVLOG: XFER-CLEAN slot=%d dci=%u ring=0x%lx bytes=0x100\n",
@@ -3070,8 +3286,22 @@
 }
 
 // When usbxhci sees EINT, invalidate the event ring so the CPU reads the FL1100's DMA'd completions.
+// S233 (NWOAS_FL_EVT_NO_INVALIDATE=1): the ring op is dc civac, not dc ivac.
+//   Why: dc ivac discards a dirty CPU line without writing it back. Any guest CPU write still in
+//   cache inside [ring, ring+0x1000) -- an event-TRB write by usbxhci, or a neighbouring Windows
+//   structure if the window ever extends past the segment -- would silently revert to the older
+//   DRAM contents. dc civac drops a CLEAN stale line exactly like dc ivac (so the next read still
+//   comes from DRAM = the FL1100's DMA'd events); it differs ONLY for a dirty line, which it writes
+//   back instead of throwing away. The HV therefore never destroys guest-written data here.
+//   Residual risk (old comment at nwoas_cache_maint): a dirty init-zero line written back over a
+//   just-DMA'd event. NWOAS_COH_KEEP already civacs this same 0x1000 page at every complete ERSTBA
+//   write (usbxhci zero-inits the ring before programming ERSTBA, xHCI 4.2), so those lines are
+//   clean before the controller can post; and if it still happened the symptom is a lost/late
+//   event (USB timeout / reset), not silently wrong file data.
 static void nwoas_fl_inval_evt(struct exc_info *ctx)
 {
+    static u32 seg_log = 0;
+    static u64 seg_last = 0;
     if (!nwoas_fl_erstba)
         return;
     u64 ep = nwoas_ipa_to_pa(nwoas_fl_erstba & ~0x3fUL);
@@ -3081,12 +3311,30 @@
      * usbxhci update before reading its ring pointer; invalidate-only here can
      * discard that update and send us to an older ring. */
     nwoas_cache_maint(ep, 16, true);
+    nwoas_fl_mc.erst++;
     u64 rb = 0;
     if (!hv_pa_read(ctx, ep, &rb, 3) || !rb)
         return;
+    if (rb != seg_last && seg_log < 8) {
+        // Once per new segment base: the ERST[0] segment size vs. the fixed 0x1000 sweep window.
+        seg_log++;
+        seg_last = rb;
+        u64 sz = 0;
+        hv_pa_read(ctx, ep + 8, &sz, 2);
+        printf("HVLOG: S233 EVTSEG seg0=0x%lx trbs=%u bytes=0x%x window=0x1000 op=%s\n",
+               rb & ~0x3fUL, (u32)(sz & 0xffff), (u32)(sz & 0xffff) * 16,
+               NWOAS_FL_EVT_NO_INVALIDATE ? "civac" : "ivac");
+    }
     u64 ring = nwoas_ipa_to_pa(rb & ~0x3fUL);
-    if (nwoas_in_dram(ring, 0x1000))
+    if (nwoas_in_dram(ring, 0x1000)) {
+#if NWOAS_FL_EVT_NO_INVALIDATE
+        nwoas_cache_maint(ring, 0x1000, true); // S233: clean+invalidate, never discard dirty lines
+#else
         nwoas_cache_maint(ring, 0x1000, false); // invalidate the event ring (a page)
+#endif
+        nwoas_fl_mc.evt++;
+    }
+    nwoas_fl_mc_tick();
 }
 
 #if NWOAS_COH_KEEP
@@ -3110,6 +3358,7 @@
 {
     if (!nwoas_fl_erstba)
         return;
+    nwoas_fl_mc.keep++;
     u64 ep = nwoas_ipa_to_pa(nwoas_fl_erstba & ~0x3fUL);
     if (nwoas_in_dram(ep, 16)) {
         nwoas_cache_maint(ep, 16, true); // ERST[0]: FL1100 DMA-reads this to locate the event ring
@@ -3118,8 +3367,10 @@
             u64 seg = 0;
             if (hv_pa_read(ctx, ep, &seg, 3) && (seg & ~0x3fUL)) {
                 u64 rp = nwoas_ipa_to_pa(seg & ~0x3fUL);
-                if (nwoas_in_dram(rp, 0x1000))
+                if (nwoas_in_dram(rp, 0x1000)) {
                     nwoas_cache_maint(rp, 0x1000, true); // usbxhci's ring zero-init -> DRAM
+                    nwoas_fl_mc.keepring++;
+                }
             }
         }
     }
@@ -3653,6 +3904,10 @@
                             reported_skip = true;
                             printf("HVLOG: S114 legacy doorbell cache sweep disabled\n");
                         }
+#elif NWOAS_FL_RING_FULL_CLEAN
+                        // S233: doorbell index = slot ID, DB Target (bits 7:0) = endpoint DCI.
+                        nwoas_fl_clean_cmd(ctx, (u32)((woff - nwoas_fl_dboff) >> 2),
+                                           (u32)(val[0] & 0xff));
 #else
                         nwoas_fl_clean_cmd(ctx);
 #endif
```
