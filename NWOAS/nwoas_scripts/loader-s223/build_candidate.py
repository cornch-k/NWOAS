#!/usr/bin/env python3
"""Prepare an uninstalled S216 payload with deterministic FD erase-fill tail.
No live image, installed boot object, source checkout or device is modified.
"""
from pathlib import Path
import hashlib,json
S=Path(__file__).resolve().parents[1];D=Path(__file__).resolve().parent
source=S/'firmware-s216/m1n1-payload-s216-compat.bin';b=source.read_bytes()
old='0ae1cb75bfc3cbc2b7b15e6408c489fe5badd3d12d53fc4353a85b6b5ad3d197'
assert hashlib.sha256(b).hexdigest()==old
fd_offset=0x20c000+0x10000;fd=b[fd_offset:]
assert len(fd)==0x1d88000 and int.from_bytes(fd[16:24],'little')==0x1e00000
pad=0x1e00000-len(fd);assert pad==0x78000
candidate=b+b'\xff'*pad
assert candidate[:len(b)]==b and len(candidate)-fd_offset==0x1e00000
assert len(candidate)%0x4000==0
out=D/'m1n1-payload-s223-fd-tail.bin'
if out.exists():assert out.read_bytes()==candidate,'existing distinct candidate preserved'
out.write_bytes(candidate)
j={'status':'ASSEMBLY VERIFIED; consult hardware-result and soak-result for runtime status','source_payload_sha256':old,'payload_sha256':hashlib.sha256(candidate).hexdigest(),'bytes':len(candidate),'fd_offset':fd_offset,'original_fd_bytes':len(fd),'image_size':0x1e00000,'padding_bytes':pad,'padding_value':'0xff (FD ErasePolarity=1)','original_bytes_unchanged':True,'known_input_copy_span_fully_supplied':True,'runtime_changed':False,'note':'Removes source overread into following load_raw allocations. No claim this caused a Windows watchdog. Runtime qualification is not inferred by this assembler.'}
(D/'manifest.json').write_text(json.dumps(j,indent=2)+'\n');print(json.dumps(j))
