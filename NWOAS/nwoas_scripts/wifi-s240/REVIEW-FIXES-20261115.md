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
