"""S243 host unit tests: channel, adapter over the real LinkNamespace, FAT append,
manifest, module.py under a fake hv, and job generation. No device access.

    cd file-delivery-s243 && python3 -I -m unittest -v test_s243   (or: python3 -I test_s243.py)
"""
import hashlib
import json
import os
import re
import runpy
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path[:0] = [str(HERE), str(ROOT / 'nvme-s124'), str(ROOT / 'transport-s123')]

from transport import LinkNamespace, NamespacePair, fat_image, CAPACITY, PORT   # noqa: E402
from readonly_namespace import SUCCESS, INVALID_FIELD, WRITE_TO_RO, Result      # noqa: E402
import s243_channel as ch                                                       # noqa: E402
from s243_adapter import ArtifactLink                                           # noqa: E402
from s243_fat import fat_append                                                 # noqa: E402
import s243_manifest as mf                                                      # noqa: E402
import make_job                                                                 # noqa: E402

TOKEN = bytes(range(16))


def req(token, rid, index, offset, length, tail=b''):
    b = bytearray(4096)
    b[:16] = ch.REQ_MAGIC
    b[16:32] = token
    struct.pack_into('<IQII', b, 32, rid, offset, length, index)
    struct.pack_into('<I', b, 52, zlib.crc32(bytes(b[:52])))
    b[56:56 + len(tail)] = tail
    return bytes(b)


def parse(win):
    assert win[:16] == ch.RESP_MAGIC
    assert zlib.crc32(win[:124]) == struct.unpack_from('<I', win, 124)[0]
    rid, status, size, off, served, pcrc = struct.unpack_from('<IIQQII', win, 32)
    index, count = struct.unpack_from('<II', win, 96)
    payload = win[128:128 + served]
    assert zlib.crc32(payload) == pcrc
    return dict(id=rid, status=status, size=size, off=off, served=served, sha=win[64:96],
                index=index, count=count, payload=payload)


class Channel(unittest.TestCase):
    def setUp(self):
        self.a = os.urandom(3 * ch.PAYLOAD_CAP + 17)
        self.c = ch.HostArtifactChannel(TOKEN).register([('A.INF', b'abc'), ('b.sys', self.a)])

    def test_register_rules(self):
        with self.assertRaises(RuntimeError):
            self.c.register([('X.Y', b'')])
        for bad in [[], [('a/b', b'')], [('..', b'')], [('a b', b'')], [('A', b''), ('a', b'')],
                    [('ok', 'str')]]:
            with self.assertRaises((ValueError, TypeError)):
                ch.HostArtifactChannel(TOKEN).register(bad)
        with self.assertRaises(ValueError):
            ch.HostArtifactChannel(TOKEN).register([(f'f{i}', b'') for i in range(65)])

    def test_source_copied(self):
        buf = bytearray(b'xyz')
        c = ch.HostArtifactChannel(TOKEN).register([('X', buf)])
        buf[0] = 0
        self.assertTrue(c.handle_request(req(TOKEN, 1, 0, 0, 3)))
        self.assertEqual(parse(c.response_window())['payload'], b'xyz')

    def test_none_window(self):
        r = parse(self.c.response_window())
        self.assertEqual((r['id'], r['status'], r['count'], r['served']), (0, ch.STATUS_NONE, 2, 0))

    def test_manifest_and_download(self):
        self.assertTrue(self.c.handle_request(req(TOKEN, 1, 1, ch.SENTINEL, 0)))
        r = parse(self.c.response_window())
        self.assertEqual((r['status'], r['size'], r['payload'], r['index']), (3, len(self.a), b'b.sys', 1))
        self.assertEqual(r['sha'], hashlib.sha256(self.a).digest())
        got, off, rid = b'', 0, 2
        while True:
            self.assertTrue(self.c.handle_request(req(TOKEN, rid, 1, off, ch.PAYLOAD_CAP)))
            r = parse(self.c.response_window())
            self.assertEqual((r['id'], r['off']), (rid, off))
            got += r['payload']
            off += r['served']
            rid += 1
            if r['status'] == ch.STATUS_FINAL:
                break
        self.assertEqual(got, self.a)

    def test_strict_rejects_leave_state(self):
        self.assertTrue(self.c.handle_request(req(TOKEN, 5, 0, 0, 3)))
        before = self.c.response_window()
        bad = [req(TOKEN, 6, 2, 0, 1), req(TOKEN, 6, 0, 4, 1), req(TOKEN, 6, 0, 0, 0),
               req(TOKEN, 6, 0, 0, ch.PAYLOAD_CAP + 1), req(TOKEN, 6, 0, ch.SENTINEL, 1),
               req(TOKEN, 4, 0, 0, 1), req(TOKEN, 0, 0, 0, 1), req(bytes(16), 6, 0, 0, 1),
               req(TOKEN, 6, 0, 0, 1, tail=b'\x01'), req(TOKEN, 6, 0, 0, 1)[:4095]]
        corrupt = bytearray(req(TOKEN, 6, 0, 0, 1)); corrupt[40] ^= 1
        bad.append(bytes(corrupt))
        for b in bad:
            self.assertFalse(self.c.handle_request(b))
            self.assertEqual(self.c.response_window(), before)
        self.assertTrue(self.c.handle_request(req(TOKEN, 5, 0, 0, 3)))     # identical retry
        self.assertTrue(self.c.handle_request(req(TOKEN, 6, 0, 3, 1)))     # EOF probe
        r = parse(self.c.response_window())
        self.assertEqual((r['status'], r['served']), (ch.STATUS_FINAL, 0))

    def test_overlay_only_window(self):
        orig = bytes(range(256)) * 16 * 20
        out = self.c.overlay(120, 20, orig)
        self.assertEqual(out[:9 * 4096], orig[:9 * 4096])
        self.assertEqual(out[9 * 4096:], self.c.response_window()[:11 * 4096])
        self.assertEqual(self.c.overlay(145, 2, orig[:8192]), orig[:8192])


