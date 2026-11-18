#!/usr/bin/env python3
"""S260 host-only tests (no Mac mini, no proxy):
 1. nwoas_pwr_layout.py == m1n1-gpu-hv2/src/nwoas_pwr.h (compiled with the host clang)
 2. power_module.py under a fake p/u/hv/iface with the real transport-s123 LinkNamespace:
    config, boot mark, NWSTAT-MARK split across chunks, duplicate chunk, NWSTAT-SET, refusal paths
 3. hvstats_report.py on the produced hvstats.jsonl
usage: python3 -I test_power_s260.py"""
import io, json, os, struct, subprocess, sys, tempfile, unittest, contextlib, zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
HDR = ROOT / 'm1n1-gpu-hv2/src/nwoas_pwr.h'
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / 'nwoas_scripts/transport-s123'))
sys.path.insert(0, str(ROOT / 'nwoas_scripts/nvme-s124'))
import nwoas_pwr_layout as PL  # noqa: E402


class Layout(unittest.TestCase):
    def test_c_header_matches(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / 't.c'
            fields = ''.join('P(cpu, %s);' % f for f in PL.CPU) + ''.join(
                'P(nvme, %s);' % f for f in PL.NVME if not f.startswith('lat_hist')) + 'P(nvme, lat_hist);'
            src.write_text('#include <stdio.h>\n#include <stddef.h>\n#include <stdbool.h>\n'
                           '#define NWOAS_PWR_HOST_TEST 1\n#include "%s"\n'
                           '#define P(s, f) printf("%%s.%%s %%zu\\n", #s, #f, offsetof(struct nwoas_pwr_##s, f))\n'
                           'int main(void){printf("snap %%zu\\n", sizeof(struct nwoas_pwr_snapshot));'
                           'printf("cpuoff %%zu\\n", offsetof(struct nwoas_pwr_snapshot, cpu));'
                           'printf("nvmeoff %%zu\\n", offsetof(struct nwoas_pwr_snapshot, nvme));%s return 0;}\n'
                           % (HDR, fields))
            exe = Path(d) / 't'
            subprocess.run(['clang', '-Wall', '-Werror', '-o', str(exe), str(src)], check=True)
            out = dict(l.rsplit(' ', 1) for l in subprocess.run([str(exe)], capture_output=True, text=True,
                                                                  check=True).stdout.split('\n') if l)
        self.assertEqual(int(out['snap']), PL.SIZE)
        self.assertEqual(int(out['cpuoff']), 128)
        self.assertEqual(int(out['nvmeoff']), 128 + PL.NCPU * len(PL.CPU) * 8)
        for i, f in enumerate(PL.CPU):
            self.assertEqual(int(out['cpu.' + f]), 8 * i, f)
        for i, f in enumerate(PL.NVME):
            key = 'nvme.lat_hist' if f.startswith('lat_hist') else 'nvme.' + f
            if f.startswith('lat_hist') and f != 'lat_hist0':
                continue
            self.assertEqual(int(out[key]), 8 * i, f)
        text = HDR.read_text()
        for name, val in (('NWOAS_PWR_MAGIC', PL.MAGIC), ('P_HV', None)):
            if val is not None:
                self.assertIn('%s   0x%08xU' % (name, val), text)
        proxy_h = (ROOT / 'm1n1-gpu-hv2/src/proxy.h').read_text()
        self.assertIn('P_HV_NWOAS_PWR = 0x%x,' % PL.P_HV_NWOAS_PWR, proxy_h)


class FakeHV:
    """Answers P_HV_NWOAS_PWR like nwoas_pwr.c (counters advance between snapshots)."""
    def __init__(self):
        self.mem = {}
        self.flags = 0
        self.t = 1000
        self.logs = []
        self.configs = []
        self.smc_calls = 0
        self.nwoas_nvme_link_handler = None

    # iface
    def writemem(self, a, b): self.mem[a] = bytes(b)
    def readmem(self, a, n):
        for base, b in self.mem.items():
            if base <= a < base + len(b):
                return b[a - base:a - base + n]
        raise KeyError(hex(a))
    # u
    def malloc(self, n): return 0x900000000 + 0x10000 * len(self.mem)
    # hv
    def log(self, s): self.logs.append(s)

    def snapshot(self):
        self.t += 24000000 * 10
        k = (self.t - 1000) // 240000000
        hdr = struct.pack(PL.HEADER_FMT, PL.MAGIC, PL.VERSION, PL.SIZE, PL.NCPU, self.t, 24000000, self.flags,
                          240000, 0, 4800, 50000 * k, 0, self.smc_calls, 0, 24000 * k, 0, 0, 0)
        cpu = b''
        for c in range(PL.NCPU):
            vals = {f: 0 for f in PL.CPU}
            vals.update(wfi_traps=1000 * k, wfi_sleeps=900 * k, wfi_sleep_ticks=int(24000000 * 10 * 0.8) * k,
                        fiq=5000 * k, fiq_tick=4000 * k, sync=100 * k, timer_inj=640 * k)
            cpu += struct.pack('<%dQ' % len(PL.CPU), *(vals[f] for f in PL.CPU))
        nv = {f: 0 for f in PL.NVME}
        nv.update(ns1_cmds=10 * k, lat_count=10 * k, lat_sum_ticks=2400 * 10 * k, lat_max_ticks=4800,
                  depth_sum=15 * k, depth_samples=10 * k, depth_max=4, read_blocks=256 * k, lat_hist1=10 * k)
        blob = hdr + cpu + struct.pack('<%dQ' % len(PL.NVME), *(nv[f] for f in PL.NVME)) + b'\0' * 32
        assert len(blob) == PL.SIZE
        return blob

    def request(self, cmd, op, a0, a1, a2, a3):
        assert cmd == PL.P_HV_NWOAS_PWR
        if op == PL.OP_QUERY:
            return (PL.MAGIC << 32) | PL.VERSION
        if op == PL.OP_CONFIG:
            self.configs.append((a0, a1, a2, a3))
            if a0 & ~PL.F_ALL:
                return 0
            self.flags = a0
            return (1 << 32) | a0
        if op == PL.OP_SNAPSHOT:
            self.writemem(a0, self.snapshot())
            return PL.SIZE
        if op == PL.OP_SMC_READ:
            self.smc_calls += 1
            out = b''.join(((struct.unpack('<I', struct.pack('<f', 1.5 + i))[0])).to_bytes(8, 'little')
                           for i in range(a1))
            self.writemem(a2, out)
            return a1
        if op == PL.OP_MCC_STAT:
            self.writemem(a0, struct.pack('<8I', *([16, 0x4210] * 4)))
            return 8
        if op == PL.OP_SLC:
            return 0
        raise AssertionError(op)


_TMP = []


def tearDownModule():
    import shutil
    for d in _TMP:
        shutil.rmtree(d, ignore_errors=True)


class WrapLink:
    """Same shape as file-delivery-s243 ArtifactLink: own 'base', everything else delegated."""
    def __init__(self, base):
        self.base = base

    def __getattr__(self, name):
        return getattr(self.base, name)


def run_module(env_extra, wrapped=False):
    from transport import LinkNamespace, fat_image
    d = tempfile.mkdtemp(prefix='s260-test-')
    _TMP.append(d)
    link = LinkNamespace(fat_image({'A.TXT': b'x'}), d)
    fake = FakeHV()
    fake._nwoas_link = WrapLink(link) if wrapped else link
    env = {'NWOAS_LINK_DIR': d}
    env.update(env_extra)
    old = dict(os.environ)
    os.environ.update(env)
    try:
        g = {'p': fake, 'u': fake, 'hv': fake, 'iface': fake}
        exec(compile((HERE / 'power_module.py').read_text(), 'power_module.py', 'exec'), g)
    finally:
        os.environ.clear()
        os.environ.update(old)
    return fake, link, d, g


def frame(link, jid, kind, body, seq):
    out = bytearray(4096)
    out[:16] = b'NWOAS-S123-LINK!'
    out[16:32] = link.token
    struct.pack_into('<6I', out, 32, jid, kind, len(body), zlib.crc32(body), seq, 0)
    out[64:64 + len(body)] = body
    return bytes(out)


class Module(unittest.TestCase):
    def test_marks_and_set(self):
        fake, link, d, g = run_module({'NWOAS_PWR_WFI': '1', 'NWOAS_PWR_SECTICK': '100', 'NWOAS_PWR_SMC': '1',
                                       'NWOAS_PWR_LIVE': '1'})
        self.assertEqual(fake.configs[0][0], PL.F_WFI_SLEEP | PL.F_SEC_TICK | PL.F_SMC_SENSE)
        self.assertEqual(fake.configs[0][1], 100)
        # worker connects (kind 4), job 7 queued
        self.assertTrue(link.receive(frame(link, 0, 4, b'WinOS worker connected', 1)))
        (Path(d) / 'job.json').write_text(json.dumps({'id': 7, 'script': 'echo'}))
        link.frame()
        self.assertTrue(link.receive(frame(link, 7, 2, b'S260 H1\r\nNWSTAT-MARK idle-be', 2)))
        self.assertTrue(link.receive(frame(link, 7, 2, b'gin\r\nNWSTAT-SET wfi=0\r\n', 3)))
        self.assertTrue(link.receive(frame(link, 7, 2, b'gin\r\nNWSTAT-SET wfi=0\r\n', 3)))  # duplicate: ignored
        self.assertTrue(link.receive(frame(link, 7, 2, b'NWSTAT-MARK idle-end\r\nNWSTAT-SET bogus=1\r\n', 4)))
        self.assertTrue(link.receive(frame(link, 7, 3, b'complete', 5)))
        recs = [json.loads(l) for l in (Path(d) / 'hvstats.jsonl').read_text().splitlines()]
        self.assertEqual([r['label'] for r in recs], ['boot', 'idle-begin', 'idle-end'])
        self.assertEqual(fake.configs[-1][0], PL.F_SEC_TICK | PL.F_SMC_SENSE)   # wfi toggled off once
        self.assertEqual(len(fake.configs), 2)
        self.assertTrue(any('ignored' in l for l in fake.logs))
        self.assertEqual(recs[2]['smc']['values'][0]['key'], 'PSTR')
        self.assertAlmostEqual(recs[2]['smc']['values'][0]['float'], 1.5)
        dl = PL.delta(recs[1]['snapshot'], recs[2]['snapshot'])
        self.assertAlmostEqual(dl['cpu'][0]['sleep_pct'], 80.0)
        self.assertEqual(dl['nvme']['lat_mean_us'], 100.0)
        buf = io.StringIO()
        sys.path.insert(0, str(HERE))
        import hvstats_report
        with contextlib.redirect_stdout(buf):
            self.assertEqual(hvstats_report.main(['x', d]), 0)
        self.assertIn('| idle |', buf.getvalue())
        self.assertIn('PSTR', buf.getvalue())

    def test_hook_under_s243_wrapper(self):
        fake, link, d, g = run_module({}, wrapped=True)
        self.assertTrue(link.receive(frame(link, 0, 4, b'c', 1)))
        (Path(d) / 'job.json').write_text(json.dumps({'id': 3, 'script': 'echo'}))
        link.frame()
        self.assertTrue(link.receive(frame(link, 3, 2, b'NWSTAT-MARK x-begin\r\n', 2)))
        labels = [json.loads(l)['label'] for l in (Path(d) / 'hvstats.jsonl').read_text().splitlines()]
        self.assertEqual(labels, ['boot', 'x-begin'])

    def test_set_refused_without_live(self):
        fake, link, d, g = run_module({'NWOAS_PWR_WFI': '1'})
        self.assertTrue(link.receive(frame(link, 0, 4, b'c', 1)))
        (Path(d) / 'job.json').write_text(json.dumps({'id': 1, 'script': 'echo'}))
        link.frame()
        self.assertTrue(link.receive(frame(link, 1, 2, b'NWSTAT-SET wfi=0\r\n', 2)))
        self.assertEqual(len(fake.configs), 1)

    def test_defaults_all_off(self):
        fake, link, d, g = run_module({})
        self.assertEqual(fake.configs, [(0, 0, 0, 0)])
        self.assertIsNone(json.loads((Path(d) / 'hvstats.jsonl').read_text().splitlines()[0])['smc'])

    def test_bad_env_refused(self):
        with self.assertRaises(AssertionError):
            run_module({'NWOAS_PWR_SECTICK': '5'})


if __name__ == '__main__':
    unittest.main()
