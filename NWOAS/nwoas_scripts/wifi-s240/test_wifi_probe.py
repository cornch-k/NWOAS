#!/usr/bin/env python3
"""Offline tests for wifi_probe.py. No serial port, no m1n1 proxy, no hardware.

Run:  python3 -I nwoas_scripts/wifi-s240/test_wifi_probe.py
      (the real-ADT test runs only when `construct` is importable, e.g. with m1n1_windows/.venv-hv)
"""
import io
import json
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import wifi_probe as wp  # noqa: E402

ROOT = HERE.parents[1]
ADT_DUMP = ROOT / 'experiments' / 'tahoe-afk-20260906' / 'current.adt'


def good_facts(**over):
    f = dict(wp.EXPECT)
    f.update(compatible=['J274AP', 'Macmini9,1', 'AppleARM'], perst_phandle=106, perst_polarity=0,
             port1_perst_phandle=106, wlan_compatible=['wlan-pcie,bcm4378', 'wlan-pcie,bcm'],
             perst_to_config_ms=100, t_refclk_to_perst_us=100)
    f.update(over)
    return f


class Clock:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t

    def sleep(self, s):
        self.t += max(s, 0.0)


class FakeHW:
    """Register-level model of: m1n1 proxy, port 0/1 regs, PHY, AP GPIO, ECAM, BCM4378 (2 fns)."""

    def __init__(self, clock, *, pcie_init_rc=0, device_present=True, refclk_ack=True,
                 ids=(wp.WIFI_ID, wp.BT_ID), mem_decode=False, exc_on=None, raise_on=None,
                 crs_reads=0, link_already_up=False):
        self.clock = clock
        self.f = good_facts()
        self.regs = {}
        self.writes, self.events = [], []
        self.pcie_init_rc = pcie_init_rc
        self.device_present, self.refclk_ack = device_present, refclk_ack
        self.exc_on, self.raise_on = exc_on, raise_on
        self.crs_left = crs_reads
        self.link_already_up = link_already_up
        self.wifi_powered = False
        self.guard = 0
        self.exc_count = 0
        self.calls = 0
        e = self.f['ecam']
        # root ports 0 and 1 (type-1 headers, Apple vendor, PCIe cap at 0x40)
        for dev, did in ((0, 0x100C), (1, 0x100C)):
            b = wp.ecam_addr(e, 0, dev, 0)
            self.regs.update({b: (did << 16) | wp.APPLE_VENDOR, b + 0x0C: 0x00010000, b + 0x18: 0,
                              b + 0x34: 0x40, b + 0x40: 0x0010, b + 0x48: 0x2810, b + 0x50: 0x0})
        # FL1100 port 1 registers: arbitrary but fixed
        for o in wp.PORT1_SNAPSHOT_OFFS:
            self.regs[self.f['port1'] + o] = 0x1000 + o
        self.regs[self.f['gpio'] + 4 * self.f['port1_perst_pin']] = 0x203
        self.regs[self.f['gpio'] + 4 * self.f['perst_pin']] = 0x200  # input, unpowered
        # endpoint functions
        self.fn = {}
        sizes = {0: (0x8000, 0x400000), 1: (0x2000, 0x400000)}
        for fn in (0, 1):
            cfg = {0x00: ids[fn], 0x04: 0x00100000 | (0x2 if mem_decode else 0),
                   0x08: 0x02800003 if fn == 0 else 0x0D110003,
                   0x0C: 0x00800000 if fn == 0 else 0, 0x2C: 0x0, 0x34: 0x48,
                   0x48: 0x0010, 0x50: 0x2810, 0x58: 0x00120000}
            for i in range(6):
                cfg[0x10 + 4 * i] = 0
            cfg[0x10], cfg[0x14] = 0x4, 0          # BAR0 64-bit
            cfg[0x18], cfg[0x1C] = 0x4, 0          # BAR2 64-bit
            self.fn[fn] = {'cfg': cfg, 'sizes': {0: sizes[fn][0], 2: sizes[fn][1]}}
        self.bar_orig = {fn: dict(self.fn[fn]['cfg']) for fn in (0, 1)}

    # --- proxy API
    def pcie_init(self):
        self.events.append('pcie_init')
        if self.pcie_init_rc == 0:
            p0 = self.f['port0']
            self.regs[p0 + wp.PORT_STATUS] = wp.STATUS_READY
            self.regs[p0 + wp.PORT_PERST] = wp.PERST_OFF
            self.regs[p0 + wp.PORT_APPCLK] = wp.APPCLK_EN | wp.APPCLK_CGDIS
            self.regs[p0 + wp.PORT_REFCLK] = wp.REFCLK_CGDIS
        return self.pcie_init_rc

    def set_exc_guard(self, mode):
        self.guard = mode

    def get_exc_count(self):
        return self.exc_count

    def _link_up(self):
        if self.link_already_up:      # someone else already powered/trained it
            return True
        p0, g = self.f['port0'], self.regs.get(self.f['gpio'] + 4 * self.f['perst_pin'], 0)
        return (self.device_present and self.wifi_powered and (g & 0xF) == 0x3
                and self.regs.get(p0 + wp.PORT_PERST, 0) & 1
                and self.regs.get(p0 + wp.PORT_REFCLK, 0) & 1
                and self.regs.get(p0 + wp.PORT_LTSSMCTL, 0) & 1)

    def _ep(self, addr):
        e = self.f['ecam']
        off = addr - e
        bus, dev, fn, reg = off >> 20, (off >> 15) & 0x1F, (off >> 12) & 7, off & 0xFFF
        return bus, dev, fn, reg

    def read32(self, addr):
        self.calls += 1
        self.clock.t += 0.0001
        if self.raise_on and addr == self.raise_on:
            raise wp.PhaseTimeout('fake UART stall')
        f = self.f
        if f['ecam'] <= addr < f['ecam'] + 0x10000000:
            bus, dev, fn, reg = self._ep(addr)
            if bus == 1:
                self.events.append(f'cfgread 1:{dev}.{fn}+{reg:#x}')
                rp_bus = self.regs.get(f['ecam'] + 0x18, 0)
                if not self._link_up() or ((rp_bus >> 8) & 0xFF) != 1 or dev != 0 or fn > 1:
                    if self.guard:
                        self.exc_count += 1
                    return 0xFFFFFFFF
                if self.exc_on == (fn, reg):
                    self.exc_count += 1
                if reg == 0 and self.crs_left:
                    self.crs_left -= 1
                    return 0xFFFF0001
                return self.fn[fn]['cfg'].get(reg, 0)
            return self.regs.get(addr, 0)
        if addr == f['port0'] + wp.PORT_LINKSTS:
            return 1 if self._link_up() else 0
        if addr == f['port0_phy'] + wp.PHY_LANE_CFG:
            v = self.regs.get(addr, 0)
            if self.refclk_ack:
                v |= (v & 0x3) << 2
            return v
        return self.regs.get(addr, 0)

    def write32(self, addr, val):
        self.writes.append((addr, val))
        f = self.f
        if f['ecam'] <= addr < f['ecam'] + 0x10000000:
            bus, dev, fn, reg = self._ep(addr)
            if bus == 1 and self._link_up():
                cfg = self.fn[fn]['cfg']
                idx = (reg - 0x10) // 4
                if 0x10 <= reg <= 0x24:
                    lo_bar = idx if idx in self.fn[fn]['sizes'] else idx - 1
                    size = self.fn[fn]['sizes'].get(lo_bar)
                    if size is None:
                        cfg[reg] = 0
                    elif idx == lo_bar:
                        cfg[reg] = (val & ~(size - 1) & 0xFFFFFFF0) | 0x4
                    else:
                        cfg[reg] = val  # upper half fully writable (size < 4 GiB)
                return
            self.regs[addr] = val
            return
        if addr == f['gpio'] + 4 * f['perst_pin']:
            self.events.append(f'perst {"deassert" if val & 1 else "assert"}')
        if addr == f['port0'] + wp.PORT_LTSSMCTL and val & 1:
            self.events.append('ltssm start')
        self.regs[addr] = val


