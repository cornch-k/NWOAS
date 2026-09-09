#!/bin/bash
# S6-D31: ONLY shutdown-order variable vs D30 is AP quiesce + IOP sleep
# while AFK/display is still alive. CPU stop/reset is ACK-gated.
# A separately logged single bootstrap reboot must precede this wrapper.
set -eu
export NWOAS_SLEEP_HANDOFF=ap-first
exec bash /Volumes/X31/NWOAS/nwoas_scripts/tahoe-dcp-swap-test.sh
