#!/usr/bin/env python3
"""Offline tests for nwoas_bt_s261.py (S261: Bluetooth fn1 on top of the S259 Wi-Fi bring-up) and
bt_module.py.  Uses the S240/S257/S259 fakes.  No serial port, no m1n1, no hardware.
Passing here is not a hardware result.

Run:  m1n1_windows/.venv-hv/bin/python3 -I -B nwoas_scripts/bt-s261/test_bt_hv.py
"""
import contextlib
import re
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
for d in (HERE, HERE.parent / 'wifi-s259', HERE.parent / 'wifi-s257', HERE.parent / 'wifi-s240',
          ROOT / 'm1n1_windows' / 'proxyclient'):
    sys.path.insert(0, str(d))
import nwoas_bt_s261 as b     # noqa: E402
import nwoas_wifi_s259 as w   # noqa: E402
import test_wifi_hv as th     # noqa: E402
import test_wifi_up as tu     # noqa: E402
import test_wifi_probe as tp  # noqa: E402
wp, wu, ws = w.wp, w.wu, w.ws

FAKE_BD = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66])
FAKE_CAL = bytes((i * 7 + 3) & 0xFF for i in range(5664))
FAKE_CALBF = bytes((i * 13 + 1) & 0xFF for i in range(5664))
FN1_PCIE, FN1_MSI = 0x48, 0x60


class FakeHW3(tu.FakeHW2):
    """FakeHW2 plus a fn1 config model: command, DevCtl (FLR), LinkCtl, MSI, vendor windows 0x70-0x88."""

    def __init__(self, clock, flr_cap=True, fn1_bar_stuck=False, **kw):
        super().__init__(clock, **kw)
        cfg = self.fn[1]['cfg']
        cfg[0x48] = 0x00020010 | (FN1_MSI << 8)              # PCIe cap, next = MSI
        cfg[0x4C] = (1 << 28) if flr_cap else 0            # DevCap FLR
        cfg[0x50] = 0x2810                                   # DevCtl RO|NS like hardware 0x2c10
        cfg[0x58] = 0x00110040
        cfg[FN1_MSI] = 0x00800005                            # MSI, 64-bit capable, next 0
        cfg[0x88] = 0x00080000
        self.bar_orig[1] = dict(cfg)
        self.flr_count = 0
        self.fn1_bar_stuck = fn1_bar_stuck

    def write32(self, addr, val):
        e = self.f['ecam']
        lo = wp.ecam_addr(e, 1, 0, 1)
        if lo <= addr < lo + 0x1000 and self._link_up():
            reg = addr - lo
            cfg = self.fn[1]['cfg']
            if 0x10 <= reg <= 0x24:
                if self.fn1_bar_stuck and reg == 0x18 and val == b.FN1_BAR2_BUS:   # sizing works, assign does not
                    self.writes.append((addr, val))
                    return
                return super().write32(addr, val)
            self.writes.append((addr, val))
            if reg == 0x50 and val & (1 << 15):
                self.flr_count += 1
                for k, v in self.bar_orig[1].items():
                    cfg[k] = v
                return
            if reg in (0x04, 0x50):
                cfg[reg] = (cfg.get(reg, 0) & 0xFFFF0000) | (val & 0xFFFF)
            else:
                cfg[reg] = val
            if reg == 0x04 and val & wu.CMD_BME:
                self.bme_seen_with.append(('fn1', [self.dart[ws.DART_TCR + 4 * s] for s in range(16)]))
            return
        return super().write32(addr, val)


def make(enabled=True, bt_enabled=True, hw_kw=None, bt=None, **ukw):
    clock = tp.Clock()
    hw = FakeHW3(clock, **(hw_kw or {}))
    th.uefi_state(hw, **ukw)
    th.wrap_intmsk(hw)
    smcs = []

    def factory(base):
        s = tp.FakeSmc(hw)
        smcs.append(s)
        return s
    logs = []
    obj = b.WifiBtS261(hw, tp.good_facts(), factory, enabled=enabled, bt_enabled=bt_enabled,
                       bt=bt if bt is not None else {'bdaddr': FAKE_BD, 'cal': FAKE_CAL, 'calbf': FAKE_CALBF},
                       sleep=clock.sleep, clock=clock, phase=lambda n, s: contextlib.nullcontext(),
                       log=logs.append, run_dir=None, wlan=tu.fake_info())
    return obj, hw, smcs, logs