class FakeSmc:
    def __init__(self, hw, info=(4, 'ui32', 0xD0), stop_raises=False):
        self.hw, self.info, self.stop_raises = hw, info, stop_raises
        self.written, self.started, self.stopped = [], False, False

    def start(self):
        self.started = True
        self.hw.events.append('smc start')

    def key_info(self, key):
        if isinstance(self.info, Exception):
            raise self.info
        return self.info

    def write32(self, key, value):
        assert key == 'gP0d'
        self.written.append(value)
        self.hw.events.append(f'smc {value:#x}')
        self.hw.wifi_powered = value == wp.SMC_GPIO_ON

    def stop(self):
        if self.stop_raises:
            raise TimeoutError('fake SMC stop stall')
        self.stopped = True
        self.hw.events.append('smc stop')


def opts(**over):
    o = types.SimpleNamespace(power_settle_ms=100, link_timeout_ms=1000, config_retry_ms=1000,
                              size_bars=True)
    for k, v in over.items():
        setattr(o, k, v)
    return o


def run_probe(hw=None, facts=None, smc_kw=None, **optkw):
    clock = hw.clock if hw else Clock()
    hw = hw or FakeHW(clock)
    smcs = []

    def factory(base):
        assert base == wp.EXPECT['smc']
        s = FakeSmc(hw, **(smc_kw or {}))
        smcs.append(s)
        return s
    phases = []

    def phase(name, seconds):
        phases.append((name, seconds))
        import contextlib
        return contextlib.nullcontext()
    probe = wp.WifiProbe(hw, facts or good_facts(), factory, opts(**optkw), sleep=clock.sleep,
                         clock=clock, phase=phase, log=lambda *_: None)
    res = probe.run()
    return res, hw, smcs, phases, clock


