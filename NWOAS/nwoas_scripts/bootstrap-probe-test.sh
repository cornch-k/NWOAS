#!/bin/bash
# NWOAS §3.2 / S6-P0: bootstrap availability after the user's DFU restore.
# [DESIGN] Hypothesis: an m1n1 proxy is currently reachable over the existing VDM link.
# PASS: one checksum-validated REQ_NOP reply at 115200. This is NOT boot/USB success.
# INCONCLUSIVE: busy/missing/inaccessible serial, timeout, or protocol error.
# Safety: zero reboots, no chainload, no kmutil, no target memory/MMIO writes,
# no process termination, no baud promotion, no .env access, one NOP only.
# Does not import m1n1.setup (which performs additional target initialization).
set -eu
ROOT=/Volumes/X31/NWOAS
DEV=/dev/cu.debug-console
PY="$ROOT/m1n1_windows/.venv-hv/bin/python3"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/bootstrap-probe-$(date '+%Y%m%d-%H%M%S').log.XXXXXX")
echo "Log: $LOG"
set +e
M1N1_KEEP_BAUD=1 M1N1TIMEOUT=3 PYTHONDONTWRITEBYTECODE=1 \
PYTHONPATH="$ROOT/m1n1_windows/proxyclient" "$PY" - "$DEV" > "$LOG" 2>&1 <<'PY'
import os
import signal
import subprocess
import sys
from m1n1.proxy import UartInterface

device = sys.argv[1]
iface = None
def deadline(signum, frame):
    raise TimeoutError('8-second probe deadline')
signal.signal(signal.SIGALRM, deadline)
signal.alarm(8)
try:
    if not os.path.exists(device):
        raise RuntimeError('serial node missing')
    owners = subprocess.run(['/usr/sbin/lsof', '-t', device], capture_output=True, text=True)
    if owners.stdout.strip():
        raise RuntimeError('serial in use; probe skipped')
    if owners.returncode not in (0, 1) or owners.stderr.strip():
        raise RuntimeError('cannot establish serial ownership; probe skipped')
    iface = UartInterface(device + ':115200')
    iface.dev.write_timeout = 3
    iface.tty_enable = False
    iface.cmd(iface.REQ_NOP)  # no optional feature negotiation
    iface.reply(iface.REQ_NOP)
    print('[FACT] PASS: m1n1 REQ_NOP response validated at 115200; no reboot performed.')
except Exception as exc:
    print(f'[UNVERIFIED] INCONCLUSIVE: {type(exc).__name__}: {exc}')
    print('No reboot/retry performed. This does not establish target boot state.')
    sys.exit(2)
finally:
    signal.alarm(0)
    if iface is not None:
        iface.dev.close()
PY
result=$?
cat "$LOG"
exit "$result"
