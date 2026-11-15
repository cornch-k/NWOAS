# Standalone census 2026-11-15 (read-only workflow; synthesis by Opus)

SYNTHESIS: S231 MacBook-dependency census and standalone plan. I only read files: no edits, no builds, no serial access, and I did not touch the running guest. Nothing below means anything boots.

KEY FACTS I CHECKED
- hv_init and hv_start are reachable only through the proxy calls P_HV_INIT and P_HV_START (m1n1-layout-s231/src/proxy.c:475-482). Nothing in src/*.c calls them. Because of this, the host has to launch the guest.
- Since S230 the NVMe PCI, control and admin paths are handled locally. The Python ECAM and BAR hooks now only raise an error (nwoas_scripts/layout-s231/module.py:23-28).
- Every NS2 command parks all CPUs in hv_exc_proxy (m1n1-layout-s231/src/hv_vm.c:1410-1412).
- Display init is skipped on mBoot-18000.121.3 (m1n1-layout-s231/src/main.c:169-172).
- chainload_load works only when CHAINLOADING is defined. It is stubbed otherwise (m1n1-layout-s231/src/chainload.c:115-146; Makefile:70-71).
- ENABLE_VGIC_MODULE is set in m1n1-layout-s231/config.h:39, not under src/.
- PSCI CPU_ON writes PMGR CPU_START itself, in C (hv_psci.c:371-400). No C hook catches the CPU_START writes from the guest prefix.
- EXECUTION-MODE.md (native-s230) says that "A MacBook provides boot orchestration and runtime services; this dependency remains."

(A) DEPENDENCY TABLE
| Item | Boot/Runtime | Req/Opt | Existing C | Missing work | Risk |
|---|---|---|---|---|---|
| HV image upload (chainload.py -r) | Boot | Req | chainload.c + rust chainload.rs (CHAINLOADING=1) | Stage-1 build with vars; HV on a FAT ESP | Low/Med |
| Guest launch: hv.init MSRs, /arm-io HW maps, setup_adt, load_raw, BootArgs, SEPFW/TrustCache, ADT upload, SPRR/GXF, hv_start | Boot | Req | Primitives only (hv.c:56,152; hv_vm.c) | Whole C "nwoas_boot" port | High (largest port) |
| 32 MB payload placement | Boot | Req | None (Python compressed_writemem) | Append payload to the stage 2 image and copy it in C | Med (heap headroom unverified) |
| Tahoe DCP/display hook | Boot | Req for a screen; headless is a HYPOTHESIS | dcp_iboot.c/afk.c exist but the Tahoe envelope is unsupported (afk.c:323-335, per report) | AFK envelope, iboot commands, HPD, timing, video BootArgs | High |
| usb-s174 guest_map (pcie_init, DMA alias window) | Boot | Req | pcie_init in C (proxy.c:605) | Call it from C with the 6 constants | Low |
| guest_module setup (mem_size shrink, carveouts, nvme_init, nvme_set_guest_map, GPT/GUID check) | Boot | Req | nvme_init, nvme_set_guest_map (nvme.c:580) | C trigger, LOW_BACKING calculation, EFI PART/GUID check | Med |
| module.py arm (fastpath 16) | Boot | Req | s229_enable | Arm itself at HV init | Low |
| PMGR PS / CPUSTART hooks + pmgr_gate | Boot (prefix only) | Req | None | C SPTE_HOOK that drops the prefix writes, or remove smp_start_secondaries from the prefix | Med |
| NS2 RAM disk/agent (link_module) | Runtime | Opt for the desktop | None | NS1-only mode, or a RAM image owned by the target (NEXT.md) | Med (re-qualification) |
| Unhandled MSR passthrough (Python handle_msr) | Runtime | Unknown | Partly C | List the MSRs that reach Python; add C stubs | Unknown |
| HV_VTIMER proxy (hv_exc.c:2388-2391) | Runtime | Unknown | Guest timers are injected in C (:896, :924) | C stub | HYPOTHESIS: likely benign |
| SError/panic/stray exceptions → host shell | Runtime | Req (policy) | Fault printing only | Hostless log/reboot policy | Med |
| ANS shutdown on Windows power-off | Runtime | Req for safety | nvme_shutdown (nvme.c:483) | Call it on CC.SHN | Med (SSD state) |
| capture/observe/query modules | Boot/Runtime | Opt | n/a | Remove from launcher :77-84 and the sha256 guard | Low |
| Boot picker entry + stock macOS | Boot | Req for the goal | Asahi stub flow (external) | Stub APFS in NWSTUB, 1TR registration | High (disk/boot policy) |

(B) MILESTONES (each keeps S231 + MacBook as the fallback)
1. Baseline. Boot S231 as it is today. Save the GPT dump, the log, and the proxy-event counts by type (VM hook, MSR, NS2, VTIMER). Validation: Windows desktop appears, and the counts are recorded.
2. Drop the diagnostic modules (capture, observe, query). Validation: same desktop, fewer NS2 events.
3. Count the MSR and VTIMER proxy traffic. This adds logging to the S231 run only. Validation: the serial log lists every MSR and HV_VTIMER that reaches the host up to the desktop.
4. NS1-only HV variant. Use s229_enable with paired=false and link_lbas=0. Reject nsid 2 and remove nwoas_nvme_link_execute. Also change module.py, which currently asserts link=8448. Validation: the desktop boots, the HV_NWOAS_NVME_LINK count is 0, and the NS1 read/write/reboot qualification passes.
5. Unplug test. On the NS1-only build, unplug the proxy cable after the desktop is up. Validation: Windows stays responsive (USB keyboard) for 10 minutes and a test file survives a reboot. This answers whether any other host trap fires.
6. C PMGR/CPUSTART hook. Remove pmgr_gate.py and the Python map_essential hooks. Validation: the C counter shows about 14 dropped CPU_START writes and Windows reports 8 CPUs.
7. Local NVMe init/arm in C, with the GUID/EFI PART check. Validation: the host makes no nvme_init/guest_map/fastpath(16) calls, and the desktop boots. The HV refuses to arm when the disk GUID does not match.
8. C guest launcher, driven by one proxy call. The host only uploads the HV and payload. Validation: the serial log shows the guest ADT/BootArgs from C, and Windows reaches the desktop (no display, or display hook only).
9. Stub container and chainload. Change NWSTUB to an APFS stub from macOS or 1TR, using Asahi stub logic (Tahoe compatibility unverified). Format the ESP as FAT. Install a CHAINLOADING=1 stage 1 whose stage 2 is the existing proxy-mode HV. Validation: the boot picker shows the stub, and serial shows the "Chainloading"/"File read successfully" lines before the proxy waits for the host.
10. Restore Macintosh HD to stock macOS in 1TR. Validation: the picker boots stock macOS, and the stub still reaches the HV.
11. Hostless stage 2. Stage 2 is the HV, the payload and auto nwoas_boot (from M8), with a USB proxy fallback window. Validation: with the MacBook disconnected, Windows boots headless, writes a marker file from a Windows startup task, and the MacBook reads the file on the next boot.
12. C Tahoe DCP. Validation: HDMI shows UEFI and the Windows desktop with no MacBook.
Goal reached = M10 + M12.

(C) TOP 5 HARDWARE-ONLY UNKNOWNS
1. Does any host trap (MSR passthrough, HV_VTIMER, SError, PMGR PS) still fire after UEFI in S231? If one does, the CPUs park once the cable is gone. M3 and M5 answer this.
2. Does Windows boot headless, without the DCP hook? Today this is backed only by a code comment (tahoe_dcp_guest_hook.py:241-247). Can C reproduce the Tahoe AFK bring-up on this firmware?
3. Does an NS1-only controller (no NS2 in Identify) boot? Nothing in the tree shows this.
4. Can the stub container be built and registered on macOS 26? This includes Asahi stub logic on Tahoe, the kmutil/bputil flags, and the boot object size limit. Note the HV is 2,162,688 B, larger than D4's 2,097,152 B. Can the GPT change happen without disturbing Windows or the S231 constants?
5. Is stage 2 safe across reboots and power-off without host-driven ANS shutdown? This covers the SSD state and the C-side heap headroom for a ~36 MB FAT read plus the copy.

FLAGGED CLAIMS FROM THE REPORTS
- bootchain: "NVMe PCI config + controller registers + admin commands boot-critical host" is outdated. These are local since S230 (module.py:23-28; EXECUTION-MODE.md).
- bootchain: the "2 MiB boot object limit" and the "2x RAM transient" are unsupported.
- bootchain: the Asahi file:line citations come from a scratchpad fetch, not this tree, and I did not verify them. The Macintosh HD restore method is unverified. Whether D4 has CHAINLOADING is unknown.
- storage: "nvme_shutdown reachable only from proxy.c:615 or chainload.c" is incomplete. main.c:200 also calls it (m1n1's own exit path).
- hvcore: the HV_VTIMER risk is real in the code: there is no VTIMER branch in handle_exception, and the only mention is __init__.py:1876. But :2388 reads CNTV_CTL_EL0, and HYPOTHESIS: under E2H that is the EL2 timer rather than the guest's, since the guest's CNTV is EL02 at hv_exc.c:902-924. Not verified.
- modules: "the S131/S132 double-start deadlock" is a hypothesis. The "~14 vs ~62" event counts come from older configs.
- modules/hvcore: the headless boot claim is a hypothesis.
- All of the "missing C work" sizes ("one-line call", "6 constants") are estimates. No port has been built or tested.



## Report: hvcore

STANDALONE-BLOCKER MAP, READ-ONLY (no edits/builds/serial; running guest not touched). Paths: PY=m1n1_windows/proxyclient/m1n1/hv/__init__.py, RG=m1n1_windows/proxyclient/tools/run_guest.py, C=m1n1-layout-s231/src (S231 HV; m1n1_windows/src has same file set). EXECUTION-MODE.md exists only at nwoas_scripts/native-s224/ and native-s230/ (read s230). The S207 README census predates S230, so the NVMe rows below were re-checked against S230/S231 module.py.

FACT, HOST AT BOOT (all before hv.start; every item is the MacBook issuing proxy calls over USB)
1. HV image upload and chainload: launcher chainload.py -r (layout-s231-guest-test.sh). Not in hv/__init__.py.
2. DCP/display boot (--pre-init-script tahoe_dcp_guest_hook.py, run RG:53-54): Python AFK client, rewrites u.ba.video. Largest Python-only setup piece. hv.start() also calls pre_guest_start() and fb_shutdown/fb_improve_logo (PY:2418-2431).
3. hv.init() (PY:1842-1955): p.hv_init() runs C hv_init (hv.c:56) for PSCI, vGIC, stage-2 root, ECV timers. Python then sets handlers, maps every /arm-io range as HW (add_tracer OFF), writes HACR/MDCR/AMX/VM keys, map_vuart, and edits the ADT (setup_adt: product names, removes USB nodes for the proxy port).
4. Guest memory layout, bootargs, payload: load_raw (PY:2186-2295) computes phys_base from heap_top and adds 16 MB slack. It maps RAM-LOW/RAM-HIGH as HW identity and unmaps carveouts. It uploads the 32 MB payload with compressed_writemem and copies SEPFW/TrustCache/preoslog. It rewrites the ADT memory-map, builds BootArgs and writes the secondary-CPU RVBARs. The payload contains the guest m1n1 prefix, so "loading the prefix" is just this upload, entry = base+0x800. RG:98-99 --raw selects load_raw. RG:103 resets the PMU panic counter.
5. Stage-2 page tables: built by C (hv_vm.c). Python only decides the policy via p.hv_map in pt_update/map_hw/map_sw/map_hook_idx (PY:242-290, 381-452). Further S231 policy comes from guest_map.py (low-4GB window map_hw, BYPASS tracers, pcie_init) and guest_module.py (S102 mem_size shrink written into BootArgs, ECAM/BAR hook tracers).
6. Last upload: ADT blob to adt_base (PY:2414-2416). Then SPRR/GXF enable, hv_start(entry, bootargs) (PY:2440, 2465).
7. vGIC and vtimer setup: C only (hv.c:111-190, hv_vgic.c:2052-2067 map the dist/redist/ITS as SPTE_HOOK; config.h:39 defines ENABLE_VGIC_MODULE). Python does nothing for vGIC.
8. CPU start/PSCI: C (hv_psci.c, hv.c:294). Python handle_smc (PY:1135) is a duplicate that is not on the live path.
9. -m modules at boot: pmgr_gate.py replaces hv.start_secondary with a logger. layout-s231/guest_module.py builds the Python NVMe model and GPT guard. link_module.py builds the NS2 RAM FAT. layout-s231/module.py arms the target controller (p.nwoas_nvme_fastpath(16,...)). query_module.py and observe.py are observation/query.

FACT, HOST AT RUNTIME (reached via hv_exc_proxy, which does hv_rendezvous on all CPUs and then blocks in uartproxy_run, hv_exc.c:81-127)
| Path | Where handled now | Standalone status |
|---|---|---|
| PMGR PS hooks (UART0/ATC0 + parents), pg_overrides, CPUSTART write hook (PY:1966-2087) | Python proxy hooks. S207 saw about 62 events, all in the guest m1n1 prefix (about 1 s). | No C implementation exists. S207 §4.3 sketches an SPTE_HOOK port. Prefix-only. |
| Unhandled guest MSR/IMPDEF sync | C hv_handle_msr_unlocked (hv_exc.c:966) and hv_handle_msr (:1764), then hv_exc_proxy(EXC_SYNC) (:2060) to Python handle_msr passthrough (PY:746-1130). SMC and WFI are fully C (:1900-2005). | Partly C. Which MSRs fall through is unverified (hypothesis). S207 saw no shell fallback. |
| Data abort on mapped hooks | C hv_vm.c dispatch. SPTE_HOOK stays in C. SPTE_PROXY_HOOK_R/W calls nwoas_nvme_fastpath_mmio first (:3822, :4079), then hv_exc_proxy(HV_HOOK_VM) (:3832, :4087). | Python hooks run from handle_vm_hook (PY:681). |
| NVMe ECAM 0x700000000 and BAR 0x700100000 | S230 module.py:25-28 re-registers both as host HOOKs that raise RuntimeError("unexpected host NVMe read/write"). Control/admin/I/O queue and IRQ 900 are C (module.py:29, hv_vm.c fastpath, hv_exc.c:489). | No longer per-trap host in the S231 config. A host read/write there would abort the run. |
| NS2 RAM disk and agent (HV_NWOAS_NVME_LINK, hv_vm.c:1393-1412) | Every NS2 command round-trips to Python _fast_link_handle (link_module.py:61-95) via handle_nwoas_nvme_link (PY:1537). | Per NEXT.md:5-9 this is the remaining NVMe host dependency. NEXT.md:39-43 keeps the DCP, vIRQ model and Windows drivers as separate milestones. |
| Guest IRQ/FIQ | C only (hv_exc_irq with ENABLE_VGIC_MODULE). The #else path would proxy (hv_exc.c:2271-2279). | Local. |
| Virtual-timer FIQ | hv_exc.c:2388-2391 calls hv_exc_proxy(HV_VTIMER) when CNTV is armed and expired. Python registers a handler (PY:1859) but handle_exception has no VTIMER branch, so it would return unhandled and drop into the shell. | HYPOTHESIS: in normal runs this does not fire, since the guest timer is injected by C (hv_inject_timer_ppi, :350). Not verified. Needs a C stub if it can fire. |
| hv_tick, every 5 kHz | hv.c:445-470. It only enters the proxy if the host sent a byte ("!" Ctrl-C) or runs hv_vuart_poll and nwoas_nvme_fastpath_poll. | C. The host is not needed unless it interrupts. |
| WDT | wdt_cpu is None (PY:2437). No hv_wdt_start. hv_wdt.c only forwards a bark to the proxy. | Disabled. |
| SError / PANIC / stray exceptions | hv_exc_serr (hv_exc.c:2441) and hv_exc_proxy(EXC_SERROR). The HV gets a guest HVC#0 or an unhandled exception and the host prints the context and stops (PY:1386-1456). | The host is the debugger/shell. Standalone needs a reboot/log policy in C. |
| hvcalls | PY:1294-1312 (BRK #0x4242, hvcall_handlers) is a hypervisor-debug feature, unused here. | Not needed. |
| Bugcheck probe (HVC #0x124) | C (nwoas_bugcheck_handle, hv_exc.c:1881). | Local. |
| MMIO trace events (EVT_MMIOTRACE/IRQTRACE) | Async host events (hv_vm.c:969, hv_aic.c:23), logging only. | Not needed standalone. |

C implementations that could run hostless (all exist already): PSCI (hv_psci.c), vGIC (hv_vgic.c), vuart (hv_vuart.c), NVMe fastpath/admin (hv_vm.c, nvme.c), WDT (hv_wdt.c, unused), FL1100 USB/AIC bridges (hv_exc.c). The pieces with no C version are:
- page-table policy, memory layout and the ADT/BootArgs edit, whose output is data only the host currently writes;
- payload placement;
- the DCP/display boot;
- the PMGR/CPUSTART hooks;
- the NS2 service;
- Python MSR passthrough;
- hv_exc_proxy itself.

The transport blocker is hv_exc_proxy. A host that disappears parks all 8 CPUs at that call. Only per-trap NS2 commands and the rare unhandled MSR/PMGR/CPUSTART/SError paths still reach it.

HYPOTHESES / UNVERIFIED
- The exact set of MSRs reaching Python handle_msr in this config is unknown (the S207 logs show none after UEFI, but they may be unlogged).
- The ADT "/cpus" cpu_impl_reg RVBAR writes and fixed layout constants (S102 shrink, low window) look portable to C, but I did not check that.
- Whether the HV_VTIMER proxy path can fire under S231 (see the table).
- The census numbers come from the older S200/S202 config, not the S231 launch.

## Report: modules

NWOAS S231 host-module census (read-only; nothing run, edited or built)

SUMMARY TABLE
| Module | Class | Runs on host at runtime? |
|---|---|---|
| display-s190/tahoe_dcp_guest_hook.py (pre-init) | REQUIRED-BOOT for a visible display, not for headless boot (hypothesis) | No. Setup only, plus a 5 s hold before guest entry |
| cpufreq-s193/capture.py | OPTIONAL-DIAG | No |
| cpufreq-s189/observe.py | OPTIONAL-DIAG | Once, inside an NS2 request handler |
| usb-s174/guest_map.py | REQUIRED-BOOT | No. It uses BYPASS tracers, which never trap to the host |
| smp-s133/pmgr_gate.py | REQUIRED-BOOT | The host hook it patches fires about 14 times, only in the guest m1n1 prefix |
| layout-s231/guest_module.py | REQUIRED-BOOT for its setup part; the Python NVMe model is no longer served at runtime | No (see below) |
| layout-s231/module.py | REQUIRED-RUNTIME (replaces the Python control path) | No. Its HOOK tracers only raise an error if hit |
| memory-s180/link_module.py | REQUIRED-RUNTIME for NS2; NS2 itself is not needed for the desktop | Yes, on every NS2 command |
| native-s230/query_module.py | OPTIONAL-DIAG | Yes, a poll at most once per second inside NS2 handling, only when NWOAS_TARGET_QUERY=1 |

FACT (host-runtime hooks that stay in the loop)
- Every host callback after guest entry is a synchronous hv_exc_proxy round trip with all CPUs rendezvoused (native-s207/README.md §1). The MacBook dropping out at that moment parks all CPUs.
- There are two classes. The Python HOOK tracers and the PMGR/CPUSTART hooks that map_essential installs are in hv/__init__.py:1966-2087. The NWOAS_NVME_LINK handler is in hv/__init__.py:1537-1558.
- The PMGR/CPUSTART hooks are not installed by any -m module. They are REQUIRED-BOOT host Python, and pmgr_gate.py only patches one of them.

PER MODULE

1. display-s190/tahoe_dcp_guest_hook.py
- At load (before hv.init) it boots the DCP from the host. It runs ASC.boot, then the RTKit management start and wait_boot, then AFK endpoint 0x23, and waits for the disp0-service announcement (:114-151). It waits for HPD and the mode count (:159), picks a timing (:161), and rewrites u.ba.video geometry (:72-95, :163-165).
- It also asserts that the HV image carries the NWOAS-DCP-DEFER marker (:119-123).
- It installs hv.pre_guest_start = configure_tahoe_display and hv.preserve_framebuffer = True (:278-279).
- configure_tahoe_display runs from hv.start(), after the ADT upload and before p.hv_start. It does a DART iotranslate check, writes colour bars, and sends commands over the AFK link. The commands are setPower, set-mode (6), swap-start (15), set-layer (16) and swap-submit (18) (:168-214). It waits for the HDMI hotplug cycle (:216-249) and holds 5 s (:265-271).
- It installs no runtime callback; only the python objects are kept alive via hv._nwoas_dcp_handoff (:277). After guest entry the DCP is not serviced by the host. This is a fact from the code. Its effect on Windows is HYPOTHESIS.
- Without it: m1n1-layout-s231/src/main.c:169-170 skips display_init() on this firmware, so nothing scans out.
- HYPOTHESIS: the guest boots headless over vUART and NVMe. The hook's own comment at :241-247 says serial-only overnight tests exist. A screen needs this hook.
- C equivalent: none for this firmware. The generic m1n1 sources exist (src/dcp_iboot.c, src/afk.c, src/display.c). But src/afk.c:323-335 prints that the measured Tahoe envelope is unsupported by the C path.
- Missing C work: the Tahoe AFK envelope, the iboot command set, HPD wait and settle, timing selection (choose_timing), the BootArgs video rewrite, and the 5 s hold.

2. cpufreq-s193/capture.py
- Read-only. It runs once at -m load time, before guest entry. It does p.read64 on the E and P cluster registers at 0x210e00000 and 0x211e00000 plus offsets (:14-25), then hv.log and writes NWOAS_LOG.cpustate.json (:26-29).
- It has no hooks, and the boot does not read its output. Classification is OPTIONAL-DIAG.
- C equivalent is not needed. P12 selection and RTC are done by UEFI at ReadyToBoot (native-s207/README.md §3).

3. cpufreq-s189/observe.py
- It wraps hv.nwoas_nvme_link_handler (:4-18). On the first NS2 request after hv._nwoas_link.connected, it does 4 x p.read64 (the P and E command and status registers) and writes native-p12.json. It is one-shot, and every later call passes straight through to the base handler.
- It must be loaded after link_module, because link_module installs the handler it wraps. Classification is OPTIONAL-DIAG.

4. usb-s174/guest_map.py
- p.pcie_init() runs in C (proxy.c:605).
- BYPASS tracers cover the FL1100 BAR (0x6c0000000+0x100000), the APCIE DART (0x681000000+0x3000000) and the USB-C controller 0x502280000+0x10000 (:8-9, :19). BYPASS has no Python handler (hv/__init__.py:347).
- hv.map_hw(window_base, backing, window_size) maps the 4 GiB low DMA alias window to the top of guest RAM (:11-16).
- The map is applied in pt_update(). The same backing is what guest_module.py removes from the guest's RAM (guest_module.py:44-55).
- Without it there is no USB-A keyboard or trackpad, and the DMA alias window is missing. The window is needed by the DART/HideHighRam logic in UEFI (guest_module.py:37-42). Classification is REQUIRED-BOOT.
- Missing C work: only a trigger. The policy is the 6 constants in the table in native-s207/README.md §3, i.e. a one-line call at HV setup.

5. smp-s133/pmgr_gate.py
- It replaces hv.start_secondary with a logger (:18-30).
- The guest m1n1 prefix's smp_start_secondaries() writes PMGR CPU_START (14 writes in about 1 s). The host HOOK cpustart_wh from map_essential (hv/__init__.py:2056-2087) normally calls hv.start_secondary at the RVBAR.
- The gate makes that a no-op, so APs stay parked until Windows issues PSCI CPU_ON. That is handled in C (hv_psci.c).
- The cause given in the gate's docstring was corrected in native-s207/README.md §4.2: the writes come from the prefix, not from Windows.
- Without it, the APs start before UEFI. HYPOTHESIS: this is the S131/S132 double-start deadlock.
- Classification is REQUIRED-BOOT.
- The C equivalent is not implemented. I found no PMGR or CPU_START hook in src/hv.c, hv_vm.c or hv_psci.c. native-s207/README.md §4.3 gives the exact semantics for a C hv_map_hook that drops these writes.
- Alternative route, HYPOTHESIS: remove smp_start_secondaries() from the guest prefix (m1n1_windows/src/payload.c:317-318). That needs a prefix rebuild.
- The same §4.3 also covers the PMGR PS hooks (the wh/rh pair, and the CPU STATE hook).

6. layout-s231/guest_module.py (NVMe host part)
- Its boot-time setup is REQUIRED-BOOT:
  - It shrinks BootArgs mem_size to exclude the window backing (:46-55).
  - It builds the carveout table (:25-30).
  - It calls p.nvme_init() (ANS2) and p.nvme_guest_map() (:106-108).
  - It checks the GPT identity of NS1 (:160-161).
  - It builds the Controller, GptGuard and namespace objects (:188-190).
  - It exports hv._nwoas_nvme = (c, memory, nsbuf) (:277).
- Its ECAM/BAR HOOK tracers (:275-276) are replaced by module.py's _unexpected_* handlers, keyed on the same ranges. The Python controller is therefore not served at runtime (module.py:23-27). It remains only as the container for NS2, which link_module needs.
- It also starts a proxy-watchdog thread (:89-105, diagnostic).
- GPT blocks: C refuses writes outside LBA 39944960-59968629 at hv_vm.c:1463. The nvme.c version of this check (nvme.c:561-573) additionally allows the GPT blocks. I did not trace which one governs the S230 path, so the GPT-write behaviour under S230 is unverified.
- C equivalents: nvme.c (ANS2 init and nvme_guest_map), hv_vm.c:1427-1730 (the fast path), and src/nwoas_frontend/*.c (frontend, admin_*, model) plus src/nwoas_s229_adapter.inc (the PCI/control/admin front end).
- Missing C work: the mem_size/BootArgs shrink (BootArgs are built in Python: load_raw, hv/__init__.py:2186), the carveout and GPT-identity checks, and the init trigger.

7. layout-s231/module.py
- At load it requires the Python controller state to be pristine and the S231 policy constants to match: block counts 61279344 and 8448, write window 39944960-59968629, mdts 8 (:5-11).
- Then it calls p.nwoas_nvme_fastpath(23) (capability check) and p.nwoas_nvme_fastpath(16, link_blocks, mdts) (:12-15). This is what makes the target own the PCI/control/admin path.
- It needs hv._nwoas_nvme (from guest_module) and _controller.ns.link (from link_module). So it fails without both (:4-6).
- Classification is REQUIRED-RUNTIME. Its Python hooks must never fire, since they raise if they do.

8. memory-s180/link_module.py
- It builds the NS2 FAT RAM disk from about 25 tool files (:23-47) and wraps controller.ns in CachedPair (:48-58).
- It installs hv.nwoas_nvme_link_handler = _fast_link_handle (:97). C sends every NSID 2 command here (hv_vm.c:1441, :1506, :1412). Each one is a host round trip. The handler validates the request, executes it against the RAM image, and writes PRP data into guest RAM through a host copy.
- Without it, hv/__init__.py:1543-1544 only logs and does not set a response, and hv_vm.c:1420-1424 then rejects the request. What the guest sees is HYPOTHESIS. NS2 is only for the NWOAS agent, not for the desktop (native-s207/README.md §2-3).
- C equivalent: none. native-s230/NEXT.md is a plan only. The missing work is a target-owned RAM image, FAT writes, the LBA 127/128/129-145 mailbox and the overlay, with differential tests.

9. native-s230/query_module.py
- Active only when NWOAS_TARGET_QUERY=1 (:74-76), which the launcher sets.
- It wraps the NS2 handler. At most once per second it reads target-query.json from the link directory. Only actions 3-10 and 17-23 are allowed (:27). It calls p.nwoas_nvme_fastpath(a) read-only (:51-54) and writes target-query-result.json. It lengthens that NS2 call.
- Classification is OPTIONAL-DIAG.

MINIMUM HOST SET (BOOT PATH), BY ELIMINATION
- REQUIRED-BOOT: usb-s174/guest_map.py, smp-s133/pmgr_gate.py, and guest_module.py's setup part.
- REQUIRED-RUNTIME: module.py (and link_module.py, which it requires; it is wired in because module.py reads _controller.ns.link at :6).
- Visible display: the display hook.
- OPTIONAL-DIAG: capture.py, observe.py, query_module.py.
- To drop observe, query or capture, remove them from the launcher -m list (layout-s231-guest-test.sh:77-84) and from its sha256 guard lines.
- Not covered by the -m list but still host-side: map_essential PMGR hooks and cpustart_wh, load_raw payload upload and ADT rewrite (hv/__init__.py:2186-2295), GptGuard-validated GPT writes, and the chainload of the HV image over USB.

NOT VERIFIED
- I did not run anything. Whether Windows boots without any given module is inference from the source and the earlier logs, not a hardware test.

## Report: storage

Under S231 the NVMe data path (read, write, flush, doorbells, CQE publishing, IRQ level) already runs in target C. NS2 is a host round trip, and the ANS bring-up, the guest-RAM window registration and the arming sequence are still triggered by host Python. Windows has no native ANS driver, only unbuilt design docs. Everything below is from read-only inspection of the source; nothing was run or built.

## FACT: what runs in target C today

| Piece | Where | Evidence |
|---|---|---|
| I/O SQ1/CQ1 doorbells (0x1008, 0x100c), command fetch, CQE write with phase published last | C | `hv_vm.c:1478-1560` (process), `hv_vm.c:1670-1740` (mmio), hooked from the abort path at `hv_vm.c:3822` and `:4079` |
| NS1 read/write/flush, LBA range check, write window 39944960-59968629 | C | `hv_vm.c:1430-1476`, `nvme.c:561` and `:621` (`nvme_rw_guest`) |
| Guest PRP translation | C | `nvme.c:590` and `:621`; it needs the host-set map described below |
| Synthetic PCI config, controller registers (CC/CSTS/AQA/ASQ/ACQ), admin queue | C since S230 | `nwoas_s229_adapter.inc` (`s229_mmio`), `src/nwoas_frontend/*` |
| Admin commands: Identify, Create/Delete queue, Get/Set Features, Get Log Page, AER (parked), Set Features 6 (volatile cache) | C | `admin_state.c`, `admin_payload.c`, `admin_ring.c` |
| IRQ 900 level and injection, with the EOI gap | C | `hv_exc.c:447-506` (`hv_bridge_nvme_irq`) |
| CC shutdown (SHN) writes cancel I/O and flush | C | `s229_mmio` calls `s229_control_local(0,...)` and the flush callback calls `nvme_flush` |

## FACT: what still needs the host

- **NS2** (the 8448-block RAM disk and agent mailbox)
  - `hv_vm.c:1388-1425` (`nwoas_nvme_link_execute`) calls `hv_exc_proxy(HV_NWOAS_NVME_LINK)`, which parks all CPUs until the MacBook answers.
  - `link_module.py:_fast_link_handle` serves the data. The NS2 image, `NamespacePair` and `LinkNamespace` live in host Python, and `NEXT.md` confirms every NS2 command goes through the MacBook.
- **Advertising NS2**
  - `s229_enable` passes `paired=true` in the identity policy, so Identify List (CNS 2) and Identify NS (CNS 0) expose namespace 2 (`admin_payload.c`).
  - `s229_enable` also rejects `link_lbas==0`, so passing 0 for the link size fails.
- **ANS bring-up**
  - `guest_module.py` runs `p.nvme_init()` and then `p.nvme_guest_map(low_backing, high_min, high_max)`, which reach C through `proxy.c:612` and `:635`.
  - `nvme_guest_page_pa` returns 0 until the host has registered that map, so all PRP and queue translation depends on it.
  - `LOW_BACKING` and the `mem_size` shrink are computed in the host module's `guest_module.py`.
- **Arming**
  - `s229_enable` is triggered by `layout-s231/module.py` through `nwoas_nvme_fastpath(16,...)`.
  - That module also requires `_link.block_count==8448` and aborts otherwise.
- **PROTECTED zone policy and GptGuard**
  - These exist only in host Python (`guest_module.py`: `PROTECTED[]`, `GptGuard`, `WindowWritableNamespace`).
  - The Python checks that the on-disk GPT is intact and that the target disk has the expected GUID before publishing the namespace.
  - The C write window is hard-coded in `hv_vm.c:1191-1192` and `nvme.c`.
- **Python fallback handlers**
  - ECAM and BAR are still HOOK tracers, but they only raise "unexpected" errors.
  - After S230 the old `Controller` is only an NS2 container.
- **Error and AER**
  - AER is parked in C and is never completed.
  - A fatal backend failure sets CSTS.CFS in C (`nwoas_frontend_fault`).
  - Nothing reports the failure to the host.
- **ANS shutdown**
  - `nvme_shutdown` is reachable only from `proxy.c:615` or `chainload.c`.
  - No Windows power-off path calls it.
  - `nvme_ensure_shutdown` is commented out (`nvme.c:483`).

## FACT: Windows ANS driver

- No native ANS driver exists in `windows_drivers`. It holds only design docs and hardware headers, namely `nvme-driver.md`, `nvme_hw.h`, `rtkit-driver.md` and others. Its own README says nothing has ever been compiled, and there is no WDK.
- `AppleNANDStorageDxe.c` is a UEFI-side driver design, and it states that Apple's NVMe is not standard.
- I found no AppleWOA ANS driver in the tree.
- A native driver would need RTKit, SART, NVMMU and linear SQ, and it would have to coexist with the HV's ANS state. It would not work as a Windows driver without the HV.

## HYPOTHESIS: minimal C changes to run the NVMe path in the HV without NS2

Ordered smallest first.

1. **Drop NS2 from the frontend.**
   - Add a mode to `s229_enable` that takes `paired=false` and `link_lbas=0`.
   - Remove or reject `nsid==2` in `nwoas_nvme_fastpath_execute` (`hv_vm.c:1442`) and delete `nwoas_nvme_link_execute`, so no code calls `hv_exc_proxy`.
   - The `link_busy` state can then go too. Local I/O never reaches NS2 afterwards.
   - The `NEXT.md` plan (a target RAM disk) is the option that keeps NS2 for the agent, and it is larger.
2. **Local init.** Call `nvme_init()` from C at HV start (`chainload.c:123` already does this at the chainload stage). Also call `nvme_set_guest_map` from C. The map values are plain constants, but `LOW_BACKING = ba.phys_base + mem_size - 4G - 0x34000` and the `mem_size` shrink must be ported (6 constants, per `native-s207/README.md`).
3. **Self-arm.** Run `s229_enable` on the first BAR access, or at HV init, with no host call.
4. **GPT policy.**
   - C already refuses writes outside the Windows zone (`NWOAS_NVME_SC_WRITE_RO`), so protection is fail-closed.
   - A local equivalent of the GptGuard checks (primary-GPT and PROTECTED-entry comparison) is optional.
   - The startup GUID and `EFI PART` check should be ported, or the HV would boot against any disk.
5. **Shutdown and error.**
   - On the CC shutdown write, also call `nvme_shutdown` or an ANS clean stop.
   - Complete AER with a CFS or log event.
   - Add a CSTS fault path that does not need the host.
6. **Not storage, but needed for standalone.** The HV image upload, the 32 MB payload load, BootArgs and ADT rewrite, and the DCP boot are all host setup (`native-s207/README.md` section 3). Removing NS2 alone does not make the boot standalone, which `NEXT.md` also says.

## HYPOTHESIS and risks

- The NS2 agent tools (NWOS.EXE and the others) and the Cinebench delivery use NS2. Dropping it removes the MacBook agent disk entirely.
- Dropping NS2 requires re-qualifying S231 on hardware. Nothing in this tree shows that an NS1-only configuration has been booted.
- Paired mode is baked into several places, including the `CachedPair` wrapper in `link_module.py`, so removing it from only one module would leave Identify and queue behavior inconsistent.

Files read: `/Users/angigyeom/Desktop/DEV/NWOAS/m1n1-layout-s231/src/hv_vm.c`, `nvme.c`, `nwoas_s229_adapter.inc`, `nwoas_frontend/*`, `hv_exc.c`, `proxy.c`, `chainload.c`; `/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/native-s230/NEXT.md`, `native-s207/README.md`, `layout-s231/guest_module.py`, `layout-s231/module.py`, `memory-s180/link_module.py`, `layout-s231-guest-test.sh`; `/Users/angigyeom/Desktop/DEV/NWOAS/windows_drivers/*`.

Not read: `m1n1_windows/proxyclient/m1n1/hv/__init__.py` (I relied on the `native-s207` README's description of it) and `EXECUTION-MODE.md` (it does not exist at `nwoas_scripts/`). Line numbers for the Python modules are not given; they are cited by name.

## Report: bootchain

STANDALONE BOOT + DUAL BOOT ANALYSIS (read-only; nothing edited, no /dev/cu.* opened, guest untouched). Paths are under /Users/angigyeom/Desktop/DEV/NWOAS unless noted. Asahi facts were read from raw GitHub source of AsahiLinux/asahi-installer (src/stub.py, osinstall.py, m1n1.py, main.py, step2/step2.sh), saved in the scratchpad.

BOTTOM LINE
Loading the HV+payload from disk is feasible with existing m1n1 machinery (Asahi-style stage1 -> chainload from ESP). Running the guest without the MacBook is NOT feasible yet. Today's HV is only a C library of primitives. The entire "run guest" logic and most exception handling live in host Python. That is the real blocker, not storage of the images.

(1) LOADING HV + PAYLOAD FROM DISK

FACTS
- m1n1 supports appended payloads and variables. src/payload.c:158-161 walks _payload_start via load_one_payload (gz, xz, fdt, cpio, kernel, sig, initramfs, logo, SEPFW magics at payload.c:~150-260). payload_run() runs from main.c:119-121 before the proxy fallback.
- Asahi-style chainload from disk is in the tree. payload.c:163-164 calls chainload_load(chainload_spec) when a "chainload=<partuuid>;<path>" variable is appended to the image. m1n1 variables are text lines + 4 zero bytes appended after m1n1.bin (Asahi m1n1.py build(): data + "var\n"... + "\0\0\0\0").
- rust/src/chainload.rs:34-85 implements it: nvme init, GPT lookup by PARTUUID, FAT fs (fatfs crate) on that partition, read the whole file into a heap Vec. It works from m1n1's own NVMe driver, so no OS is needed.
- It exists only if built with CHAINLOADING=1. Makefile:70-71 and chainload.c:117-146 show it is stubbed ("Chainloading files not supported") otherwise.
- chainload_image() (chainload.c:23-110) treats the file as an m1n1-format raw image: copies it, appends vars, copies SEPFW, builds boot args, jumps to the new image at +0x800 (chainload.c:105 area). So stage 2 must be a raw m1n1.bin (HV build with payload appended is fine).
- Layout already reserves a 512 MiB ESP at LBA 39944960-40076031 (layout-s231/manifest.json:54), inside the Windows write zone (nvme.c:543 NVME_WRITE_FIRST_LBA=39944960).
- Sizes: HV image 2,162,688 B and payload 33,669,120 B (layout-s231-guest-test.sh checks). Total ~35.8 MB fits easily on the ESP. It is read in one go into heap (Vec) and then malloc'd again in chainload_image (needs about 2x RAM transient; unverified headroom).
- The installed D4 boot object is a "bare" m1n1 (NWOAS-STATUS-2026-09-06.md:478-485), registered with `kmutil configure-boot` in 1TR, raw payload 2,097,152 B.

HYPOTHESES
- The installed D4 build probably does NOT have CHAINLOADING=1 (or has no chainload var). No evidence either way; I could not find the D4 binary in the tree (m1n1-nwoas-bare-DCPFIX.bin at nwoas_scripts/ does not exist; copies are at ../ level, e.g. /Users/angigyeom/Desktop/DEV/NWOAS/m1n1-nwoas-bare-DCPFIX.bin, not inspected). Check with `strings | grep "Searching for partition UUID"`.
- The 2 MiB limit on the boot object (D4 is exactly 2,097,152 B) may constrain stage1; stage 2 on ESP avoids it, which is exactly why Asahi splits stage1/stage2.

THE HARD BLOCKER: no standalone "hv start" path in C
FACTS
- hv_start(entry, regs) (src/hv.c:152) and hv_init() (hv.c:56) are reachable only via proxy calls P_HV_INIT/P_HV_START (src/proxy.c:475-482). No C caller exists (grep of src/*.c).
- Host Python does all setup in HV.init()/load_raw()/start() (m1n1_windows/proxyclient/m1n1/hv/__init__.py:1842-1955, 2186-2295, 2402-2465): HCR/HACR/MDCR/AMX/VM-key MSRs, map every /arm-io range as HW, ADT rewrite (setup_adt 2107), memory map, BootArgs build, 32 MB payload copy, SEPFW/TrustCache copies, map_essential PMGR/CPUSTART hooks (1966-2090), pt_update, ADT upload, framebuffer handling, SPRR/GXF enable, then hv_start.
- Other per-boot host steps: Tahoe DCP bring-up over AFK in Python (display-s190/tahoe_dcp_guest_hook.py:114-275, listed as the largest setup port in native-s207/README.md section 3), and the 8 -m modules (pcie_init, nvme_init, guest map, PMGR gate, NVMe model).
- After guest entry, every unhandled trap is a synchronous round trip: _hv_exc_proxy rendezvouses all CPUs and blocks in uartproxy_run (hv_exc.c:81-127). Host-owned runtime pieces per native-s207/README.md section 3: PMGR/CPUSTART hooks (~55 round trips in guest m1n1 prefix), NVMe PCI config + controller registers + admin commands (336 boot-time lines, open-ended at runtime, boot-critical), NS2 RAM FAT/worker (native-s230/NEXT.md, not needed for desktop), GPT write guard (keep). MacBook absent = all CPUs parked.
- Already local in C: vGIC, timers, PSCI CPU_ON, vUART, I/O queue 1 fast path, IRQ 900, FL1100/xHCI helpers (README s207 table). native-s230/NEXT.md says S230 owns synthetic PCI/control/admin in target C, with only the NS2 RAM disk (8448 x 4096 B = 34,603,008 B) still host-forwarded. This is newer than the s207 table; I did not verify S230 C source line-by-line, so check how far "admin/PCI local" actually is (HYPOTHESIS: layout-s231/module.py "target-C NVMe control" may mean NVMe admin is already local, which would remove blocker (b) below).
- native-s230/NEXT.md states plainly: "Removing NS2 data transfers alone is not standalone boot." EXECUTION-MODE.md was not found at nwoas_scripts/EXECUTION-MODE.md (only native-s230/EXECUTION-MODE.md exists; I did not read it).

HYPOTHESIS: required work for a standalone stage 2, in dependency order
1. Build HV with CHAINLOADING=1 and make a stage1 (new bare m1n1 + `chainload=<ESP partuuid>;m1n1/stage2.bin` var). Harmless to test first as a pure chainload of the existing HV (no payload) to prove disk loading works; the proxy still takes over after.
2. Write a C "nwoas_boot" step in the HV (called from payload_run/main, after nvme init) that replicates HV.init/load_raw/start: ports of setup_adt, memory map/ADT edits, bootargs, payload copy from stage-2 appended blob (payload already read from FAT) to guest RAM, stage-2 mappings (guest_map.py constants "6 constants, mechanically portable"), map_essential PMGR/CPUSTART as C hooks (README s207 sec 4 says semantics are portable), SPRR/GXF, then hv_start(). This is the big port.
3. Port Tahoe DCP init (AFK client in Python) to C or defer (display_init skipped on Tahoe, NWOAS-DCP-DEFER main.c:167-172). Without it there is no video. This is the largest setup-time port.
4. Make all remaining runtime hooks local or remove them (README s207 sec 6; NS2 NEXT.md). Otherwise a missing host parks CPUs. Cable-drop resilience is unverified (README s207 sec 7).
5. Keep a fallback: leave `uartproxy` reachable (chainload=absent or a boot-time USB wait, main.c:~100-110 already waits for a host on the early proxy timeout in RELEASE+CHAINLOADING builds, config.h:48-52) so the MacBook path still works.
Cheaper intermediate step (HYPOTHESIS): put only the 33.6 MB payload and HV on the ESP, and have a stub in C do steps 2 (without DCP/video) to prove that disk-loaded payload boots to UEFI with serial capture before porting DCP.

(2) DUAL BOOT: SEPARATE STUB APFS CONTAINER

FACTS about Asahi's design (asahi-installer main branch)
- Stub = a tiny APFS container with a minimal macOS (System/Data/Preboot/Recovery volumes) so iBoot/LocalPolicy will boot a custom object from it. main.py:18 notes "2.5GB stub + 5GB OS + 0.5GB EFI = 8GB"; the stub is created with `dutil.addPartition(free, "apfs", name, STUB_SIZE)` or partitionDisk (main.py:321,362), then StubInstaller.prepare_volume (stub.py:51-98) adds Data, System (groupWith Data), Preboot, Recovery volumes via diskutil apfs addVolume (roles D/S/B/R). install_files (stub.py:248+) copies files from an IPSW/installed macOS (needs a macOS IPSW or the running OS; stub files modified: ProductUserVisibleVersion gets "(stub)" at stub.py:277).
- osinstall.py:187-198 builds the boot object with m1n1.build(m1n1.bin, boot_obj_path, vars) where vars include chosen.asahi,efi-system-partition and chainload=<ESP uuid>;<next_object>. So stage1 is m1n1.bin + vars, stage2 lives on the ESP (FAT) -- exactly the mechanism in section (1).
- Registration is done by step2.sh in 1TR (one true recoveryOS): bputil/kmutil configure-boot on the new stub volume group (main.py:685-700 step2(); step2.sh checks bputil "Paired" and "one true recoveryOS", then runs kmutil configure-boot, which I did not see in the 60 lines fetched but is what this project already uses: NWOAS-STATUS-2026-09-06.md:484). bless --setBoot sets the default boot volume (main.py:653-683). Reduced Security is required (set_reduced_security, main.py:637+, bputil -g).
- Macintosh HD can then be returned to normal: its boot policy would be unregistered from the custom object, restoring the stock kernel (HYPOTHESIS for the exact method: in 1TR use Startup Security Utility / `bputil -n -v <vgid>` or `kmutil configure-boot -c <kernelcache> -v` with the stock kernelcache; I have not verified this; the project's own recovery in STATUS 2026-09-07 ("Startup Disk ... D4 복귀", lines 497-508) shows the re-register procedure works in the other direction.)
- Project status: NWSTUB is only a reserved Basic Data 4 GB gap, LBA 38968576-39944703 (manifest.json:50-52), not an APFS container; nvme.c:540 documents it as unwritable by the HV. The review note (manifest.json:83) says NWSTUB has Basic Data type and Windows will see it.
- Layout order: macOS APFS 128006-38968337, NWSTUB 38968576-39944703, ESP 39944960-40076031, MSR 40076032..., Windows 40080128-59968511, Recovery 59968630-61279338. 4 GB = 976128 blocks x 4096 (4,000,000,000 B approx), enough for Asahi's 2.5 GB stub.

Can we reuse asahi-installer? Mostly yes for the stub, not for the rest.
- Reusable: stub.py StubInstaller (volume roles, install_files, step2 script), diskutil wrappers, m1n1.build vars, step2.sh, bless/bputil flow. Run it from macOS Terminal, choosing "install into free space"/"existing empty APFS container" (main.py:280-300 action_install_into_container uses an empty APFS container; action_install_into_free:327-362 needs free space, which we do not have since GPT is pre-laid out).
- Not reusable as-is: the OS template step (osinstall.py: partition_disk, rootfs image, firmware) and the installer's own partition resizing, which would conflict with our fixed layout and the HV GPT-write guard. Use a template with no OS image, only stub + boot object.
- Install the stub BEFORE the HV GPT guard ever matters: that guard only runs inside the HV; Mac-side partitioning is done from macOS/1TR normally.

HYPOTHESIS: Concrete ordered steps for dual boot
0. Make Macintosh HD the stock macOS again first? No -- do this last. Keep today's D4 boot working until the stub boots.
1. In macOS, change the 4 GB NWSTUB gap to an APFS partition: `diskutil apfs` cannot convert Basic Data in place; instead use diskutil to re-type/format (diskutil eraseVolume/addPartition apfs on that slice) or delete the slice and `diskutil apfs create`. Beware GPT edits shift partition IDs; recapture GPT after, update HV constants (nvme.c:543 and guest_module.py GptGuard) and gpt_partition_s231.py. Blocker: the HV's NVME write protection refuses all writes outside the Windows zone and refuses GPT edits (manifest review), so do this from macOS, never through the HV.
2. Run asahi-installer's stub logic against that empty APFS container (action_install_into_container): creates Data/System/Preboot/Recovery volumes and copies stub macOS files. Needs network/IPSW (stub files come from an IPSW or the current macOS; check stub.py load_ipsw/install_files 24-50, 248+). Blocker/risk: macOS 26 Tahoe vs installer tested versions; MIN_MACOS_VERSION 13.5 is fine, but Tahoe support status of asahi-installer is unverified.
3. Build the boot object: m1n1.bin (CHAINLOADING build, S231 HV or a small stage 1) + vars `chosen.asahi,efi-system-partition=<ESP uuid>` and `chainload=<ESP uuid>;m1n1/boot.bin`; put stage 2 in the ESP FAT filesystem (mount from macOS or from Windows after setup; ESP must be formatted FAT32; its filesystem state is unknown (HYPOTHESIS: not formatted, since Windows Setup cannot create ESP under this chain per manifest review).
4. Boot into 1TR, run step2.sh/`kmutil configure-boot -c <boot object> --raw --entry-point 2048 --lowest-virtual-address 0 -C -v <stub volume>` (same flags the project already used for D4; verify exact flags in the existing D4 install notes, not in the fetched 60 lines), set Reduced Security for the stub volume (bputil -g/-v), then `bless --setBoot` to the stub or leave Macintosh HD as default and pick the stub via boot picker (power-button hold, "Entering startup options").
5. Test: boot stub -> stage 1 m1n1 -> chainload from ESP -> HV -> (still host-driven until step 2 of section (1) is done). Verify with the serial log that "Valid payload found"/"Chainloading ..." lines appear.
6. Only after the stub boots and returns control reliably, restore Macintosh HD to stock: in 1TR/Startup Security use the stock boot object (do not touch its Data volume), verify normal macOS boot, keep a recovery boot path (Recovery partition 59968630-61279338 still present).
7. Update HV GPT guard/layout docs for NWSTUB now being APFS (not Basic Data) and hidden from Windows (the review notes it is visible; fix by APFS type GUID, which diskutil will set).

BLOCKERS (ranked)
1. No C-side guest launcher: standalone boot needs porting HV.init/load_raw/start, ADT/bootargs, PMGR/CPUSTART hooks, Tahoe DCP (video) to C (README s207 sec 1, 3). Largest effort, hardware-only validation.
2. Remaining host runtime dependencies (NS2 RAM disk/worker, any NVMe admin round trips not yet local) park CPUs if the cable is absent (native-s230/NEXT.md, README s207 sec 7).
3. Unknown whether the installed D4 stage 1 has CHAINLOADING; bare m1n1 boot object is size-limited (2 MiB); heap headroom for a ~36 MB FAT read + copy.
4. ESP not known to be formatted/accessible; Windows Setup cannot create ESP/MSR under the current GPT guard (manifest.json:83), so ESP/MSR must be created from macOS.
5. Stub creation needs a macOS IPSW/stub file source and a working 1TR step2 on Tahoe; Asahi installer Tahoe compatibility unverified.
6. Any GPT/layout change invalidates S231 constants (nvme.c:543, GptGuard hashes in layout-s231-guest-test.sh) and needs a new HV build + re-qualification; there is no hardware validation here, so none of this is claimed to boot.

RECOMMENDED NEXT SINGLE EXPERIMENT (cheap, reversible)
Build a CHAINLOADING=1 stage 1 with `chainload=<ESP partuuid>;m1n1/boot.bin`, format the ESP FAT from macOS, put the existing S231 HV there, install as a custom boot object on a test path (not Macintosh HD) and confirm the "Chainloading..."/"File read successfully" serial lines. This proves the disk-load half independent of the harder standalone-launcher port.