def bw(obj, off, val):
    obj.bt_hook_write(b.BT_CTRL_IPA + off, val, 32)


def br(obj, off, width=32):
    return obj.bt_hook_read(b.BT_CTRL_IPA + off, width)


def c1(hw, off):
    return hw.fn[1]['cfg'].get(off, 0)


class S261(unittest.TestCase):
    def up(self, **kw):
        obj, hw, smcs, logs = make(**kw)
        obj.boot_force_off()
        th.ctl_write(obj, w.R_CMD, w.CMD_POWER_UP)          # WLN0._INI (S259)
        return obj, hw, smcs, logs

    def assert_port1_clean(self, hw):
        th.S259.assert_no_port1_writes(self, hw)

    def test_constants(self):
        self.assertEqual(b.RP_WIN_S259, 0xE080E000)
        self.assertEqual(b.RP_WIN_S261, 0xE0F0E000)
        self.assertEqual(b.BT_MAGIC.to_bytes(4, 'little'), b'NWBT')
        self.assertEqual(b.FN1_BAR0_BUS % b.FN1_BAR0_SIZE, 0)
        self.assertEqual(b.FN1_BAR2_BUS % b.FN1_BAR2_SIZE, 0)
        # fn1 BARs: above the fn0 BARs, inside the new window, inside the UEFI RAM reservation
        self.assertGreaterEqual(b.FN1_BAR0_BUS, w.WINDOW_BUS[1] + 1)
        self.assertLessEqual(b.FN1_BAR2_BUS + b.FN1_BAR2_SIZE - 1, b.WINDOW_S261[1])
        self.assertLessEqual(b.FN1_BAR0_BUS + b.FN1_BAR0_SIZE, b.FN1_BAR2_BUS)
        self.assertEqual(sum(w.RESERVED_RAM) - 1, b.WINDOW_S261[1])
        self.assertEqual(b.BT_CTRL_IPA, w.CTRL_IPA + w.CTRL_SIZE)
        self.assertLessEqual(b.CALBF_OFF + b.CAL_MAX, b.BT_CTRL_SIZE)
        self.assertLessEqual(b.CAL_OFF + b.CAL_MAX, b.CALBF_OFF)

    def test_power_up_assigns_fn1(self):
        obj, hw, smcs, logs = self.up()
        self.assertEqual(th.ctl_read(obj, w.R_STATE), w.STATE_UP, logs[-10:])
        self.assertEqual(br(obj, b.R_MAGIC), b.BT_MAGIC)
        self.assertEqual(br(obj, b.R_STATE), b.BT_UP, logs[-10:])
        self.assertEqual(br(obj, b.R_AVAIL), 1)
        self.assertEqual(br(obj, b.R_IDS), b.BT_ID)
        self.assertEqual(br(obj, b.R_BAR0), b.FN1_BAR0_BUS)
        self.assertEqual(br(obj, b.R_BAR2), b.FN1_BAR2_BUS)
        self.assertEqual(br(obj, b.R_EPOCH), 1)
        self.assertEqual(c1(hw, 0x10) & ~0xF, b.FN1_BAR0_BUS)
        self.assertEqual(c1(hw, 0x18) & ~0xF, b.FN1_BAR2_BUS)
        self.assertEqual(c1(hw, 0x04) & 0x407, b.FN1_CMD_QUIET)      # decode on, bus master OFF, INTx off
        self.assertFalse(c1(hw, 0x50) & w.DEVCTL_NS_RO)
        self.assertEqual(hw.regs[wp.ecam_addr(hw.f['ecam'], 0, 0, 0, 0x20)], b.RP_WIN_S261)
        self.assertEqual(br(obj, b.R_WINDOW), b.RP_WIN_S261)
        # fn0 exactly as S259
        cfg0 = hw.fn[0]['cfg']
        self.assertEqual(cfg0[0x10] & ~0xF, w.BAR0_BUS)
        self.assertEqual(cfg0[0x18] & ~0xF, w.BAR2_BUS)
        self.assertEqual(cfg0[0x04] & 0x406, 0x6)
        self.assertEqual(br(obj, b.R_MSICAP), FN1_MSI)
        self.assertTrue(br(obj, b.R_DEVCAP) & b.DEVCAP_FLR)
        self.assertTrue(any(l.startswith('[s261] READY fn1') for l in logs))
        self.assert_port1_clean(hw)

    def test_bt_disabled_is_s259(self):
        obj, hw, smcs, logs = self.up(bt_enabled=False)
        self.assertEqual(th.ctl_read(obj, w.R_STATE), w.STATE_UP)
        self.assertEqual(br(obj, b.R_STATE), b.BT_FAILED)
        self.assertEqual(b.BT_ERRORS[br(obj, b.R_ERROR)], 'DISABLED')
        self.assertEqual(br(obj, b.R_AVAIL), 0)
        lo = wp.ecam_addr(hw.f['ecam'], 1, 0, 1)
        self.assertFalse([a for a, _ in hw.writes if lo <= a < lo + 0x1000 and a - lo not in range(0x10, 0x28)],
                         'fn1 config must not be written (BAR sizing restore only)')
        self.assertEqual(hw.regs[wp.ecam_addr(hw.f['ecam'], 0, 0, 0, 0x20)], b.RP_WIN_S259)
        bw(obj, b.R_CMD, b.BT_CMD_ENSURE)
        self.assertEqual(br(obj, b.R_STATE), b.BT_FAILED)

    def test_wifi_disabled_hides_both(self):
        obj, hw, smcs, logs = make(enabled=False)
        bw(obj, b.R_CMD, b.BT_CMD_ENSURE)
        self.assertEqual(b.BT_ERRORS[br(obj, b.R_ERROR)], 'WIFI_DISABLED')
        self.assertEqual(br(obj, b.R_AVAIL), 0)
        self.assertEqual(hw.writes, [])
        self.assertEqual(smcs, [])

    def test_fn1_failure_keeps_wifi(self):
        obj, hw, smcs, logs = self.up(hw_kw={'fn1_bar_stuck': True})
        self.assertEqual(th.ctl_read(obj, w.R_STATE), w.STATE_UP)
        self.assertEqual(br(obj, b.R_STATE), b.BT_FAILED)
        self.assertEqual(b.BT_ERRORS[br(obj, b.R_ERROR)], 'FN1_BARS')
        self.assertEqual(c1(hw, 0x04) & 0x7, 0, 'fn1 decode off after a failure')
        self.assertEqual(hw.regs[wp.ecam_addr(hw.f['ecam'], 0, 0, 0, 0x20)], b.RP_WIN_S259)
        self.assertEqual(hw.fn[0]['cfg'][0x04] & 0x6, 0x6, 'Wi-Fi keeps bus master')
        self.assertEqual(br(obj, b.R_BAR0), 0)
        self.assert_port1_clean(hw)

    def test_bt_ini_before_wifi_ini_powers_chip(self):
        obj, hw, smcs, logs = make()
        obj.boot_force_off()
        bw(obj, b.R_CMD, b.BT_CMD_ENSURE)                     # BTH0._INI first
        self.assertEqual(th.ctl_read(obj, w.R_STATE), w.STATE_UP)
        self.assertEqual(br(obj, b.R_STATE), b.BT_UP)
        n = len(smcs)
        th.ctl_write(obj, w.R_CMD, w.CMD_POWER_UP)          # WLN0._INI second: no change
        self.assertEqual(len(smcs), n)
        self.assertEqual(br(obj, b.R_EPOCH), 1)
        self.assertEqual(th.ctl_read(obj, w.R_AVAIL), 1)

    def test_ensure_does_not_retry_failed_chip(self):
        obj, hw, smcs, logs = make(hw_kw={'device_present': False})
        obj.boot_force_off()
        th.ctl_write(obj, w.R_CMD, w.CMD_POWER_UP)
        self.assertEqual(th.ctl_read(obj, w.R_STATE), w.STATE_FAILED)
        n = len(smcs)
        bw(obj, b.R_CMD, b.BT_CMD_ENSURE)
        self.assertEqual(len(smcs), n, 'BT must not re-power a chip Wi-Fi failed on')
        self.assertEqual(b.BT_ERRORS[br(obj, b.R_ERROR)], 'CHIP_DOWN')
        self.assertEqual(br(obj, b.R_AVAIL), 0)

    def test_quiet_and_ensure(self):
        obj, hw, smcs, logs = self.up()
        bw(obj, b.R_CFG_OFF, 0x04)
        bw(obj, b.R_CFG_DATA, wu.CMD_BME | wu.CMD_INTX_DIS)
        self.assertEqual(c1(hw, 0x04) & 0x407, 0x406)
        bw(obj, b.R_CMD, b.BT_CMD_QUIET)                      # BTH0._PS3
        self.assertEqual(br(obj, b.R_STATE), b.BT_QUIET)
        self.assertEqual(c1(hw, 0x04) & 0x407, b.FN1_CMD_QUIET)
        self.assertTrue(hw.wifi_powered, 'chip power stays (owned by Wi-Fi)')
        bw(obj, b.R_CFG_OFF, 0x04)
        bw(obj, b.R_CFG_DATA, wu.CMD_BME)
        self.assertEqual(br(obj, b.R_CFG_ERR), b.CFG_NOT_UP)          # no DMA while quiet
        bw(obj, b.R_CMD, b.BT_CMD_ENSURE)                     # BTH0._PS0
        self.assertEqual(br(obj, b.R_STATE), b.BT_UP)
        self.assertEqual(c1(hw, 0x04) & 0x407, b.FN1_CMD_QUIET)      # driver turns BME on itself

    def test_wifi_power_down_takes_bt_down_and_back(self):
        obj, hw, smcs, logs = self.up()
        th.ctl_write(obj, w.R_CMD, w.CMD_POWER_DOWN)        # WLN0._PS3
        self.assertEqual(br(obj, b.R_STATE), b.BT_OFF)
        self.assertEqual(br(obj, b.R_BAR0), 0)
        for fn in (0, 1):                                   # chip lost power: BARs back to reset values
            for k in (0x10, 0x14, 0x18, 0x1C, 0x04):
                hw.fn[fn]['cfg'][k] = hw.bar_orig[fn][k]
        bw(obj, b.R_CMD, b.BT_CMD_ENSURE)
        self.assertEqual(th.ctl_read(obj, w.R_STATE), w.STATE_UP)
        self.assertEqual(br(obj, b.R_STATE), b.BT_UP)
        self.assertEqual(br(obj, b.R_EPOCH), 2)
        self.assert_port1_clean(hw)

    def test_cfg_policy(self):
        caps = {'pcie': 0xAC, 'msi': 0x58, 'msi64': True}
        P = b.bt_cfg_write_policy
        self.assertEqual(P(0x80, 0x18002000, 0, caps), 0x18002000)
        self.assertEqual(P(0x74, 0x18011000, 0, caps), 0x18011000)
        self.assertEqual(P(0x84, 0x19000000, 0, caps), 0x19000000)
        self.assertIsNone(P(0x80, 0x18002004, 0, caps))
        self.assertIsNone(P(0x70, 0x0, 0, caps))
        self.assertEqual(P(0x88, 0xFFFFFFFF, 0x00000123, caps), 0x00090123)
        self.assertEqual(P(0x88, 0x0, 0x00090123, caps), 0x00000123)
        self.assertEqual(P(0x04, 0xFFFFFFFF, 0, caps), 0x406)
        self.assertEqual(P(0xAC + 8, 0xFFFF, 0, caps), 0xFFFF & ~0x0810 & ~0x8000)
        self.assertEqual(P(0xAC + 0x10, 0xFFFFFFFF, 0x10140, caps), 0x143)
        self.assertEqual(P(0x58, 0xFFFFFFFF, 0x00800005, caps), 0x00810005)
        self.assertEqual(P(0x5C, 0x00200000, 0, caps), 0x00200000)
        self.assertIsNone(P(0x5C, 0xFFFFF000, 0, caps))
        self.assertIsNone(P(0x60, 1, 0, caps))
        self.assertEqual(P(0x64, 0x12345678, 0, caps), 0x5678)
        for off in (0x00, 0x10, 0x18, 0x3C, 0x100):
            self.assertIsNone(P(off, 0, 0, caps), hex(off))

    def test_config_access(self):
        obj, hw, smcs, logs = self.up()
        bw(obj, b.R_CFG_OFF, 0x0)
        self.assertEqual(br(obj, b.R_CFG_DATA), b.BT_ID)
        for off, val in ((0x80, 0x18002000), (0x70, 0x1810A000), (0x74, 0x18011000), (0x78, 0x18107000),
                         (0x84, 0x19000000)):
            bw(obj, b.R_CFG_OFF, off)
            bw(obj, b.R_CFG_DATA, val)
            self.assertEqual(br(obj, b.R_CFG_ERR), b.CFG_OK)
            self.assertEqual(c1(hw, off), val)
        bw(obj, b.R_CFG_OFF, 0x88)
        bw(obj, b.R_CFG_DATA, br(obj, b.R_CFG_DATA) | (1 << 16))
        self.assertEqual(c1(hw, 0x88), 0x00090000)
        bw(obj, b.R_CFG_OFF, 0x18)                             # BARs: refused
        bw(obj, b.R_CFG_DATA, 0)
        self.assertEqual(br(obj, b.R_CFG_ERR), b.CFG_NOT_ALLOWED)
        self.assertEqual(c1(hw, 0x18) & ~0xF, b.FN1_BAR2_BUS)
        # MSI enable refused while the address is unset (0) ...
        bw(obj, b.R_CFG_OFF, FN1_MSI)
        bw(obj, b.R_CFG_DATA, b.MSI_ENABLE)
        self.assertEqual(br(obj, b.R_CFG_ERR), b.CFG_NOT_ALLOWED)
        # ... and accepted with a RAM address inside the DART window
        bw(obj, b.R_CFG_OFF, FN1_MSI + 4)
        bw(obj, b.R_CFG_DATA, 0x00200000)
        bw(obj, b.R_CFG_OFF, FN1_MSI)
        bw(obj, b.R_CFG_DATA, b.MSI_ENABLE)
        self.assertEqual(br(obj, b.R_CFG_ERR), b.CFG_OK)
        self.assertTrue(c1(hw, FN1_MSI) & b.MSI_ENABLE)
        bw(obj, b.R_CMD, b.BT_CMD_QUIET)
        self.assertFalse(c1(hw, FN1_MSI) & b.MSI_ENABLE, 'quiet turns MSI off')
        self.assert_port1_clean(hw)

    def test_flr(self):
        obj, hw, smcs, logs = self.up()
        bw(obj, b.R_CFG_OFF, 0x04)
        bw(obj, b.R_CFG_DATA, wu.CMD_BME | wu.CMD_INTX_DIS)
        bw(obj, b.R_RESET, 1)
        self.assertEqual(br(obj, b.R_RESET), b.RESET_OK, logs[-5:])
        self.assertEqual(hw.flr_count, 1)
        self.assertEqual(c1(hw, 0x10) & ~0xF, b.FN1_BAR0_BUS)
        self.assertEqual(c1(hw, 0x18) & ~0xF, b.FN1_BAR2_BUS)
        self.assertEqual(c1(hw, 0x04) & 0x407, b.FN1_CMD_QUIET)
        self.assertFalse(c1(hw, 0x50) & (w.DEVCTL_NS_RO | b.DEVCTL_FLR))
        self.assertEqual(br(obj, b.R_RESET_COUNT), 1)
        self.assertEqual(hw.fn[0]['cfg'][0x04] & 0x6, 0x6, 'fn0 untouched by fn1 FLR')

    def test_flr_without_capability(self):
        obj, hw, smcs, logs = self.up(hw_kw={'flr_cap': False})
        bw(obj, b.R_RESET, 1)
        self.assertEqual(br(obj, b.R_RESET), b.RESET_NO_FLR)
        self.assertEqual(hw.flr_count, 0)

    def test_adt_registers(self):
        obj, hw, smcs, logs = make()
        bd = br(obj, b.R_BDADDR_LO).to_bytes(4, 'little') + br(obj, b.R_BDADDR_HI).to_bytes(4, 'little')[:2]
        self.assertEqual(bd, FAKE_BD)
        self.assertEqual(br(obj, b.R_CAL_LEN), len(FAKE_CAL))
        self.assertEqual(br(obj, b.R_CALBF_LEN), len(FAKE_CALBF))
        cal = b''.join(br(obj, b.CAL_OFF + i, 64).to_bytes(8, 'little') for i in range(0, len(FAKE_CAL), 8))
        self.assertEqual(cal[:len(FAKE_CAL)], FAKE_CAL)
        calbf = b''.join(br(obj, b.CALBF_OFF + i, 64).to_bytes(8, 'little') for i in range(0, len(FAKE_CALBF), 8))
        self.assertEqual(calbf[:len(FAKE_CALBF)], FAKE_CALBF)
        board = b''.join(br(obj, b.R_BOARD + i).to_bytes(4, 'little') for i in range(0, 16, 4))
        self.assertEqual(board.rstrip(b'\0'), b'atlantisb')
        self.assertFalse(any(FAKE_BD.hex() in l for l in logs))
        self.assertEqual(hw.writes, [])
        obj2, *_ = make(bt={'bdaddr': FAKE_BD, 'cal': bytes(0x1801), 'calbf': FAKE_CALBF})
        self.assertEqual(br(obj2, b.R_CAL_LEN), 0, 'oversized blob is refused, not truncated')

    def test_hook_never_raises(self):
        obj, hw, smcs, logs = make()

        def boom(*a, **k):
            raise RuntimeError('fake proxy dead')
        obj.bt_reg_read = boom
        self.assertEqual(obj.bt_hook_read(b.BT_CTRL_IPA, 32), 0xFFFFFFFF)
        obj.bt_reg_write = boom
        obj.bt_hook_write(b.BT_CTRL_IPA + b.R_CMD, 1, 32)
        self.assertEqual(obj.bt_state, b.BT_FAILED)

    def test_ssdt_matches_constants(self):
        inc = (HERE / 'acpi' / 'NwoasBtJ274.asl.inc').read_text()
        for base, size in ((b.FN1_BAR0_BUS + b.CPU_OFFSET, b.FN1_BAR0_SIZE),
                           (b.FN1_BAR2_BUS + b.CPU_OFFSET, b.FN1_BAR2_SIZE), (b.BT_CTRL_IPA, b.BT_CTRL_SIZE),
                           (b.BT_RESOURCE_WINDOW[0], b.BT_RESOURCE_WINDOW[1] - b.BT_RESOURCE_WINDOW[0] + 1)):
            self.assertIn('0x%016X, 0x%016X, 0x0,' % (base, base + size - 1), inc)
        self.assertIn('0x%08X' % b.BT_MAGIC, inc)
        self.assertIn('"NWOA5F69"', inc)
        self.assertNotIn('Interrupt', re.sub(r'//.*', '', inc))
        # S259 producer (NWC0) and S261 producer (NWC1) must not overlap
        s259_hi = w.WINDOW_BUS[1] + w.CPU_OFFSET
        self.assertGreater(b.BT_RESOURCE_WINDOW[0], s259_hi)


