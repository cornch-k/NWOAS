#!/usr/bin/env python3
"""Parse the module map and ARM64 stack candidates from a Windows triage dump."""
import struct
import sys
from pathlib import Path


def u32(blob, off):
    return struct.unpack_from('<I', blob, off)[0]


def u64(blob, off):
    return struct.unpack_from('<Q', blob, off)[0]


blob = Path(sys.argv[1]).read_bytes()
if blob[:8] != b'PAGEDU64' or u32(blob, 0xF98) != 4:
    raise SystemExit('expected a 64-bit Windows triage dump')

t = 0x2000
stack_off, stack_size = u32(blob, t + 0x28), u32(blob, t + 0x2C)
modules_off, modules_count = u32(blob, t + 0x30), u32(blob, t + 0x34)
strings_off = u32(blob, t + 0x38)

def name_at(off):
    chars = u32(blob, off)
    return blob[off + 4:off + 4 + chars * 2].decode('utf-16le', 'replace')

modules = []
for index in range(modules_count):
    entry = modules_off + index * 0x90
    name_off = u32(blob, entry)
    base = u64(blob, entry + 0x38)
    size = u64(blob, entry + 0x48)
    modules.append((base, base + size, name_at(name_off)))

def module_at(address):
    for start, end, name in modules:
        if start <= address < end:
            return start, name
    return None

context = 0x348
sp = u64(blob, context + 0x100)
pc = u64(blob, context + 0x108)
print(f'bugcheck={u32(blob, 0x38):#x} params={[hex(u64(blob, 0x40+i*8)) for i in range(4)]}')
print(f'processors={u32(blob, 0x34)} sp={sp:#x} pc={pc:#x} module={module_at(pc)}')
print(f'stack_file={stack_off:#x}+{stack_size:#x} top={u64(blob, t+0x48):#x}')
print('stack module candidates:')
last = None
for off in range(stack_off, stack_off + stack_size, 8):
    address = u64(blob, off)
    hit = module_at(address)
    if hit:
        item = (hit[1], address - hit[0])
        if item != last:
            print(f'  file+{off-stack_off:#05x} {address:#018x} {item[0]}+{item[1]:#x}')
        last = item
    else:
        last = None

print('modules:')
for start, end, name in modules:
    print(f'  {start:#018x}-{end:#018x} {name}')
