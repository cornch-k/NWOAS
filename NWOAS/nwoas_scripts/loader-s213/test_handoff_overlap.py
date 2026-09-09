#!/usr/bin/env python3
"""S213: pure host range-overlap regression test for the T8103 UEFI handoff.

Reads real constants from tracked source (read-only) and models only the two
allocation rules that matter (m1n1 heapblock bump allocator, PrePi temp stack).
No hardware, no build, no log or dump reads. Stdlib only.

  python3 nwoas_scripts/loader-s213/test_handoff_overlap.py
"""
import json, re, struct, unittest
from pathlib import Path

ROOT = Path('/Volumes/X31/NWOAS')
MU = ROOT / 'apple_silicon_platforms_mu'
S204 = ROOT / 'nwoas_scripts/firmware-s204'
M1N1 = ROOT / 'm1n1-guest-s197/src'

DSC_INC = MU / 'Silicon/Apple/T810XFamilyPkg/T810XFamilyPkg.dsc.inc'
ENTRY_S = MU / 'Silicon/Apple/AppleSiliconPkg/PrePi/AArch64/ModuleEntryPoint.S'
FDF_OLD = S204 / 'MacMini2020.fdf.before'       # image_size 0x100E0000 (S192/S197/S203)
FDF_NEW = S204 / 'MacMini2020.fdf.candidate'    # image_size 0x01E00000 (S204)


def pcd(name):
    m = re.search(r'\.%s\|(0x[0-9a-fA-F]+)' % name, DSC_INC.read_text())
    assert m, name
    return int(m.group(1), 16)


def parse_fdf(path):
    t = path.read_text()
    size = int(re.search(r'^Size\s*=\s*(0x[0-9a-fA-F]+)\|gArmTokenSpaceGuid\.PcdFdSize', t, re.M).group(1), 16)
    fv = re.search(r'^(0x[0-9a-fA-F]+)\|(0x[0-9a-fA-F]+)\s*\n\s*gArmTokenSpaceGuid\.PcdFvBaseAddress', t, re.M)
    data = re.search(r'DATA = \{(.*?)\}', t, re.S).group(1)
    raw = bytes(int(b, 16) for b in re.findall(r'0x([0-9a-fA-F]{2})', re.sub(r'#[^\n]*', '', data)))
    return {'fd_size': size, 'fv_off': int(fv.group(1), 16), 'fv_size': int(fv.group(2), 16),
            'text_offset': struct.unpack_from('<Q', raw, 8)[0],
            'image_size': struct.unpack_from('<Q', raw, 16)[0], 'magic': raw[0x38:0x3C], 'stub_len': len(raw)}


def heapblock_alloc_aligned(heap_base, size, align):
    """src/heapblock.c:35-44 verbatim semantics."""
    block = (heap_base + align - 1) & ~(align - 1)
    return block, block + size


def overlaps(a0, a1, b0, b1):
    return a0 < b1 and b0 < a1


