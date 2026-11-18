#!/usr/bin/env python3
"""Offline tests for nwoas_wifi_s259.py (the S259 hypervisor-side Wi-Fi power/link/BAR/DART logic).
Uses the S240/S257 fake proxy + fake BCM4378 (wifi-s240/test_wifi_probe.py, wifi-s257/test_wifi_up.py).
No serial port, no m1n1, no hardware.  Passing here is not a hardware result.

Run:  m1n1_windows/.venv-hv/bin/python3 -I -B nwoas_scripts/wifi-s259/test_wifi_hv.py
"""
import contextlib
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'wifi-s257'))
sys.path.insert(0, str(HERE.parent / 'wifi-s240'))
import nwoas_wifi_s259 as w   # noqa: E402
import test_wifi_up as tu      # noqa: E402
import test_wifi_probe as tp   # noqa: E402
wp, wu, ws = w.wp, w.wu, w.ws

L1 = 0xAE0160000


def uefi_state(hw, dart0_like_dart1=True, intx_masked=False):
    """Port 0 as UEFI leaves it on an unpowered chip (AppleSiliconPciPlatformDxe + PciBusDxe + EBS)."""
    hw.pcie_init()
    f, e = hw.f, hw.f['ecam']
    p0 = f['port0']
    hw.regs[f['gpio'] + 4 * f['perst_pin']] = 0x3          # PERST# deasserted (output 1)
    hw.regs[p0 + wp.PORT_REFCLK] = wp.REFCLK_EN
    hw.regs[p0 + wp.PORT_APPCLK] = wp.APPCLK_EN
    hw.regs[p0 + wp.PORT_LTSSMCTL] = 1
    hw.regs[wp.ecam_addr(e, 0, 0, 0, 0x18)] = 0x00010100      # pri 0 sec 1 sub 1 (boot logs)
    # Root port 0 exactly as read on hardware 2026-11-18 12:56 (run-20261118-125628-ynmpoboc
    # event-002 config_saved): PciBusDxe I/O|MEM + EBS MmioOr16(...,6) -> COMMAND 0x0007, memory window
    # closed (0xfff0), prefetchable window closed with a 64-bit upper base of 0xffffffff.
    hw.regs[wp.ecam_addr(e, 0, 0, 0, 0x04)] = 0x00100007
    hw.regs[wp.ecam_addr(e, 0, 0, 0, 0x20)] = 0x0000FFF0
    hw.regs[wp.ecam_addr(e, 0, 0, 0, 0x24)] = 0x0001FFF1
    hw.regs[wp.ecam_addr(e, 0, 0, 0, 0x28)] = 0xFFFFFFFF
    hw.regs[wp.ecam_addr(e, 0, 0, 0, 0x2C)] = 0x0
    hw.regs[p0 + w.INTMSK] = 0xFFFFFFFF if intx_masked else 0xFFFFFFF0
    d1 = w.DART1_BASE
    for s in range(16):
        hw.regs[d1 + ws.DART_TCR + 4 * s] = ws.DART_TCR_TRANSLATE
        hw.regs[d1 + ws.DART_TTBR + 16 * s] = (L1 >> 12) | ws.DART_TTBR_VALID
    hw.regs[d1 + ws.DART_ENABLED_STREAMS] = 0xFFFF
    if dart0_like_dart1:
        for s in range(16):
            hw.dart[ws.DART_TCR + 4 * s] = ws.DART_TCR_TRANSLATE
            hw.dart[ws.DART_TTBR + 16 * s] = (L1 >> 12) | ws.DART_TTBR_VALID
    return hw


def wrap_intmsk(hw):
    """PORT_INTMSKCLR (W1C on the mask) for the fake."""
    orig = hw.write32
    p0 = hw.f['port0']

    def w32(addr, val):
        if addr == p0 + w.INTMSKCLR:
            hw.writes.append((addr, val))
            hw.regs[p0 + w.INTMSK] = hw.regs.get(p0 + w.INTMSK, 0) & ~val
            return
        return orig(addr, val)
    hw.write32 = w32


