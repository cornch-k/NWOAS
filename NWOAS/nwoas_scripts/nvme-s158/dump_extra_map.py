#!/usr/bin/env python3
"""Bounded extra virtual-address map / lookup for a 64-bit Windows triage dump.

Independent diagnostic, read-only. Companion to rescue-s138/parse-triage.py,
which only maps the call-stack window and the driver list. A triage dump can
carry three more VA-backed regions that WinDbg uses to read data such as the
0x133 arg4 DPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK:

  * the call-stack window       (TopOfStack .. +SizeOfCallStack)
  * the optional data page      (DataPageAddress / DataPageOffset / DataPageSize)
  * the optional data blocks    (DataBlocksOffset, DataBlocksCount entries of
                                 TRIAGE_DATA_BLOCK {u64 Address; u32 Offset; u32 Size})

Layout reference: TRIAGE_DUMP64 / TRIAGE_DATA_BLOCK in Microsoft Research
Singularity RDK base/Windows/Inc/Dump.h (archived public source). The first ~0x48 bytes of TRIAGE_DUMP64
are the same fields parse-triage.py already relies on (offsets 0x28/0x2C
stack, 0x30/0x34 drivers, 0x38 strings, 0x48 TopOfStack), so the tail fields
used here (0x60.. DataPage*, 0x70.. DebuggerData*, 0x78.. DataBlocks*) are
read from the same structure, then *validated* against file bounds and
plausibility before being trusted. Nothing is decoded from a guessed layout:
covered bytes are printed as raw hex only. The internal layout of
DPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK is NOT known to this tool and is not decoded.

Usage:
  dump_extra_map.py DUMP [--va 0x...]... [--bytes N] [--scan]

  --va     extra virtual addresses to look up (arg4 of 0x133 is looked up
           automatically when the bugcheck code is 0x133)
  --bytes  maximum bytes of raw hex to print per hit (default 0x100)
  --scan   also search the whole file for the little-endian 8-byte value of
           each looked-up VA (pointer occurrences), bounded to 32 hits

Exit code 0 = ran, 2 = not a 64-bit triage dump / unreadable header.
"""
import argparse
import struct
import sys
from pathlib import Path

DUMP_SIGNATURE = b'PAGEDU64'
DUMP_TYPE_TRIAGE = 4
TRIAGE_HEADER_OFFSET = 0x2000
TRIAGE_DUMP_VALID = 0x44475254  # 'DGRT' multi-character constant
KDBG_OWNER_TAG = b'KDBG'
MAX_DATA_BLOCKS = 4096          # plausibility cap; real dumps carry far fewer
MAX_HEX_BYTES = 0x1000          # hard cap for --bytes
KERNEL_VA_MIN = 0xFFFF000000000000  # ARM64/x64 kernel canonical-high range

# TriageOptions bit names per public dbghelp headers (informational only).
TRIAGE_OPTION_BITS = {
    0x001: 'CONTEXT', 0x002: 'EXCEPTION', 0x004: 'PRCB', 0x008: 'PROCESS',
    0x010: 'THREAD', 0x020: 'STACK', 0x040: 'DRIVER_LIST', 0x080: 'BROKEN_DRIVER',
    0x100: 'MMINFO', 0x200: 'DATAPAGE', 0x400: 'DEBUGGER_DATA', 0x800: 'DATA_BLOCKS',
}


