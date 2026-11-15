"""Offline replica of nwoas_agx_probe() field reads, on a saved ADT dump.
Usage: python3 -I adt_probe_offline.py <proxyclient dir> <adt dump>"""
import sys, struct
sys.path.insert(0, sys.argv[1])
from m1n1.adt import load_adt
adt = load_adt(open(sys.argv[2], 'rb').read())
root = adt
print('target-type', getattr(root, 'target_type', None), 'model', getattr(root, 'model', None))
ch = adt['/chosen']
for k in ('board_id', 'chip_id', 'firmware_version', 'system_firmware_version'):
    try: print('chosen', k, getattr(ch, k))
    except Exception as e: print('chosen', k, 'ERR', e)
def props(node, names):
    for n in names:
        try: v = getattr(node, n.replace('-', '_'))
        except Exception as e: v = f'MISSING ({type(e).__name__})'
        print(f'  {n}: {v}')
for path in ('/arm-io/sgx', '/arm-io/gfx-asc'):
    try: n = adt[path]
    except Exception as e:
        print(path, 'not found', e); continue
    print(path, 'compatible', getattr(n, 'compatible', None))
    try:
        for i, r in enumerate(n.reg): print(f'  reg[{i}] raw', r, 'xlated', n.get_reg(i))
    except Exception as e: print('  reg ERR', e)
    try: print('  interrupts', list(n.interrupts))
    except Exception as e: print('  interrupts MISSING', e)
sgx = adt['/arm-io/sgx']
props(sgx, ['gpu-region-base', 'gpu-region-size', 'gfx-handoff-base', 'gfx-handoff-size',
            'gfx-shared-region-base', 'gfx-shared-region-size', 'perf-state-count',
            'perf-state-table-count', 'gpu-num-perf-states', 'gpu-perf-base-pstate',
            'gpu-power-sample-period'])
try: print('  perf-states len', len(sgx._properties['perf-states']) if hasattr(sgx, '_properties') else 'n/a')
except Exception as e: print('  perf-states', e)
cm = adt['/chosen/carveout-memory-map']
for k in sorted(cm._properties.keys() if hasattr(cm, '_properties') else []):
    print('  carveout', k, cm._properties[k])