class FakeHvMaps:
    """m1n1 hv tracer map subset: add/del + pt_update counter (DictRangeMap like the real one)."""
    def __init__(self):
        from m1n1.utils import DictRangeMap
        self.mmio_maps = DictRangeMap()
        self.pt_updates = 0

    def add_tracer(self, zone, ident, mode, read=None, write=None, **kw):
        self.mmio_maps[zone, ident] = (mode, ident, read, write, kw)

    def del_tracer(self, zone, ident):
        del self.mmio_maps[zone, ident]

    def pt_update(self):
        self.pt_updates += 1

    def top(self, addr):
        v = self.mmio_maps[addr]
        return sorted(v.values(), reverse=True)[0][1] if v else None


class Fence(unittest.TestCase):
    def setUp(self):
        self.obj, self.hw, self.smcs, self.logs = make()
        self.hv = FakeHvMaps()
        for b0, n0 in b.BT_BAR_EMUL:
            self.hv.add_tracer(b._irange(b0, n0), 'bt-s261-bar-emul', b._TraceMode().BYPASS)
        self.obj.fence_install(self.hv)
        self.obj.boot_force_off()

    def fenced(self):
        return all(self.hv.top(a) == b.FENCE_ID for a, _ in b.BT_BAR_EMUL)

    def test_boot_fenced_and_arm(self):
        self.assertTrue(self.fenced())
        self.assertEqual(self.hv.pt_updates, 0, 'module time: hv.start maps it')
        th.ctl_write(self.obj, w.R_CMD, w.CMD_POWER_UP)
        self.assertTrue(self.fenced())
        g = br(self.obj, b.R_GEN)
        bw(self.obj, b.R_ARM, g - 1)
        self.assertEqual(br(self.obj, b.R_ARM), b.ARM_STALE)
        self.assertTrue(self.fenced())
        bw(self.obj, b.R_ARM, g)
        self.assertEqual(br(self.obj, b.R_ARM), b.ARM_OK, self.logs[-4:])
        self.assertTrue(all(self.hv.top(a) == 'bt-s261-bar-emul' for a, _ in b.BT_BAR_EMUL))
        self.assertGreater(self.hv.pt_updates, 0)

    def test_power_cycle_refences_before_power_off(self):
        th.ctl_write(self.obj, w.R_CMD, w.CMD_POWER_UP)
        bw(self.obj, b.R_ARM, br(self.obj, b.R_GEN))
        g = br(self.obj, b.R_GEN)
        th.ctl_write(self.obj, w.R_CMD, w.CMD_POWER_DOWN)          # WLN0._PS3 (the 2026-11-18 18:2x case)
        self.assertTrue(self.fenced())
        self.assertGreater(br(self.obj, b.R_GEN), g)
        i_f = next(i for i, l in enumerate(self.logs) if 'fence ON (chip power down' in l)
        i_off = next(i for i, l in enumerate(self.logs) if 'power off (CMD_POWER_DOWN)' in l)
        self.assertLess(i_f, i_off, 'fence must close before the chip loses power')
        bw(self.obj, b.R_ARM, g)                                      # stale driver arm: refused
        self.assertNotEqual(br(self.obj, b.R_ARM), b.ARM_OK)
        self.assertEqual(self.obj.fence_read(b.BT_BAR_EMUL[1][0] + 0x20045C, 32), 0xFFFFFFFF)

    def test_wifi_chip_reset_bracket_fences(self):
        th.ctl_write(self.obj, w.R_CMD, w.CMD_POWER_UP)
        bw(self.obj, b.R_ARM, br(self.obj, b.R_GEN))
        self.assertFalse(self.fenced())
        g = br(self.obj, b.R_GEN)
        th.ctl_write(self.obj, w.R_CFG_OFF, wu.EP_LINKCTL_CFG)       # Wi-Fi driver LinkCtl write
        th.ctl_write(self.obj, w.R_CFG_DATA, 0)
        self.assertTrue(self.fenced())
        self.assertGreater(br(self.obj, b.R_GEN), g)

    def test_quiet_fences_and_arm_restores_bars(self):
        th.ctl_write(self.obj, w.R_CMD, w.CMD_POWER_UP)
        bw(self.obj, b.R_CMD, b.BT_CMD_QUIET)
        self.assertTrue(self.fenced())
        bw(self.obj, b.R_CMD, b.BT_CMD_ENSURE)
        for k in (0x10, 0x14, 0x18, 0x1C):                             # a chip reset cleared fn1
            self.hw.fn[1]['cfg'][k] = self.hw.bar_orig[1][k]
        self.hw.fn[1]['cfg'][0x04] = 0x00100000
        bw(self.obj, b.R_ARM, br(self.obj, b.R_GEN))
        self.assertEqual(br(self.obj, b.R_ARM), b.ARM_OK, self.logs[-4:])
        self.assertEqual(c1(self.hw, 0x18) & ~0xF, b.FN1_BAR2_BUS)
        self.assertEqual(c1(self.hw, 0x04) & 0x407, b.FN1_CMD_QUIET)
        self.assertFalse(self.fenced())


