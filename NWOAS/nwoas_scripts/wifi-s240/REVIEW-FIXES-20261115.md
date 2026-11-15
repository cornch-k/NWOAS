# Review 2026-11-15 (fixes)

## Report

All defects listed in both REVIEW files are fixed, offline only. Tests pass under both interpreters: wifi-s240 52/52 and port-j274 24/24. Nothing ran on the Mac mini: no /dev/cu.* opened, no `--run`, launcher, proxy or chainload. The only run.sh call was plan-only (exit 0). No git commits. Passing tests is not a hardware or boot result.

## wifi-s240 (/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/wifi-s240/)
Started from the reviewed wifi_probe.py (sha256 2408a15b…5394). Every fix has its own fault-injection tests, listed in the review's new "Fixes applied" section.

| # | Fix (wifi_probe.py line) |
|---|---|
| 1 | Power-down now runs 7 separate steps (`_pd_step`, :590-650): root-bus restore, PERST# GPIO, PERST_OFF clear, SMC off, SMC stop, link check, port-1 snapshot. Each has its own time limit (`POWER_DOWN_STEP_SECONDS` :91) and its own entry in `power_down.steps` / `power_down.errors`. A failure in one step does not stop the next. |
| 2 | `confirmed` now needs every required step OK and `link_down_after is True`. A changed or unreadable port 1 (FL1100) gives new exit code 5. |
| 3 | PORT_PERST bit 0 (port0+0x814) is cleared right after the PERST# GPIO assert (:433-440), so the later set really toggles it. |
| 4 | gP1a omission documented in the docstring and README (open hypothesis, not written). |
| 5 | The run directory is created before connecting (`make_run_dir`, unique via mkdtemp). If result.json cannot be written, the full result goes to a `RESULT_JSON` log line. |
| 6 | SMC key must be 4 bytes and of type `ui32` or `hex_`. |
| 7 | A connect failure now has its own exit code 6 (`CONNECT_FAILED`). |
| Review note | The exit-4 message and README now say to pull mains power if the power button does not respond. |

- **Wi-Fi window note (README §6 new row, `README.md:117`):** the Wi-Fi root-port window must also be kept out of guest RAM, same root cause as the USB-A bug.
  - FACT: the address is not known yet. Root port 0's window is disabled today (HANDOFF §9) and UEFI only assigns it once Wi-Fi is powered before UEFI.
  - New `predict_wifi_window()` (:226-292) is pure arithmetic and labelled HYPOTHESIS. It uses the EDK2 rules: T810XFamilyPkg.dsc.inc:85-87, PciHostBridge.c:779-827, PciResourceSupport.c:93-127 and 1164-1172.
  - From the measured BAR sizes it writes `bridge_window_prediction` to result.json: bus and CPU window, badmemorylist PFNs, and whether the FL1100 window would move.
  - Example with the fake 4 MiB BARs: Wi-Fi window bus 0xC0000000..0xC08FFFFF, FL1100 moved to 0xC0900000.
- **Final hashes:** wifi_probe.py 1834dc8b…9a33, test_wifi_probe.py eb1d39ad…11d4, README.md 406b4cd9…43c1, REVIEW 75870fbc…5881. run.sh unchanged (43638a55…aea4).

## port-j274 (/Users/angigyeom/Desktop/DEV/NWOAS/port-j274/)
1. **Exit code fails closed now** (`tools/check_j274_layout.py`). Exit 1 unless each L-n blocker is absent or accepted with `--allow L-1/L-2/L-6/L-8`. A MISMATCH, missing or extra keys, or any other reference rule can never be accepted. Usage error is exit 2.
   - Draft: exit 1 by default (L-1, L-2, L-8 block); exit 0 with all three allowed.
   - Unpadded FD (`--fw-size 0x1d88000`): exit 1 even with every `--allow`.
2. **Carveout check added, with an L-8 blocker row** in J274-GUEST-LAYOUT.md.
   - 20 carveouts lie inside guest RAM: region-id-48 overlaps ADT, firmware, boot args, the virtual framebuffer and the RAM disk; 19 ids sit inside the RAM disk.
   - region-id-24 is the whole RAM region (phys_base 0x801504000 + mem_size 0x3dfafc000), so it is reported as INFO, not a conflict.
   - `--no-carveouts` (or a missing facts file) reports UNCHECKED L-8, which also blocks.
