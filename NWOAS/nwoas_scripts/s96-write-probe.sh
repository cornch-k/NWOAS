#!/bin/bash
# S96: chainload the nvme_write hv once, then host-side single-block write round-trip in WINTEST.
set -eu
ROOT=/Volumes/X31/NWOAS; M1N1="$ROOT/m1n1_windows"; DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"; HV="$M1N1/build/m1n1-s96-nvme-write.bin"
LOG="$ROOT/nwoas_scripts/logs/ans2-s96-write-$(date '+%Y%m%d-%H%M%S').log"
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$M1N1/proxyclient"
[ -e "$DEV" ] || { echo "STOP: CDC missing"; exit 1; }
/usr/sbin/lsof -t "$DEV" >/dev/null 2>&1 && { echo "STOP: serial busy"; exit 1; }
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "f63512503f6bef4a06e2b3e9155fcb5ada134043204dd0e26015b88044a5caf5" ]
echo "Log: $LOG"
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG" || { echo "STOP: chainload unconfirmed"; exit 2; }
"$PY" -u "$ROOT/nwoas_scripts/ans2-s87/write_probe_s96.py" 2>&1 | tee -a "$LOG"
