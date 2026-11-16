# S244 — run the S243 stack on macOS 13.5 OS firmware (stub entry, D4 m1n1)

**Hardware result 2026-11-16 11:28 KST: first run on the Mac mini PASSED to the Windows login screen and desktop** (owner saw the login screen and signed in; log `logs/fw135-s244-20261116-112810.b2S5zl`: probe G-1/G-2 PASS, preflight PASS, framebuffer 1920x1080 in /vram, guest skew 0x34000, GPT guard "13.5 stub APFS", S150 PASS, "Windows worker connected"). Boot chain: 13.5 stub (GPT slot 3, attributes cleared in S245) -> D4 m1n1 on OS firmware iBoot-8422.141.2 / system firmware mBoot-18000.121.3. GPU acceleration not present. The original offline-only status follows.

~~Status: WRITTEN AND TESTED OFFLINE ONLY. Nothing here has run on the Mac mini.~~ No `/dev/cu.*`
device was opened, nothing was chainloaded, nothing was committed. Whether the display, USB fix and
job channel work on 13.5 firmware can only be decided on the Mac mini. Build/test success here is
not boot success.

Tags: **[FACT]** = file:line, command output or disassembly seen in this session. **[HYP]** = not verified.

## 1. Summary

| Item | S243 (Tahoe OS firmware `mBoot-18000.121.3`) | S244 (13.5 OS firmware `iBoot-8422.141.2`) |
|---|---|---|
| Launcher | `display-s243-guest-test.sh` | `fw135-s244-guest-test.sh` (copy, 6 hunks, §5) |
| HV / payload | S233b / S237 | **unchanged** (same files, same pins) |
| Display | host Python Tahoe DCP hook (`display-s241/tahoe_dcp_guest_hook.py`) does mode/swap | **no hook**: D4, S233b and the guest m1n1 each run m1n1's own C `display_init()` |
| New gate before chainload | none | `probe_fw135_s244.py` on D4 (read-only) |
| `--pre-init-script` | Tahoe hook | `fw135_preflight.py` (read-only, aborts before `hv.init()`) |
| Host `-m` modules | 9 modules | same 9, same order, same pins |

The Tahoe hook cannot run on 13.5 at all: it asserts `mBoot-18000.121.3` [FACT `display-s241/tahoe_dcp_guest_hook.py:125`].

## 2. What changes under 13.5, module by module

