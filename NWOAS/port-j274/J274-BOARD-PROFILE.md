# J274 board profile vs the reference J313 boot path (port step 0-1)

Date: 2026-11-15. Offline only: no `/dev/cu.*` opened, no launcher run, nothing
in `external/windows-on-m1` executed. Machine-readable copy: `j274-board-profile.json`
(47 rows, same ids). Build/offline success is not boot success.

Path prefixes: `E/` = `/Users/angigyeom/Desktop/DEV/NWOAS/external/windows-on-m1`,
`N/` = `/Users/angigyeom/Desktop/DEV/NWOAS`.
`LOG` = `N/nwoas_scripts/logs/layout-s231-20261115-191038.Vc0lr2` (S231 boot, today 19:10),
`VUART` = same name + `.vuart`,
`ADT` = `N/experiments/tahoe-afk-20260906/d6-adt.bin` (J274 ADT saved 2026-09-06; same
iBoot `mBoot-18000.121.3` as LOG:160-161), parsed offline with our own
`N/m1n1_windows/proxyclient/m1n1/adt.py`.

Status: **FACT** = read from a saved J274 log/dump or our source at the cited place.
**SAME_BY_CODE** = our code writes the same bits; live register not read.
**DERIVED** = computed from FACT inputs with the reference arithmetic.
**PROPOSED** = a draft choice. **HYPOTHESIS** = inferred. **UNKNOWN** = needs a hardware read.

## Scope: which reference files carry J313 constants

| Reference file | J313-specific content |
|---|---|
| `E/guest_layout.py` | default path `config/j313-guest-layout.json` (:10), `cpu_count == 8` (:46-47), `low_mem_ipa >= 0x1000` (:103-104), header symbol `J313_AUTONOMOUS_LAYOUT` (:192) |
| `E/config/j313-guest-layout.json` | all 23 values (:2-24) |
| `E/launch_profile.py` | none board-specific; default display `physical` (:79) is wrong for J274 |
| `E/standalone_image.py`, `E/bootstrap_image.py` | none board-specific (magics `ASIWINGU`/`ASIBOOT0`, 16 KiB alignment, xz, CRC32); "J313" only in docstrings |
| `E/m1n1_windows/src/hv_autonomous_*.c` | uses `J313_AUTONOMOUS_LAYOUT` and J313 golden/schema throughout (`hv_autonomous_runtime.c:46,207,314,352,386-396,113-114`); Type-C power note (`hv_autonomous_profile.c:50-55`) |
| `E/m1n1_windows/src/hv_bootstrap*.c` | none board-specific |
| `E/m1n1_windows/src/hv_launch_j313.{c,h}`, `hv_launch_golden_j313.c` | target id, masks, golden snapshot (RAM, entry, bootargs, regions, MPIDRs, IRQ routes, devices) |
| `E/m1n1_windows/src/hv_vgic.c:47-51`, `hv_irq_routes.{c,h}` | xHCI/DART bases, low-DMA end, IRQ 857 route, synthetic NVMe vINTID 64 |
| `E/m1n1_windows/src/hv.c:201-204`, `hv_agx_*` | AGX route registration is mandatory in `hv_init`; board_id 0x26 / 13.5 gate |
| `E/m1n1_windows/src/dcp.c:601-604`, `display.c:837-840` | J313AP + 13.5 + internal panel only |
| `E/mu/Platform/MacBookAirMid2020Pkg/*` | FD base `0x8510B4000` (fdf:37), generated PCDs (`J313GuestLayout.dsc.inc:6-15`), DSDT (:124-251), MCFG ECAM (:34), WindowsAutoBootDxe (dsc:113-118), J313 ACPI device tables |
| `E/scripts/build-standalone.sh:212-214,271` | builds `MacBookAirMid2020Pkg` only |

## Constants

