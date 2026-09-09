#!/usr/bin/env python3
"""Read-only raw code-pointer candidates; this is not a stack unwind."""
import sys, json, importlib.util, bisect, struct
from pathlib import Path
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('extra',root/'nvme-s158/dump_extra_map.py')
mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
d=mod.Dump(sys.argv[1]);d.parse();d.parse_modules();d.parse_regions()
maps={}
for name,file in [('ntoskrnl.exe','kernel'),('storport.sys','storport'),('stornvme.sys','stornvme')]:
    rows=json.loads((root/f'nvme-s159/{file}-symbol-map.json').read_text())
    maps[name]=(rows,[x[0] for x in rows])
print('Raw code-pointer candidates, not unwound frames')
for kind,start,end,off,label in d.regions:
    hits=[]
    # VA alignment, not file alignment.
    for delta in range((-start)%8,end-start-7,8):
        val=d.u64(off+delta)
        for base,limit,name in d.modules:
            if base<=val<limit and name.lower() in maps:
                rva=val-base;rows,keys=maps[name.lower()]
                idx=bisect.bisect_right(keys,rva)-1
                sym=f'{rows[idx][1]}+0x{rva-rows[idx][0]:x}' if idx>=0 else '?'
                hits.append(f'  {start+delta:#018x}: {val:#018x} {name}+{rva:#x} {sym}')
    if hits:
        print(f'{label}: {start:#x}..{end:#x}')
        print('\n'.join(hits))
