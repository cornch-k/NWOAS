#!/bin/bash
# NWOAS S6-P1: first bootstrap boot after DFU + approved kmutil installation.
# [DESIGN] Hypothesis: the installed known-good reaches m1n1 proxy on current firmware.
# PASS: checksum-validated REQ_NOP at 115200; Windows/HID remain unverified.
# INCONCLUSIVE: no proxy response. Capture banner before deciding; silence is not proof.
# Safety: ONE reboot serial, no retries, no chainload, no kmutil, no process kills.
# Host .env is stdin-only to sudo and never printed. Capture starts BEFORE reboot.
set -eu
ROOT=/Volumes/X31/NWOAS
export M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" - <<'PY'
from pathlib import Path
import datetime
import signal
import subprocess
import threading
import time
import serial
from m1n1.proxy import UartInterface

root = Path('/Volumes/X31/NWOAS')
stem = 'bootstrap-serial-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
log = root / 'nwoas_scripts/logs' / (stem + '.log')
raw = log.with_suffix('.serial.bin')
print('LOG:', log, flush=True)
owners = subprocess.run(['/usr/sbin/lsof', '-t', '/dev/cu.debug-console'], capture_output=True)
if owners.stdout.strip() or owners.stderr.strip() or owners.returncode not in (0, 1):
    raise SystemExit('STOP: serial ownership uncertain or busy. No reboot performed.')
password = (root / '.env').read_bytes().rstrip(b'\r\n')
if not password or b'\n' in password or b'\r' in password:
    raise SystemExit('STOP: invalid host credential file; contents withheld.')
port = serial.Serial('/dev/cu.debug-console', 115200, timeout=0.1, write_timeout=3)
stop = threading.Event()
chunks = []
errors = []
def capture():
    try:
        with raw.open('xb') as output:
            while not stop.is_set():
                data = port.read(4096)
                if data:
                    chunks.append(data)
                    output.write(data)
                    output.flush()
    except Exception as exc:
        errors.append(type(exc).__name__ + ': ' + str(exc))
thread = threading.Thread(target=capture)
thread.start()
try:
    with log.open('x') as output:
        started = time.monotonic()
        print('ONE reboot serial; banner capture already active.', flush=True)
        result = subprocess.run(
            ['sudo', '-S', '-k', '-p', '', str(root / 'macvdmtool/macvdmtool'), 'reboot', 'serial'],
            input=password+b'\n', stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        report = (result.stdout + result.stderr).replace(password, b'[REDACTED]').decode(errors='replace')
        password = b''
        output.write(report)
        output.flush()
        print(report, end='', flush=True)
        if result.returncode:
            raise RuntimeError(f'VDM exit={result.returncode}; no reboot retry')
        remaining = max(0, 35 - (time.monotonic() - started))
        stop.wait(remaining)
        stop.set()
        thread.join(timeout=2)
        if thread.is_alive() or errors:
            raise RuntimeError('Serial capture failed: ' + ', '.join(errors))
        banner = b''.join(chunks).decode(errors='replace')
        output.write('\n--- serial banner ---\n' + banner + '\n')
        print(f'Captured {sum(map(len, chunks))} bytes. Banner tail:\n' + '\n'.join(banner.splitlines()[-35:]), flush=True)
        iface = UartInterface(port)
        iface.tty_enable = False
        iface.dev.timeout = 3
        def deadline(signum, frame):
            raise TimeoutError('NOP deadline')
        signal.signal(signal.SIGALRM, deadline)
        signal.alarm(8)
        try:
            iface.cmd(iface.REQ_NOP)
            iface.reply(iface.REQ_NOP)
            verdict = '[FACT] PASS: m1n1 proxy NOP validated at 115200 after one reboot.'
        except Exception as exc:
            verdict = f'[UNVERIFIED] INCONCLUSIVE: {type(exc).__name__}: {exc}; no reboot retry.'
        finally:
            signal.alarm(0)
        output.write('\n' + verdict + '\n')
        print(verdict, flush=True)
finally:
    password = b''
    stop.set()
    thread.join(timeout=2)
    port.close()
PY
