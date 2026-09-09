"""Read-only T8103 cluster state before UEFI with S140 host initialization omitted.

STATUS is a selected operating point, not a calibrated frequency measurement.
Do not claim the requested state is the delivered rate under throttling.
"""
import json
from pathlib import Path
import os

if p.get_chipid() != 0x8103:
    raise RuntimeError('S164 state capture is only defined for T8103')

states = {}
for name, base in [('E', 0x210e00000), ('P', 0x211e00000)]:
    cmd = p.read64(base + 0x20020)
    status = p.read64(base + 0x20050)
    states[name] = {
        'command': hex(cmd), 'status': hex(status),
        'desired1': cmd & 0x1f, 'desired2': (cmd >> 12) & 0xf,
        'busy': bool(cmd & (1 << 31)), 'apsc_busy': bool(cmd & (1 << 7)), 'apsc_disabled': bool(cmd & (1 << 22)),
        'status_current': (status >> 4) & 0xf, 'status_target': status & 0xf,
        'ppt_control': hex(p.read64(base + 0x48400)),
        'llc_control': hex(p.read64(base + 0x40240)),
        'amx_control': hex(p.read64(base + 0x40250)),
    }
hv.log('[S164] read-only cluster state ' + json.dumps(states, sort_keys=True))
if os.environ.get('NWOAS_LOG'):
    Path(os.environ['NWOAS_LOG'] + '.cpustate.json').write_text(
        json.dumps({'phase': 'pre-UEFI; S140 host init omitted', 'clusters': states}, indent=2) + '\n')