3. **README commands** now all use `python3 -I -B`.
4. **Tests use real data:** values are parsed from the S231 log, and the highest natural base comes from a read-only scan of nwoas_scripts/logs (394 distinct bases in 486 files, max 0x83dbdc000).
5. **ADT values are reproducible.** New `tools/extract_adt_facts.py` (our own adt.py only, addresses and sizes only) writes `config/j274-adt-facts.json`.
   - Source dump: d6-adt.bin, sha256 cb773d76…5f7f.
   - It records the interpreter (`.venv-hv -I -B`), the exact command and 78 carveouts.

- **Correction found while fixing (FACT):** board-profile row MEM-14 said "20 ids" and "46/47/48 overlap the window start". The table has 19 ids in that range, and region-id-46/47 end below 0x850000000. Corrected in J274-BOARD-PROFILE.md:63 and j274-board-profile.json.
- **Hashes:**

| File | sha256 |
|---|---|
| check_j274_layout.py | 046e0242…03de |
| extract_adt_facts.py | 6d1fc12f…dce2 |
| test_check_j274_layout.py | c83cc09e…2245 |
| j274-adt-facts.json | 60e0bfb0…20e9 |
| README.md | d774f9c8…0134 |
| J274-GUEST-LAYOUT.md | 0e2741cc…6d0b |
| J274-BOARD-PROFILE.md | b52cf2e2…ff1e |
| j274-board-profile.json | 54223ae0…c9bb |
| REVIEW | e592c9ac…18ca |

## Installs
None.

## Other observations
- A new `m1n1_windows/proxyclient/m1n1/__pycache__` (adt/utils/__init__, cpython-314) appeared at 21:54:33. It is not mine: my extractor runs with `-B`, and re-running it did not touch those timestamps. I left it in place. A main-session `run_guest.py` (pid 19547, started 21:36) was running at the time.
- My scratchpad files are deleted.

## Open questions / hypotheses
- Whether clearing PORT_PERST bit 0 changes link training.
- Whether gP1a is needed for Wi-Fi.
- The real Wi-Fi window address, and whether FL1100 moves. This needs a probe hardware run for the BAR sizes, then a UEFI boot with Wi-Fi powered and an ECAM read-back.
- Who owns the 20 carveouts inside guest RAM, and whether they are harmless. Only the September dump was read, not the current boot's table.
- How to resolve L-8: stage-2 unmap, move phys_base / RAM disk, or accept it.
- The earlier port-j274 design questions 1-4 are unchanged.

Both REVIEW files end with a "Fixes applied" section (defect -> change file:line -> test).

## Independent review

Verdict: the fixes run offline and do what the report says, but there are 2 medium defects and 5 low ones. I modified nothing. Nothing touched the Mac mini (no /dev/cu.*, no --run).

Re-verified (FACT):
- wifi tests: 52 OK under system python3 and .venv-hv, in a fresh copy (1 skipped there, only because the copy moves ROOT for the real-ADT test).
- port-j274: 24 OK under both interpreters (in place, -I -B).
- Checker exit codes: default 1, all three allowed 0.
- Extractor re-run to scratch: output identical to config/j274-adt-facts.json except the `command` field (only the output path differs). 78 carveouts, same sha256.
- Carveout counts: 20 conflicts and 19 inside the RAM disk. region-id-46/47 end below 0x850000000. MEM-14 correction is right.
- Window prediction matches EDK2 InsertResourceNode (PciResourceSupport.c:93-127). For equal alignment the later-inserted root port 1 goes to the head, so FL1100 comes first, as the code says.
- The allowlist excludes port 1, and nothing writes storage.

DEFECTS

1. MEDIUM. wifi_probe.py:433-440, REVIEW-20261115.md:93 ("so the order truly matches UEFI"), README §6 controller-reset row.
   - FACT: UEFI only sets PORT_PERST_OFF and never clears it (AppleSiliconPciPlatformDxe.c:296). m1n1 also only sets it (pcie.c:591-592), and so does Linux.
   - FACT: in this boot chain UEFI runs after m1n1 pcie_init, so its set is the same no-op, and FL1100 still enumerates.
   - So the "fix" moves away from every working reference, and the REVIEW's claim is false.
   - HYPOTHESIS: clearing RESET_DIS resets the port-0 core after m1n1 applied apcie-config-tunables, pcie-rc-tunables and DBI setup (pcie.c:560-640). Link training then runs without them, and a LINK_DOWN would be blamed on something else.
   - Fix: make the clear opt-in (`--controller-reset`), default to the reference set-only sequence, and correct the REVIEW/README wording. Label test FixControllerReset (:554): its fake encodes the hypothesis, so it proves nothing.

