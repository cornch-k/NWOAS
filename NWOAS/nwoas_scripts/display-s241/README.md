# display-s241: 1080p-first mode rank for the Tahoe DCP pre-guest hook

Plan: `nwoas_scripts/plan-20261115/display.md`, step 1 (smaller variant at `:129`).
Status: **offline-checked only. Never run on the Mac mini. No 1080p picture has ever
been produced by this path** (`display.md:64`, `:105`). Build/offline success is not
a display success; only a person looking at the monitor can confirm the picture.

## Files

| Path | What |
|---|---|
| `nwoas_scripts/display-s241/tahoe_dcp_guest_hook.py` | Copy of `display-s190/tahoe_dcp_guest_hook.py` with one ranking line changed |
| `nwoas_scripts/display-s241/test_mode_rank.py` | Offline unittest: pulls `choose_timing()` out of both hooks via `ast`, no m1n1 import |
| `nwoas_scripts/display-s241-guest-test.sh` | Copy of `usb-s233b-guest-test.sh`; only hook path, hook pin, log prefix, banner (+1 header comment) changed |

Originals untouched: `display-s190/tahoe_dcp_guest_hook.py` still
`418aeaab35221382cd4e03ae096bdd3c426a276bc2674f8682510b8fece0aec6`; the S241 hook
imports `hpd_wait` from `display-s190` exactly as before (hook `:22-23`), so the
launcher's `hpd_wait.py` pin is unchanged.

| File | SHA-256 |
|---|---|
| `display-s241/tahoe_dcp_guest_hook.py` | `cfa0a363f0b0b0eb77faa68648ea973622a27f18129ac92bc328cd7e1f2bcb8e` |

## Hook diff (complete)

```diff
--- display-s190/tahoe_dcp_guest_hook.py
+++ display-s241/tahoe_dcp_guest_hook.py
@@ -57,7 +57,8 @@
     def rank(m):
         size = (m[1], m[2])
         preferred = 0 if preferred_size and size == preferred_size else 1
-        standard = {(1280, 720): 0, (1920, 1080): 1}.get(size, 2)
+        # S241: prefer 1080p; 720p stays the fallback when 1080p is not usable.
+        standard = {(1920, 1080): 0, (1280, 720): 1}.get(size, 2)
         refresh_error = abs(m[3] - 60 * 65536)
         return preferred, standard, refresh_error, m[1] * m[2], m[4], m[5]
```

Why nothing else changed (FACT, file:line in the S241 copy):

- `FB_WIDTH/FB_HEIGHT/FB_STRIDE` at `:41-43` are dead defaults. Before any use they are
  overwritten from the chosen mode at `:164-165` (`FB_WIDTH, FB_HEIGHT = _boot_mode[1], _boot_mode[2]`,
  `FB_STRIDE = FB_WIDTH * 4`). Changing them would have no runtime effect and would hide
  the 720p fallback, so they were left alone.
- VRAM checks already scale with the chosen mode: filter `m[1]*m[2]*4 <= vram_size` (`:53`)
  and `assert vram.size >= need` with `need = FB_STRIDE * FB_HEIGHT` (`:83`, `:86`).
- Second selection before guest entry passes `(FB_WIDTH, FB_HEIGHT)` as `preferred_size`
  (`:175-176`) and asserts it equals the boot-time pick (`:177`); BootArgs is re-asserted
  (`:181-183`); both DARTs are re-checked for the full `FB_STRIDE*FB_HEIGHT` (`:184-187`).
  All of these use the globals, so they follow 1080p automatically.
- Colour bars: `FB_WIDTH // 8` = 240 px per bar at 1920, one row = 7680 bytes = stride (`:189-194`).