| Component | Keys off firmware / ADT? | Effect on 13.5 | Action |
|---|---|---|---|
| D4 m1n1 (installed, MD5 7bec0225…) | `main.c` defers `display_init()` only if `os_firmware.iboot == "mBoot-18000.121.3"` [FACT, same code as `m1n1-usb-s233/src/main.c:169-172`; D4 binary contains the marker and the string] | D4 runs `display_init()` → external/dummy branch → `display_configure(NULL)` → `display_shutdown(DCP_SLEEP_IF_EXTERNAL)` [FACT `display.c:641-647`, `main.c:175`] | none (probe reports the mode it chose) |
| S233b HV (`usb-s233/out/m1n1-s233b-ringclean.bin`) | same deferral (marker present once) [FACT `strings`]; `hv_init()` wakes and **quiesces** DCP when `display_is_external` [FACT `m1n1-usb-s233/src/hv.c:59-61`] | S233b does its own modeset+sleep at boot, then at `hv.init` wakes DCP and quiesces it. On Tahoe both were skipped (`display_is_external` stays false when deferred). **New code path on 13.5.** | none; risk R2 |
| S233b FBPROBE | hard-coded `0xbe3f60000`, 1280x720x4, `dc ivac` (invalidate, no clean) ~120 s into the Windows kernel [FACT `hv_exc.c:226-251`, `memory.c:39`, compiled in: `config.h:39`, 2 `FBPROBE` strings in the binary] | harmless only while that range is inside `/vram` (it is the Tahoe `/vram` base [FACT saved ADTs: `/vram` 0xbe3f60000 size 0x1500000]). If 13.5 iBoot moves `/vram`, this would discard dirty cache lines of whatever RAM is there | **checked** by probe + preflight (`G2_fbprobe_inside_vram`), refuses otherwise |
| S233b other | `cpufreq_fixup` only for `V_UNKNOWN < version <= V13_3` [FACT `cpufreq.c:239`]; `dcp.c:152` dptx only V13_5 (M1 config has no dptx [FACT `display.c:37-43`]); `afk.c:326` Tahoe-only branch | no change (13.5 > 13.3; Tahoe was V_UNKNOWN) | none |
| Guest m1n1 prefix (S237 payload 0x0..0x20c000, = S215 build) | **no deferral at all**: `loader-s197/guest-compat-s197.patch:33-46` replaces it with plain `display_init()`; built by `loader-s215/build.sh` [FACT; prefix has no `NWOAS-DCP-DEFER` string] | On Tahoe the guest already ran `display_init()`; its C DCP path cannot speak Tahoe AFK (`afk.c:326` string present in the prefix), so it failed and left the hook's surface alone [HYP: guest m1n1 output is not captured in any log]. On 13.5 the same call is expected to **succeed** and modeset again [HYP] | none; risk R2/R10 |
| Guest m1n1 kboot (FDT for UEFI) | `isp_init()` allocates a heap only for `V13_5` etc. [FACT `isp.c:111-120`, `kboot.c:2721`]; FDT firmware-version/compat props switch on V13_5 [FACT `kboot.c:1484-1503`, `kboot_gpu.c:673-690`] | J274 has no `/arm-io/isp` [FACT both saved ADTs] → no heap. FDT props are not consumed by Mu/Windows [FACT Mu reads only BootArgs, below] | ISP presence **checked** |
| Guest `mem_size` skew 0x34000 | pinned in `layout-s231/guest_module.py:44,52` and `usb-s174/guest_map.py:13`; real value = 16K guard + 16K kboot log (`kboot.c:2624`) + SIO firmware data (`sio.c:11,38`, sizes from the OS-paired ADT) [FACT] | ADT becomes the 13.5 DeviceTree; SIO tunables length or a guest framebuffer relocation (`display.c:503-511`) would change the skew → UEFI backing ≠ HV backing | **modelled + checked** (`G2_guest_skew_matches_pin`); model reproduces the measured 0x34000 on both saved Tahoe ADTs [FACT tests] |
| `display-s241/tahoe_dcp_guest_hook.py` | asserts `mBoot-18000.121.3` (`:125`) | cannot run | removed from launcher |
| `display-s190/hpd_wait.py` | Tahoe DCP RPC only, imported by the hook | unused | pin removed |
| `cpufreq-s193/capture.py`, `cpufreq-s189/observe.py` | read-only cluster registers, no firmware key [FACT] | PMP firmware becomes 13.5; recorded P-states may differ [HYP] | none (compare JSON with S243 runs) |
| `smp-s133/pmgr_gate.py` | only replaces `hv.start_secondary` [FACT] | none | none |
| `usb-s174/guest_map.py` | `p.pcie_init()` (C, ADT PCIe tunables), backing from `u.ba.mem_size` [FACT `:7,14`] | tunables now from the 13.5 ADT [HYP same values] | none; risk R5 |
| `layout-s231/guest_module.py` | GptGuard pins slot 3 bytes; skew 0x34000; `check_irqs` IRQ 900 [FACT] | GptGuard stops after stub-135 changed slot 3 (stub-135 README §1/§4 R5) | **stub-135 step 9 first** (`guard_update.py`); its apply script rewrites the pin in every top-level `*.sh` that holds `e85591e9…`, this launcher included [FACT `stub-135/tool/guard_update.py:94-107`] |
| `layout-s231/module.py`, `nvme-s139/controller.py`, `memory-s180/link_module.py`, `file-delivery-s243/*`, `native-s230/query_module.py` | NVMe/NS2 only; ANS, SMC and USB firmware are **not** in the 13.5 bundle (system firmware) [FACT stub-135 README §2] | none expected [HYP] | none |
| `tools/chainload.py -r` | DCP only for `--xnu` [FACT `chainload.py:156-158`] | none | none |
| Project Mu (S216 source, S237 build) | framebuffer = `BootArgs->video.base/stride/height` (`firmware-s216/source-candidate/.../PrePi/AdtParser.c:111-112`), GOP = `BootArgs->video.base/width/height` (`SimpleFbDxe.c:176-179`) [FACT] | takes whatever mode m1n1 left in BootArgs | none |

