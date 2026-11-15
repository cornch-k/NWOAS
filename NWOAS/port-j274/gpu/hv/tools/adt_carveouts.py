import sys
sys.path.insert(0, sys.argv[1])
from m1n1.adt import load_adt
adt = load_adt(open(sys.argv[2], 'rb').read())
GB, HB, SB = 0xBFFFB8000, 0xBFFF70000, 0xBFFF78000
sgx = adt['/arm-io/sgx']
print('sgx property names:', sorted(sgx._properties.keys()))
cm = adt['/chosen/carveout-memory-map']
ids = []
for k, v in cm._properties.items():
    if k.startswith('region-id-'):
        b, s = v[0], v[1]; ids.append(int(k[10:]))
        tag = ''.join(t for t, x in (('[gpu-region]', GB), ('[handoff]', HB), ('[shared]', SB)) if b <= x < b + s)
        print(f'{k:16s} 0x{b:011x} size 0x{s:x} {tag}')
print('region ids:', len(ids), 'max', max(ids))
mem = adt['/chosen']
for k in ('dram_base', 'dram_size'):
    print(k, hex(getattr(mem, k)) if hasattr(mem, k) else 'n/a')
for k in sorted(mem._properties.keys()):
    if 'memory' in k or 'dram' in k or 'region' in k: print('chosen', k)
