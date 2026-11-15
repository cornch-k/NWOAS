# Gap list: our host-Python duties vs the reference C implementation

Source of the duties: `N/nwoas_scripts/layout-s231/STANDALONE-CENSUS-20261115.md`
table (A) lines 18-32 and the hvcore runtime table lines 86-98. Reference =
`E/m1n1_windows/src/` unless noted. Read-only comparison; nothing built.
"Covered" means the reference has C code doing the same job on J313. It does not
mean that code works on J274.

Verdicts: **Covered** (reuse with J274 data), **Partly** (reference covers part;
the rest is listed), **Different design** (reference avoids the duty another
way), **Not covered**.

## Boot-time duties (census table A)

| # | Our duty (census line) | Reference C | Verdict | J274 gap / note |
|---|---|---|---|---|
| G-1 | HV image upload, `chainload.py -r` (:18) | `boot.bin` on the Asahi ESP; Stage 0 decompresses and chainloads Stage 1 (`hv_bootstrap.c:123-160`, `main.c:150-164`, `hv_stage_role.c:5-24`); image format `E/bootstrap_image.py:34-69` | Covered | needs a boot object/stub decision (`DECISIONS.md`, `dualboot.md`); our D4 bare m1n1 may lack CHAINLOADING (census :59) |
| G-2a | `hv.init` MSRs (HACR/MDCR/MDSCR/AMX/VM keys/APSTS/ACTLR) (:19) | `hv_prepare_guest_cpu_state` (`hv.c:311-341`, `hv_guest_cpu_state.c:23-37`), called right after `hv_init` (`hv_autonomous_runtime.c:316-322`) | Covered | same bits as our `__init__.py:1911-1953` (SAME_BY_CODE) |
| G-2b | map every `/arm-io` range as HW (:19) | `map_arm_io_ranges` (`hv_autonomous_runtime.c:182-202`) | Covered | J274 still needs its own overrides afterwards (G-5) |
| G-2c | `setup_adt`: product names, remove proxy-port USB nodes and CPUs (`__init__.py:2107-2152`; LOG:303-319) | none: ref copies the ADT and edits only `/chosen/memory-map` (`hv_autonomous_runtime.c:231-263`) | Not covered | HYPOTHESIS: removing usb-drd0/atc0/acio0 nodes matters only while a USB proxy owns port 0; must be decided for J274 standalone |
| G-2d | `__OS_LOG` removal from aop/dcp nubs (LOG:333-336, `__init__.py:2255-2258`) | none | Not covered | HYPOTHESIS whether needed (port/boot.md §4.1 row 14,19) |
| G-2e | `load_raw`: guest base, RAM map, carveout unmap (:19) | fixed layout + `map_stage2` maps `[phys_base, effective_end)` only (`hv_autonomous_runtime.c:312-329`); TZ0/1/3 on J274 lie above the RAM end (LOG:168-170) so they stay unmapped automatically | Different design | J274 must add the vram/scanout HW map (L-4) and decide RAM-LOW (L-5) |
| G-2f | BootArgs build, ADT memory-map rewrite, SEPFW/TrustCache/preoslog copy, RVBARs (:19) | `prepare_boot_data` (`hv_autonomous_runtime.c:204-310`), `prepare_secondary_rvbars` (:159-180) | Covered | video fields come from the layout (`:272-277`); J274 needs the HDMI vram base instead (FB-1) and the S102 shrink (G-6) |
| G-2g | ADT upload, SPRR/GXF enable, `hv_start` (:19) | `enter_validated_guest` (`hv_autonomous_runtime.c:129-142`) after four contract checkpoints | Covered | golden snapshot must be rebuilt for J274 (profile ID-1, MEM-4, CPU-3) |
| G-3 | 32 MB payload placement (`compressed_writemem`) (:20) | Mu FD xz-packed behind `ASIWINGU` (`E/standalone_image.py:41-84`), decompressed + CRC (`hv_autonomous_runtime.c:427-441`), copied to `firmware_base` (:233-234) | Different design | our payload = guest m1n1 prefix + FD (`N/nwoas_scripts/loader-s223/manifest.json`, fd_offset 0x21c000); ref has no prefix and enters Mu directly. Dropping the prefix also removes the CPU_START writes that need G-8 (HYPOTHESIS) |
| G-4 | Tahoe DCP/display hook (:21) | `display_prepare_guest_surface` requires internal panel (`display.c:837-840`) and J313AP + 13.5 (`dcp.c:601-604`); virtual display needs a USB host (`hv_configure_fb_stream`, `hv_autonomous_runtime.c:471-477`) | Not covered | start J274 with display=none; Tahoe AFK path stays ours (`display-s190/tahoe_dcp_guest_hook.py`) |
| G-5 | `usb-s174/guest_map.py`: `pcie_init`, FL1100 BAR + APCIE DART BYPASS, USB-C BYPASS, 4 GiB low window (:22; `guest_map.py:7-19`) | low window: `hv_map_hw(low_mem_ipa, low_mem_pa, size)` (`hv_autonomous_runtime.c:326`); xHCI: `hv_vgic_rearm_j313_xhci_trace` hook + DART stage-2 map (`hv_vgic.c:93-133,216-221`); no `pcie_init` call anywhere in the standalone path (grep) | Partly | missing: real-apcie `pcie_init` for FL1100/Ethernet/Wi-Fi; window size/IPA differ (L-1, L-2); BYPASS is equivalent to the HW pass-through left by G-2b |
| G-6 | `guest_module.py` setup: mem_size shrink (S102), carveout read, `nvme_init`, `nvme_set_guest_map`, GPT/GUID check (:23; `guest_module.py:20-60,106-108,160-161`) | `hv_pci_init(ecam, bar, vintid)` → `hv_nvme_init_backend` → `nvme_init()` (`hv_pci.c:189-213`, `hv_nvme.c:332-356`); no shrink (ref keeps backing inside RAM and Mu reserves it via PCD); no GPT/GUID check, no write window (grep of `hv_nvme*.c`; `E/documentation/LIMITATIONS.md:35-36`) | Partly | not covered: S102 shrink (L-3), carveout policy, write window 39944960-59968629, GPT/GUID guard. Our own C already has the window (`m1n1-layout-s231/src/hv_vm.c:1463`, `nvme.c:561-573`) |
| G-7 | `layout-s231/module.py` arm, `nwoas_nvme_fastpath(16, …)` (:24; `module.py:8-14`) | ref arms its own vNVMe on first CC.EN (`hv_nvme.c` `cc_write`) | Different design | our S229/S230 frontend is not in the ref tree; self-arm at HV init is our own small C change |
| G-8 | PMGR PS / CPUSTART hooks + `pmgr_gate.py` (:25) | no PMGR/CPU_START hook in the ref HV (grep finds only `hv_psci.c`, `smp.c`); standalone uses "native C hooks or safe hardware pass-through" (`hv_launch_j313.c:66-70`); PSCI writes CPU_START directly | Different design | the writes come from our guest m1n1 prefix (census :171); ref has no prefix. If we keep the prefix, a C drop-hook is still needed |
| G-9 | NS2 RAM disk / agent, `link_module.py` (:26) | none | Not covered | NS1-only mode (census milestone 4) or our own target-side RAM image |
| G-10 | capture/observe/query modules (:31) | none needed | Not needed | drop from the J274 standalone build |
| G-11 | boot picker entry + stock macOS (:32) | Asahi stub + `install-esp.sh` replace of `m1n1/boot.bin` (`E/documentation/INSTALL.md:179-185`, `E/scripts/install-esp.sh`) | Covered (procedure) | 13.5 vs 26.x stub decision and GPT guard (`dualboot.md` §2.4) |

