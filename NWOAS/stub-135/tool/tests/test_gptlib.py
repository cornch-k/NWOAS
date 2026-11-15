"""Offline tests for gptlib and guard_update using the real S231 GPT blocks saved on 2026-11-15.

Run:  python3 -I stub-135/tool/tests/test_gptlib.py      (no device access, no network)
The fixture is the table written by layout-s231/gpt_partition_s231.py at 18:02 (gpt-plan/new-lba-*.bin),
with LBA 0 from the before-apply copy (apply asserted LBA 0 unchanged).
"""
import hashlib, json, re, struct, subprocess, sys, tempfile, unittest, uuid, zlib
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL))
import gptlib
import guard_update

NW = Path('/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts')
PLAN = NW / 'layout-s231' / 'gpt-plan'
BEFORE_APPLY = PLAN / 'before-apply-20261115-180232-726737'
GUEST_MODULE = NW / 'layout-s231' / 'guest_module.py'


def current_blocks():
    meta = json.loads((PLAN / 'plan.json').read_text())
    b = {0: (BEFORE_APPLY / 'lba-0.bin').read_bytes()}
    for l, h in meta['new_sha256'].items():
        b[int(l)] = (PLAN / f'new-lba-{l}.bin').read_bytes()
        assert hashlib.sha256(b[int(l)]).hexdigest() == h
    return b


def rebuild(blocks, mutate_entries=None, mutate_ph=None, mutate_bh=None, backup_entries_too=True):
    """Return new blocks after mutate_entries(bytearray of 16 KiB) with CRCs recomputed."""
    b = dict(blocks)
    arr = bytearray(b''.join(b[l] for l in gptlib.PRIMARY_ENTRIES))
    if mutate_entries:
        mutate_entries(arr)
    ecrc = zlib.crc32(bytes(arr))
    for j, l in enumerate(gptlib.PRIMARY_ENTRIES):
        b[l] = bytes(arr[j * 4096:(j + 1) * 4096])
    if backup_entries_too:
        for j, l in enumerate(gptlib.BACKUP_ENTRIES):
            b[l] = bytes(arr[j * 4096:(j + 1) * 4096])
    for l, mut in ((1, mutate_ph), (gptlib.BACKUP_HEADER, mutate_bh)):
        h = bytearray(b[l])
        struct.pack_into('<I', h, 88, ecrc)
        if mut:
            mut(h)
        struct.pack_into('<I', h, 16, 0)
        struct.pack_into('<I', h, 16, zlib.crc32(bytes(h[:92])))
        b[l] = bytes(h)
    return b


def set_slot(slot, type_=None, guid=None, start=None, end=None, attrs=None, name=None):
    def f(arr):
        o = (slot - 1) * 128
        if type_ is not None: arr[o:o + 16] = type_.bytes_le
        if guid is not None: arr[o + 16:o + 32] = guid.bytes_le
        if start is not None: struct.pack_into('<Q', arr, o + 32, start)
        if end is not None: struct.pack_into('<Q', arr, o + 40, end)
        if attrs is not None: struct.pack_into('<Q', arr, o + 48, attrs)
        if name is not None:
            nm = name.encode('utf-16le'); arr[o + 56:o + 128] = nm + bytes(72 - len(nm))
    return f


APFS_ONLY = set_slot(3, type_=gptlib.T_APFS)


class Fixture(unittest.TestCase):
    def test_constants_match_plan_and_guard(self):
        meta = json.loads((PLAN / 'plan.json').read_text())
        self.assertEqual([e.hex() for e in gptlib.EXPECTED_ENTRIES], meta['new_entry_bytes'])
        src = GUEST_MODULE.read_text()
        prot = re.findall(r"'([0-9a-f]{256})'", src[src.index('PROTECTED=['):])[:4]
        self.assertEqual(prot, [gptlib.EXPECTED_ENTRIES[i].hex() for i in (0, 1, 2, 6)])
        p3 = meta['partitions'][2]
        self.assertEqual((p3['start'], p3['end'], int(p3['attrs'], 16), p3['guid']),
                         (gptlib.STUB_FIRST, gptlib.STUB_LAST, gptlib.STUB_ATTRS, str(gptlib.STUB_GUID)))

    def test_current_disk_is_accepted(self):
        tbl = gptlib.read_table(current_blocks())
        s3 = gptlib.check_current(tbl)
        self.assertEqual(s3['type'], gptlib.T_BASIC)
        self.assertEqual(gptlib.is_current_or_stub(tbl), 'placeholder')

    def test_plan_changes_only_slot3_type(self):
        tbl = gptlib.read_table(current_blocks())
        pred = gptlib.predict_after(tbl)
        for b, a in zip(tbl['entries'], pred):
            self.assertEqual((b['start'], b['end'], b['guid'], b['attrs']), (a['start'], a['end'], a['guid'], a['attrs']))
            self.assertEqual(a['type'], gptlib.T_APFS if a['slot'] == 3 else b['type'])