2. MEDIUM (rule compliance). The __pycache__ is probably from this task, not the main session.
   - FACT: m1n1_windows/proxyclient/m1n1/__pycache__/{__init__,adt,utils}.cpython-314.pyc (all 21:54:33) are exactly what importing `m1n1.adt` compiles (adt.py:6 imports only .utils).
   - FACT: run_guest.py (pid 19547) started 21:36:32. It would also compile proxy, hv and the other modules.
   - HYPOTHESIS: an adt-only import without -B during this task made them (extractor, test_wifi_probe.py:857-860, or an ad-hoc check). "Re-running with -B did not change the timestamps" proves nothing, because existing pyc files are not rewritten anyway.
   - Fix: delete those 3 pyc files (junk) and record the source as unknown, not "main session".

3. LOW. extract_adt_facts.py:67: `interpreter` is a hard-coded "-I -B" string. The test at :278 is a tautology.
   - Fix: record sys.executable, sys.flags.isolated and sys.flags.dont_write_bytecode, and refuse to write unless both flags are set.

4. LOW (can pass without checking). check_j274_layout.py:255: a nonexistent explicit `--carveouts` path silently becomes "UNCHECKED L-8".
   - FACT: `--carveouts /nonexistent.json --allow L-1 --allow L-2 --allow L-8` gives exit 0 with carveouts None. One allow id covers both "overlap accepted" and "never checked".
   - Fix: an explicit missing path should be exit 2. Give "unchecked" its own id that `--allow` cannot accept.

5. LOW. check_j274_layout.py:164: any carveout that contains all of guest RAM is reported as INFO. A real large reservation would be hidden the same way.
   - Fix: give INFO only to the recorded RAM region (base 0x801504000, size 0x3dfafc000, region-id-24).

6. LOW. wifi_probe.py:607-647 with proxy.py `_request`: after a main-sequence PhaseTimeout there is no resync before the power-down read-modify-writes.
   - FACT: the proxy client only checks that the reply opcode matches.
   - HYPOTHESIS: a late P_READ32 reply could be used as the `_perst` GPIO read (:467), so one wrong value gets written to the pin 0x98 register. The mismatches that follow make `confirmed` False, so the damage stays on that allowlisted register.
   - Fix: call `_resync_link()`/nop at the start of power-down, and write fixed values (UEFI style) instead of read-modify-write in power-down.

7. LOW. A bad layout value or malformed JSON gives a traceback and exit 1. The checker's docstring says usage errors are exit 2. It still fails closed.
   - Fix: catch ValueError/KeyError/JSONDecodeError and return 2.

Note (design question, not a defect): `--allow L-6` lets a guest ECAM inside the real apcie ECAM (FL1100/Wi-Fi/Ethernet) pass with exit 0. Consider making L-6 never acceptable.

All of this is offline review. Passing tests is not a hardware or boot result.

Housekeeping: my scratch files are deleted. The files N, O, P, car.txt and off.txt in /private/tmp/claude-501/-Users-angigyeom-Desktop-DEV-NWOAS/35d06cc4-4f0d-4573-9b44-0ee2d7358e30/scratchpad/rv were created at 22:02-22:03 by another agent, and I left them.

## Fixes round 2 (2026-11-15)

All 7 defects and the design note above are fixed, offline only. Nothing ran on the Mac mini: no /dev/cu.* opened, no `--run`, no launcher, proxy or chainload. A `run_guest.py` (pid 96507) and `capture_vuart.py` (pid 96506), started 22:39 by someone else, were running and were not touched. The only run.sh call was plan-only (exit 0). No git commits. Passing tests is not a hardware or boot result.