def make(enabled=True, hw_kw=None, wlan=None, **ukw):
    clock = tp.Clock()
    hw = tu.FakeHW2(clock, **(hw_kw or {}))
    uefi_state(hw, **ukw)
    wrap_intmsk(hw)
    smcs = []

    def factory(base):
        assert base == wp.EXPECT['smc']
        s = tp.FakeSmc(hw)
        smcs.append(s)
        return s
    logs = []
    obj = w.WifiS259(hw, tp.good_facts(), factory, enabled=enabled, sleep=clock.sleep, clock=clock,
                     phase=lambda n, s: contextlib.nullcontext(), log=logs.append, run_dir=None,
                     wlan=wlan if wlan is not None else tu.fake_info())
    return obj, hw, smcs, logs


def ctl_write(obj, off, val):
    obj.hook_write(w.CTRL_IPA + off, val, 32)


def ctl_read(obj, off):
    return obj.hook_read(w.CTRL_IPA + off, 32)


class S259(unittest.TestCase):
    def assert_no_port1_writes(self, hw):
        e = hw.f['ecam']
        for a, v in hw.writes:
            if a == hw.f['port0'] + wp.PORT_PERST:              # S240 bring-up only SETS PERST_OFF
                self.assertTrue(v & wp.PERST_OFF, 'PORT_PERST_OFF must never be cleared')
                continue
            self.assertFalse(hw.f['port1'] <= a < hw.f['port1'] + 0x1000000, f'port1 write {a:#x}')
            self.assertFalse(w.DART1_BASE <= a < w.DART1_BASE + 0x4000, f'dart-apcie1 write {a:#x}')
            self.assertFalse(wp.ecam_addr(e, 2, 0, 0) <= a < wp.ecam_addr(e, 4, 0, 0), f'FL1100 cfg write {a:#x}')

    def power_up(self, obj):
        ctl_write(obj, w.R_CMD, w.CMD_POWER_UP)

    def test_magic_and_initial_state(self):
        obj, hw, smcs, _ = make()
        self.assertEqual(ctl_read(obj, w.R_MAGIC), w.MAGIC)
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_OFF)
        self.assertEqual(ctl_read(obj, w.R_AVAIL), 0)
        self.assertEqual(ctl_read(obj, 0x3FFC), 0)
        self.assertEqual(hw.writes, [])

    def test_boot_force_off_from_unpowered(self):
        obj, hw, smcs, _ = make()
        pd = obj.boot_force_off()
        self.assertTrue(pd['confirmed'], pd)
        self.assertEqual(smcs[0].written, [wp.SMC_GPIO_OFF])
        self.assertTrue(smcs[0].stopped)
        self.assertEqual(hw.regs[hw.f['gpio'] + 4 * hw.f['perst_pin']], 0x2)
        self.assert_no_port1_writes(hw)

    def test_boot_force_off_drops_a_left_on_chip(self):
        obj, hw, smcs, _ = make(hw_kw={'link_already_up': True})
        pd = obj.boot_force_off()
        self.assertTrue(pd['confirmed'])
        self.assertTrue(pd['link_down_after'])

    def test_boot_force_off_link_stuck_raises(self):
        obj, hw, smcs, _ = make(hw_kw={'link_already_up': True, 'link_stuck_up': True})
        hw.was_up = True
        with self.assertRaises(RuntimeError) as cm:
            obj.boot_force_off()
        self.assertIn('link still up', str(cm.exception))

    def test_power_up_happy(self):
        obj, hw, smcs, logs = make()
        obj.boot_force_off()
        self.power_up(obj)
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_UP, logs[-12:])
        self.assertEqual(ctl_read(obj, w.R_AVAIL), 1)
        self.assertEqual(ctl_read(obj, w.R_ERROR), 0)
        self.assertEqual(ctl_read(obj, w.R_IDS), wp.WIFI_ID)
        self.assertEqual(ctl_read(obj, w.R_BAR0), w.BAR0_BUS)
        self.assertEqual(ctl_read(obj, w.R_BAR2), w.BAR2_BUS)
        self.assertEqual(ctl_read(obj, w.R_CHIPID) & 0xFFFF, 0x4378)
        self.assertEqual(ctl_read(obj, w.R_DART_CHECK), 1)
        cfg = hw.fn[0]['cfg']
        self.assertEqual(cfg[0x10] & ~0xF, w.BAR0_BUS)
        self.assertEqual(cfg[0x18] & ~0xF, w.BAR2_BUS)
        self.assertEqual(cfg[0x04] & 0x406, 0x6)                     # MEM | BME, INTx enabled
        self.assertFalse(cfg[hw.devctl_reg] & w.DEVCTL_NS_RO)
        rp = hw.regs[wp.ecam_addr(hw.f['ecam'], 0, 0, 0, 0x20)]
        self.assertEqual(rp, 0xE080E000)                              # S257 hardware readback value
        self.assertEqual(smcs[-1].written[-1], wp.SMC_GPIO_ON)
        self.assertTrue(smcs[-1].stopped)                             # quiesced after the write (dcp.c pattern)
        # bus master only after the DART was checked (both bus-master writes saw translate on)
        self.assertTrue(hw.bme_seen_with)
        for _, tcrs in hw.bme_seen_with:
            self.assertTrue(all(t & ws.DART_TCR_TRANSLATE for t in tcrs))
        self.assertEqual(ctl_read(obj, w.R_SEQ), 1)
        self.assert_no_port1_writes(hw)
        self.assertTrue(any(l.startswith('[s259] READY') for l in logs))

    def test_post_uefi_root_port_io_bit_kept(self):
        # 2026-11-18 12:56 hardware failure: S257's check wanted (rp cmd & 7) == MEM, UEFI leaves I/O on.
        obj, hw, smcs, logs = make()
        self.power_up(obj)
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_UP, [l for l in logs if 'FAIL' in l or 'mismatch' in l])
        e = hw.f['ecam']
        self.assertEqual(hw.rp_cmd_history, [0x0003, 0x0007])          # BME off for BARs, then MEM|BME (+I/O)
        self.assertEqual(hw.regs[wp.ecam_addr(e, 0, 0, 0, 0x24)] & 0xFFF0FFF0, 0x0000FFF0)
        self.assertEqual(hw.regs[wp.ecam_addr(e, 0, 0, 0, 0x28)], 0)
        bl = obj.events[-1]['bar_layout']
        self.assertEqual(bl['mismatch'], [])
        self.assertEqual(bl['readback']['rp 0x04 cmd'], '0x3')

    def test_bar_readback_mismatch_names_register(self):
        obj, hw, smcs, logs = make()
        orig = hw.write32
        win = wp.ecam_addr(hw.f['ecam'], 0, 0, 0, 0x20)

        def w32(addr, val):
            if addr == win:
                val = 0x0000FFF0                                         # window write does not stick
            return orig(addr, val)
        hw.write32 = w32
        self.power_up(obj)
        self.assertEqual(w.ERRORS[ctl_read(obj, w.R_ERROR)], 'BAR_SETUP')
        self.assertIn('rp 0x20 window: want 0xe080e000 got 0xfff0', obj.events[-1]['failure'])
        self.assertTrue(any('BAR read-back mismatch' in l and 'rp 0x20' in l for l in logs))
        self.assertFalse(hw.wifi_powered)
        self.assertFalse(hw.bme_seen_with)

    def test_dart0_copied_from_dart1(self):
        obj, hw, smcs, _ = make(dart0_like_dart1=False)
        self.power_up(obj)
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_UP)
        self.assertEqual(ctl_read(obj, w.R_DART_CHECK), 2)
        for s in range(16):
            self.assertEqual(hw.dart[ws.DART_TCR + 4 * s], ws.DART_TCR_TRANSLATE)
            self.assertEqual(hw.dart[ws.DART_TTBR + 16 * s], (L1 >> 12) | ws.DART_TTBR_VALID)
        self.assert_no_port1_writes(hw)

    def test_dart1_not_uefi_shape_refuses_before_bus_master(self):
        obj, hw, smcs, _ = make()
        hw.regs[w.DART1_BASE + ws.DART_TTBR] = 0
        self.power_up(obj)
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_FAILED)
        self.assertEqual(w.ERRORS[ctl_read(obj, w.R_ERROR)], 'DART_MISMATCH')
        self.assertFalse(hw.bme_seen_with)
        self.assertFalse(hw.wifi_powered)
        self.assertEqual(ctl_read(obj, w.R_AVAIL), 0)

    def test_intx_unmasked_when_masked(self):
        obj, hw, smcs, _ = make(intx_masked=True)
        self.power_up(obj)
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_UP)
        self.assertFalse(hw.regs[hw.f['port0'] + w.INTMSK] & w.INTX_BITS)

    def test_device_absent_fails_and_powers_off(self):
        obj, hw, smcs, _ = make(hw_kw={'device_present': False})
        self.power_up(obj)
        self.assertEqual(w.ERRORS[ctl_read(obj, w.R_ERROR)], 'LINK_DOWN')
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_FAILED)
        self.assertEqual(smcs[-1].written[-1], wp.SMC_GPIO_OFF)
        self.assertEqual(hw.regs[hw.f['gpio'] + 4 * hw.f['perst_pin']], 0x2)
        self.assertFalse(hw.dart_dead)

    def test_link_already_up_refuses(self):
        obj, hw, smcs, _ = make(hw_kw={'link_already_up': True})
        self.power_up(obj)
        self.assertEqual(w.ERRORS[ctl_read(obj, w.R_ERROR)], 'LINK_ALREADY_UP')
        self.assertFalse(any(v == wp.SMC_GPIO_ON for s in smcs for v in s.written))

    def test_power_down_then_up_again(self):
        obj, hw, smcs, _ = make()
        self.power_up(obj)
        ctl_write(obj, w.R_CMD, w.CMD_POWER_DOWN)
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_OFF_REQ)
        self.assertFalse(hw.wifi_powered)
        self.assertEqual(hw.fn[0]['cfg'][0x04] & wu.CMD_BME, 0)
        self.assertFalse(hw.dart_dead, 'dart-apcie0 must stay reachable (PORT_PERST_OFF kept)')
        self.assertEqual(ctl_read(obj, w.R_AVAIL), 1)                   # _STA keeps the device present
        # second _PS0: full bring-up again (fresh chip after PERST#/power)
        for k in (0x10, 0x14, 0x18, 0x1C):
            hw.fn[0]['cfg'][k] = hw.bar_orig[0][k]
        hw.fn[0]['cfg'][0x04] = 0x00100000
        self.power_up(obj)
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_UP)
        self.assertEqual(ctl_read(obj, w.R_SEQ), 3)
        self.assert_no_port1_writes(hw)

    def test_power_up_idempotent(self):
        obj, hw, smcs, _ = make()
        self.power_up(obj)
        n = len(smcs)
        self.power_up(obj)
        self.assertEqual(len(smcs), n, 'no SMC traffic when already up')
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_UP)

    def test_config_window(self):
        obj, hw, smcs, _ = make()
        ctl_write(obj, w.R_CFG_OFF, 0x0)
        self.assertEqual(ctl_read(obj, w.R_CFG_DATA), 0xFFFFFFFF)       # not up yet
        self.assertEqual(ctl_read(obj, w.R_CFG_ERR), w.CFG_NOT_UP)
        self.power_up(obj)
        self.assertEqual(ctl_read(obj, w.R_CFG_DATA), wp.WIFI_ID)
        ctl_write(obj, w.R_BAR0WIN, 0x18001000)
        self.assertEqual(hw.fn[0]['cfg'][0x80], 0x18001000)
        self.assertEqual(ctl_read(obj, w.R_BAR0WIN), 0x18001000)
        ctl_write(obj, w.R_CFG_OFF, 0x10)                                # BAR: refused
        ctl_write(obj, w.R_CFG_DATA, 0)
        self.assertEqual(ctl_read(obj, w.R_CFG_ERR), w.CFG_NOT_ALLOWED)
        self.assertEqual(hw.fn[0]['cfg'][0x10] & ~0xF, w.BAR0_BUS)
        ctl_write(obj, w.R_CFG_OFF, 0x04)                                # MEM forced on
        ctl_write(obj, w.R_CFG_DATA, 0x0404)
        self.assertEqual(hw.fn[0]['cfg'][0x04], 0x0406)
        ctl_write(obj, w.R_CFG_OFF, hw.devctl_reg)                       # NS/RO cannot be turned on
        ctl_write(obj, w.R_CFG_DATA, 0x2810)
        self.assertEqual(hw.fn[0]['cfg'][hw.devctl_reg], 0x2000)
        ctl_write(obj, w.R_CFG_OFF, 0x81)
        self.assertEqual(ctl_read(obj, w.R_CFG_ERR), w.CFG_BAD_OFFSET)
        self.assert_no_port1_writes(hw)

    def test_cfg_policy(self):
        self.assertIsNone(w.cfg_write_policy(0x80, 0x18000123, 0, 0xB4))
        self.assertEqual(w.cfg_write_policy(0x04, 0xFFFF0000, 0, 0xB4), 0x2)
        self.assertEqual(w.cfg_write_policy(0xBC, 0xFFFFFFFF, 0x10140, 0xB4), 0x143)
        self.assertIsNone(w.cfg_write_policy(0x18, 1, 0, 0xB4))
        self.assertEqual(w.cfg_write_policy(0xB4, 0xFFFFFFFF, 0, 0xB4), 0xF7EF)

    def test_disabled_never_touches_hardware(self):
        obj, hw, smcs, _ = make(enabled=False)
        self.power_up(obj)
        self.assertEqual(ctl_read(obj, w.R_STATE), w.STATE_FAILED)
        self.assertEqual(w.ERRORS[ctl_read(obj, w.R_ERROR)], 'DISABLED')
        self.assertEqual(ctl_read(obj, w.R_AVAIL), 0)
        self.assertEqual(hw.writes, [])
        self.assertEqual(smcs, [])

    def test_hook_never_raises(self):
        obj, hw, smcs, _ = make()

        def boom(*a, **k):
            raise RuntimeError('fake proxy dead')
        obj.reg_read = boom
        self.assertEqual(obj.hook_read(w.CTRL_IPA, 32), 0xFFFFFFFF)
        obj.reg_write = boom
        obj.hook_write(w.CTRL_IPA + w.R_CMD, 1, 32)
        self.assertEqual(obj.state, w.STATE_FAILED)

    def test_adt_data_registers(self):
        obj, hw, smcs, logs = make()
        info = tu.fake_info()
        mac = ctl_read(obj, w.R_MAC_LO).to_bytes(4, 'little') + ctl_read(obj, w.R_MAC_HI).to_bytes(4, 'little')[:2]
        self.assertEqual(mac, info['mac'])
        self.assertEqual(ctl_read(obj, w.R_CAL_LEN), len(info['cal']))
        cal = b''.join(ctl_read(obj, w.CAL_OFF + i).to_bytes(4, 'little') for i in range(0, len(info['cal']), 4))
        self.assertEqual(cal[:len(info['cal'])], info['cal'])
        self.assertEqual(ctl_read(obj, w.R_ANTENNA).to_bytes(4, 'little').rstrip(b'\0'), b'X3')
        board = b''.join(ctl_read(obj, w.R_BOARD + i).to_bytes(4, 'little') for i in range(0, 16, 4))
        self.assertEqual(board.rstrip(b'\0'), b'atlantisb')
        self.assertFalse(any(info['mac'].hex() in l for l in logs), 'MAC must not be logged')
        self.assertEqual(hw.writes, [])

    def test_contract_constants_match_ssdt(self):
        inc = (HERE / 'acpi' / 'NwoasWifiJ274.asl.inc').read_text()
        for base, size in ((w.BAR0_BUS + w.CPU_OFFSET, w.BAR0_SIZE), (w.BAR2_BUS + w.CPU_OFFSET, w.BAR2_SIZE),
                           (w.CTRL_IPA, w.CTRL_SIZE)):
            self.assertIn('0x%016X, 0x%016X, 0x0,' % (base, base + size - 1), inc)
        self.assertIn('{ %d }' % w.WLAN_IRQ, inc)
        self.assertIn('0x%08X' % w.MAGIC, inc)