class After(unittest.TestCase):
    def setUp(self):
        self.before = current_blocks()

    def test_type_only_change_ok(self):
        rep = gptlib.validate_after(self.before, rebuild(self.before, APFS_ONLY))
        self.assertEqual(set(rep['slot3']), {'type'})
        self.assertEqual(rep['changed'], [])

    def test_guid_attrs_name_change_reported(self):
        g = uuid.UUID('11111111-2222-4333-8444-555555555555')
        after = rebuild(self.before, set_slot(3, type_=gptlib.T_APFS, guid=g, attrs=0, name='Windows'))
        rep = gptlib.validate_after(self.before, after)
        self.assertEqual(set(rep['slot3']), {'type', 'guid', 'attrs', 'name'})
        self.assertEqual(rep['slot3_guid'], str(g))
        self.assertEqual(gptlib.is_current_or_stub(gptlib.read_table(after)), 'stub')
        with self.assertRaises(gptlib.GptError):
            gptlib.check_current(gptlib.read_table(after))

    def test_mbr_rewrite_reported_but_must_stay_protective(self):
        after = rebuild(self.before, APFS_ONLY)
        m = bytearray(after[0]); m[440:444] = b'ABCD'; after[0] = bytes(m)
        self.assertIn('LBA0 protective MBR rewritten (still protective)', gptlib.validate_after(self.before, after)['changed'])
        m[446 + 4] = 0x07; after[0] = bytes(m)
        with self.assertRaises(gptlib.GptError):
            gptlib.validate_after(self.before, after)

    def refuse(self, after, text):
        with self.assertRaises(gptlib.GptError) as cm:
            gptlib.validate_after(self.before, after)
        self.assertIn(text, str(cm.exception))

    def test_refuse_slot3_moved(self):
        self.refuse(rebuild(self.before, set_slot(3, type_=gptlib.T_APFS, start=gptlib.STUB_FIRST + 256)), 'slot 3 moved')

    def test_refuse_slot3_resized(self):
        self.refuse(rebuild(self.before, set_slot(3, type_=gptlib.T_APFS, end=gptlib.STUB_LAST - 1)), 'slot 3 moved')

    def test_refuse_slot3_not_apfs(self):
        hfs = uuid.UUID('48465300-0000-11aa-aa11-00306543ecac')
        self.refuse(rebuild(self.before, set_slot(3, type_=hfs)), 'expected Apple_APFS')

    def test_refuse_slot3_unchanged(self):
        self.refuse(dict(self.before), 'expected Apple_APFS')

    def test_refuse_other_slots(self):
        for slot in (1, 2, 4, 5, 6, 7):
            def both(arr, slot=slot):
                APFS_ONLY(arr); set_slot(slot, attrs=1)(arr)
            self.refuse(rebuild(self.before, both), f'slot {slot} changed')

    def test_refuse_new_partition_in_gap(self):
        def add(arr):
            APFS_ONLY(arr)
            e = gptlib.T_BASIC.bytes_le + uuid.uuid4().bytes_le + struct.pack('<QQQ', 38968338, 38968575, 0)
            arr[7 * 128:8 * 128] = e + bytes(128 - len(e))
        self.refuse(rebuild(self.before, add), 'slot 8 changed')

    def test_refuse_disk_guid_change(self):
        g = uuid.uuid4().bytes_le
        def ph(h): h[56:72] = g
        self.refuse(rebuild(self.before, APFS_ONLY, mutate_ph=ph, mutate_bh=ph), 'disk GUID')

    def test_refuse_backup_mismatch(self):
        self.refuse(rebuild(self.before, APFS_ONLY, backup_entries_too=False), 'backup entries differ')

    def test_refuse_header_geometry(self):
        def ph(h): struct.pack_into('<Q', h, 48, gptlib.LAST_USABLE - 1)
        self.refuse(rebuild(self.before, APFS_ONLY, mutate_ph=ph, mutate_bh=ph), 'geometry')

    def test_refuse_bad_crc(self):
        after = rebuild(self.before, APFS_ONLY)
        a = bytearray(after[2]); a[300] ^= 1; after[2] = bytes(a)
        self.refuse(after, 'CRC')


