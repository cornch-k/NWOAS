# display-s242: 1080p on the Mac mini (S237 stack + display-s241 hook)

Launcher: `../display-s242-guest-test.sh` = `fw-s237-guest-test.sh` with only the DCP pre-init hook swapped to
`display-s241/tahoe_dcp_guest_hook.py` (1920x1080@60 first, 720p fallback), log prefix and banner.

Result 2026-11-15 22:40-22:50 (Mac mini M1, installed Windows 11 25H2):

| Check | Result |
|---|---|
| DCP mode set | 1920x1080@60 Hz (`mode_set_gated`), swap ACK before guest entry |
| Windows boot + remote worker | connected within ~1 min of restart |
| Picture (user looked at the monitor) | Windows first used its remembered 1280x720; after the user set 1920x1080 in Windows display settings, the monitor shows 1920x1080 |
| Windows-reported resolution via job | not obtained: PowerShell output from worker jobs comes back empty (open) |

`run.sh` restarts the current session, launches S242 and queues `D1-display.cmd`. Full log in `results.txt`.