## 3. Display path on 13.5

Sequence (code facts; every runtime outcome is [HYP]):

1. iBoot (13.5) → D4. Boot args video is the 640x1136 placeholder on this Mac mini (seen on Tahoe, S243 log lines 139-145 [FACT]). D4 `display_init()` → `display_configure(NULL)` → mode set, swap to `/vram`, boot args updated (`display.c:595`), DCP slept.
2. Probe (`probe_fw135_s244.py`, read-only) reads D4's boot args/ADT: firmware, resolution m1n1 chose, `/vram`, predicted guest skew.
3. `chainload.py -r` S233b → S233b `display_init()` again (same chooser, same mode), sleep; at `hv.init` C `hv_init()` wakes + quiesces DCP (`hv.c:59-61`).
4. `fw135_preflight.py` re-checks on S233b's state. No `preserve_framebuffer`, so HV start does `fb_improve_logo` + `fb_shutdown(True)` (`m1n1/hv/__init__.py:2435-2442`) — the upstream default.
5. Guest m1n1 (EL1 under HV, DCP MMIO pass-through) `display_init()` → third modeset, sleep; copies boot args to UEFI. Mu builds GOP from BootArgs; Windows uses that framebuffer (basic display).

**Resolution.** m1n1's chooser (`display.c:114-141`): valid → fits `/vram` → width ≤ 1920 → height ≤ 1200 → fps ≤ 60 → then largest width, height, fps. With the 21 MiB Tahoe-sized `/vram`, a 16:9 1080p/1440p/4K monitor gives **1920x1080 @ ≤60 Hz**; a monitor that offers 1920x1200 gives 1920x1200 (S241 would have chosen 1080p). Every stage calls `display_configure(NULL)`, so forcing exactly 1080p would need an m1n1 rebuild (guest prefix at least); not done. Format is BGRA 32 bpp (`display.c:381,582`); D4 also uses depth 32 [FACT D4 disassembly at file offset 0xed5c `cmp x10, #0x20`, next to the `display: swapped!` reference].

**Need for a host hook: none found.** The only host-side role left is gating, done by the probe and preflight.

## 4. D4 source (what matches MD5 7bec0225…)

[FACT] D4 = `experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin` = `recovery-tahoe-proxy-20260906/m1n1-bare.bin` (same MD5). It was made on 2026-09-06 22:26 by recompiling only `main.o` and relinking (`experiments/tahoe-afk-20260906/build-defer.log`) in `m1n1_windows`; its banner on hardware is `m1n1 v1.0.2-1471-g59fb544-dirty` (S243 log line 42). `main-before-defer.c` + the defer hunk is the whole `main.c` change. It is **not** `m1n1-layout-s231` (that tree has the later Tahoe AFK changes; D4 has no `measured Tahoe envelope`/`NWOAS-AFK-GEOMETRY` strings), although `m1n1-layout-s231/src/main.c:169-172` carries the identical deferral. The MD5 was not reproduced by a rebuild (the Sept dirty tree no longer exists); instead the display behaviour that matters was checked in the binary: deferral string + `mBoot-18000.121.3`, `External display found`, `iBoot-8422.141.2` table entry, depth 32. The 13.5 C display/AFK path in D4 is the pre-NWOAS upstream one (fixed 64-byte AFK ring blocks), which is what 13.5 firmware uses [HYP, upstream design].