| Id | What | J313 (ref) | J274 | Status | J274 evidence |
|---|---|---|---|---|---|
| ID-1 | contract target id | `0x3331334a` ('J313') | `0x3437324a` ('J274') | DERIVED | ADT `/target-type`; LOG:122 |
| ID-2 | board-id | `0x26` (`hv_agx_retained_platform.c:454`) | `0x22` | FACT | LOG:123; ADT `/chosen/board-id` = 34 |
| ID-3 | display-handoff gate | J313AP, 13.5, not external (`dcp.c:601-604`) | J274AP, `disp0.external = 1`, OS FW "unknown (mBoot-18000.121.3)" | FACT | ADT; LOG:160 |
| ID-4 | guest surface handoff | internal panel only (`display.c:837-840`) | external HDMI | FACT | ADT; `N/nwoas_scripts/logs/appark-chainload-1700.log:117-118` |
| ID-5 | OS FW gate for GPU | `V13_5` (`hv_agx_retained_platform.c:457`) | Tahoe mBoot-18000.121.3 | FACT | LOG:160-161 |
| MEM-1 | pinned `phys_base` | `0x850000000` | `0x850000000` | PROPOSED | natural base varies `0x83b754000`..`0x83dbdc000` (461 logs) |
| MEM-2 | platform RAM end | `0x9df708000` (golden :55) | `0xbe1000000` | FACT | LOG:134-135, :167, :320; identical in all 461 logs |
| MEM-3 | configured `ram_end` | `0xa00000000` | `0xbe1000000` | PROPOSED | = MEM-2 |
| MEM-4 | golden RAM size | `0x18f708000` | `0x391000000` (no S102 shrink) | DERIVED | MEM-3 − MEM-1 |
| MEM-5 | devtree size | region `0x5c000`, golden size `0x5941c` | `0x5c000` (data 0x58c28) | FACT | LOG:147, :111 |
| SZ-1 | TrustCache size | (implied `0x58000`) | `0x60000` | FACT | LOG:330; ADT memory-map |
| SZ-2 | SEPFW size | – | `0x59c000` | FACT | LOG:329; ADT |
| SZ-3 | preoslog size | – | `0x40000` | FACT | LOG:331; ADT |
| SZ-4 | Mu FD size | `0x1d88000` (golden :67) | `0x1d88000` raw / `0x1e00000` padded | FACT | `N/nwoas_scripts/loader-s223/manifest.json` |
| MEM-6 | `adt_base` | `0x851000000` | `0x851000000` | DERIVED | phys_base + 16 MiB |
| MEM-7 | `firmware_base` = FD base = entry | `0x8510b4000` | `0x8510bc000` | DERIVED | + 0x5c000 + 0x60000 |
| MEM-8 | `boot_args_base` | `0x8533e8000` | `0x853498000` | DERIVED | + 0x1e00000 + 0x59c000 + 0x40000 |
| MEM-9 | ramdisk window | `0x860000000`/`0x40000000` | kept, unused | PROPOSED | our Mu has no `PcdPreloadedRamdisk*` |
| MEM-10 | low-memory alias | IPA `0x100000`, size `0x3ff00000`, backing `0x8a0100000` | IPA `0x0`, size `0x100000000`, backing `0xae0fcc000` | FACT | LOG:348; `usb-s174/guest_map.py:11-15`; `layout-s231/guest_module.py:44` |
| MEM-11 | advertised RAM shrink (S102) | none (`hv_autonomous_runtime.c:265-266`) | `0x3a45dc000 → 0x2a45dc000` | FACT | LOG:350; `guest_module.py:44-55`; `NwoasHideHighRamDxe.c:374-377` |
| MEM-12 | RAM-LOW identity map | not mapped | `[0x800000000,0x801504000)` HW | FACT | LOG:407 |
| MEM-13 | RAM above platform end | not mapped (spec 2026-11-06) | two HW ranges; HDMI scanout at `0xbe3f60000` lives there | FACT | LOG:408-412, :279 |
| MEM-14 | iBoot carveouts in window | 13 ids in `0x860500000..0x86f518000` | ids 46/47/48 (`0x822400000..0x860500000`) + 20 ids in `0x864000000..0x86fe38000` | FACT (Sep dump) | ADT `/chosen/carveout-memory-map` |
| FB-1 | framebuffer | `0x85f000000` 2560x1600 s=10240 | scanout 1280x720 s=5120 at vram `0xbe3f60000` | FACT | LOG:278-279 |
| FB-2 | default display flag | physical | must be none (0x0) | FACT | ID-3/ID-4 |
| IO-1 | emulated NVMe ECAM | `0x690000000` | `0x700000000` | FACT | `guest_module.py:22`; LOG:404; our `MCFG.aslc:46-47` |
| IO-2 | emulated NVMe BAR | `0x400000000` (512 MiB) | `0x700100000` (16 KiB) | FACT | `guest_module.py:22`; LOG:405; our `DSDT.asl:119` |
| IO-3 | NVMe vINTID | 64 (also `hv_irq_routes.h:24`) | 900 | FACT | LOG:356; our `DSDT.asl:122` |
| IO-4 | xHCI base | `0x502280000` | `0x502280000` | FACT | ADT usb-drd1 reg[0]; LOG:398 |
| IO-5 | usb-drd1 IRQ | 857 | 857 | FACT | ADT; our `DSDT.asl:156-158` |
| IO-6 | dart-usb1 | `0x502f00000`/`0x502f80000` | same | FACT | ADT; LOG:205 |
| IO-7 | xHCI low-DMA end | `0x40000000` (`hv_vgic.c:48`) | UNKNOWN (likely tied to MEM-10) | HYPOTHESIS | – |
| IO-8 | vUART | `0x235200000` | `0x235200000`, IRQ 605 | FACT | ADT; LOG:375 |
| IO-9 | /arm-io ranges | from ADT | 11 ranges `0x200000000..0x780000000` | FACT | ADT; LOG:290-300 |
| IO-10 | SPI-HID | present | absent | FACT | ADT |
| CPU-1 | MPIDRs | 0-3, 0x10100-0x10103 | same | FACT | VUART:164-236 |
| CPU-2 | RVBAR regs | from ADT | `0x210150000..0x211350000` | FACT | LOG:339-345 |
| CPU-3 | HACR/MDCR/MDSCR/AMX/ACTLR/keys/APSTS | `hv_guest_cpu_state.c:23-37` | same bits | SAME_BY_CODE | `N/m1n1_windows/proxyclient/m1n1/hv/__init__.py:1911-1953`; raw live values UNKNOWN |
| CPU-4 | cpu_count | 8 | 8 | FACT | VUART:164-236 |
| GPU-1 | AGX IRQ routes | 563-566,579,575-578 | sgx: 563-566,579; 575-578 UNKNOWN | FACT/UNKNOWN | ADT `/arm-io/sgx` |
| GPU-2 | gpu-region / handoff | `0x9fffb8000`/`0x9fff70000` | `0xbfffb8000`/`0xbfff70000` | FACT | ADT; LOG:182-183 |
| GPU-3 | SGX MMIO / broker | `0x204000000` / `0x300000000` | same / outside J274 arm-io | FACT | ADT |
| MU-1 | Mu package | MacBookAirMid2020Pkg | our MacMini2020Pkg | FACT | ref copy of MacMini2020Pkg has no layout include (`E/mu/Platform/MacMini2020Pkg/MacMini2020.dsc:70-73`) |
| MU-2 | layout PCDs | `J313GuestLayout.dsc.inc:6-15` | BootArgs `0x853498000`, ADT `0x851000000`; low window not expressible (UINT32) | DERIVED | our fixed PCDs `0x840000000/0x840004000` (`T810XFamilyPkg.dsc.inc:19-20`) |
| MU-3 | DSDT resources | J313 DSDT :124-251 | ours: COM0, XHC1 857, NVP0 900, real apcie, FL1100 698 | FACT | our `DSDT.asl:119-425` |
| MU-4 | WindowsAutoBootDxe | present | absent | FACT | grep |
| MU-5 | J313 ACPI tables | AGX/ANS/input | not applicable | FACT | – |

## Things that need a hardware read (UNKNOWN list)

| Item | Why it matters | How to read (when the hardware is free) |
|---|---|---|
| Raw ACTLR_EL12 / AMX_CTL on J274 after `hv.init` | golden snapshot for POST_HV_INIT (only masked bits compared) | S231 run with a read-only `p.mrs` dump at the end of `HV.init()` |
| Carveout table on the current boot | MEM-14 was read from a September dump | `p.mcc_get_carveouts()` and ADT `/chosen/carveout-memory-map` on the next S231 boot |
| AGX routes 575-578 owners | only if the GPU stage is ever ported | ADT `/arm-io/gfx-asc` interrupts |
| Whether stage-1 m1n1 heap stays below `0x850000000` | the reference standalone path has no heap ceiling check (grep of `hv_autonomous*.c`; ceiling exists only in assisted `proxy.c:44-57`) | print `heapblock_alloc(0)` before STAGE_BOOT_DATA |
| Whether TrustCache/SEPFW/preoslog sizes stay constant across iBoot updates | pinned `firmware_base`/`boot_args_base` depend on them | compare LOG lines on every macOS update |
