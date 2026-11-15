"""Data/rodata/reloc comparison for builds whose .data moved (bindiff.py asserts
equal layout). Usage: tailcheck.py A.elf A.bin B.elf B.bin path/to/bindiff.py"""
import runpy, sys, struct
g = runpy.run_path(sys.argv[5], run_name='lib')
a = g['load'](sys.argv[1], sys.argv[2]); b = g['load'](sys.argv[3], sys.argv[4])
sa, sb = g['symbolizer'](a), g['symbolizer'](b)
for k in ('_text_size', '_rodata_end', '_rela_start', '_rela_end', '_data_start', '_bss_start'):
    print(f'{k:12s} A={a["nmap"][k]:#x} B={b["nmap"][k]:#x} delta={b["nmap"][k]-a["nmap"][k]:#x}')
da = a['img'][a['nmap']['_data_start']:a['nmap']['_bss_start']]
db = b['img'][b['nmap']['_data_start']:b['nmap']['_bss_start']]
print('.data size A/B', hex(len(da)), hex(len(db)), 'identical:', da == db)
if da != db and len(da) == len(db):
    diffs = [i for i in range(0, len(da), 8) if da[i:i+8] != db[i:i+8]]
    print('  differing 8-byte words in .data:', len(diffs), [hex(a['nmap']['_data_start']+i) for i in diffs[:8]])
ta = a['img'][a['nmap']['_bss_start']:]; tb = b['img'][b['nmap']['_bss_start']:]
print('bss-region file bytes: A len', hex(len(ta)), 'B len', hex(len(tb)), 'identical:', ta == tb,
      'nonzero bytes A/B:', sum(1 for x in ta if x), sum(1 for x in tb if x))
stra = g['strings'](a['img'], a['nmap']['_text_size'], a['nmap']['_rela_start'])
strb = g['strings'](b['img'], b['nmap']['_text_size'], b['nmap']['_rela_start'])
print('rodata strings only in A:', len(stra - strb)); [print('   A', s) for s in sorted(stra - strb)]
print('rodata strings only in B:', len(strb - stra)); [print('   B', s) for s in sorted(strb - stra)]
ra, rb = g['relocs'](a, sa), g['relocs'](b, sb)
print(f'relocations: A={len(ra)} B={len(rb)} symbolized-equal={ra == rb}')
from collections import Counter
ca, cb = Counter(map(str, ra)), Counter(map(str, rb))
import re
m = lambda r: (r[1], re.sub(r'\?0x[0-9a-f]+', '?X', r[2]))
na, nb = Counter(map(m, ra)), Counter(map(m, rb))
print('relocations by (type, target) with anonymous addresses masked: only-A', sum((na-nb).values()), 'only-B', sum((nb-na).values()))
for k, v in (na - nb).items(): print('   reloc only A', v, k)
for k, v in (nb - na).items(): print('   reloc only B', v, k)
