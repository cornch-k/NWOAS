# Draft J274 guest layout (`config/j274-guest-layout.json`)

Format: exactly the 23 keys of the reference `E/guest_layout.py:15-37` /
`E/config/j313-guest-layout.json`. Nothing was built or booted. The file is a
draft for step 1 of `N/nwoas_scripts/plan-20261115/port/boot.md` §4.

## 1. Values

| Key | J313 (ref) | J274 draft | Basis |
|---|---|---|---|
| layout_version | 1 | 1 | – |
| phys_base | 0x850000000 | 0x850000000 | PROPOSED (section 2) |
| ram_end | 0xa00000000 | 0xbe1000000 | FACT platform RAM end (LOG:134-135, :320) |
| adt_base | 0x851000000 | 0x851000000 | DERIVED phys_base + 16 MiB |
| adt_max_size | 0xb4000 | 0xbc000 | = firmware_base − adt_base (covers ADT 0x5c000 + TrustCache 0x60000), same convention as J313 |
| firmware_base | 0x8510b4000 | 0x8510bc000 | DERIVED |
| firmware_max_size | 0x1e00000 | 0x1e00000 | FD Size in both fdf files |
| boot_args_base | 0x8533e8000 | 0x853498000 | DERIVED with padded FD 0x1e00000; raw FD 0x1d88000 gives 0x853420000 |
| boot_args_size | 0x4000 | 0x4000 | – |
| ramdisk_base / max | 0x860000000 / 0x40000000 | same | PROPOSED placeholder; our Mu has no ramdisk PCDs |
| low_mem_ipa | 0x100000 | **0x0** | FACT S231 (LOG:348) — violates ref rule |
| low_mem_pa | 0x8a0100000 | 0xae0fcc000 | FACT S231 = RAM end − 4 GiB − 0x34000 |
| low_mem_size | 0x3ff00000 | **0x100000000** | FACT S231 — does not fit the UINT32 PCD |
| virtual_fb_base | 0x85f000000 | 0x85f000000 | placeholder guest surface (real HDMI scanout is vram 0xbe3f60000, LOG:279) |
| virtual_fb width/height/stride | 2560/1600/10240 | 1280/720/5120 | FACT S231 HDMI mode (LOG:278-279) |
| pci_ecam | 0x690000000 | 0x700000000 | FACT S231 (0x690000000 is the real J274 apcie ECAM) |
| pci_bar_window | 0x400000000 | 0x700100000 | FACT S231 |
| nvme_vintid | 64 | 900 | FACT S231 |
| xhci_base | 0x502280000 | 0x502280000 | FACT ADT usb-drd1 |
| cpu_count | 8 | 8 | FACT |

Offline check (`python3 -I -B tools/check_j274_layout.py`): every pinned address
re-derives exactly from the J274 sizes; one reference rule fails on purpose
(`low_mem_ipa < 0x1000`, L-1); one extra finding (`low_mem_size` > UINT32, L-2).
These two are the S231 low-window design and are listed as GAP G-6. The carveout
check reports L-8 (section 3). The checker exits 1 until each of L-1, L-2 and L-8 is
either fixed or accepted explicitly with `--allow`.

## 2. phys_base: ours moves every boot; how the reference handles it

**What moves on J274 [FACT].** Our Python `HV.load_raw()` sets
`phys_base = u.heap_top` (`N/m1n1_windows/proxyclient/m1n1/hv/__init__.py:2205`),
then `guest_base = phys_base + 16 MiB + ADT + TrustCache` (:2208). `heap_top` sits
above the proxy heap, which starts where iBoot happened to load m1n1. Today's
logs: m1n1 base `0x8025e0000`, `0x8027ac000`, `0x803168000`, `0x803300000`
(LOG:97 and three other 2026-11-15 S231 logs). Across all 461 saved host logs
the guest base takes 372 distinct values between `0x83b754000` and `0x83dbdc000`
(about 36 MiB spread); the RAM end is always `0xbe1000000`. The S213 loader
report shows this drift already broke a fixed address once
(`N/nwoas_scripts/loader-s213/README.md:102-135`).

**What the reference does [FACT].**

| Path | Mechanism | Evidence |
|---|---|---|
| Assisted (Python host) | pins the heap: `u.heap_top = layout.phys_base` unless `--no-pin`, so `load_raw()` lands at the same address every boot | `E/run_uefi.py:1-14` (rationale), `:272-277` |
| Assisted, heap safety | after the descriptor is published, m1n1 heap allocations are capped below guest RAM | `E/m1n1_windows/src/proxy.c:40-57` (`seal_heap_below_guest`) |
| Standalone | no proxy heap at all; `phys_base` is a compile-time constant from the generated header | `E/m1n1_windows/src/hv_autonomous_layout.generated.h:33-57` |
| Standalone, RAM end | `effective_end = min(layout.ram_end, boot_args.phys_base + mem_size)`, used for both BootArgs and the stage-2 map | `hv_autonomous_memory.c:5-22`, `hv_autonomous_runtime.c:363-371`, design `E/docs/superpowers/specs/2026-08-07-standalone-runtime-ram-bound-design.md` |
| Standalone, sub-layout | recomputes ADT/TC/FW/SEPFW/preoslog/bootargs with the same arithmetic as `load_raw` and fails closed unless they equal the pinned values | `hv_autonomous_runtime.c:216-229`, `hv_assisted_layout.c:28-54` |
| Mu | FD linked at `firmware_base` because PrePi uses the hard-coded `PcdFdBaseAddress`; BootArgs/ADT PCDs generated from the same JSON | `E/mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.fdf:31-38`, `J313GuestLayout.dsc.inc:6-15`, `E/guest_layout.py:134-148,200-216` |