## 5. Launcher diff (`fw135-s244-guest-test.sh` vs `display-s243-guest-test.sh`)

1. Header comment.
2. Log name `logs/fw135-s244-*`; `S244=` dir variable.
3. Echo lines `[S244]`.
4. Pins: removed `display-s190/hpd_wait.py`, `display-s241/tahoe_dcp_guest_hook.py`; added `fw135_model.py`, `fw135_preflight.py`, `probe_fw135_s244.py`, D4 MD5.
5. After `proxy_ready.py`: probe gate; any non-zero exit stops before chainload (`1` not 13.5, `2` proxy error, `3` running m1n1 is not D4, `4` layout check).
6. `--pre-init-script` = `fw135_preflight.py`.

Unchanged on purpose: HV/payload/module pins and order, all env vars (including the now-unused `NWOAS_DCP_LOG`, `NWOAS_HV_IMAGE`), VUART capture, delivery options. The `layout-s231/guest_module.py` pin `e85591e9…` stays until stub-135 step 9's apply script rewrites it.

Gate override: `NWOAS_FW135_HEADLESS_OK=1` relaxes only the two video-geometry checks (no HDMI sink). The memory-safety checks (FBPROBE in `/vram`, `/vram` outside guest RAM, skew == 0x34000) are never relaxed.

## 6. Verified offline vs. hypothesis

| Claim | Status |
|---|---|
| Tahoe hook stops on 13.5 | FACT (`:125`) |
| D4, S233b, guest prefix run `display_init()` on 13.5 | FACT (code/strings); success on hardware HYP |
| S233b `hv_init` wake+quiesce runs only on 13.5 | FACT (code); effect HYP |
| Mu/Windows framebuffer comes from BootArgs | FACT (Mu source) |
| Skew model = 0x34000 on both saved Tahoe ADTs | FACT (`test_fw135_model.py`, 13 tests OK) |
| Skew stays 0x34000 on the 13.5 ADT | HYP → checked live by probe/preflight, confirmed only by the vuart line in §7 step C5 |
| FBPROBE range is inside 13.5 `/vram` | HYP → checked live |
| m1n1 picks 1920x1080 on the attached monitor | HYP → probe prints it |
| `stub-135/tool/probe_fw135.py` crashes: `bytes()` of a `str` `system_firmware_version` | FACT on both saved ADTs (`test_stub_probe_bug_system_fw_is_str`); use `probe_fw135_s244.py` for stub-135 step 7 instead |
| PCIe/ATC tunables from 13.5 ADT behave like Tahoe's | HYP |

## 7. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | The 13.5 stub entry itself does not boot / USB CDC does not come up | stub-135 README; probe exit 2 |
| R2 | Four DCP transitions (D4 set+sleep, S233b set+sleep, S233b wake+quiesce, guest set+sleep); the guest one runs at EL1 under our HV and has never succeeded before | watch HDMI at each stage; S233b messages are in `$LOG` |
| R3 | 13.5 ADT changes the guest skew → UEFI/HV low-window backing mismatch (memory aliasing) | probe + preflight refuse; §8 C5 compares the two backing values |
| R4 | FBPROBE `dc ivac` on a hard-coded address | refused unless inside `/vram`; real fix = HV rebuild without the constant |
| R5 | PCIe/USB-C tunables differ in the 13.5 ADT (USB-A FL1100 path, proxy CDC) | compare FL1100/HID lines with S243 runs |
| R6 | PMP/AOP/SIO are now 13.5; no host module talks to them, only P-state observations may differ | read-only JSONs |
| R7 | GptGuard stops (slot 3 changed) | stub-135 step 9 before the launcher |
| R8 | 1920x1200 instead of 1080p on 16:10 monitors | accept, or rebuild guest m1n1 with a mode preference |
| R10 | Guest m1n1 console output is not in any log (S243 log has none), so a guest display failure is visible only as no picture | HDMI observation |

