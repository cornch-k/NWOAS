#!/usr/bin/env python3
"""Offline tests for wifi_probe.py. No serial port, no m1n1 proxy, no hardware.

Run:  python3 -I -B nwoas_scripts/wifi-s240/test_wifi_probe.py
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
                 crs_reads=0, link_already_up=False, link_stuck_up=False, nop_clears_stale=True):
        self.clock = clock
        self.f = good_facts()
        self.regs = {}
        self.writes, self.events = [], []
        self.pcie_init_rc = pcie_init_rc
        self.device_present, self.refclk_ack = device_present, refclk_ack
        self.exc_on, self.raise_on = exc_on, raise_on
        self.crs_left = crs_reads
        self.link_already_up = link_already_up
        self.link_stuck_up = link_stuck_up            # fault: link never goes down once up
        self.refclk_en_under_reset = None             # observation only: PORT_PERST_OFF clear at REFCLK_EN?
        self.stale = []                                # late replies: returned by the next read32 calls
        self.nop_clears_stale = nop_clears_stale       # does the proxy resync remove them?
        self.ops = []                                  # ('r', addr) / ('w', addr, val) / ('nop',)
        self.was_up = False
        self.smc_off_seen = False
        self.fault = None                              # fault hook: fault(op, addr, val) may raise
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

    def nop(self):
        if self.fault:
            self.fault('nop', None, None)
        self.ops.append(('nop',))
        self.events.append('proxy nop')
        if self.nop_clears_stale:
            self.stale.clear()

    def _link_up(self):
        if self.link_stuck_up and self.was_up:
            return True
        if self.link_already_up:      # someone else already powered/trained it; our SMC off drops it
            up = not self.smc_off_seen
        else:
            p0, g = self.f['port0'], self.regs.get(self.f['gpio'] + 4 * self.f['perst_pin'], 0)
            up = bool(self.device_present and self.wifi_powered and (g & 0xF) == 0x3
                      and self.regs.get(p0 + wp.PORT_PERST, 0) & 1
                      and self.regs.get(p0 + wp.PORT_REFCLK, 0) & 1
                      and self.regs.get(p0 + wp.PORT_LTSSMCTL, 0) & 1)
        self.was_up = self.was_up or up
        return up

    def _ep(self, addr):
        e = self.f['ecam']
        off = addr - e
        bus, dev, fn, reg = off >> 20, (off >> 15) & 0x1F, (off >> 12) & 7, off & 0xFFF
        return bus, dev, fn, reg

    def read32(self, addr):
        self.calls += 1
        self.clock.t += 0.0001
        if self.fault:
            self.fault('r', addr, None)
        if self.raise_on and addr == self.raise_on:
            raise wp.PhaseTimeout('fake UART stall')
        self.ops.append(('r', addr))
        if self.stale:
            return self.stale.pop(0)
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
        if self.fault:
            self.fault('w', addr, val)
        self.writes.append((addr, val))
        self.ops.append(('w', addr, val))
        f = self.f
        prev = self.regs.get(addr, 0)
        if addr == f['port0'] + wp.PORT_PERST:
            self.events.append('port perst set' if val & wp.PERST_OFF else 'port perst clear')
        if addr == f['port0_phy'] + wp.PHY_LANE_CFG and val & wp.REFCLK0REQ and not prev & wp.REFCLK0REQ:
            self.events.append('refclk0 req')
        if addr == f['port0'] + wp.PORT_REFCLK and val & wp.REFCLK_EN and not prev & wp.REFCLK_EN:
            self.events.append('port refclk en')
            self.refclk_en_under_reset = not (self.regs.get(f['port0'] + wp.PORT_PERST, 0) & wp.PERST_OFF)
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
        self.keys = []
        self.fault = None             # fault hook: fault(op) may raise
        self.stop_sleep = 0.0         # real seconds to block in stop() (alarm test)

    def start(self):
        self.started = True
        self.hw.events.append('smc start')

    def key_info(self, key):
        if isinstance(self.info, Exception):
            raise self.info
        return self.info

    def write32(self, key, value):
        self.keys.append(key)
        if self.fault:
            self.fault('write32')
        assert key == 'gP0d'
        self.written.append(value)
        self.hw.events.append(f'smc {value:#x}')
        self.hw.wifi_powered = value == wp.SMC_GPIO_ON
        self.hw.smc_off_seen = value == wp.SMC_GPIO_OFF

    def stop(self):
        if self.fault:
            self.fault('stop')
        if self.stop_sleep:
            import time as _t
            _t.sleep(self.stop_sleep)
        if self.stop_raises:
            raise TimeoutError('fake SMC stop stall')
        self.stopped = True
        self.hw.events.append('smc stop')


PD_ORDER = ['link_resync', 'root_bus_restore', 'perst_gpio_assert', 'perst_off_clear', 'smc_off', 'smc_stop',
            'link_check', 'port1_snapshot']


def opts(**over):
    o = types.SimpleNamespace(power_settle_ms=100, link_timeout_ms=1000, config_retry_ms=1000,
                              size_bars=True, controller_reset=False)
    for k, v in over.items():
        setattr(o, k, v)
    return o


def run_probe(hw=None, facts=None, smc_kw=None, arm=None, phase_impl=None, **optkw):
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
        if arm:
            arm(name, hw, smcs)
        if phase_impl:
            return phase_impl(name, seconds)
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
        self.assertEqual(g, 0x2, 'PERST# must be left asserted (fixed output-0 value)')
        self.assertEqual(hw.regs[hw.f['port0'] + wp.PORT_PERST], 0)

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
                          'config-read'] + ['power-down:' + s for s in PD_ORDER])
        self.assertEqual(res['power_down']['required_steps'], PD_ORDER[:-1])
        self.assertEqual(res['power_down']['errors'], {})
        self.assertIs(res['power_down']['link_down_after'], True)
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


def fault_in_step(target, exc=None):
    """arm hook: every proxy MMIO access and every SMC call raises while power-down step
    `target` runs; everything else (including later power-down steps) works normally."""
    def arm(name, hw, smcs):
        on = name == f'power-down:{target}'

        def boom(*_):
            raise (exc or wp.PhaseTimeout)(f'injected fault in {target}')
        hw.fault = boom if on else None
        for s in smcs:
            s.fault = boom if on else None
    return arm


class FixPowerDownIndependent(unittest.TestCase):
    """REVIEW defect 1: each power-down step runs on its own, with its own error record."""

    def test_gpio_failure_still_turns_smc_off_and_stops(self):
        res, hw, smcs, _, _ = run_probe(arm=fault_in_step('perst_gpio_assert', RuntimeError))
        pd = res['power_down']
        self.assertEqual(pd['steps']['perst_gpio_assert'], 'error')
        self.assertIn('injected fault', pd['errors']['perst_gpio_assert'])
        for s in ('perst_off_clear', 'smc_off', 'smc_stop', 'link_check', 'port1_snapshot'):
            self.assertEqual(pd['steps'][s], 'ok', s)
        self.assertEqual(smcs[0].written[-1], wp.SMC_GPIO_OFF)
        self.assertTrue(smcs[0].stopped)
        self.assertFalse(hw.wifi_powered)
        self.assertFalse(pd['confirmed'])

    def test_each_step_fails_alone(self):
        for target in PD_ORDER:
            with self.subTest(step=target):
                res, hw, smcs, phases, _ = run_probe(arm=fault_in_step(target))
                pd = res['power_down']
                self.assertEqual(list(pd['errors']), [target])
                self.assertEqual([n for n, _ in phases if n.startswith('power-down:')],
                                 ['power-down:' + s for s in PD_ORDER], 'every step still attempted')
                for s in PD_ORDER:
                    if s != target:
                        self.assertEqual(pd['steps'][s], 'ok', s)
                if target == 'port1_snapshot':       # not part of power confirmation -> exit 5 path
                    self.assertTrue(pd['confirmed'])
                    self.assertIsNone(pd['port1_unchanged'])
                else:
                    self.assertFalse(pd['confirmed'])
                if target != 'smc_off':
                    self.assertFalse(hw.wifi_powered)
                if target != 'smc_stop':
                    self.assertTrue(smcs[0].stopped)

    def test_step_limits_come_from_table(self):
        _, _, _, phases, _ = run_probe()
        for name, sec in phases:
            if name.startswith('power-down:'):
                self.assertEqual(sec, wp.POWER_DOWN_STEP_SECONDS[name.split(':', 1)[1]])

    def test_real_alarm_per_step_does_not_starve_next_step(self):
        # smc.stop() blocks for 3 s with a real SIGALRM limit of 1 s; link check and port-1
        # snapshot must still run under their own (fresh) alarms.
        old = dict(wp.POWER_DOWN_STEP_SECONDS)
        wp.POWER_DOWN_STEP_SECONDS['smc_stop'] = 1

        def arm(name, hw, smcs):
            if name == 'power-down:smc_stop':
                smcs[0].stop_sleep = 3.0

        def real_only_for_pd(name, seconds):
            import contextlib
            return wp.alarm_phase(name, seconds) if name.startswith('power-down:') else contextlib.nullcontext()
        try:
            res, hw, smcs, _, _ = run_probe(arm=arm, phase_impl=real_only_for_pd)
        finally:
            wp.POWER_DOWN_STEP_SECONDS.clear()
            wp.POWER_DOWN_STEP_SECONDS.update(old)
        pd = res['power_down']
        self.assertIn('PhaseTimeout', pd['errors']['smc_stop'])
        self.assertEqual(pd['steps']['link_check'], 'ok')
        self.assertEqual(pd['steps']['port1_snapshot'], 'ok')
        self.assertFalse(pd['confirmed'])


class FixConfirmation(unittest.TestCase):
    """REVIEW defect 2: a link that stays up is not a power-down; a changed port 1 is reported."""

    def test_link_stuck_up_is_unconfirmed(self):
        clock = Clock()
        res, hw, smcs, _, _ = run_probe(FakeHW(clock, link_stuck_up=True))
        self.assertEqual(res['verdict'], 'PASS_IDS')
        self.assertIs(res['power_down']['link_down_after'], False)
        self.assertEqual(res['power_down']['errors'], {})
        self.assertFalse(res['power_down']['confirmed'])

    def test_link_check_error_is_unconfirmed(self):
        res, *_ = run_probe(arm=fault_in_step('link_check'))
        self.assertIsNone(res['power_down']['link_down_after'])
        self.assertFalse(res['power_down']['confirmed'])

    def test_port1_changed_is_reported(self):
        def arm(name, hw, smcs):
            if name == 'power-down:smc_stop':
                hw.regs[hw.f['port1'] + wp.PORT_STATUS] ^= 0x10
        res, *_ = run_probe(arm=arm)
        self.assertTrue(res['power_down']['confirmed'])
        self.assertIs(res['power_down']['port1_unchanged'], False)


class FixControllerReset(unittest.TestCase):
    """Round-2 defect 1: the default bring-up is the reference set-only PORT_PERST_OFF sequence
    (UEFI AppleSiliconPciPlatformDxe.c:296, m1n1 pcie.c:591-592); the clear is opt-in via
    --controller-reset.  These tests check the flag plumbing and the write order only.  The fake
    hardware does NOT model whether either sequence trains a link; nothing here says which one
    works on the Mac mini."""

    UEFI_ORDER = ['perst assert', 'refclk0 req', 'port refclk en', 'port perst set',
                  'perst deassert', 'ltssm start']

    def test_default_is_reference_set_only(self):
        res, hw, smcs, _, _ = run_probe()
        ev = hw.events
        stop = ev.index('ltssm start')
        self.assertNotIn('port perst clear', ev[:stop], 'default must not clear PORT_PERST_OFF')
        idx = [ev.index(e) for e in self.UEFI_ORDER]
        self.assertEqual(idx, sorted(idx), ev)
        self.assertEqual(res['port0_perst_reg'], {'before': '0x1', 'controller_reset': False})
        self.assertFalse(res['controller_reset_option'])
        self.assertIs(hw.refclk_en_under_reset, False, 'REFCLK_EN with PORT_PERST_OFF still set')
        bringup = [v for a, v in hw.writes[:[w[0] for w in hw.writes].index(
            hw.f['port0'] + wp.PORT_LTSSMCTL)] if a == hw.f['port0'] + wp.PORT_PERST]
        self.assertTrue(bringup and all(v & wp.PERST_OFF for v in bringup), bringup)

    def test_opt_in_clears_under_perst(self):
        res, hw, smcs, _, _ = run_probe(controller_reset=True)
        ev = hw.events
        seq = ['perst assert', 'port perst clear', 'refclk0 req', 'port refclk en', 'port perst set',
               'perst deassert', 'ltssm start']
        idx = [ev.index(e) for e in seq]
        self.assertEqual(idx, sorted(idx), ev)
        self.assertEqual(res['port0_perst_reg'],
                         {'before': '0x1', 'controller_reset': True, 'after_clear': '0x0'})
        self.assertTrue(res['controller_reset_option'])
        self.assertIs(hw.refclk_en_under_reset, True)

    def test_flag_parsing(self):
        self.assertFalse(wp.parse_args([]).controller_reset)
        self.assertTrue(wp.parse_args(['--controller-reset']).controller_reset)

    def test_plan_names_the_choice(self):
        lines = []
        self.assertEqual(wp.main([], log=lines.append), 0)
        self.assertIn('PORT_PERST_OFF only set', lines[0])
        lines = []
        self.assertEqual(wp.main(['--controller-reset'], log=lines.append), 0)
        self.assertIn('--controller-reset', lines[0])
        self.assertIn('NOT in any reference', lines[0])


class FixPowerDownResync(unittest.TestCase):
    """Round-2 defect 6: power-down starts with a proxy resync and writes fixed values."""

    def _pd_ops(self, hw):
        return hw.ops[max(i for i, o in enumerate(hw.ops) if o == ('nop',)):]

    def test_resync_is_first_and_writes_are_fixed(self):
        res, hw, smcs, phases, _ = run_probe()
        pd_phases = [n for n, _ in phases if n.startswith('power-down:')]
        self.assertEqual(pd_phases[0], 'power-down:link_resync')
        self.assertIs(res['power_down']['link_resynced'], True)
        self.assertEqual(hw.ops.count(('nop',)), 1)
        ops = self._pd_ops(hw)
        gpio = hw.f['gpio'] + 4 * hw.f['perst_pin']
        pperst = hw.f['port0'] + wp.PORT_PERST
        self.assertIn(('w', gpio, 0x2), ops)
        self.assertIn(('w', pperst, 0x0), ops)
        self.assertFalse([o for o in ops if o[0] == 'r' and o[1] in (gpio, pperst)],
                         'no read-modify-write of PERST# / PORT_PERST in power-down')
        self.assertEqual(res['power_down']['fixed_writes'], {'perst_gpio': '0x2', 'port0_perst': '0x0'})

    def test_stale_reply_is_not_written_back(self):
        # A late reply is returned by the next reads and the resync does NOT remove it (worst case).
        # The power-down writes must still be the fixed values.
        clock = Clock()
        hw = FakeHW(clock, nop_clears_stale=False)
        hw.raise_on = wp.ecam_addr(hw.f['ecam'], 1, 0, 1, 0x00)

        def arm(name, hw_, smcs):
            if name == 'power-down:link_resync':
                hw.stale[:] = [0xDEADBEEF] * 3
        res, hw, smcs, _, _ = run_probe(hw, arm=arm)
        self.assertTrue(res['verdict'].startswith('ERROR: PhaseTimeout'), res['verdict'])
        gpio = hw.f['gpio'] + 4 * hw.f['perst_pin']
        pperst = hw.f['port0'] + wp.PORT_PERST
        pd_writes = [o for o in self._pd_ops(hw) if o[0] == 'w']
        self.assertIn(('w', gpio, 0x2), pd_writes)
        self.assertIn(('w', pperst, 0x0), pd_writes)
        self.assertFalse([o for o in pd_writes if o[2] == 0xDEADBEEF or o[2] & 0xFFFF0000 == 0xDEAD0000])
        self.assertEqual(hw.regs[gpio], 0x2)
        self.assertEqual(hw.regs[pperst], 0x0)

    def test_resync_removes_stale_reply(self):
        clock = Clock()
        hw = FakeHW(clock)
        hw.raise_on = wp.ecam_addr(hw.f['ecam'], 1, 0, 1, 0x00)

        def arm(name, hw_, smcs):
            if name == 'power-down:link_resync':
                hw.stale[:] = [0x1] * 3        # would read as "link up" if not removed
        res, hw, smcs, _, _ = run_probe(hw, arm=arm)
        self.assertEqual(hw.stale, [])
        self.assertTrue(res['power_down']['confirmed'], res['power_down'])

    def test_resync_failure_is_unconfirmed_but_rest_runs(self):
        res, hw, smcs, _, _ = run_probe(arm=fault_in_step('link_resync', RuntimeError))
        pd = res['power_down']
        self.assertEqual(list(pd['errors']), ['link_resync'])
        self.assertIs(pd['link_resynced'], False)
        self.assertIn('link_resync', pd['required_steps'])
        self.assertFalse(pd['confirmed'])
        self.assertFalse(hw.wifi_powered)
        self.assertTrue(smcs[0].stopped)

    def test_real_interface_drain_then_iface_resync_then_proxy_nop(self):
        calls = []

        class Dev:
            def __init__(self):
                self.pending = [b'\x01\x02\x03', b'\xff\x55']

            @property
            def in_waiting(self):
                return len(self.pending[0]) if self.pending else 0

            def read(self, n):
                calls.append('drain')
                return self.pending.pop(0)

        class Iface:
            dev = Dev()

            def _resync_link(self):
                calls.append('iface resync')
        clock = Clock()
        hw = FakeHW(clock)
        hw.iface = Iface()
        orig_nop = hw.nop

        def nop():
            calls.append('proxy nop')
            orig_nop()
        hw.nop = nop
        res, *_ = run_probe(hw)
        self.assertEqual(calls, ['drain', 'drain', 'iface resync', 'proxy nop'])
        self.assertEqual(res['power_down']['resync'], {'drained_bytes': 5, 'iface_resync': True})
        self.assertTrue(res['power_down']['confirmed'])

    def test_nothing_done_needs_no_resync(self):
        clock = Clock()
        res, hw, smcs, _, _ = run_probe(FakeHW(clock, pcie_init_rc=1))
        self.assertEqual(res['power_down']['required_steps'], [])
        self.assertTrue(res['power_down']['confirmed'])
        self.assertEqual(hw.writes, [])


class FixGp1aNotWritten(unittest.TestCase):
    """REVIEW defect 4: only gP0d is written; gP1a is documented as an open hypothesis."""

    def test_only_gp0d_written(self):
        res, hw, smcs, _, _ = run_probe()
        self.assertEqual(set(smcs[0].keys), {'gP0d'})

    def test_docs_mention_gp1a(self):
        self.assertIn('gP1a', wp.__doc__)
        self.assertIn('gP1a', (HERE / 'README.md').read_text())


class FixSmcKeyType(unittest.TestCase):
    """REVIEW defect 6: the SMC key must be a 4-byte ui32/hex_ key."""

    def test_wrong_type_refuses_without_write(self):
        for info in ((4, 'flt ', 0), (4, 'si32', 0), (4,), None, 'ui32', (2, 'ui16', 0)):
            with self.subTest(info=info):
                res, hw, smcs, _, _ = run_probe(smc_kw={'info': info})
                self.assertTrue(res['verdict'].startswith('REFUSED: SMC key'), res['verdict'])
                self.assertEqual(smcs[0].written, [])
                self.assertTrue(smcs[0].stopped)
                self.assertTrue(res['power_down']['confirmed'])
                self.assertEqual(hw.writes, [])

    def test_accepted_types(self):
        for t in wp.SMC_KEY_TYPES:
            with self.subTest(type=t):
                res, *_ = run_probe(smc_kw={'info': (4, t, 0xD0)})
                self.assertEqual(res['verdict'], 'PASS_IDS')


class BridgeWindowPrediction(unittest.TestCase):
    """Wi-Fi root-port window must stay out of guest RAM too (HYPOTHESIS arithmetic only)."""

    def test_fake_sizes(self):
        res, *_ = run_probe()
        pr = res['bridge_window_prediction']
        self.assertTrue(pr['hypothesis'])
        self.assertEqual(pr['wifi_window_bus'], ['0xc0000000', '0xc08fffff'])
        self.assertEqual(pr['wifi_window_cpu'], ['0x6c0000000', '0x6c08fffff'])
        self.assertEqual(pr['wifi_badmemorylist_pfns'], ['0xc0000', '0xc08ff'])
        self.assertEqual(pr['fl1100_window_bus'], ['0xc0900000', '0xc09fffff'])
        self.assertTrue(pr['fl1100_moves'])

    def test_small_bars_keep_fl1100(self):
        fns = {0: {'present': True, 'bars': [{'bar': 0, 'type': 'mem64', 'prefetch': False, 'size': '0x8000'}]},
               1: {'present': True, 'bars': [{'bar': 0, 'type': 'mem32', 'prefetch': False, 'size': '0x2000'}]}}
        pr = wp.predict_wifi_window(fns)
        self.assertFalse(pr['fl1100_moves'])
        self.assertEqual(pr['wifi_window_bus'], ['0xc0100000', '0xc01fffff'])

    def test_prefetchable_64bit_not_in_low_window(self):
        fns = {0: {'present': True, 'bars': [{'bar': 0, 'type': 'mem64', 'prefetch': True, 'size': '0x400000'}]}}
        pr = wp.predict_wifi_window(fns)
        self.assertIsNone(pr['wifi_window_bus'])
        self.assertIn('pmem64_note', pr)

    def test_unmeasured(self):
        res, *_ = run_probe(size_bars=False)
        self.assertFalse(res['bridge_window_prediction']['available'])


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
        for d in self.out.glob('run-*'):
            d.chmod(0o700)
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

    def test_controller_reset_flag_reaches_probe(self):
        hw = FakeHW(Clock())
        connect, st = self._connect(hw)
        rc = wp.main(['--run', '--controller-reset', '--out-dir', str(self.out), '--power-settle-ms', '0'],
                     runner=self._free, connect=connect, phase=self._null_phase, log=self.lines.append)
        self.assertEqual(rc, 0, self.lines)
        res = json.loads(next(self.out.glob('run-*/result.json')).read_text())
        self.assertTrue(res['controller_reset_option'])
        self.assertIn('port perst clear', hw.events)

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

    def test_connect_failure_exit_6(self):
        # REVIEW defect 7: connect failure has its own exit code; no power step was attempted
        def connect(args):
            raise OSError('fake UART open failure')
        rc = wp.main(['--run', '--out-dir', str(self.out)], runner=self._free, connect=connect,
                     log=self.lines.append)
        self.assertEqual(rc, wp.EXIT_CONNECT)
        self.assertTrue(any('No power-on step was attempted' in x for x in self.lines))
        res = json.loads(next(self.out.glob('run-*/result.json')).read_text())
        self.assertTrue(res['verdict'].startswith('CONNECT_FAILED'))

    def _run_main(self, hw, phase=None, smc_kw=None, connect_hook=None):
        connect, st = self._connect(hw, smc_kw)
        if connect_hook:
            inner = connect

            def connect(args):
                connect_hook()
                return inner(args)
        return wp.main(['--run', '--out-dir', str(self.out), '--power-settle-ms', '0'], runner=self._free,
                       connect=connect, phase=phase or self._null_phase, log=self.lines.append)

    def test_link_stuck_up_exit_4(self):
        rc = self._run_main(FakeHW(Clock(), link_stuck_up=True))
        self.assertEqual(rc, wp.EXIT_POWER_UNCONFIRMED)
        self.assertTrue(any('pull mains power' in x for x in self.lines))

    def test_gpio_fault_in_power_down_exit_4(self):
        hw = FakeHW(Clock())
        arm = fault_in_step('perst_gpio_assert')

        def phase(name, seconds):
            arm(name, hw, [])
            return self._null_phase(name, seconds)
        rc = self._run_main(hw, phase=phase)
        self.assertEqual(rc, wp.EXIT_POWER_UNCONFIRMED)
        self.assertFalse(hw.wifi_powered, 'SMC off still written')

    def test_port1_changed_exit_5(self):
        hw = FakeHW(Clock())

        def phase(name, seconds):
            if name == 'power-down:smc_stop':
                hw.regs[hw.f['port1'] + wp.PORT_STATUS] ^= 0x10
            return self._null_phase(name, seconds)
        rc = self._run_main(hw, phase=phase)
        self.assertEqual(rc, wp.EXIT_PORT1)

    def test_port1_unverified_exit_5(self):
        hw = FakeHW(Clock())
        arm = fault_in_step('port1_snapshot')

        def phase(name, seconds):
            arm(name, hw, [])
            return self._null_phase(name, seconds)
        rc = self._run_main(hw, phase=phase)
        self.assertEqual(rc, wp.EXIT_PORT1)

    def test_same_second_runs_get_separate_dirs(self):
        # REVIEW defect 5
        now = __import__('datetime').datetime(2026, 11, 15, 20, 0, 0)
        a, b = wp.make_run_dir(self.out, now), wp.make_run_dir(self.out, now)
        self.assertNotEqual(a, b)
        self.assertTrue(a.name.startswith('run-20261115-200000-'))
        self.assertEqual(self._run_main(FakeHW(Clock())), 0)
        self.assertEqual(self._run_main(FakeHW(Clock())), 0)
        self.assertEqual(len(list(self.out.glob('run-*/result.json'))), 2)

    def test_unwritable_out_dir_refuses_before_connect(self):
        blocker = self.out / 'not-a-dir'
        blocker.write_text('x')
        connect, st = self._connect(FakeHW(Clock()))
        rc = wp.main(['--run', '--out-dir', str(blocker)], runner=self._free, connect=connect,
                     log=self.lines.append)
        self.assertEqual(rc, wp.EXIT_REFUSED)
        self.assertEqual(st['called'], 0)
        self.assertTrue(any('nothing was touched' in x for x in self.lines))

    def test_result_write_failure_keeps_result_in_log(self):
        hw = FakeHW(Clock())
        self.locked = []

        def lock_run_dir():
            for d in self.out.glob('run-*'):
                d.chmod(0o500)
                self.locked.append(d)
        rc = self._run_main(hw, connect_hook=lock_run_dir)
        self.assertEqual(rc, 0)
        self.assertTrue(self.locked)
        line = next(x for x in self.lines if x.startswith('RESULT_JSON '))
        self.assertEqual(json.loads(line[len('RESULT_JSON '):])['verdict'], 'PASS_IDS')
        for d in self.locked:
            d.chmod(0o700)

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
