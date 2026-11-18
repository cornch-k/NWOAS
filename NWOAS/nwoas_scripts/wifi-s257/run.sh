#!/bin/bash
# S257 Wi-Fi stage 2 wrapper (BCM4378 power -> link -> chip -> OTP -> firmware running).
# NO chainload, NO guest, NO storage access. Without --run it only prints the plan
# (or, with --check-files, checks the firmware files offline). With --run it talks to an
# idle, freshly booted m1n1 proxy and always powers the chip back down at the end.
# A PASS is a proxy-side hardware observation, not a driver, Windows or boot result.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
M1N1="$ROOT/m1n1_windows"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"
UP="$ROOT/nwoas_scripts/wifi-s257/wifi_up.py"
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1

case " $* " in
*" --run "*)
    [ -e "$DEV" ] || { echo 'STOP: CDC proxy device missing'; exit 1; }
    if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then echo 'STOP: serial busy'; exit 1; fi
    LOG=$(mktemp "$ROOT/nwoas_scripts/logs/wifi-s257-$(date '+%Y%m%d-%H%M%S').XXXXXX")
    echo "Log: $LOG"
    shasum -a 256 "$UP" "$ROOT/nwoas_scripts/wifi-s257/wifi_scan.py" "$ROOT/nwoas_scripts/wifi-s240/wifi_probe.py" >> "$LOG"
    # Every phase has its own alarm inside and the output is capped inside (console lines, steps).
    # The outer limit sends SIGINT (KeyboardInterrupt), so the power-down in `finally` still runs;
    # never cut the output with head/SIGPIPE or kill with SIGTERM/SIGKILL mid-sequence.
    timeout -s INT 1200 "$PY" -I -B -u "$UP" --proxyclient "$M1N1/proxyclient" "$@" 2>&1 | tee -a "$LOG"
    exit "${PIPESTATUS[0]}"
    ;;
*)
    exec "$PY" -I -B -u "$UP" --proxyclient "$M1N1/proxyclient" "$@"
    ;;
esac