def cmd(op, lba, n=1, ns=1, addr=0x10000):
    b = bytearray(64); b[0] = op
    struct.pack_into('<I', b, 4, ns); struct.pack_into('<Q', b, 24, addr)
    struct.pack_into('<Q', b, 40, lba); struct.pack_into('<I', b, 48, n - 1)
    return bytes(b)


class Memory:
    def __init__(self, data): self.data = data
    def contains(self, a, n): return 0x10000 <= a and a + n <= 0x10000 + len(self.data)
    def read(self, a, n): return self.data[a - 0x10000:a - 0x10000 + n]


class Adapter(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = LinkNamespace(fat_image({'TEST.TXT': b'abc'}), self.tmp.name)
        self.blob = os.urandom(200000)
        self.link = ArtifactLink(self.base, [('one.inf', b'hello'), ('two.sys', self.blob)])
        self.tok = self.base.token

    def tearDown(self): self.tmp.cleanup()

    def guest_fetch(self, index, start_id):
        rid = start_id
        assert self.link.io(cmd(1, 127), Memory(req(self.tok, rid, index, ch.SENTINEL, 0))).status == SUCCESS
        m = parse(self.link.io(cmd(2, 129, 16)).data)
        out, off = b'', 0
        while off < m['size'] or not out and m['size'] == 0:
            rid += 1
            assert self.link.io(cmd(1, 127), Memory(req(self.tok, rid, index, off, ch.PAYLOAD_CAP))).status == SUCCESS
            r = parse(self.link.io(cmd(2, 129, 16)).data)
            out += r['payload']; off += r['served']
            if r['status'] == ch.STATUS_FINAL:
                break
        return m, out, rid

    def test_two_artifacts_roundtrip(self):
        m, out, rid = self.guest_fetch(1, 1)
        self.assertEqual(out, self.blob)
        self.assertEqual(m['sha'], hashlib.sha256(self.blob).digest())
        m, out, _ = self.guest_fetch(0, rid + 1)
        self.assertEqual((m['payload'], out), (b'one.inf', b'hello'))

    def test_port_fat_capacity_preserved(self):
        before = bytes(self.base.image); frame = self.base.frame()
        self.link.io(cmd(1, 127), Memory(req(self.tok, 1, 0, ch.SENTINEL, 0)))
        self.assertEqual(bytes(self.base.image), before)
        self.assertEqual(self.base.frame(), frame)
        mixed = self.link.io(cmd(2, 128, 16)).data
        self.assertEqual(mixed[:4096], frame)
        self.assertEqual(mixed[4096:], self.link.channel.response_window()[:15 * 4096])
        self.assertEqual(self.link.block_count, CAPACITY)
        self.assertEqual(self.link.io(cmd(2, 256, 16)).data, self.base.io(cmd(2, 256, 16)).data)

    def test_reject_gap_writes(self):
        mem = Memory(req(self.tok, 1, 0, ch.SENTINEL, 0) * 2)
        for lba, n in [(126, 2), (127, 2), (129, 1), (144, 1), (255, 2), (0, 1), (8447, 2)]:
            self.assertNotEqual(self.link.io(cmd(1, lba, n), mem).status, SUCCESS)

    def test_invalid_request_and_prp(self):
        self.assertEqual(self.link.io(cmd(1, 127), Memory(bytes(4096))).status, INVALID_FIELD)
        self.assertEqual(self.link.io(cmd(1, 127, addr=0), Memory(req(self.tok, 1, 0, 0, 1))).status, INVALID_FIELD)
        self.assertEqual(self.link.io(cmd(1, 127), None).status, INVALID_FIELD)

    def test_namespace_dispatch_isolation(self):
        class Primary:
            def __init__(s): s.calls = []
            def io(s, c, m=None): s.calls.append(c); return Result(0)
        primary = Primary(); pair = NamespacePair(primary, self.link)
        self.assertEqual(pair.io(cmd(1, 127, ns=2), Memory(req(self.tok, 1, 0, 0, 1))).status, SUCCESS)
        self.assertEqual(primary.calls, [])

    def test_partition_write_remains_ram_only(self):
        self.assertEqual(self.link.io(cmd(1, 300), Memory(b'Z' * 4096)).status, SUCCESS)
        self.assertEqual(self.link.io(cmd(2, 300)).data, b'Z' * 4096)


def link_module_files():
    """The exact file set memory-s180/link_module.py puts on the NS2 FAT."""
    src = (ROOT / 'memory-s180/link_module.py').read_text()
    files = {}
    for m in re.finditer(r"'([A-Z0-9]+\.[A-Z0-9]+)':(?:\(_root/'([^']+)'\)\.read_bytes\(\)|(b'[^\n]*?'),?\n)", src):
        name, path, literal = m.groups()
        files[name] = (ROOT / path).read_bytes() if path else eval(literal)
    return files


class Fat(unittest.TestCase):
    def test_equals_fat_image_small(self):
        f = {'A.TXT': b'a' * 5000, 'EMPTY.BIN': b''}
        self.assertEqual(fat_append(fat_image(f), 'NWGET243.EXE', b'x' * 9000),
                         fat_image({**f, 'NWGET243.EXE': b'x' * 9000}))

    def test_equals_fat_image_real_link_module_set(self):
        files = link_module_files()
        self.assertEqual(len(files), 22)
        client = (HERE / 'client/NWGET243.EXE').read_bytes()
        self.assertEqual(fat_append(fat_image(files), 'NWGET243.EXE', client),
                         fat_image({**files, 'NWGET243.EXE': client}))

    def test_refusals(self):
        img = fat_image({'A.TXT': b'a'})
        for name in ['A.TXT', 'lower.exe', 'TOOLONGNAME.EXE', 'NOEXT', 'A.B.C']:
            with self.assertRaises(ValueError):
                fat_append(img, name, b'x')
        dirty = bytearray(img); dirty[257 * 4096 + 40] = 1      # FAT1 != FAT2
        with self.assertRaises(ValueError):
            fat_append(bytes(dirty), 'B.TXT', b'x')
        dirty = bytearray(img); dirty[269 * 4096 + 4096 + 3] = 7  # data after last file
        with self.assertRaises(ValueError):
            fat_append(bytes(dirty), 'B.TXT', b'x')
        with self.assertRaises(ValueError):
            fat_append(img, 'BIG.BIN', b'x' * (8200 * 4096))
        with self.assertRaises(ValueError):
            fat_append(img[:-1], 'B.TXT', b'x')

    def test_gap_and_mbr_untouched(self):
        img = fat_image({'A.TXT': b'a'})
        out = fat_append(img, 'B.TXT', b'bbbb')
        self.assertEqual(out[:256 * 4096], img[:256 * 4096])


class Manifest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        (self.d / 'pkg').mkdir()
        (self.d / 'pkg/b.sys').write_bytes(b'B' * 100)
        (self.d / 'pkg/a.inf').write_bytes(b'A')
        (self.d / 'pkg/.hidden').write_bytes(b'H')

    def tearDown(self): self.tmp.cleanup()

    def test_create_load(self):
        m = mf.create([str(self.d / 'pkg')])
        self.assertEqual([e['name'] for e in m['artifacts']], ['a.inf', 'b.sys'])
        arts, meta = mf.load(m)
        self.assertEqual(arts[1], ('b.sys', b'B' * 100))

    def test_load_refusals(self):
        m = mf.create([str(self.d / 'pkg')])
        for mutate in [lambda e: e.update(sha256='0' * 64), lambda e: e.update(bytes=2),
                       lambda e: e.update(path='pkg/a.inf'), lambda e: e.update(name='a/b'),
                       lambda e: e.update(extra=1), lambda e: e.update(sha256='AB' * 32)]:
            mm = json.loads(json.dumps(m)); mutate(mm['artifacts'][0])
            with self.assertRaises(ValueError):
                mf.load(mm)
        os.symlink(self.d / 'pkg/a.inf', self.d / 'link.inf')
        with self.assertRaises(ValueError):
            mf.load({'version': 1, 'artifacts': [mf.entry_for(str(self.d / 'pkg/a.inf')) | {'path': str(self.d / 'link.inf')}]})
        with self.assertRaises(ValueError):
            mf.load(m, max_total=50)

    def test_from_env(self):
        self.assertIsNone(mf.from_env({}))
        f = str(self.d / 'pkg/a.inf')
        sha = hashlib.sha256(b'A').hexdigest()
        m = mf.from_env({'NWOAS_DELIVER_FILE': f, 'NWOAS_DELIVER_SHA256': sha})
        self.assertEqual(mf.load(m)[0], [('a.inf', b'A')])
        with self.assertRaises(ValueError):
            mf.load(mf.from_env({'NWOAS_DELIVER_FILE': f, 'NWOAS_DELIVER_SHA256': '1' * 64}))
        for env in [{'NWOAS_DELIVER_FILE': f}, {'NWOAS_DELIVER_SHA256': sha},
                    {'NWOAS_DELIVER_FILE': f, 'NWOAS_DELIVER_SHA256': sha, 'NWOAS_DELIVER_MANIFEST': f}]:
            with self.assertRaises(ValueError):
                mf.from_env(env)
        p = self.d / 'm.json'
        p.write_text(json.dumps(mf.create([f])))
        self.assertEqual(mf.from_env({'NWOAS_DELIVER_MANIFEST': str(p)})['artifacts'][0]['sha256'], sha)


class FakeHV:
    def __init__(self, link):
        class Ctrl: pass
        self.ctrl = Ctrl()
        self.ctrl.ns = NamespacePair(object(), link)
        self._nwoas_nvme = (self.ctrl, None)
        self._nwoas_link = link
        self.logs = []

    def log(self, s): self.logs.append(s)


class Module(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        self.env_backup = dict(os.environ)
        for k in ('NWOAS_DELIVER_MANIFEST', 'NWOAS_DELIVER_FILE', 'NWOAS_DELIVER_SHA256'):
            os.environ.pop(k, None)
        os.environ['NWOAS_LINK_DIR'] = str(self.d / 'link')
        self.files = {'TEST.TXT': b'abc'}
        self.link = LinkNamespace(fat_image(self.files), self.d / 'link')
        self.hv = FakeHV(self.link)

    def tearDown(self):
        os.environ.clear(); os.environ.update(self.env_backup)
        self.tmp.cleanup()

    def run_module(self):
        return runpy.run_path(str(HERE / 'module.py'), init_globals={'hv': self.hv})

    def test_noop_without_env(self):
        before = bytes(self.link.image)
        self.run_module()
        self.assertIs(self.hv._nwoas_link, self.link)
        self.assertEqual(bytes(self.link.image), before)
        self.assertIn('no artifact delivery requested', self.hv.logs[0])

    def test_wraps_and_appends_client(self):
        (self.d / 'x.sys').write_bytes(b'payload')
        p = self.d / 'm.json'; p.write_text(json.dumps(mf.create([str(self.d / 'x.sys')])))
        os.environ['NWOAS_DELIVER_MANIFEST'] = str(p)
        self.run_module()
        w = self.hv._nwoas_link
        self.assertIsInstance(w, ArtifactLink)
        self.assertIs(self.hv.ctrl.ns.link, w)
        self.assertIs(w.base, self.link)
        client = (HERE / 'client/NWGET243.EXE').read_bytes()
        self.assertEqual(bytes(self.link.image), fat_image({**self.files, 'NWGET243.EXE': client}))
        info = json.loads((self.d / 'link/artifact.json').read_text())
        self.assertEqual(info['artifacts'][0]['sha256'], hashlib.sha256(b'payload').hexdigest())
        self.assertEqual(info['client_sha256'], hashlib.sha256(client).hexdigest())

    def test_refuses_after_worker_connected(self):
        (self.d / 'x.sys').write_bytes(b'payload')
        os.environ['NWOAS_DELIVER_FILE'] = str(self.d / 'x.sys')
        os.environ['NWOAS_DELIVER_SHA256'] = hashlib.sha256(b'payload').hexdigest()
        self.link.connected = True
        with self.assertRaises(RuntimeError):
            self.run_module()

    def test_refuses_bad_sha(self):
        (self.d / 'x.sys').write_bytes(b'payload')
        os.environ['NWOAS_DELIVER_FILE'] = str(self.d / 'x.sys')
        os.environ['NWOAS_DELIVER_SHA256'] = '0' * 64
        before = bytes(self.link.image)
        with self.assertRaises(ValueError):
            self.run_module()
        self.assertEqual(bytes(self.link.image), before)
        self.assertIs(self.hv._nwoas_link, self.link)


class Job(unittest.TestCase):
    M = {'version': 1, 'artifacts': [
        {'name': 'AppleAgxRenderAdmission.kmdonly.inf', 'path': '/x', 'bytes': 1, 'sha256': 'a' * 64},
        {'name': 'AppleAgxRenderAdmission.sys', 'path': '/y', 'bytes': 1, 'sha256': 'b' * 64},
        {'name': 'appleagxrenderadmission.cat', 'path': '/z', 'bytes': 1, 'sha256': 'c' * 64}]}

    def test_build(self):
        t = make_job.build(self.M, 'C:\\NWOAS\\drop\\gpu-j274')
        t.encode('ascii')
        self.assertLessEqual(len(t), 4032)
        self.assertIn('"%~d0\\NWGET243.EXE" 1 ' + 'b' * 64 + ' "%T%\\AppleAgxRenderAdmission.sys"', t)
        self.assertEqual(t.count('NWGET243.EXE"'), 3)
        self.assertNotIn('Remove-Item', t)
        self.assertIn('Remove-Item', make_job.build(self.M, 'C:\\a', [2], True))

    def test_refusals(self):
        for d in ['D:\\x', 'C:\\a b', 'C:\\a\\..\\b', 'C:\\a&b', 'C:\\a\\', 'C:\\%x%', 'C:\\a\\.']:
            with self.assertRaises(ValueError):
                make_job.build(self.M, d)
        for idx in [[], [3], [0, 0]]:
            with self.assertRaises(ValueError):
                make_job.build(self.M, 'C:\\a', idx)
        bad = json.loads(json.dumps(self.M)); bad['artifacts'][0]['name'] = 'a&b.inf'
        with self.assertRaises(ValueError):
            make_job.build(bad, 'C:\\a')
        many = {'version': 1, 'artifacts': [{'name': f'file{i:02}.bin', 'path': '/', 'bytes': 0,
                                             'sha256': '0' * 64} for i in range(30)]}
        with self.assertRaises(ValueError):
            make_job.build(many, 'C:\\a')


if __name__ == '__main__':
    unittest.main()