class Dump:
    def __init__(self, path):
        self.path = Path(path)
        self.blob = self.path.read_bytes()
        self.size = len(self.blob)
        self.notes = []          # unknowns / inconsistencies, reported verbatim
        self.regions = []        # (kind, va_start, va_end, file_off, label)

    # ---- bounded primitives -------------------------------------------------
    def in_file(self, off, length):
        return 0 <= off and 0 <= length and off + length <= self.size

    def u32(self, off):
        if not self.in_file(off, 4):
            raise ValueError(f'u32 read at {off:#x} outside file ({self.size:#x})')
        return struct.unpack_from('<I', self.blob, off)[0]

    def u64(self, off):
        if not self.in_file(off, 8):
            raise ValueError(f'u64 read at {off:#x} outside file ({self.size:#x})')
        return struct.unpack_from('<Q', self.blob, off)[0]

    def note(self, text):
        self.notes.append(text)

    # ---- header -------------------------------------------------------------
    def parse(self):
        b = self.blob
        if b[:8] != DUMP_SIGNATURE:
            raise SystemExit(f'not a PAGEDU64 dump: {b[:8]!r}')
        dump_type = self.u32(0xF98)
        if dump_type != DUMP_TYPE_TRIAGE:
            raise SystemExit(f'DumpType={dump_type} (expected {DUMP_TYPE_TRIAGE}=triage)')

        self.machine = self.u32(0x30)
        self.processors = self.u32(0x34)
        self.bugcheck = self.u32(0x38)
        self.params = [self.u64(0x40 + i * 8) for i in range(4)]

        t = TRIAGE_HEADER_OFFSET
        h = {}
        for name, off in (
            ('ServicePackBuild', 0x00), ('SizeOfDump', 0x04), ('ValidOffset', 0x08),
            ('ContextOffset', 0x0C), ('ExceptionOffset', 0x10), ('MmOffset', 0x14),
            ('UnloadedDriversOffset', 0x18), ('PrcbOffset', 0x1C), ('ProcessOffset', 0x20),
            ('ThreadOffset', 0x24), ('CallStackOffset', 0x28), ('SizeOfCallStack', 0x2C),
            ('DriverListOffset', 0x30), ('DriverCount', 0x34), ('StringPoolOffset', 0x38),
            ('StringPoolSize', 0x3C), ('BrokenDriverOffset', 0x40), ('TriageOptions', 0x44),
        ):
            h[name] = self.u32(t + off)
        h['TopOfStack'] = self.u64(t + 0x48)
        # 0x50..0x5f is the ArchitectureSpecific union, including IA64 BStore.
        # It occupies space in the serialized 64-bit header on ARM64 too.
        h['DataPageAddress'] = self.u64(t + 0x60)
        h['DataPageOffset'] = self.u32(t + 0x68)
        h['DataPageSize'] = self.u32(t + 0x6C)
        h['DebuggerDataOffset'] = self.u32(t + 0x70)
        h['DebuggerDataSize'] = self.u32(t + 0x74)
        h['DataBlocksOffset'] = self.u32(t + 0x78)
        h['DataBlocksCount'] = self.u32(t + 0x7C)
        self.h = h

        # SizeOfDump is the authoritative bound; never trust beyond the file.
        self.bound = min(h['SizeOfDump'], self.size) if h['SizeOfDump'] else self.size
        if h['SizeOfDump'] > self.size:
            self.note(f'SizeOfDump {h["SizeOfDump"]:#x} exceeds file size {self.size:#x} (truncated dump?)')
        elif h['SizeOfDump'] and h['SizeOfDump'] < self.size:
            self.note(f'file has {self.size - h["SizeOfDump"]:#x} trailing bytes past SizeOfDump')

        # Validity marker: informational, layout per public header.
        vo = h['ValidOffset']
        if self.in_file(vo, 4) and vo + 4 <= self.bound:
            v = self.u32(vo)
            if v != TRIAGE_DUMP_VALID:
                self.note(f'ValidOffset {vo:#x} holds {v:#x}, expected {TRIAGE_DUMP_VALID:#x}')
        else:
            self.note(f'ValidOffset {vo:#x} out of bounds')

        # Cross-check: the existing parser hardcodes the CONTEXT at 0x348.
        if h['ContextOffset'] != 0x348:
            self.note(f'ContextOffset={h["ContextOffset"]:#x}, parse-triage.py assumes 0x348')

    def bounded(self, off, size, what):
        """True if [off, off+size) lies within SizeOfDump and the file."""
        if size == 0:
            return False
        ok = off < self.bound and off + size <= self.bound
        if not ok:
            self.note(f'{what}: file range {off:#x}+{size:#x} exceeds bound {self.bound:#x}')
        return ok

    # ---- driver list (same layout parse-triage.py already uses) ------------
    def parse_modules(self):
        self.modules = []
        h = self.h
        stride = 0x90
        count = h['DriverCount']
        if not self.bounded(h['DriverListOffset'], count * stride, 'DriverList'):
            return
        for i in range(count):
            e = h['DriverListOffset'] + i * stride
            name_off = self.u32(e)
            base = self.u64(e + 0x38)
            size = self.u64(e + 0x48)
            name = '?'
            if self.in_file(name_off, 4):
                chars = self.u32(name_off)
                if chars <= 260 and self.in_file(name_off + 4, chars * 2):
                    name = self.blob[name_off + 4:name_off + 4 + chars * 2].decode('utf-16le', 'replace')
            if base < KERNEL_VA_MIN or size == 0 or size > 0x10000000:
                self.note(f'driver entry {i} implausible base={base:#x} size={size:#x} name={name}')
                continue
            self.modules.append((base, base + size, name))

    def module_of(self, va):
        for start, end, name in self.modules:
            if start <= va < end:
                return name, va - start
        return None

    # ---- extra VA regions -----------------------------------------------------
    def parse_regions(self):
        h = self.h
        # 1. call stack window
        if self.bounded(h['CallStackOffset'], h['SizeOfCallStack'], 'CallStack'):
            top = h['TopOfStack']
            if top >= KERNEL_VA_MIN:
                self.regions.append(('stack', top, top + h['SizeOfCallStack'],
                                     h['CallStackOffset'], 'call-stack window'))
            else:
                self.note(f'TopOfStack {top:#x} not a kernel VA; stack window unmapped')

        # 2. data page
        if h['DataPageSize'] or h['DataPageOffset'] or h['DataPageAddress']:
            if h['DataPageAddress'] >= KERNEL_VA_MIN and \
               self.bounded(h['DataPageOffset'], h['DataPageSize'], 'DataPage'):
                self.regions.append(('datapage', h['DataPageAddress'],
                                     h['DataPageAddress'] + h['DataPageSize'],
                                     h['DataPageOffset'], 'data page'))
            else:
                self.note(f'DataPage fields present but implausible: '
                          f'va={h["DataPageAddress"]:#x} off={h["DataPageOffset"]:#x} size={h["DataPageSize"]:#x}')

        # 3. data blocks
        self.data_blocks = []   # (index, va, off, size, plausible, why)
        n = h['DataBlocksCount']
        if n == 0 and h['DataBlocksOffset'] == 0:
            self.note('no data blocks (DataBlocksCount=0, DataBlocksOffset=0)')
        elif n > MAX_DATA_BLOCKS:
            self.note(f'DataBlocksCount={n} exceeds plausibility cap {MAX_DATA_BLOCKS}; table ignored')
        elif not self.bounded(h['DataBlocksOffset'], n * 16, 'DataBlocks table'):
            pass
        else:
            for i in range(n):
                e = h['DataBlocksOffset'] + i * 16
                va, off, size = self.u64(e), self.u32(e + 8), self.u32(e + 12)
                why = []
                if va < KERNEL_VA_MIN:
                    why.append('non-kernel VA')
                if size == 0 or size > 0x100000:
                    why.append(f'size {size:#x} implausible')
                if not (off < self.bound and off + size <= self.bound):
                    why.append('file range out of bounds')
                ok = not why
                self.data_blocks.append((i, va, off, size, ok, ', '.join(why)))
                if ok:
                    self.regions.append(('datablock', va, va + size, off, f'data block #{i}'))

        # 4. debugger data: only used as a consistency signal (KDBG tag, KernBase)
        self.kdbg = None
        if h['DebuggerDataSize'] and self.bounded(h['DebuggerDataOffset'], h['DebuggerDataSize'], 'DebuggerData'):
            o = h['DebuggerDataOffset']
            if h['DebuggerDataSize'] >= 0x20:
                tag = self.blob[o + 0x10:o + 0x14]
                kernbase = self.u64(o + 0x18)
                self.kdbg = (tag, kernbase)
                if tag != KDBG_OWNER_TAG:
                    self.note(f'DebuggerData OwnerTag {tag!r} != KDBG; not a KDDEBUGGER_DATA64 block')
                elif self.modules and self.modules[0][0] != kernbase:
                    self.note(f'KDBG KernBase {kernbase:#x} != first driver base {self.modules[0][0]:#x}')

    # ---- lookup ---------------------------------------------------------------
    def lookup(self, va):
        hits = []
        for kind, start, end, off, label in self.regions:
            if start <= va < end:
                hits.append((kind, label, off + (va - start), end - va))
        return hits

    def watchdog_pointer(self):
        # Microsoft bugcheck 0x133: arg4 for subtype 0, arg3 for subtype 1.
        if self.bugcheck != 0x133 or self.params[0] not in (0, 1):
            return None
        index = 3 if self.params[0] == 0 else 2
        return index + 1, self.params[index]

    def scan_pointer(self, va, limit=32):
        needle = struct.pack('<Q', va)
        out, pos = [], 0
        while len(out) < limit:
            pos = self.blob.find(needle, pos, self.bound)
            if pos < 0:
                break
            out.append(pos)
            pos += 1
        return out