Consequences for J274:

1. Pinning `0x850000000` keeps about 292 MiB of margin above the highest natural
   base seen (`0x83dbdc000`), so the assisted S231-style launcher could pin the
   heap the same way. [DERIVED]
2. The pinned `firmware_base` and `boot_args_base` depend on iBoot-provided
   sizes (ADT 0x5c000, TrustCache 0x60000, SEPFW 0x59c000, preoslog 0x40000). If a
   macOS/iBoot update changes any of them, the reference runtime refuses to boot
   (fail-closed) and the layout and Mu must be regenerated. [FACT from code;
   whether sizes change on update is UNKNOWN]
3. The reference standalone path has no check that stage-1 m1n1's own heap stays
   below `phys_base` (no ceiling call in `hv_autonomous*.c`; grep). On J274 the
   stage-1 heap would start near `0x803xxxxxx` and needs roughly the decompressed
   FD (30 MiB) — far below `0x850000000`. [HYPOTHESIS: not measured]
4. Our Mu today takes BootArgs/ADT from fixed PCDs `0x840000000/0x840004000`
   (`N/apple_silicon_platforms_mu/Silicon/Apple/T810XFamilyPkg/T810XFamilyPkg.dsc.inc:19-20`)
   and is entered through the guest m1n1 prefix in the 32 MiB payload. With
   `phys_base = 0x850000000` those PCDs fall below guest RAM, so they must be
   generated from the layout like the reference does. [FACT]

## 3. Known conflicts between our S231 design and the reference format

| # | Conflict | Effect if the reference runtime were used unchanged | Options |
|---|---|---|---|
| L-1 | low window at IPA 0 (ref requires ≥ 0x1000) | `load_layout()` rejects the file | relax the rule for J274, or move to the ref's 1 MiB-guarded window (needs Mu + hardware retest) |
| L-2 | 4 GiB window > UINT32 `PcdLowMemoryWindowSize` | PCD cannot hold it | our Mu does not read that PCD (it derives backing from BootArgs, `NwoasHideHighRamDxe.c:374-377`); keep ours |
| L-3 | S102 shrinks advertised RAM but maps all of it | ref advertises `ram_end − phys_base` (`hv_autonomous_runtime.c:265-266`); our HideHighRam would then compute backing = `0xbe1000000` (wrong) and the S102 UEFI-overwrite bug could return | add a "guest_mem_size" (advertised) field separate from the mapped `ram_end` |
| L-4 | HDMI scanout at vram `0xbe3f60000` above RAM end | ref maps nothing above effective `ram_end`; the guest would lose the framebuffer | explicit HW map of the `/vram` range (S231 maps it, LOG:410) |
| L-5 | RAM-LOW `[0x800000000,0x801504000)` mapped by S231 | not mapped by ref | HYPOTHESIS: probably unused by Mu/Windows; verify |
| L-6 | ECAM `0x690000000` in ref is the real J274 apcie ECAM | hooking it would hide FL1100 USB-A, Ethernet, Wi-Fi | keep `0x700000000` (done in draft) |
| L-7 | `nvme_vintid` is also hard-coded as 64 in `E/.../hv_irq_routes.h:24` | the layout value alone does not change it | J274 header constant 900 |
| **L-8 (blocker)** | iBoot carveouts inside guest RAM `[0x850000000, 0xbe1000000)`. FACT (`config/j274-adt-facts.json`, Sep-06 dump `d6-adt.bin` sha256 cb773d76…5f7f): region-id-48 `[0x840500000,0x860500000)` overlaps ADT, firmware, boot args, virtual framebuffer and the RAM disk start; 19 ids in `[0x864000000,0x86fe38000)` lie inside the RAM disk (MEM-14 said 20; the extracted table has 19). region-id-46/47 end at 0x840400000/0x840500000, below `phys_base`, so they are not inside this draft window. region-id-24 `[0x801504000,0xbe1000000)` is the whole RAM region (= boot_args phys_base + mem_size) and is not counted. Owners UNKNOWN | the guest can read/write memory that iBoot reserved for a coprocessor. HYPOTHESIS: may be harmless (the natural S231 base already includes the same span); but if one of them belongs to the NVMe/ANS coprocessor, this is the one path here that could reach macOS storage | read the current boot's table and owners (`p.mcc_get_carveouts()`, ADT); then stage-2 unmap them, move `phys_base`/RAM disk, or accept with `--allow L-8`. `tools/check_j274_layout.py` reports each one as `CONFLICT L-8` |
