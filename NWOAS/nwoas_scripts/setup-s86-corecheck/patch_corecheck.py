#!/usr/bin/env python3
"""S86: exact ARM64 22621.1 winsetup.dll minimum cores 2 -> 1.
Only three comparisons and their diagnostic threshold change, plus PE checksum.
Input hash and instruction semantics are mandatory. Zero cores still fails.
Does not activate CPUs, change firmware, install Windows, or touch disks.
"""
from pathlib import Path
import hashlib,json,struct
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
ROOT=Path(__file__).resolve().parent
src=ROOT/'original/winsetup.dll'
b=bytearray(src.read_bytes())
assert hashlib.sha256(b).hexdigest()=='5368c4f724a56f89c7cb907bc4580ef6884ede01e7d19a3f063e8344b1de7189'
pe=struct.unpack_from('<I',b,0x3c)[0]
assert struct.unpack_from('<H',b,pe+4)[0]==0xaa64
md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
patches=[]
for rva,kind,delta in [(0x8ec5c,'cmp',0x400),(0x8ec64,'cmp',0x400),(0x8ec80,'mov',0x20),(0x8ecd0,'cmp',0x400)]:
 off=rva-0xc00;old=bytes(b[off:off+4]);ins=list(md.disasm(old,0x180000000+rva))[0]
 assert (ins.mnemonic,ins.op_str)==(kind,'w21, #2' if kind=='cmp' else 'w4, #2')
 new=struct.pack('<I',struct.unpack('<I',old)[0]-delta)
 ins2=list(md.disasm(new,ins.address))[0]
 assert (ins2.mnemonic,ins2.op_str)==(kind,'w21, #1' if kind=='cmp' else 'w4, #1')
 b[off:off+4]=new
 patches.append(dict(rva=hex(rva),offset=hex(off),before=old.hex(),after=new.hex(),disassembly=f'{ins2.mnemonic} {ins2.op_str}'))
# Final cset w0,hs is retained, so success is unsigned count >= threshold.
assert list(md.disasm(bytes(b[0x8ecd4-0xc00:0x8ecd8-0xc00]),0x18008ecd4))[0].op_str=='w0, hs'
checksum_off=pe+24+64
struct.pack_into('<I',b,checksum_off,0)
s=0
for i in range(0,len(b),2):
 w=int.from_bytes(b[i:i+2],'little');s+=w;s=(s&0xffff)+(s>>16)
s=(s&0xffff)+(s>>16);s=(s+len(b))&0xffffffff
struct.pack_into('<I',b,checksum_off,s)
orig=src.read_bytes();allowed=set(range(checksum_off,checksum_off+4))
for p in patches:allowed.update(range(int(p['offset'],16),int(p['offset'],16)+4))
assert all(i in allowed for i,(a,z) in enumerate(zip(orig,b)) if a!=z)
out=ROOT/'patched';out.mkdir(exist_ok=True);(out/'winsetup.dll').write_bytes(b)
report={'experiment':'S86','original_sha256':hashlib.sha256(orig).hexdigest(),'patched_sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b),'patches':patches,'pe_checksum':hex(s),'signature':'Authenticode signature no longer valid after research patch; loader acceptance unverified','boundary_cases':[{'cores':n,'original':n>=2,'patched':n>=1} for n in [0,1,2,8]],'hardware_result':'UNVERIFIED'}
(ROOT/'patch-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