class Sequence(unittest.TestCase):
    def assert_clean_power_down(self, res, hw, smcs):
        self.assertTrue(res['power_down']['confirmed'], res['power_down'])
        self.assertFalse(hw.wifi_powered)
        self.assertEqual(smcs[0].written[-1], wp.SMC_GPIO_OFF)
        self.assertTrue(smcs[0].stopped)
        g = hw.regs[hw.f['gpio'] + 4 * hw.f['perst_pin']]
        self.assertEqual(g & 0xF, 0x2, 'PERST# must be left asserted (output 0)')
        self.assertEqual(hw.regs[hw.f['port0'] + wp.PORT_PERST] & 1, 0)

    def assert_no_forbidden_writes(self, hw):
        allowed = wp.build_allowlist(hw.f)
        for a, _ in hw.writes:
            self.assertIn(a, allowed, hex(a))
            self.assertFalse(hw.f['port1'] <= a < hw.f['port1'] + 0x1000000, f'port1 write {a:#x}')
            self.assertFalse(wp.ecam_addr(hw.f['ecam'], 2, 0, 0) <= a < wp.ecam_addr(hw.f['ecam'], 4, 0, 0),
                             f'FL1100/Ethernet config write {a:#x}')
            self.assertNotEqual(a, wp.ecam_addr(hw.f['ecam'], 0, 1, 0, 0x18), 'root port 1 bus write')

    def test_happy_path(self):
        res, hw, smcs, phases, clock = run_probe()
        self.assertEqual(res['verdict'], 'PASS_IDS')
        self.assertFalse(res['hardware_success_claim'])
        self.assertFalse(res['writes_to_storage'])
        fn0, fn1 = res['functions'][0], res['functions'][1]
        self.assertEqual((fn0['vendor'], fn0['device']), ('0x14e4', '0x4425'))
        self.assertEqual((fn1['vendor'], fn1['device']), ('0x14e4', '0x5f69'))
        self.assertEqual([(b['bar'], b['type'], b['size']) for b in fn0['bars'] if b['size'] != '0x0'],
                         [(0, 'mem64', '0x8000'), (2, 'mem64', '0x400000')])
        self.assertEqual(fn1['bars'][0]['size'], '0x2000')
        self.assertTrue(all(b['restored'] for b in fn0['bars'] + fn1['bars']))
        for fn in (0, 1):
            for off in range(0x10, 0x28, 4):
                self.assertEqual(hw.fn[fn]['cfg'][off], hw.bar_orig[fn][off], f'fn{fn} BAR {off:#x}')
        self.assertTrue(fn0['pcie']['no_snoop'] and fn0['pcie']['relaxed_ordering'])
        self.assertEqual(smcs[0].written, [wp.SMC_GPIO_ON, wp.SMC_GPIO_OFF])
        self.assert_clean_power_down(res, hw, smcs)
        self.assert_no_forbidden_writes(hw)
        self.assertTrue(res['power_down']['port1_unchanged'])
        self.assertTrue(res['power_down']['root_bus_restored'])
        self.assertEqual(hw.regs[hw.f['ecam'] + 0x18], 0, 'root port 0 bus numbers restored')
        ev = hw.events
        order = [ev.index('pcie_init'), ev.index('smc 0x800001'), ev.index('perst assert'),
                 ev.index('perst deassert'), ev.index('ltssm start'),
                 next(i for i, x in enumerate(ev) if x.startswith('cfgread 1:')),
                 ev.index('smc 0x800000'), ev.index('smc stop')]
        self.assertEqual(order, sorted(order), ev)
        self.assertLess(clock.t, 5.0)
        self.assertEqual([p[0] for p in phases],
                         ['pcie_init', 'preflight', 'smc-on', 'port0-bringup', 'link-wait',
                          'config-read', 'power-down'])
        self.assertTrue(all(0 < s <= 30 for _, s in phases))

    def test_device_absent_link_down(self):
        clock = Clock()
        res, hw, smcs, _, clock = run_probe(FakeHW(clock, device_present=False))
        self.assertEqual(res['verdict'], 'LINK_DOWN')
        self.assertFalse(any(e.startswith('cfgread 1:') for e in hw.events), 'no bus-1 reads when link down')
        self.assert_clean_power_down(res, hw, smcs)
        self.assert_no_forbidden_writes(hw)
        self.assertLess(clock.t, 3.0, 'bounded wait')

    def test_refclk_timeout_still_powers_down(self):
        clock = Clock()
        res, hw, smcs, _, _ = run_probe(FakeHW(clock, refclk_ack=False))
        self.assertTrue(res['verdict'].startswith('ERROR: RuntimeError: REFCLK0ACK'), res['verdict'])
        self.assert_clean_power_down(res, hw, smcs)

    def test_pcie_init_failure_refuses_before_smc(self):
        clock = Clock()
        res, hw, smcs, _, _ = run_probe(FakeHW(clock, pcie_init_rc=0xFFFFFFFFFFFFFFFF))
        self.assertTrue(res['verdict'].startswith('REFUSED: pcie_init'), res['verdict'])
        self.assertEqual(smcs, [])
        self.assertEqual(hw.writes, [])
        self.assertTrue(res['power_down']['confirmed'])

    def test_smc_key_missing_refuses_without_write(self):
        res, hw, smcs, _, _ = run_probe(smc_kw={'info': (0, '', 0)})
        self.assertTrue(res['verdict'].startswith('REFUSED: SMC key'), res['verdict'])
        self.assertEqual(smcs[0].written, [])
        self.assertTrue(smcs[0].stopped)
        self.assertEqual(hw.writes, [])
        self.assertTrue(res['power_down']['confirmed'])

    def test_smc_key_error_refuses(self):
        res, hw, smcs, _, _ = run_probe(smc_kw={'info': RuntimeError('SMC error')})
        self.assertTrue(res['verdict'].startswith('ERROR: RuntimeError'), res['verdict'])
        self.assertEqual(smcs[0].written, [])
        self.assertTrue(res['power_down']['confirmed'])

    def test_wrong_target_refuses_before_any_proxy_call(self):
        clock = Clock()
        hw = FakeHW(clock)
        res, hw, smcs, phases, _ = run_probe(hw, facts=good_facts(compatible=['J313AP', 'MacBookAir10,1']))
        self.assertTrue(res['verdict'].startswith('REFUSED: wrong target'))
        self.assertEqual((hw.calls, hw.writes, hw.events, smcs, phases), (0, [], [], [], []))

    def test_adt_address_mismatch_refuses(self):
        for over in ({'gpio': 0x23c100004}, {'perst_pin': 0x99}, {'port1': 0x682004000},
                     {'perst_phandle': 7}, {'wlan_compatible': ['wlan-pcie,bcm4387']}):
            res, hw, smcs, _, _ = run_probe(facts=good_facts(**over))
            self.assertTrue(res['verdict'].startswith('REFUSED'), (over, res['verdict']))
            self.assertEqual((hw.calls, smcs), (0, []))

    def test_id_mismatch(self):
        clock = Clock()
        res, hw, smcs, _, _ = run_probe(FakeHW(clock, ids=(0x440114E4, 0x5F6914E4)))
        self.assertEqual(res['verdict'], 'ID_MISMATCH')
        self.assert_clean_power_down(res, hw, smcs)

    def test_memory_decode_enabled_skips_sizing(self):
        clock = Clock()
        res, hw, smcs, _, _ = run_probe(FakeHW(clock, mem_decode=True))
        self.assertIn('skipped', res['functions'][0]['bars'])
        bar_writes = [a for a, _ in hw.writes if wp.ecam_addr(hw.f['ecam'], 1, 0, 0) <= a < wp.ecam_addr(hw.f['ecam'], 1, 1, 0) + 0x1000]
        self.assertEqual(bar_writes, [])

    def test_exception_during_read_skips_sizing(self):
        clock = Clock()
        res, hw, smcs, _, _ = run_probe(FakeHW(clock, exc_on=(0, 0x08)))
        self.assertEqual(res['functions'][0]['exceptions_during_read'], 1)
        self.assertIn('exception', res['functions'][0]['bars'])
        self.assert_clean_power_down(res, hw, smcs)

    def test_no_bar_sizing_flag(self):
        res, hw, smcs, _, _ = run_probe(size_bars=False)
        self.assertEqual(res['verdict'], 'PASS_IDS')
        self.assertIn('--no-bar-sizing', res['functions'][0]['bars'])

    def test_timeout_mid_config_powers_down(self):
        clock = Clock()
        hw = FakeHW(clock)
        hw.raise_on = wp.ecam_addr(hw.f['ecam'], 1, 0, 1, 0x00)
        res, hw, smcs, _, _ = run_probe(hw)
        self.assertTrue(res['verdict'].startswith('ERROR: PhaseTimeout'), res['verdict'])
        self.assertEqual(hw.guard, 0, 'exception guard switched back off')
        self.assert_clean_power_down(res, hw, smcs)

    def test_crs_retry(self):
        clock = Clock()
        res, *_ = run_probe(FakeHW(clock, crs_reads=3))
        self.assertEqual(res['verdict'], 'PASS_IDS')

    def test_crs_forever_is_bounded(self):
        clock = Clock()
        res, hw, smcs, _, clock = run_probe(FakeHW(clock, crs_reads=10 ** 9), config_retry_ms=200)
        self.assertTrue(res['verdict'].startswith('NO_CONFIG_RESPONSE'), res['verdict'])
        self.assertLess(clock.t, 3.0)
        self.assert_clean_power_down(res, hw, smcs)

    def test_link_already_up_skips_bringup(self):
        clock = Clock()
        res, hw, smcs, _, _ = run_probe(FakeHW(clock, link_already_up=True))
        self.assertEqual(res['verdict'], 'PASS_IDS')
        self.assertNotIn('ltssm start', hw.events)
        self.assertTrue(res['notes'])
        self.assertTrue(res['link_was_up_before_power'])
        self.assert_clean_power_down(res, hw, smcs)

    def test_smc_stop_failure_is_unconfirmed(self):
        res, hw, smcs, _, _ = run_probe(smc_kw={'stop_raises': True})
        self.assertFalse(res['power_down']['confirmed'])
        self.assertTrue(res['power_down']['smc_off_written'])


