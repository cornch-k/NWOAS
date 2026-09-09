#!/bin/bash
# S6-D8: one experimental AFK interface-3 in-band getModeCount command.
# Hypothesis: AFKEPV2 command header can carry existing read-only iBoot opcode3.
# PASS: a response matching command/category; mode counts require separate decoding.
# Reattach only to exact post-D7 state. No reboot/reset/init/modeset/allocations.
# Nominal baud remains 115200; use the verified direct USB CDC transport.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-inband-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
from pathlib import Path
from types import SimpleNamespace
import os, signal, struct, subprocess, time
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.fw.afk.rbep import AFKRingBuf
port = '/dev/cu.usbmodemC07HL05SQ6NY1'
assert not subprocess.run(['/usr/sbin/lsof','-t',port], capture_output=True).stdout.strip()
def timeout(*args): raise TimeoutError('bounded in-band experiment')
signal.signal(signal.SIGALRM, timeout)
signal.alarm(25)
i = UartInterface(port + ':115200')
i.dev.timeout = 3
i.dev.write_timeout = 3
try:
    i.cmd(i.REQ_NOP); i.reply(i.REQ_NOP)
    p = M1N1Proxy(i)
    assert p.get_base() == 0x803a84000, 'Boot changed'
    shared, asc = 0x80d224000, 0x231c00000
    fixture = Path('/Volumes/X31/NWOAS/nwoas_scripts/logs/tahoe-dcp-compat-20260906-224851.R8x5nC.msg1.bin').read_bytes()
    assert i.readmem(shared + 0x4180, len(fixture)) == fixture
    ep = SimpleNamespace(iface=i)
    tx, rx = AFKRingBuf(ep,shared,0x4000),AFKRingBuf(ep,shared+0x4000,0x4000)
    for ring in (tx,rx):
        assert ring.block_size == 128
        ring.rptr,ring.wptr = ring.get_rptr(),ring.get_wptr()
    assert (tx.rptr,tx.wptr,rx.rptr,rx.wptr) == (256,256,256,256), 'State changed; do not repeat'
    assert p.read32(asc+0x48) & 3 == 1
    assert p.read32(asc+0x8114) & (1<<17), 'Pending mailbox must be inspected first'
    # [DESIGN] in-band command, cookie=0, category=1, opcode=0xc0.
    # Command header: flags=0, command sequence=0, max response=20.
    iboot = struct.pack('<IIII',3,16,0,0)
    body = struct.pack('<QBBB5x',0,0xc0,1,0) + struct.pack('<BBHI',0,0,0,20) + iboot
    packet = bytes(8) + struct.pack('<BBHI',1,0,3,len(body)) + body
    print('[DESIGN] in-band query packet excluding QE magic/size:',packet.hex())
    wptr = tx.write(packet)
    p.write64(asc+0x8800,(0xa2<<48)|wptr)
    p.write64(asc+0x8808,0x23)
    print('[FACT] ONE in-band mode-count request sent; TX wptr',hex(wptr))
    deadline = time.monotonic()+10
    count = 0
    while time.monotonic() < deadline:
        if not p.read32(asc+0x8114) & (1<<17):
            m0,m1=p.read64(asc+0x8830),p.read64(asc+0x8838)
            print('[FACT] mailbox',hex(m0),hex(m1))
            if m1&255 != 0x23:
                print('[STOP] Unexpected endpoint; captured without reply')
                break
        for data in rx.read():
            count += 1
            frame=struct.pack('<4sI',b'IOP ',len(data)-8)+data
            Path(os.environ['NWOAS_PROTOCOL_LOG']+f'.msg{count}.bin').write_bytes(frame)
            print('[FACT] RX',frame.hex())
        if count: break
        time.sleep(.02)
    print('[FACT] Final pointers',tx.get_rptr(),tx.get_wptr(),rx.get_rptr(),rx.get_wptr())
    print('[FACT] CPU status',hex(p.read32(asc+0x48)),'frames',count)
finally:
    signal.alarm(0)
    i.dev.close()
PY