ROOT = HERE.parents[1]
ADT_DUMP = ROOT / 'experiments' / 'tahoe-afk-20260906' / 'current.adt'


@unittest.skipUnless(ADT_DUMP.is_file(), 'offline ADT dump missing')
class Module(unittest.TestCase):
    """wifi_module.py exactly as run_guest.py runs it (runpy with hv/u/p), on the real offline ADT,
    a fake hv tracer map (m1n1 DictRangeMap) and the fake hardware."""

    def run_module(self):
        import runpy
        import types
        sys.path.insert(0, str(ROOT / 'm1n1_windows' / 'proxyclient'))
        from m1n1.adt import load_adt
        from m1n1.utils import DictRangeMap, irange
        from m1n1.hv.types import TraceMode
        adt = load_adt(ADT_DUMP.read_bytes())
        hw = tu.FakeHW2(tp.Clock())
        uefi_state(hw)

        class HV:
            def __init__(s):
                s.mmio_maps = DictRangeMap()

            def add_tracer(s, zone, ident, mode, read=None, write=None, **kw):
                s.mmio_maps[zone, ident] = (mode, ident, read, write, kw)
        hv = HV()
        for r in adt['/arm-io'].ranges:
            hv.add_tracer(irange(r.parent_addr, r.size), 'HW', TraceMode.OFF)
        u = types.SimpleNamespace(adt=adt, ba=types.SimpleNamespace(phys_base=0x83BED8000, mem_size_actual=0x3C0000000))
        old_smc, old_dir = w.wp.RealSmc, w.make_run_dir
        w.wp.RealSmc = lambda uu, base: tp.FakeSmc(hw)
        w.make_run_dir = lambda: None
        try:
            runpy.run_path(str(HERE / 'wifi_module.py'), init_globals={'hv': hv, 'u': u, 'p': hw},
                           run_name='<hv_script>')
        finally:
            w.wp.RealSmc, w.make_run_dir = old_smc, old_dir
        return hv, hw, irange, TraceMode

    def test_bar_range_is_hypervisor_emulated(self):
        hv, hw, irange, TraceMode = self.run_module()
        base, size = w.BAR_EMUL
        self.assertEqual((base, size), (0x6E0000000, 0x900000))
        # the same choice m1n1 hv.pt_update makes: highest mode among the overlapping tracers wins
        for zone, maps in hv.mmio_maps.overlaps(irange(base, size)):
            top = sorted(maps.values(), reverse=True)[0]
            self.assertEqual((top[0], top[1]), (TraceMode.BYPASS, 'wifi-s259-bar-emul'), hex(zone.start))
            self.assertTrue(base <= zone.start and zone.stop <= base + size)
        for addr in (w.BAR0_BUS + w.CPU_OFFSET + 0x120, w.BAR0_BUS + w.CPU_OFFSET + 0xC34,
                     w.BAR2_BUS + w.CPU_OFFSET, base + size - 4):    # 2026-11-18 L2C_ERR_ADR values + ends
            top = sorted(hv.mmio_maps[addr].values(), reverse=True)[0]
            self.assertEqual(top[0], TraceMode.BYPASS, hex(addr))
        # FL1100 BAR / neighbours untouched by this module
        self.assertEqual(sorted(hv.mmio_maps[0x6C0000000].values(), reverse=True)[0][0], TraceMode.OFF)
        self.assertEqual(sorted(hv.mmio_maps[base + size].values(), reverse=True)[0][0], TraceMode.OFF)
        ctl = sorted(hv.mmio_maps[w.CTRL_IPA].values(), reverse=True)[0]
        self.assertEqual((ctl[0], ctl[1]), (TraceMode.HOOK, 'wifi-s259-ctl'))


if __name__ == '__main__':
    unittest.main(verbosity=1)
