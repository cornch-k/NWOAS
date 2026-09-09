#!/bin/bash
# NWOAS S6-D1: measure AFK ring geometry on unchanged Tahoe firmware.
# [DESIGN] Diagnostic-only AFK change; reset the already-crashed DCP before
# chainload so its old failure does not hide the ring negotiation.
# PASS: NWOAS-AFK-GEOMETRY records actual size/bufsz. Not display/Windows PASS.
# Safety: ZERO machine reboots here, one chainload, no guest, no kmutil,
# no disk writes on mini, 115200 fixed. Do not terminate during upload.
set -eu
echo "STOP: S6-D1 chainload/reset path is inconclusive; use prepared 1TR diagnostic package." >&2
exit 1
ROOT=/Volumes/X31/NWOAS
export NWOAS_AFK_ACTION="${1:-chainload}"
export M1N1DEVICE=/dev/cu.debug-console M1N1_KEEP_BAUD=1
export PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$ROOT/m1n1_windows/proxyclient"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-afk-diag-$(date '+%Y%m%d-%H%M%S').XXXXXX")
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import hashlib
import pathlib
import runpy
import subprocess
import sys
import os
root = pathlib.Path('/Volumes/X31/NWOAS')
payload = root / 'experiments/tahoe-afk-20260906/m1n1-afk-diag.bin'
assert hashlib.sha1(payload.read_bytes()).hexdigest() == 'dd8a02bf19ed25e6d8cb33392e56aea4fb58306f'
owners = subprocess.run(['/usr/sbin/lsof', '-t', '/dev/cu.debug-console'], capture_output=True)
if owners.stdout.strip() or owners.stderr.strip() or owners.returncode not in (0, 1):
    raise SystemExit('STOP: serial busy or ownership uncertain')
if os.environ['NWOAS_AFK_ACTION'] in ('resume-d1', 'dcp-restart'):
    from m1n1.proxy import UartInterface, M1N1Proxy
    from m1n1.proxyutils import bootstrap_port
    from m1n1.adt import load_adt
    from types import SimpleNamespace
    iface = UartInterface('/dev/cu.debug-console:115200')
    p = M1N1Proxy(iface)
    bootstrap_port(iface, p)
    assert p.get_base() == 0x8020bc000
    u = SimpleNamespace(adt=load_adt((root / 'experiments/tahoe-afk-20260906/current.adt').read_bytes()))
else:
    from m1n1.setup import p, u, iface
assert u.adt.model == 'Macmini9,1', 'Wrong target'
if os.environ['NWOAS_AFK_ACTION'] == 'dcp-restart':
    cpu = u.adt['/arm-io/dcp'].get_reg(0)[0]
    node = u.adt['/arm-io/pmgr']
    dev = next(dev for dev in node.devices if dev.name == 'DISP0_CPU0')
    reg = node.ps_regs[dev.psreg]
    addr = node.get_reg(reg.reg)[0] + reg.offset + dev.psidx * 8
    print(f'[FACT] ASC control before stop={p.read32(cpu + 0x44):#x}')
    assert (p.read32(addr) >> 4) & 15 == 15
    p.clear32(cpu + 0x44, 0x10)
    p.set32(addr, 1 << 10)
    p.set32(addr, 1 << 31)
    p.udelay(10)
    p.clear32(addr, 1 << 31)
    p.clear32(addr, 1 << 10)
    print(f'[FACT] ASC control after reset={p.read32(cpu + 0x44):#x}')
    print('[FACT] display_start_dcp result:', p.display_start_dcp())
    raise SystemExit(0)
if os.environ['NWOAS_AFK_ACTION'] == 'inspect':
    for dev in u.adt['/arm-io/pmgr'].devices:
        if any(part in dev.name for part in ('DISP', 'DCP')):
            print(dev)
    print('DCP power-gates:', u.adt['/arm-io/dcp'].power_gates)
    (root / 'experiments/tahoe-afk-20260906/current.adt').write_bytes(u.adt.build())
    raise SystemExit(0)
if os.environ['NWOAS_AFK_ACTION'] == 'resume-d1':
    # One-session recovery of the already uploaded image; no second upload.
    # Addresses from 215841.Yhzf12 and the unchanged chainload.py layout.
    from m1n1 import asm
    stub_addr, image_size, new_base = 0x80c048200, 0x7e4000, 0x8020bc000
    image_addr = stub_addr - image_size
    entry = new_base + 0x800
    expected_stub = asm.ARMAsm(f'''
1:
        ldp x4, x5, [x1], #16
        stp x4, x5, [x2]
        dc cvau, x2
        ic ivau, x2
        add x2, x2, #16
        sub x3, x3, #16
        cbnz x3, 1b
        ldr x1, ={entry}
        br x1
''', stub_addr)
    actual_stub = iface.readmem(stub_addr, expected_stub.len)
    print('Stub actual:', actual_stub.hex(), 'expected:', expected_stub.data.hex())
    reset_name = b'DISP0_CPU0\0'
    assert actual_stub in (expected_stub.data, reset_name + expected_stub.data[len(reset_name):]), 'Unexpected staged stub damage'
    assert iface.readmem(image_addr, 256) == payload.read_bytes()[:256]
    # chainload.py places its stub beyond its allocation. The extra string
    # argument reused that address; restore only after exact damage matching.
    iface.writemem(stub_addr, expected_stub.data)
    p.dc_cvau(stub_addr, expected_stub.len)
    p.ic_ivau(stub_addr, expected_stub.len)
    node = u.adt['/arm-io/pmgr']
    devices = [dev for dev in node.devices if dev.name == 'DISP0_CPU0']
    assert len(devices) == 1
    dev = devices[0]
    assert not dev.flags.no_ps
    reg = node.ps_regs[dev.psreg]
    addr = node.get_reg(reg.reg)[0] + reg.offset + dev.psidx * 8
    value = p.read32(addr)
    print(f'[FACT] DCP reset register={addr:#x} value={value:#x}')
    assert (value >> 4) & 15 == 15, 'DCP not active'
    # Same sequence as src/pmgr.c; avoids cached C ADT pointers invalidated
    # by chainload.py push_adt(). All addresses resolved from actual ADT.
    p.set32(addr, 1 << 10)
    p.set32(addr, 1 << 31)
    p.udelay(10)
    p.clear32(addr, 1 << 31)
    p.clear32(addr, 1 << 10)
    print('[FACT] Direct DCP reset complete; resuming uploaded image')
    p.reload(stub_addr, new_base + image_size - 0x4000, image_addr, new_base, image_size)
    iface.nop()
    print('[FACT] Diagnostic proxy alive')
    raise SystemExit(0)
if os.environ['NWOAS_AFK_ACTION'] != 'chainload':
    raise SystemExit('Unknown action')
original_reload = p.reload
def reset_dcp_then_reload(*args, **kwargs):
    # Upload completed before this hook; existing public proxy reset API.
    result = p.pmgr_reset(0, 'DISP0_CPU0')
    print('[FACT] DCP reset result:', result, flush=True)
    if result != 0:
        raise RuntimeError('DCP reset failed; no chainload jump')
    return original_reload(*args, **kwargs)
p.reload = reset_dcp_then_reload
sys.argv = ['chainload.py', '-r', str(payload)]
runpy.run_path(str(root / 'm1n1_windows/proxyclient/tools/chainload.py'), run_name='__main__')
PY