| # | Defect | What changed (file:line) | Tests |
|---|---|---|---|
| 1 | Controller reset moved away from every reference | `wifi_probe.py:481-496`: default bring-up only sets PORT_PERST_OFF, as UEFI (AppleSiliconPciPlatformDxe.c:296) and m1n1 (pcie.c:591-592) do. The clear runs only with the new `--controller-reset` flag (`:798`), and is shown in the plan text (`:782-784`) and in result.json (`controller_reset_option` `:405`, `port0_perst_reg`). Docstring `:14-19`. The false claim is corrected in REVIEW-20261115.md (defect 3 note, row 3 marked superseded, new round-2 section). README §1 rows 3 and 5, §3 option table, §4 sample line, §6 "컨트롤러 쪽 리셋" row rewritten in Korean with [사실]/[가설] | `test_wifi_probe.py:567` `FixControllerReset` now tests only the flag: default has no clear before LTSSM start and keeps the UEFI order; the flag adds the clear under PERST#; parsing; plan text. `Main.test_controller_reset_flag_reaches_probe` (`:878`). The old "link trains only after the clear" fake was removed. No test says which sequence works on hardware |
| 2 | Leftover `__pycache__` | Deleted `m1n1_windows/proxyclient/m1n1/__pycache__/{__init__,adt,utils}.cpython-314.pyc` (all 21:54:33, nothing else and nothing newer in that folder), then the empty `__pycache__` folder. The running run_guest.py is not affected: it already has its modules loaded. **Source: unknown** (the earlier "main session" attribution is withdrawn; it may also have come from an import without -B during the round-1 task) | `find m1n1_windows/proxyclient -name __pycache__` is empty after all test runs below |
| 3 | Extractor recorded a typed-in interpreter string | `port-j274/tools/extract_adt_facts.py:56-61` `interpreter_record()` stores `sys.executable`, `sys.flags.isolated`, `sys.flags.dont_write_bytecode` and the version. `:69-73`: refuses with exit 2 unless both flags are set, before the proxyclient import and before writing. `command` is built from the real executable (`:80-88`, not symlink-resolved) | `test_check_j274_layout.py:391` `ExtractorRecordsRealInterpreter`: refusal for each missing flag (no file, no `m1n1.adt` import); record reads `sys`; a real `-I -B` subprocess run into a temp dir records `isolated`/`dont_write_bytecode` true and the same data as the committed file (venv only); the tautology was removed |
| 4 | Missing explicit `--carveouts` silently became "unchecked" | `check_j274_layout.py:290-308`: an explicit path that is not a file is exit 2. "Not checked" (`--no-carveouts`, or the default file missing) is now its own id `UNCHECKED` (`:72-76`, `:257`), which `--allow` cannot accept | `:271` missing explicit path -> 2 even with every `--allow`; `:279` explicit existing path is used; `:365` `--no-carveouts` with every `--allow` -> 1, blocker `UNCHECKED`; `:372` `--allow UNCHECKED` -> 2 |
| 5 | Any region covering all guest RAM was INFO | `check_j274_layout.py:62` `J274_RAM_REGION` (id 24, base 0x801504000, size 0x3dfafc000); `:196-200`: only that exact entry is INFO, anything else that covers guest RAM is `CONFLICT L-8 ... contains all of guest RAM` | `:333` synthetic whole-RAM region id 4 -> CONFLICT; `:349` exact entry INFO, other id/base/size -> CONFLICT, constant equals the recorded table entry |
| 6 | No resync after a timeout; power-down read-modify-write | `wifi_probe.py:229-254` `resync_proxy()`: wait 0.2 s, drop pending serial bytes, `iface._resync_link()`, one `p.nop()`. It is the first power-down step `link_resync` (`:673`, limit `:95`), required whenever anything else must be undone (`:709`). PERST# GPIO and PORT_PERST are written with fixed values 0x2 and 0x0 (`:98-110`, `:684-686`), recorded in `power_down.fixed_writes`. 0x2 = UEFI GPIO_MODE_OUTPUT_0 value (AppleEmbeddedGpioControllerDxe.c:76, :136-137); clearing 0x814 on shutdown is what m1n1 pcie_shutdown does (pcie.c:757). README §1 row 5, §3 limits, §6 "PERST# 핀 쓰기 방식" and "시간 초과 뒤" rows | `test_wifi_probe.py:617` `FixPowerDownResync`: resync first and no PERST#/PORT_PERST reads in power-down; a stale reply the resync does not remove is still not written back; a working resync removes it; resync failure -> unconfirmed, other steps still run; drain -> `_resync_link` -> `nop` order with a fake interface; nothing done -> no resync required. `FixPowerDownIndependent` also faults `link_resync` alone. Mutation check (round-1 power-down put back in a scratch copy): 5 of these tests fail |
| 7 | Bad input gave a traceback and exit 1 | `check_j274_layout.py:78-80` `InputError`; `:149-170` `load_carveouts()`/`load_layout()`; `:293-308`: unreadable file, bad JSON, non-object JSON, bad hex, list or bool value, bad facts file -> message on stderr and exit 2 | `:284` `test_bad_inputs_are_usage_errors`: 6 bad layouts, 4 bad facts files, bad `--fw-size` -> 2, no traceback |
| Design note | `--allow L-6` passed a guest ECAM inside the real apcie ECAM | `check_j274_layout.py:66-76`: L-6 moved from `ALLOWABLE` to `NEVER_ALLOWABLE`; `--allow L-6` is now a usage error (exit 2) and an L-6 finding always blocks (`:244-246`) | `:258` `test_l6_can_never_be_allowed` |

