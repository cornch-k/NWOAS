#!/bin/bash
# S6-D30: ONLY variable vs D29 is omission of interface CLOSE. This matches
# m1n1 dcp_ib_shutdown -> afk_epic_shutdown_ep -> rtkit_sleep ordering.
# A separately logged single bootstrap reboot must precede this wrapper.
# PMGR reset occurs only after AFK, AP, and IOP acknowledgements.
set -eu
export NWOAS_SLEEP_HANDOFF=no-close
exec bash /Volumes/X31/NWOAS/nwoas_scripts/tahoe-dcp-swap-test.sh
