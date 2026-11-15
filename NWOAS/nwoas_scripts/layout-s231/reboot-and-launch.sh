#!/bin/bash
# S231: reboot the Mac mini from the current m1n1 proxy (no guest may be running), wait for the
# installed m1n1 to come back, then start layout-s231-guest-test.sh once. No retries.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$ROOT/m1n1_windows/proxyclient"
PY="$ROOT/m1n1_windows/.venv-hv/bin/python3"
if /usr/sbin/lsof -t "$DEV" >/dev/null 2>&1; then echo 'STOP: serial owned (guest running?)'; exit 1; fi
timeout 20 "$PY" -c "
from m1n1.proxy import UartInterface,M1N1Proxy
i=UartInterface();p=M1N1Proxy(i,debug=False);p.nop();p.reboot();print('reboot sent')" 2>&1 | grep -v RuntimeWarning
for k in $(seq 1 30); do [ -e "$DEV" ] || break; sleep 1; done
for k in $(seq 1 60); do [ -e "$DEV" ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
[ -e "$DEV" ] || { echo 'STOP: proxy did not come back'; exit 1; }
sleep 3
exec caffeinate -dims bash "$ROOT/nwoas_scripts/layout-s231-guest-test.sh"