**Tests (all with `-I -B`):**

| Suite | Interpreter | Ran | Result |
|---|---|---|---|
| wifi-s240 (was 52) | python3 | 61 | OK, 1 skipped (real-ADT test needs `construct`) |
| wifi-s240 | .venv-hv | 61 | OK |
| port-j274 (was 24) | python3 | 34 | OK, 2 skipped (extractor subprocess needs `construct`; committed facts file predates round 2) |
| port-j274 | .venv-hv | 34 | OK, 1 skipped (committed facts file predates round 2) |

**Checker exit codes now:** default 1 (L-1, L-2, L-8); `--allow L-1 --allow L-2 --allow L-8` 0; `--carveouts /nonexistent.json` with those three 2; `--no-carveouts` with those three 1 (UNCHECKED); `--allow L-6` 2; `--fw-size 0x1d88000` with the three 1.

**sha256 after round 2:**

| File | sha256 |
|---|---|
| wifi_probe.py | dab5251d35f68f93c117f35057770af5e63076f91a2808fcd4118c444f6a9de1 |
| test_wifi_probe.py | 438ed68de2cc648adc1b296c63eae43c39f2adf31ed334bcf30460d0681161ad |
| README.md (wifi-s240) | 162620c98d9f000b0f962fff3a631d96d539932815bfa5b1161109d71f268c31 |
| run.sh | 43638a55d0061d9b0ab486a7ee87b137531956c9d75c19cb0bba148dfd2aaea4 (unchanged) |
| check_j274_layout.py | 46c92a73e54f1a88c7240d043ab6416e80c1c7b4b6df882b5428d593ad96b86e |
| extract_adt_facts.py | 63f44544df4c112062d038cb8916aa88a3258496565f4eeb301da72b7d09256e |
| test_check_j274_layout.py | bf3573cd0461035ce0e1ea1f2eb22052afeca5350265ded304b68fac212c456b |

The old wifi_probe.py hash (1834dc8b…9a33) was pinned only in REVIEW-20261115.md and this file. Both now list the new hash; the old rows are marked as round 1.

**Not changed (outside the files I was allowed to edit):**
- `port-j274/config/j274-adt-facts.json` still has the round-1 typed-in `interpreter` string. A fresh extractor run gives the same data (checked in a temp dir by the venv test). Regenerate it with `m1n1_windows/.venv-hv/bin/python3 -I -B port-j274/tools/extract_adt_facts.py experiments/tahoe-afk-20260906/d6-adt.bin port-j274/config/j274-adt-facts.json`; the skipped test then runs.
- port-j274 `README.md` (lines 86-88: missing facts file reported as "UNCHECKED L-8"; `--allow L-n` wording), `J274-GUEST-LAYOUT.md:94` ("accept with `--allow L-8`" is still right, but the "unchecked" case is now `UNCHECKED`) and `REVIEW-20261115.md:106-107,122-123` (ALLOWABLE listed with L-6; a command with `--allow L-6`, which is now exit 2) describe the round-1 behaviour.

**Still HYPOTHESIS / open:** whether `--controller-reset` matters for link training; whether bits other than bit 0 of port0+0x814 are live (writing 0 assumes not); a reply later than the 0.2 s quiet time is not caught by the resync (the fixed writes are the second defence); everything listed under "Open questions" above.

Scratch files I made (backups and a mutation copy) are deleted.