## 8. First-boot test sequence

Precondition: stub-135 steps 1-6 done; the Mac mini boots the new stub entry (13.5) and D4 waits for
the MacBook; HDMI monitor connected and on.

A. Read-only probe (replaces stub-135 step 7):
```
cd /Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows
M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONPATH=proxyclient \
  .venv-hv/bin/python3 -B ../nwoas_scripts/display-s244/probe_fw135_s244.py
```
Pass: `G-1 PASS: 13.5 OS firmware (iBoot-8422.141.2)`, `running_m1n1_is_d4: true`, `G-2 PASS`, and
`resolution` ≠ `640x1136` (m1n1 set a mode). Also look at HDMI: an m1n1 logo is expected [HYP].
Exit 4 → send the `FAIL` lines; do not run the launcher.

B. GptGuard update (stub-135 step 9): run `guard_update.py`, review, then `bash apply-guard-update.sh`.
Check `grep -c e85591e9 /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/fw135-s244-guest-test.sh` → 0.
(The apply script changes `fw135-s244-guest-test.sh`'s sha256; §9 lists the pre-update value.)

C. Launch (same delivery options as S243):
```
bash /Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/fw135-s244-guest-test.sh
```
Check in order:
1. Terminal: `[S244-probe] G-1 PASS`, `G-2 PASS`.
2. `$LOG`, S233b part: `OS FW version: 13.5 (iBoot-8422.141.2)`, `display: ... reconfiguring`, `display: timing mode: valid=1 1920x1080 ...`, `display: swapped!`, `display: Sleeping DCP (external)`; **no** `NWOAS-DCP-DEFER`.
3. `$LOG`: `[S244] PASS`; `$LOG.fw135-preflight.json` written; `Shutting down framebuffer...` (default path, not `Preserving framebuffer`).
4. `$LOG`: `[S102] guest mem_size ... window backing 0xB`.
5. `$LOG.vuart`: `HVLOG: NWOAS EARLY guest RAM ... backing 0xX` — **X must equal B**. If not, power the Mac mini off at once (skew mismatch, R3).
6. HDMI: m1n1 logo(s) → UEFI → Windows at the m1n1 mode.
7. `$LOG.vuart` ~120 s into the kernel: `FBPROBE base=0xbe3f60000` (inside `/vram`, already checked).
8. Job channel / USB: same checks as S243 (`$LOG.link/native-p12.json`, target query, FL1100/HID lines).

Back to the Tahoe path: stub-135 README rollback sections (D4 on Macintosh HD) and `display-s243-guest-test.sh`.

## 9. Files

| File | sha256 |
|---|---|
| `fw135_model.py` | `ed7b7857dc2cafe6d5a7a2dc40122ba2c93e4d702361d83bb70104e287c26e89` |
| `fw135_preflight.py` | `d66b576bebc355541419ec17bca324d5c9f26e14db8c5f7fde931605dbb6a398` |
| `probe_fw135_s244.py` | `f456c3283d5a68e42d7fb323ea08111ada58380af332fc84533f12e5bf5fbd4b` |
| `test_fw135_model.py` | `204249dc5932d6611b9bf3265c32d9581f4b164e6526c48bdcdb5dba1b0c80fc` |
| `../fw135-s244-guest-test.sh` (before stub-135 step 9) | `b91d87065095a1c4da126fc7bbae922836c08ccf96309170d2911752dedc85db` |

Tests: `cd m1n1_windows && PYTHONPATH=proxyclient .venv-hv/bin/python3 -B ../nwoas_scripts/display-s244/test_fw135_model.py` → `Ran 13 tests ... OK`.
