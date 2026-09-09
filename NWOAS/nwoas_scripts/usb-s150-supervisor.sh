#!/bin/bash
# Continue S150 across guest-requested full-system resets and USB re-enumeration.
set -u
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
RUN=/Volumes/X31/NWOAS/nwoas_scripts/usb-s150-guest-test.sh
JOURNAL=/Volumes/X31/NWOAS/nwoas_scripts/logs/usb-s150-supervisor.log
while true; do
    while [ ! -e "$DEV" ]; do sleep 1; done
    if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then sleep 1; continue; fi
    echo "$(date '+%Y-%m-%d %H:%M:%S') launch" >> "$JOURNAL"
    "$RUN"
    rc=$?
    echo "$(date '+%Y-%m-%d %H:%M:%S') exit=$rc" >> "$JOURNAL"
    sleep 2
done
