"""Opt-in pre-UEFI P7 -> P12 comparison. Leave existing APSC/throttles alone.

No turbo states, SMC ownership change, Windows clock-report patch or MCC writes.
The default launchers do not load this module. A failed transition attempts P7
and aborts this launch rather than silently booting an unverified setting.
"""
import os
import time

P_CMD = 0x211e20020
PS_FIELDS = 0xf01f  # DESIRED1[4:0], DESIRED2[15:12], T8103 only
SET = 1 << 25
BUSY = 1 << 31
PROTECTED = (1 << 22) | (1 << 42)  # APSC disable and fixed-PLL relock


def wait_idle(proxy):
    # Polling is through USB RPC before Windows starts. Check a 250ms host
    # budget between completed reads; the transport has its own timeout.
    # This is not a claim of 400us hardware latency.
    deadline = time.monotonic() + 0.25
    while True:
        value = proxy.read64(P_CMD)
        if not value & BUSY:
            return value
        if time.monotonic() >= deadline:
            raise TimeoutError('S164 P-state transition stayed busy')


def request_state(proxy, state, reference):
    wait_idle(proxy)  # Also applies to fallback: do not write another request while BUSY.
    proxy.mask64(P_CMD, PS_FIELDS, SET | state | (state << 12))
    value = wait_idle(proxy)
    if value & PS_FIELDS != state | (state << 12):
        raise RuntimeError('S164 requested P-state readback mismatch')
    if (value ^ reference) & PROTECTED:
        raise RuntimeError('S164 pre-existing CPU feature controls changed')
    return value


if os.environ.get('NWOAS_CPU_PSTATE') != '12':
    raise RuntimeError('S164 P12 module needs an explicit NWOAS_CPU_PSTATE=12')
if p.get_chipid() != 0x8103:
    raise RuntimeError('S164 P12 module is T8103 only')
before = p.read64(P_CMD)
if before & BUSY or before & PS_FIELDS != 7 | (7 << 12):
    raise RuntimeError('S164 P12 comparison requires the validated S140 P7 baseline')
try:
    after = request_state(p, 12, before)
except (RuntimeError, TimeoutError) as error:
    hv.log(f'[S164] P12 request failed: {error}; attempting bounded P7 restoration')
    try:
        restored = request_state(p, 7, before)
    except (RuntimeError, TimeoutError) as restore_error:
        hv.log(f'[S164] P7 restoration failed or skipped while busy: {restore_error}; abort launch')
        raise
    hv.log(f'[S164] P12 failed; P7 restored, command={restored:#018x}')
    raise
status = p.read64(P_CMD + 0x30)
hv.log(f'[S164] requested P12 after P7 baseline: {before:#018x} -> {after:#018x}; actual performance unmeasured')
hv.log(f'[S164] P-cluster selected status={status:#018x}, current={(status>>4)&15}, target={status&15}, apsc_busy={bool(after & (1<<7))}')
