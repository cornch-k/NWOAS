#!/bin/bash
# S95: after Recovery-mode APFS shrink, re-read GPT read-only through the m1n1 proxy.
# One chainload of the S93 hv (has nvme_init/nvme_read), then ans2-s87/probe.py.
# Never writes storage. Requires serial free and mini sitting in fresh m1n1 proxy.
set -eu
ROOT=/Volumes/X31/NWOAS; M1N1="$ROOT/m1n1_windows"; DEV=/dev/cu.usbmodemC07HL05SQ6NY1
PY="$M1N1/.venv-hv/bin/python3"; HV="$M1N1/build/m1n1-s93-nvme-irq.bin"
LOG="$ROOT/nwoas_scripts/logs/ans2-s95-gpt-$(date '+%Y%m%d-%H%M%S').log"
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$M1N1/proxyclient" NWOAS_ANS_EXPERIMENT=S95
[ -e "$DEV" ] || { echo "STOP: CDC missing"; exit 1; }
/usr/sbin/lsof -t "$DEV" >/dev/null 2>&1 && { echo "STOP: serial busy"; exit 1; }
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "2c7d0012089f37ef025bea6344781e858347883446bc865fe9f6bbc86fc4ef63" ]
echo "Log: $LOG"
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG" || { echo "STOP: chainload unconfirmed"; exit 2; }
"$PY" -u "$ROOT/nwoas_scripts/ans2-s87/probe.py" 2>&1 | tee -a "$LOG"