## Runtime duties (census hvcore table)

| # | Our duty (census line) | Reference C | Verdict | Note |
|---|---|---|---|---|
| R-1 | unhandled MSR passthrough → Python `handle_msr` (:27, :87) | ref still falls back to `hv_exc_proxy(EXC_SYNC)` (`hv_exc.c:2036`) | Not covered | HYPOTHESIS: without a host `uartproxy_run` (`hv_exc.c:327`) blocks; the reboot branch (`hv_exc.c:340-344`) runs only when it returns. Which MSRs reach it on J274 is UNKNOWN (census milestone 3) |
| R-2 | `HV_VTIMER` proxy (:28, :92) | still present (`hv_exc.c:2354`) | Not covered | same as ours; HYPOTHESIS that it does not fire |
| R-3 | SError / panic / stray exceptions → host shell (:29, :95) | still `hv_exc_proxy(EXC_SERROR)` (`hv_exc.c:2034, 2388`) | Not covered | a hostless log/reboot policy is new work in both trees |
| R-4 | ANS shutdown on Windows power-off (:30) | not found in the standalone path (only `nvme_init` in `hv_nvme.c:337`) | Not covered | ref Mu has `AppleAnsExitBootServicesGateLib` (strategy.md §3.3), not read here |
| R-5 | NS2 per-command round trip (:90) | none | Not covered | see G-9 |
| R-6 | guest IRQ/FIQ, vGIC, vtimer injection (:79, :91) | C in both trees (`hv_init` owns vGIC/PSCI, `hv_autonomous_runtime.c:449-450`) | Covered | ours already local |
| R-7 | `hv_tick`, vUART poll (:93) | `map_vuart_from_adt` (`hv_autonomous_runtime.c:331-345`); debug transport bound only in debug profiles (`hv_autonomous_boot_runtime.c:54-70`) | Covered | – |
| R-8 | PSCI CPU_ON (:80) | `hv_psci.c` | Covered | ours already local |
| R-9 | proxy fallback window when a host is present | 3 s USB window + `accept_proxy` (`hv_autonomous_boot_runtime.c:128-157`, `hv_autonomous_profile.c:31-45`) | Covered | matches census milestone 11 "USB proxy fallback window" |

## Summary counts

| Verdict | Boot rows | Runtime rows |
|---|---|---|
| Covered | 6 (G-1, G-2a, G-2b, G-2f, G-2g, G-11) | 4 (R-6..R-9) |
| Partly | 2 (G-5, G-6) | 0 |
| Different design | 4 (G-2e, G-3, G-7, G-8) | 0 |
| Not covered | 4 (G-2c, G-2d, G-4, G-9) | 5 (R-1..R-5) |
| Not needed | 1 (G-10) | 0 |