Fallback behaviour: if the sink advertises no usable 1920x1080 (or VRAM is too small),
1080p is not in `usable`, and 1280x720 (rank 1) wins exactly as before.
Edge case (not hit by today's sink): rank order is `standard` before `refresh_error`, so a
sink that offers 1080p only at 50 Hz would get 1080p@50 rather than 720p@60.

## Launcher diff (complete, vs `usb-s233b-guest-test.sh`)

```
1a2
> # S241: usb-s233b-guest-test.sh with only the DCP pre-init hook swapped to display-s241 (1080p-first mode rank). NOT hardware-qualified.
15c16   LOG prefix  usb-s233b-  ->  display-s241-
54c55   banner      [S233B] ...  ->  [S241] S233B stack unchanged; DCP pre-init hook display-s241 prefers 1920x1080@60 (720p fallback); ...
57c58   hook pin    display-s190/...418aeaab...  ->  display-s241/...cfa0a363...
76c77   --pre-init-script display-s190/...  ->  display-s241/...
```

HV binary, payload, modules, env vars and every other pin are byte-identical to S233B.

## VRAM math (FACT from logs, read only)

Source: `nwoas_scripts/logs/usb-s233b-20261115-200823.QXp7BD:277-279` and
`nwoas_scripts/logs/tahoe-dcp-mapping-20260906-222407.odx2cC:27`.

| Item | Value |
|---|---|
| `/vram` base (BootArgs `video.base`) | 0xbe3f60000 (51069190144) |
| `/vram` size (`vram_size`) | 22,020,096 B (0x1500000, 21 MiB) |
| DCP DART remap of VRAM (Sept. log) | DVA 0x13dc000 -> 0xbe3f60000, size 0x1500000 |
| Hook DVA (S241 `:184`) | 0x13DC000 |

| Mode (32 bpp) | Stride (B) | Bytes needed | Fits 22,020,096? |
|---|---|---|---|
| 1280x720 | 5120 | 3,686,400 | yes |
| 1920x1080 | 7680 | 8,294,400 | yes (37.7 %) |
| 1920x1080, Mu 16 KiB round-up (`AdtParser.c:66-67`) | 7680 | 8,306,688 | yes |
| 3840x2160 | 15360 | 33,177,600 | no (filtered at `:53`) |

Today's sink advertises 1920x1080 at 59.94/60/50 Hz with tails (64,0) and (0,0)
(`usb-s233b-...QXp7BD:277`). Offline pick with the S241 rank: `(1, 1920, 1080, 3932160, 0, 0)`.

## Tests run (offline only)

| Test | Result |
|---|---|
| `python3 -I -B nwoas_scripts/display-s241/test_mode_rank.py -v` (7 tests) | all pass |
| original rank on today's list | `(1, 1280, 720, 3932160, 0, 0)` (matches logged pick) |
| S241 rank on today's list | `(1, 1920, 1080, 3932160, 0, 0)` |
| S241 second call with `preferred_size` | same mode |
| S241 with all 1080p entries removed | `(1, 1280, 720, 3932160, 0, 0)` |
| S241 with VRAM = 8,294,399 | 720p fallback |
| 4K-only list | rejected by VRAM filter |
| `ast.parse` of S241 hook | ok |
| `bash -n display-s241-guest-test.sh` | ok |
| every `[ "$(stat ...)" ]` / `[ "$(shasum ...)" ]` line of the new launcher (23 lines: 31-51, 57-58), run alone in a subshell | 23/23 PASS |

Not run: the launcher itself, proxy tools, chainload, run_guest, anything on `/dev/cu.*`.

## Validation on hardware (owner at the monitor)

Run `nwoas_scripts/display-s241-guest-test.sh` only when the hardware is free and someone
is watching the HDMI monitor. Then, from the log `nwoas_scripts/logs/display-s241-*`:

1. `grep -a 'selected timing' LOG` -> both lines `(1, 1920, 1080, 3932160, 0, 0)`.
2. `grep -a 'guest framebuffer BootArgs' LOG` -> `'new': (51069190144, 1, 7680, 1920, 1080, 32)`.
3. `grep -a 'mode_set_gated' LOG` -> DCP firmware says `1920x1080@60 Hz` (720p runs print
   `1280x720@60 Hz`, `usb-s233b-...QXp7BD:465`). This is the firmware's own statement and the
   best no-person check.
4. `grep -a 'mode/swap ACK' LOG` -> initial and reapply both present; `LOG.dcp.state.json`
   `timing` = `[1, 1920, 1080, 3932160, 0, 0]`.
5. **Person:** colour bars fill the full monitor width for 5 s before guest entry.
6. **Person:** Windows Settings > Display shows 1920x1080; dxdiag shows Microsoft Basic Display Adapter.
7. Soak as long as a normal 720p run; compare WHEA/bugcheck counts in `LOG.vuart`.

Steps 1-4 passing without 5-6 is not success.

## FBMASK check note (what it can and cannot tell)

FACT (`m1n1-usb-s233/src/hv_exc.c:226-339`, the source tree of the S233B HV):

- The probe runs **120 s after the guest first runs at a kernel VA**, not at guest entry
  (`:233-238`). In `usb-s233b-...QXp7BD.vuart:1175-1358` it appears after `MC t=155s`.
- Geometry is **hard-coded**: `base=0xbe3f60000, width=1280, height=720, stride=5120` (`:240-241`),
  bars compared as `x/160` (`:269`). It reads only the first 3,686,400 bytes. At 1080p that
  is still inside the 8,294,400-byte buffer, so no out-of-range read. `hv_vm.c:1153-1155`
  (HID tile hashes) has the same hard-coded 720p geometry and is also read-only.
- Mask = 4x4 OR of "pixel != pixel[0]", 320x180 bits; `fbmask_log_to_png.py LOG.vuart out.png` decodes it.

What 1080p would look like through this 720p-stride window (math, not observed):
720p row `3m` = 1080p row `2m` pixels 0-1279; row `3m+1` = row `2m` pixels 1280-1919 +
row `2m+1` pixels 0-639; row `3m+2` = row `2m+1` pixels 640-1919. So the mask shows only
the top 480 of 1080 rows, squashed, with every vertical edge ghosted twice 160 mask
columns apart, and no bottom taskbar band.

Limit (FACT): in the latest 720p run the mask is solid white (`nonbg=920576` of 921,600
pixels, `vuart:1175`; decoded mask has 57,545 of 57,600 bits set, the clear bits only in rows 0-11). With a
wallpaper-filled desktop the mask cannot distinguish 720p from 1080p. **Do not use FBMASK
as the mode check for S241**; use steps 1-4 above, then the person checks 5-6. A useful
FBMASK check would need a new HV build that reads `hv.tba.video` geometry and runs at guest
entry (not done, out of scope here).

## Rollback

Nothing existing was modified. To go back to 720p, run `usb-s233b-guest-test.sh` (or any
older launcher) again; they still pin and load `display-s190`. To remove S241 entirely,
delete `nwoas_scripts/display-s241/` and `nwoas_scripts/display-s241-guest-test.sh`.
No disk, NVRAM, m1n1, Mu or Windows state depends on these files. HYPOTHESIS (not tested):
if a 1080p run leaves a black screen, a fresh 720p launch recovers it, because every launch
restarts DCP and re-runs mode selection before guest entry (hook `:114-166`).