class HandoffOverlap(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ba = pcd('PcdBootArgsPointer')
        cls.adt = pcd('PcdAdtPointer')
        cls.old = parse_fdf(FDF_OLD)
        cls.new = parse_fdf(FDF_NEW)
        cls.hw = json.loads((S204 / 'hardware-result.json').read_text())
        cls.entry = int(cls.hw['actual_entry'], 16)
        h = (M1N1 / 'xnuboot.h').read_text()
        rv3 = int(re.search(r'CMDLINE_LENGTH_RV3\s+(\d+)', h).group(1))
        # revision/version(4)+pad(4), 4 x u64, boot_video 6 x u64, machine_type u32 + pad,
        # devtree ptr, devtree_size u32 + pad -> union at 112; rv3 = cmdline + 2 x u64.
        cls.boot_args_len = 112 + rv3 + 16
        p = (M1N1 / 'payload.c').read_text()
        cls.kernel_align = eval(re.search(r'#define KERNEL_ALIGN \((.*?)\)', p).group(1))
        cls.copy_line = 'heapblock_alloc_aligned(kernel->image_size, KERNEL_ALIGN)' in p

    def test_source_constants(self):
        self.assertEqual(self.adt, self.ba + 0x4000)
        self.assertEqual(self.entry, self.ba, 'curated actual_entry equals PcdBootArgsPointer')
        self.assertEqual(int(self.hw['configured_adt_copy'], 16), self.adt)
        for v in (self.old, self.new):
            self.assertEqual(v['fd_size'], 0x1E00000)
            self.assertEqual((v['fv_off'], v['fv_size'], v['stub_len']), (0x8000, 0x1D80000, 0x40))
            self.assertEqual(v['magic'], b'ARM\x64')
        self.assertEqual(self.old['image_size'], 0x100E0000)
        self.assertEqual(self.new['image_size'], 0x01E00000)
        self.assertEqual(self.kernel_align, 2 << 20)
        self.assertTrue(self.copy_line, 'load_kernel reserves image_size bytes at 2 MiB alignment')
        s = ENTRY_S.read_text()
        self.assertIn('mov   sp, x7', s)                       # temp stack top = FvBase = FdBase + 0x8000
        self.assertIn('MOV64 (x3, FixedPcdGet64(PcdBootArgsPointer))', s)
        self.assertIn('MOV64 (x4, FixedPcdGet64(PcdAdtPointer))', s)
        self.assertEqual(self.boot_args_len, 0x480)

    def test_image_size_constraints(self):
        fd_len = self.new['fv_off'] + self.new['fv_size']
        for v in (self.old, self.new):
            self.assertGreaterEqual(v['image_size'], v['fd_size'])   # m1n1 reservation covers UEFI's FD claim
            self.assertGreaterEqual(v['image_size'], fd_len)
        self.assertEqual(self.new['image_size'], self.new['fd_size'])

    def test_kernel_block_address_is_header_independent(self):
        # Sweep heap_base across a 2 MiB period: block never depends on image_size.
        for hb in range(0x83FE00000, 0x840200001, 0x4000):
            k_old, hb_old = heapblock_alloc_aligned(hb, self.old['image_size'], self.kernel_align)
            k_new, hb_new = heapblock_alloc_aligned(hb, self.new['image_size'], self.kernel_align)
            self.assertEqual(k_old, k_new)
            self.assertEqual(hb_old - hb_new, 0x100E0000 - 0x01E00000)

    def test_observed_entry_collides_for_both_headers(self):
        k = self.entry
        stub_end = k + self.new['fv_off']
        for v in (self.old, self.new):
            fd_end = k + v['fd_size']
            # boot_args copy lands on the FD's own Linux header stub.
            self.assertTrue(overlaps(self.ba, self.ba + self.boot_args_len, k, fd_end))
            self.assertLess(self.ba + self.boot_args_len, self.adt)
            # ADT copy starts inside the stub; any size >= 0x4000 crosses the temp stack
            # top and the FV header at FdBase + 0x8000, regardless of image_size.
            for adt_size in (0x4000, 0x10000, 0x100000):
                self.assertTrue(overlaps(self.adt, self.adt + adt_size, k, fd_end))
                self.assertGreaterEqual(self.adt + adt_size, stub_end)

    def test_what_269mib_actually_protects(self):
        # Model only: a 2 MiB-aligned FD base whose live FD ends below the window.
        k = 0x83E200000
        fd_len = self.new['fv_off'] + self.new['fv_size']
        self.assertLessEqual(k + fd_len, self.ba)
        win = (self.ba, self.adt + 0x100000)
        # Old header: window sits inside m1n1's reservation slack (no later heap user there).
        self.assertTrue(overlaps(*win, k + fd_len, k + self.old['image_size']))
        # New header: window sits beyond the reservation, i.e. in m1n1's post-kernel heap
        # (DT buffer, malloc arena), which UEFI does not consume.
        self.assertFalse(overlaps(*win, k, k + self.new['image_size']))

    def test_reservation_invariant_for_a_fix(self):
        # Any placement that keeps [BA, ADT + adt_size) disjoint from [FdBase, FdBase + PcdFdSize)
        # and from the stub stack is free of this failure; the observed K violates it.
        def ok(k, adt_size):
            return not overlaps(self.ba, self.adt + adt_size, k, k + self.new['fd_size'])
        self.assertFalse(ok(self.entry, 0x4000))
        self.assertTrue(ok(0x840200000, 0x100000))       # FD above the window (S215 placement rule)
        self.assertFalse(ok(0x83E200000 + 0x1C00000, 0x4000))


if __name__ == '__main__':
    unittest.main(verbosity=2)
