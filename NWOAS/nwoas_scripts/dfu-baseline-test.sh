#!/bin/bash
# NWOAS S6-P2: reproduce session-5 baseline after DFU firmware change.
# [DESIGN] ONLY environment variable under study is the restored firmware environment;
# reuse the exact d4986f8d hv, guest payload and evtdump module without rebuilding.
# PASS requires observed Windows kernel/Setup; cursor is the separate §3.2 criterion.
# Failure before Windows is a bootstrap/firmware regression, not a Path-B result.
# Safety: zero reboots, single chainload, no automatic retry, no process kills,
# 115200 fixed, no kmutil, no .env access. Never terminate during upload.
set -eu
ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
PY="$M1N1/.venv-hv/bin/python3"
export M1N1DEVICE=/dev/cu.debug-console M1N1_KEEP_BAUD=1
export PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$M1N1/proxyclient"
export NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/dfu-baseline-$(date '+%Y%m%d-%H%M%S').log.XXXXXX")
export NWOAS_LOG="$LOG"
echo "Log: $LOG"
[ "$(shasum "$M1N1/build/m1n1.bin" | cut -d ' ' -f 1)" = d4986f8dcee82e8a0aed5d4de0f808020bc12fc2 ]
if lsof -t "$M1N1DEVICE" >/dev/null 2>&1; then
    echo 'STOP: serial busy; no processes terminated.'
    exit 1
fi
shasum "$M1N1/build/m1n1.bin" "$M1N1/m1n1-payload-iort-noleafdma-v1.bin" \
    "$M1N1/pcie_emul-exp5-evtdump.py" >> "$LOG"
echo 'Single chainload, no reboot.' | tee -a "$LOG"
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$M1N1/build/m1n1.bin" >> "$LOG" 2>&1
"$PY" -u - >> "$LOG" 2>&1 <<'PY'
import signal
from m1n1.proxy import UartInterface
def timeout(signum, frame):
    raise TimeoutError('post-chainload NOP deadline')
signal.signal(signal.SIGALRM, timeout)
signal.alarm(8)
iface = UartInterface('/dev/cu.debug-console:115200')
try:
    iface.cmd(iface.REQ_NOP)
    iface.reply(iface.REQ_NOP)
    print('[FACT] CHAINLOAD-PROXY-VERIFIED')
finally:
    signal.alarm(0)
    iface.dev.close()
PY
echo 'Chainload proxy verified; starting unchanged guest baseline.' | tee -a "$LOG"
exec "$PY" -u "$M1N1/proxyclient/tools/run_guest.py" \
    -r "$M1N1/m1n1-payload-iort-noleafdma-v1.bin" \
    -m "$M1N1/pcie_emul-exp5-evtdump.py" >> "$LOG" 2>&1