class GuardUpdate(unittest.TestCase):
    def test_patch_and_tool(self):
        before = current_blocks()
        g = uuid.UUID('aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee')
        after = rebuild(before, set_slot(3, type_=gptlib.T_APFS, guid=g, attrs=0))
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            for name, blk in (('b', before), ('a', after)):
                (td / name).mkdir()
                for l, x in blk.items():
                    (td / name / f'lba-{l}.bin').write_bytes(x)
            r = subprocess.run([sys.executable, '-I', '-B', str(TOOL / 'guard_update.py'), '--guest-module', str(GUEST_MODULE),
                                '--before', str(td / 'b'), '--after', str(td / 'a'), '--scripts', str(NW),
                                '--out', str(td / 'out')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            summ = json.loads((td / 'out' / 'summary.json').read_text())
            self.assertIn('display-s242-guest-test.sh', summ['launchers_pinned'])
            self.assertIn('layout-s231-guest-test.sh', summ['launchers_pinned'])
            new_src = (td / 'out' / 'guest_module.py').read_text()
            self.assertEqual(hashlib.sha256(new_src.encode()).hexdigest(), summ['new_guest_module_sha256'])
            sh = (td / 'out' / 'apply-guard-update.sh').read_text()
            self.assertEqual(sh.count('sed -i'), len(summ['launchers_pinned']) + 1)   # + manifest.json
            # The original module must be untouched.
            self.assertEqual(hashlib.sha256(GUEST_MODULE.read_bytes()).hexdigest(), guard_update.GUEST_MODULE_SHA_S231)
            self.check_inserted_logic(new_src, before, after)

    def check_inserted_logic(self, new_src, before, after):
        """Execute ONLY the PROTECTED list + inserted block with fake backend/hv, for each disk state."""
        start = new_src.index('PROTECTED=[bytes.fromhex(h) for h in (')
        end = new_src.index('gpt_writes=0', start)
        snippet = new_src[start:end]
        logs = []
        class HV:
            def log(self, m): logs.append(m)
        for blocks, want3 in ((before, gptlib.EXPECTED_ENTRIES[2]), (after, after[2][256:384])):
            ns = dict(backend=lambda lba, n=1, b=blocks: b[lba], hv=HV())
            exec(snippet, ns)
            self.assertEqual(ns['PROTECTED'][2], want3)
            self.assertEqual(ns['PROTECTED'][3], gptlib.EXPECTED_ENTRIES[6])
        bad = dict(after); x = bytearray(bad[2]); x[256 + 50] ^= 1; bad[2] = bytes(x)
        with self.assertRaises(AssertionError):
            exec(snippet, dict(backend=lambda lba, n=1: bad[lba], hv=HV()))
        self.assertEqual(logs, ['[stub-135] GPT slot 3 = NWSTUB placeholder', '[stub-135] GPT slot 3 = 13.5 stub APFS'])

    def test_real_gptguard_accepts_patched_protected(self):
        sys.path.insert(0, str(NW / 'nvme-s124'))
        from gpt_guard import GptGuard, GptViolation
        before = current_blocks()
        after = rebuild(before, set_slot(3, type_=gptlib.T_APFS))
        patched_prot = [gptlib.EXPECTED_ENTRIES[i] for i in (0, 1, 2, 6)]
        new_prot = list(patched_prot); new_prot[2] = after[2][256:384]
        def rd(blocks):
            return lambda lba, n=1: b''.join(blocks.get(lba + i, bytes(4096)) for i in range(n))
        GptGuard(rd(before), None, gptlib.DISK_BLOCKS, 39944960, 59968629, patched_prot)   # today: passes
        with self.assertRaises(GptViolation):                                               # unpatched after change
            GptGuard(rd(after), None, gptlib.DISK_BLOCKS, 39944960, 59968629, patched_prot)
        GptGuard(rd(after), None, gptlib.DISK_BLOCKS, 39944960, 59968629, new_prot)         # patched after change


class Simulate(unittest.TestCase):
    def test_nwstub_simulate_prints_plan(self):
        with tempfile.TemporaryDirectory() as td:
            for l, x in current_blocks().items():
                (Path(td) / f'lba-{l}.bin').write_bytes(x)
            r = subprocess.run([sys.executable, '-I', '-B', str(TOOL / 'nwstub.py'), '--simulate-gpt', td],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn('AFTER (planned)', r.stdout)
            self.assertIn('type -> Apple_APFS', r.stdout)
            r2 = subprocess.run([sys.executable, '-I', '-B', str(TOOL / 'nwstub.py'), '--simulate-gpt', td, '--apply'],
                                capture_output=True, text=True)
            self.assertNotEqual(r2.returncode, 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
