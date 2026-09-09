#!/bin/bash
# S6-D29: ONLY timing variable vs D26 is shutdown/sleep immediately after a
# successful mode+swap, before the periodic HDMI hotplug cycle. A separately
# logged single bootstrap reboot must precede this wrapper.
# PMGR reset occurs only after CLOSE, AFK, AP, and IOP acknowledgements.
set -eu
export NWOAS_SLEEP_HANDOFF=1
exec bash /Volumes/X31/NWOAS/nwoas_scripts/tahoe-dcp-swap-test.sh
