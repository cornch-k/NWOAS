#!/usr/bin/env python3
"""Verify observed S161 host/UEFI backing and dynamic wide-DART coverage."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('log',type=Path);a=p.parse_args()
s=a.log.read_text(errors='replace');v=Path(str(a.log)+'.vuart').read_text(errors='replace')
def one(pattern,body):
 m=re.findall(pattern,body)
 if len(m)!=1:raise ValueError(f'Expected one {pattern!r}; found {len(m)}')
 return m[0]
host=int(one(r'\[S102\].*window backing (0x[0-9a-fA-F]+) is now outside guest RAM',s),16)
early=int(one(r'EARLY DART IOVA .* -> backing PA (0x[0-9a-fA-F]+)',v),16)
x=one(r'DART backing computed (0x[0-9a-fA-F]+) \(phys_base=(0x[0-9a-fA-F]+) mem_size=(0x[0-9a-fA-F]+) guest_end=(0x[0-9a-fA-F]+) win=(0x[0-9a-fA-F]+) mode=1 valid=1\)',v)
backing,phys,mem,guest_end,win=map(lambda z:int(z,16),x)
x=one(r'WIDE-DART reserved L1\[(\d+)\.\.(\d+)\] tables=(\d+) l2base=(0x[0-9a-fA-F]+) phys_base=(0x[0-9a-fA-F]+) backing=(0x[0-9a-fA-F]+)',v)
first,last,count=map(int,x[:3]);widephys,wideback=map(lambda z:int(z,16),x[4:])
x=one(r'WIDE-DART identity \[(0x[0-9a-fA-F]+)\.\.(0x[0-9a-fA-F]+)\] L1\[(\d+)\.\.(\d+)\] tables=(\d+) ptes=(\d+)',v)
finalstart,finalend=map(lambda z:int(z,16),x[:2]);ff,fl,fc,ptes=map(int,x[2:])
valid=(host==early==backing==guest_end==wideback==finalend==phys+mem and phys==widephys==finalstart and
       win==0x100000000 and first==ff==phys>>25 and last==fl==(backing-1)>>25 and count==fc==last-first+1 and ptes*0x4000>=mem)
r={'pass':valid,'log':str(a.log),'host_backing':hex(host),'early_backing':hex(early),'backing':hex(backing),'phys_base':hex(phys),'mem_size':hex(mem),'wide_l1':[first,last],'wide_tables':count,'final_identity_ptes':ptes}
print(json.dumps(r,indent=2));raise SystemExit(0 if valid else 1)
