#!/bin/bash
# S240 Wi-Fi milestone 1 probe wrapper. NO chainload, NO guest, NO storage access.
# Default prints the plan only. With --run it talks to an idle, freshly booted m1n1 proxy:
# SMC gP0d on -> port 0 link -> read 1:0.0/1:0.1 config -> always power back down.
# A PASS here is a config-space observation, not a driver, Windows or boot result.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
PROBE="$ROOT/nwoas_scripts/wifi-s240/wifi_probe.py"
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1

case " $* " in
*" --run "*)
    [ -e "$DEV" ] || { echo 'STOP: CDC proxy device missing'; exit 1; }
    if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then echo 'STOP: serial busy'; exit 1; fi
    LOG=$(mktemp "$ROOT/nwoas_scripts/logs/wifi-s240-$(date '+%Y%m%d-%H%M%S').XXXXXX")
    echo "Log: $LOG"
    shasum -a 256 "$PROBE" >> "$LOG"
    "$PY" -I -B -u "$PROBE" --proxyclient "$M1N1/proxyclient" "$@" 2>&1 | tee -a "$LOG"
    exit "${PIPESTATUS[0]}"
    ;;
*)
    exec "$PY" -I -B -u "$PROBE" --proxyclient "$M1N1/proxyclient" "$@"
    ;;
esac