def hexdump(blob, off, length, va):
    lines = []
    for i in range(0, length, 16):
        chunk = blob[off + i:off + min(i + 16, length)]
        hx = ' '.join(f'{c:02x}' for c in chunk)
        asc = ''.join(chr(c) if 32 <= c < 127 else '.' for c in chunk)
        lines.append(f'    {va + i:#018x}  {hx:<47}  {asc}')
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dump')
    ap.add_argument('--va', action='append', default=[], help='extra VA (hex) to look up')
    ap.add_argument('--bytes', type=lambda s: int(s, 0), default=0x100)
    ap.add_argument('--scan', action='store_true')
    args = ap.parse_args()
    max_bytes = max(0, min(args.bytes, MAX_HEX_BYTES))

    d = Dump(args.dump)
    try:
        d.parse()
    except ValueError as exc:
        print(f'header unreadable: {exc}')
        return 2
    d.parse_modules()
    d.parse_regions()
    h = d.h

    print(f'file={d.path.name} size={d.size:#x} SizeOfDump={h["SizeOfDump"]:#x} bound={d.bound:#x}')
    print(f'machine={d.machine:#x} processors={d.processors} build={h["ServicePackBuild"]:#x}')
    print(f'bugcheck={d.bugcheck:#x} params={[hex(p) for p in d.params]}')
    opts = h['TriageOptions']
    names = [n for bit, n in TRIAGE_OPTION_BITS.items() if opts & bit]
    print(f'TriageOptions={opts:#x} [{" ".join(names)}] (bit names per public headers)')
    print(f'ContextOffset={h["ContextOffset"]:#x} CallStack={h["CallStackOffset"]:#x}+{h["SizeOfCallStack"]:#x} '
          f'TopOfStack={h["TopOfStack"]:#x}')
    print(f'DataPage va={h["DataPageAddress"]:#x} off={h["DataPageOffset"]:#x} size={h["DataPageSize"]:#x}')
    print(f'DebuggerData off={h["DebuggerDataOffset"]:#x} size={h["DebuggerDataSize"]:#x}'
          + (f' tag={d.kdbg[0]!r} KernBase={d.kdbg[1]:#x}' if d.kdbg else ''))
    print(f'DataBlocks off={h["DataBlocksOffset"]:#x} count={h["DataBlocksCount"]}')

    if getattr(d, 'data_blocks', None):
        print('data blocks:')
        for i, va, off, size, ok, why in d.data_blocks:
            mod = d.module_of(va)
            attr = f'{mod[0]}+{mod[1]:#x}' if mod else 'no module'
            print(f'  #{i:<3} va={va:#018x} size={size:#8x} file={off:#x} {"ok" if ok else "REJECTED: " + why} [{attr}]')

    print('mapped VA regions (validated):')
    for kind, start, end, off, label in d.regions:
        print(f'  {kind:<9} {start:#018x}-{end:#018x} file={off:#x} {label}')

    targets = []
    watchdog = d.watchdog_pointer()
    if watchdog:
        arg, address = watchdog
        targets.append((f'0x133 arg{arg} (DPC_WATCHDOG_GLOBAL_TRIAGE_BLOCK pointer)', address))
    for s in args.va:
        targets.append((f'--va {s}', int(s, 16)))

    for label, va in targets:
        print(f'\nlookup {label}: {va:#018x}')
        mod = d.module_of(va)
        print(f'  module attribution: ' + (f'{mod[0]}+{mod[1]:#x}' if mod else 'none (not inside any listed driver image)'))
        hits = d.lookup(va)
        if not hits:
            print('  NOT CAPTURED: no validated region covers this VA; contents unavailable in this dump')
        for kind, rlabel, foff, avail in hits:
            n = min(avail, max_bytes)
            print(f'  captured by {rlabel} ({kind}) at file {foff:#x}, {avail:#x} bytes available; raw hex ({n:#x} bytes, undecoded):')
            print(hexdump(d.blob, foff, n, va))
        if args.scan:
            occ = d.scan_pointer(va)
            print(f'  pointer-value occurrences in file (<=32): {[hex(o) for o in occ] or "none"}')

    if d.notes:
        print('\nnotes / unknowns:')
        for n in d.notes:
            print(f'  - {n}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