ADT_DUMP = ROOT / 'experiments' / 'tahoe-afk-20260906' / 'current.adt'


@unittest.skipUnless(ADT_DUMP.is_file(), 'offline ADT dump missing')
class Module(unittest.TestCase):
    def run_module(self, env=None):
        import os
        import runpy
        import types
        sys.path.insert(0, str(ROOT / 'm1n1_windows' / 'proxyclient'))
        from m1n1.adt import load_adt
        from m1n1.utils import DictRangeMap, irange
        from m1n1.hv.types import TraceMode
        adt = load_adt(ADT_DUMP.read_bytes())
        hw = FakeHW3(tp.Clock())
        th.uefi_state(hw)

        class HV:
            def __init__(s):
                s.mmio_maps = DictRangeMap()

            def add_tracer(s, zone, ident, mode, read=None, write=None, **kw):
                s.mmio_maps[zone, ident] = (mode, ident, read, write, kw)

            def del_tracer(s, zone, ident):
                del s.mmio_maps[zone, ident]

            def pt_update(s):
                pass
        hv = HV()
        for r in adt['/arm-io'].ranges:
            hv.add_tracer(irange(r.parent_addr, r.size), 'HW', TraceMode.OFF)
        u = types.SimpleNamespace(adt=adt, ba=types.SimpleNamespace(phys_base=0x83BED8000, mem_size_actual=0x3C0000000))
        old = (w.wp.RealSmc, b.make_run_dir, dict(os.environ))
        w.wp.RealSmc = lambda uu, base: tp.FakeSmc(hw)
        b.make_run_dir = lambda: None
        os.environ.update(env or {})
        out = []
        import builtins
        real_print = builtins.print
        builtins.print = lambda *a, **k: out.append(' '.join(str(x) for x in a))
        try:
            runpy.run_path(str(HERE / 'bt_module.py'), init_globals={'hv': hv, 'u': u, 'p': hw},
                           run_name='<hv_script>')
        finally:
            builtins.print = real_print
            w.wp.RealSmc, b.make_run_dir = old[0], old[1]
            os.environ.clear()
            os.environ.update(old[2])
        return hv, hw, irange, TraceMode, out

    def top(self, hv, addr):
        return sorted(hv.mmio_maps[addr].values(), reverse=True)[0]

    def test_tracers(self):
        hv, hw, irange, TraceMode, out = self.run_module()
        for base, size in [w.BAR_EMUL]:
            for addr in (base, base + size - 4):
                self.assertEqual(self.top(hv, addr)[0], TraceMode.BYPASS, hex(addr))
        for base, size in b.BT_BAR_EMUL:                              # fenced at boot, BYPASS underneath
            for addr in (base, base + size - 4):
                self.assertEqual(self.top(hv, addr)[:2], (TraceMode.HOOK, b.FENCE_ID), hex(addr))
                self.assertIn('bt-s261-bar-emul', hv.mmio_maps[addr])
        self.assertEqual(self.top(hv, w.BAR_EMUL[0])[1], 'wifi-s259-bar-emul')
        # the hole between fn1 BAR0 and BAR2 (no BAR decodes there) and the FL1100 BAR stay untouched
        self.assertEqual(self.top(hv, b.BT_BAR_EMUL[0][0] + b.FN1_BAR0_SIZE)[0], TraceMode.OFF)
        self.assertEqual(self.top(hv, 0x6C0000000)[0], TraceMode.OFF)
        self.assertEqual(self.top(hv, w.CTRL_IPA)[:2], (TraceMode.HOOK, 'wifi-s259-ctl'))
        self.assertEqual(self.top(hv, b.BT_CTRL_IPA)[:2], (TraceMode.HOOK, 'bt-s261-ctl'))
        self.assertIs(hv._nwoas_bt_s261, hv._nwoas_wifi_s259)
        self.assertTrue(any('BOOT ADT bluetooth cal=5664 calbf=5664 bdaddr=present' in l for l in out), out)
        # the real ADT values reach the BT page (contents not printed)
        obj = hv._nwoas_bt_s261
        self.assertEqual(br(obj, b.R_CALBF_LEN), 5664)
        self.assertNotEqual(br(obj, b.R_BDADDR_LO) | br(obj, b.R_BDADDR_HI), 0)

    def test_bt_off_env(self):
        hv, hw, irange, TraceMode, out = self.run_module({'NWOAS_BT': '0'})
        obj = hv._nwoas_bt_s261
        self.assertFalse(obj.bt_enabled)
        self.assertEqual(self.top(hv, b.BT_CTRL_IPA)[:2], (TraceMode.HOOK, 'bt-s261-ctl'))


if __name__ == '__main__':
    unittest.main(verbosity=1)