class Guards(unittest.TestCase):
    def test_guarded_mmio_blocks_port1(self):
        clock = Clock()
        hw = FakeHW(clock)
        m = wp.GuardedMmio(hw, wp.build_allowlist(hw.f))
        with self.assertRaises(wp.Refusal):
            m.w32(hw.f['port1'] + wp.PORT_PERST, 0)
        with self.assertRaises(wp.Refusal):
            m.w32(wp.ecam_addr(hw.f['ecam'], 2, 0, 0, 0x10), 0)
        self.assertEqual(hw.writes, [])

    def test_serial_free(self):
        def runner(rc, out=b'', err=b''):
            calls = []

            def r(cmd, **kw):
                calls.append(cmd)
                return subprocess.CompletedProcess(cmd, rc, out, err)
            r.calls = calls
            return r
        ok = runner(1)
        wp.check_serial_free(['/dev/a', '/dev/b'], ok)
        self.assertEqual([c[-1] for c in ok.calls], ['/dev/a', '/dev/b'])
        for bad in (runner(0, b'1234\n'), runner(1, b'', b'lsof: status error'), runner(2)):
            with self.assertRaises(wp.Refusal):
                wp.check_serial_free(['/dev/a'], bad)

        def boom(cmd, **kw):
            raise FileNotFoundError(cmd[0])
        with self.assertRaises(wp.Refusal):
            wp.check_serial_free(['/dev/a'], boom)


class Main(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.out = Path(self.tmp.name)
        self.lines = []

    def tearDown(self):
        self.tmp.cleanup()

    def _connect(self, hw, smc_kw=None):
        state = {'called': 0, 'closed': 0}

        def connect(args):
            state['called'] += 1
            return hw, good_facts(), (lambda base: FakeSmc(hw, **(smc_kw or {}))), \
                (lambda: state.__setitem__('closed', state['closed'] + 1))
        return connect, state

    def test_dry_run_touches_nothing(self):
        def runner(*a, **k):
            raise AssertionError('lsof must not run in dry mode')
        connect, st = self._connect(None)
        rc = wp.main(['--out-dir', str(self.out)], runner=runner, connect=connect, log=self.lines.append)
        self.assertEqual(rc, 0)
        self.assertEqual(st['called'], 0)
        self.assertIn('nothing executed', self.lines[0])
        self.assertEqual(list(self.out.iterdir()), [])

    def test_serial_owned_refuses(self):
        connect, st = self._connect(None)
        rc = wp.main(['--run', '--out-dir', str(self.out)],
                     runner=lambda cmd, **k: subprocess.CompletedProcess(cmd, 0, b'999\n', b''),
                     connect=connect, log=self.lines.append)
        self.assertEqual(rc, 3)
        self.assertEqual(st['called'], 0)
        self.assertTrue(self.lines[0].startswith('STOP: serial port'))

    def _free(self, cmd, **k):
        return subprocess.CompletedProcess(cmd, 1, b'', b'')

    def _null_phase(self, name, seconds):
        import contextlib
        return contextlib.nullcontext()

    def test_full_run_writes_result(self):
        clock = Clock()
        hw = FakeHW(clock)
        connect, st = self._connect(hw)
        rc = wp.main(['--run', '--out-dir', str(self.out), '--power-settle-ms', '0'], runner=self._free,
                     connect=connect, phase=self._null_phase, log=self.lines.append)
        self.assertEqual(rc, 0, self.lines)
        self.assertEqual((st['called'], st['closed']), (1, 1))
        res = json.loads(next(self.out.glob('run-*/result.json')).read_text())
        self.assertEqual(res['verdict'], 'PASS_IDS')
        self.assertEqual(res['facts']['port0'], '0x681000000')
        self.assertFalse(hw.wifi_powered)

    def test_unconfirmed_power_down_exit_4(self):
        clock = Clock()
        hw = FakeHW(clock)
        connect, _ = self._connect(hw, smc_kw={'stop_raises': True})
        rc = wp.main(['--run', '--out-dir', str(self.out), '--power-settle-ms', '0'], runner=self._free,
                     connect=connect, phase=self._null_phase, log=self.lines.append)
        self.assertEqual(rc, 4)
        self.assertTrue(any('power-down NOT confirmed' in x for x in self.lines))

    def test_connect_refusal_exit_3(self):
        def connect(args):
            raise wp.Refusal('wrong target, compatible=[J313AP]')
        rc = wp.main(['--run', '--out-dir', str(self.out)], runner=self._free, connect=connect,
                     log=self.lines.append)
        self.assertEqual(rc, 3)

    def test_option_bounds(self):
        with self.assertRaises(SystemExit):
            wp.parse_args(['--link-timeout-ms', '999999'])

    def test_alarm_phase_raises(self):
        import time as _t
        with self.assertRaises(wp.PhaseTimeout):
            with wp.alarm_phase('unit', 1):
                _t.sleep(3)


class RealAdt(unittest.TestCase):
    """Uses the recorded J274 ADT dump (data only; nothing is printed from it)."""

    def test_recorded_adt_matches_expect(self):
        if not ADT_DUMP.exists():
            self.skipTest('ADT dump not present')
        pc = ROOT / 'm1n1_windows' / 'proxyclient'
        sys.path.insert(0, str(pc))
        try:
            from m1n1.adt import load_adt
        except ImportError as e:
            self.skipTest(f'proxyclient deps missing ({e}); run with m1n1_windows/.venv-hv python')
        finally:
            sys.path.remove(str(pc))
        f = wp.adt_facts(load_adt(ADT_DUMP.read_bytes()))
        wp.validate_facts(f)
        self.assertEqual(f['perst_to_config_ms'], 100)
        self.assertEqual(f['t_refclk_to_perst_us'], 100)
        self.assertEqual(f['perst_polarity'], 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
