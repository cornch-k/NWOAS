# SPDX-License-Identifier: MIT
"""NWOAS S261: BCM4378 Bluetooth (PCIe 1:0.1, fn1 of the Wi-Fi combo chip) handed to Windows as
ACPI\\NWOA5F69, on top of the S259 Wi-Fi bring-up (nwoas_scripts/wifi-s259, unchanged, imported).

Contract: research-20261118/bt/CONTRACT-BT.md.  Test plan: research-20261118/bt/TESTPLAN-BT.md.

What S261 adds to S259 (everything else is the S259 code path, byte for byte):
  * after S259 _bring_up() succeeded for fn0 (power, link, fn0 BARs, DevCtl, DART check, bus master),
    _bt_setup(): fn1 BAR0 (32 KiB) at bus 0xE0900000, fn1 BAR2 (4 MiB) at bus 0xE0C00000, root port 0
    memory window widened from 0xE0000000-0xE08FFFFF to 0xE0000000-0xE0FFFFFF (still inside the 16 MiB
    the S259 UEFI already keeps out of guest RAM), fn1 DevCtl NoSnoop/RO off, fn1 command MEM|INTx-off.
    fn1 bus master stays OFF; the Windows driver turns it on through the BT control page.
    A fn1 failure never fails Wi-Fi: fn1 decode is switched off again and the window narrowed back.
  * a second control page (BT_CTRL_IPA, Python HOOK) for \\_SB.NWC1.BTH0: state, fn1 config access with
    its own allow table, function level reset (FLR) on request, the ADT /arm-io/bluetooth data
    (BD address, both Taurus calibration blobs) the driver cannot get on Windows.
  * the BAR emulation (BYPASS) ranges for the fn1 BARs are installed by bt_module.py.

The chip power stays owned by Wi-Fi (WLN0 _INI/_PS0/_PS3).  BTH0._INI / _PS0 write BT CMD 1
("make sure chip + fn1 are up"), BTH0._PS3 writes BT CMD 2 ("fn1 quiet": bus master off).
Built and tested offline only (test_bt_hv.py).  Not a boot, not a Bluetooth result.
"""
import datetime
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
_S259 = HERE.parent / 'wifi-s259'
if str(_S259) not in sys.path:
    sys.path.insert(0, str(_S259))
import nwoas_wifi_s259 as s259  # noqa: E402  (S259, hardware: Wi-Fi connected 2026-11-18 16:2x)

wp, wu, ws = s259.wp, s259.wu, s259.ws
Fail = s259.Fail
TAG_BT = '[s261]'

# ---------------------------------------------------------------- contract constants
# CONTRACT-BT.md section 1/2.  fw-bt-s261/build.py and test_bt_hv.py refuse a mismatch with the SSDT.
BT_CTRL_IPA, BT_CTRL_SIZE = 0x348004000, 0x4000         # right after the S259 Wi-Fi page
BT_ID = wp.BT_ID                                         # 0x5F6914E4 (S240/S257 hardware PASS_IDS)
FN1_BAR0_BUS, FN1_BAR0_SIZE = 0xE0900000, 0x8000         # S257 hardware sizing: fn1 BAR0 0x8000 mem64
FN1_BAR2_BUS, FN1_BAR2_SIZE = 0xE0C00000, 0x400000       # S257 hardware sizing: fn1 BAR2 0x400000 mem64
WINDOW_S261 = (0xE0000000, 0xE0FFFFFF)                   # root port 0 window with fn1 (S259: ..0xE08FFFFF)
CPU_OFFSET = s259.CPU_OFFSET
BT_BAR_EMUL = [(FN1_BAR0_BUS + CPU_OFFSET, FN1_BAR0_SIZE), (FN1_BAR2_BUS + CPU_OFFSET, FN1_BAR2_SIZE)]
BT_RESOURCE_WINDOW = (FN1_BAR0_BUS + CPU_OFFSET, WINDOW_S261[1] + CPU_OFFSET)   # NWC1 producer range


def rp_window_value(lo, hi):
    """Type-1 header 0x20 (memory base/limit) for a 1 MiB granular window."""
    return ((hi >> 16) & 0xFFF0) << 16 | ((lo >> 16) & 0xFFF0)


RP_WIN_S259 = rp_window_value(*s259.WINDOW_BUS)          # 0xE080E000 (S257 hardware readback)
RP_WIN_S261 = rp_window_value(*WINDOW_S261)              # 0xE0F0E000

BT_MAGIC, BT_VERSION = 0x5442574E, 1                     # 'NWBT'
R_MAGIC, R_VERSION, R_STATE, R_ERROR, R_CMD, R_SEQ, R_AVAIL, R_IDS = 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C
R_CFG_OFF, R_CFG_DATA, R_CFG_ERR = 0x20, 0x24, 0x28
R_BAR0, R_BAR2, R_CHIP_STATE, R_DEVCTL, R_EPCMD = 0x2C, 0x30, 0x34, 0x38, 0x3C
R_RESET, R_RESET_COUNT, R_EPOCH, R_RID2SID, R_CMD_COUNT = 0x40, 0x44, 0x48, 0x4C, 0x50
R_WINDOW, R_DEVCAP, R_MSICAP = 0x54, 0x58, 0x5C
R_GEN, R_ARM = 0x80, 0x84                                # S261b fence: generation, arm (write = generation)
ARM_NONE, ARM_OK, ARM_STALE, ARM_NOT_UP, ARM_BARS = 0, 1, 2, 3, 4
FENCE_ID = 'bt-s261-bar-fence'
R_BDADDR_LO, R_BDADDR_HI, R_CAL_LEN, R_CALBF_LEN, R_BOARD, BOARD_LEN = 0x60, 0x64, 0x68, 0x6C, 0x70, 16
CAL_OFF, CALBF_OFF, CAL_MAX = 0x1000, 0x2800, 0x1800     # bluetooth-taurus-calibration / -bf (5664 B on J274)

BT_OFF, BT_UP, BT_QUIET, BT_FAILED = 0, 1, 2, 3
BT_CMD_ENSURE, BT_CMD_QUIET = 1, 2
RESET_NONE, RESET_OK, RESET_NO_FLR, RESET_MISMATCH, RESET_NOT_UP, RESET_EXCEPTION = 0, 1, 2, 3, 4, 5
CFG_OK, CFG_NOT_UP, CFG_NOT_ALLOWED, CFG_BAD_OFFSET, CFG_EXCEPTION = 0, 1, 2, 3, 4

BT_ERRORS = {0: 'NONE', 1: 'DISABLED', 2: 'CHIP_DOWN', 3: 'NO_FN1', 4: 'FN1_BARS', 5: 'FN1_LAYOUT',
             6: 'WINDOW', 7: 'DEVCTL', 8: 'CMD', 9: 'PORT1_CHANGED', 10: 'ERROR', 11: 'WIFI_DISABLED',
             12: 'FN1_ASSIGNED'}
BT_ERR = {v: k for k, v in BT_ERRORS.items()}

# fn1 vendor config registers the Linux hci_bcm4377 driver writes (facts from that driver):
#   0x80 BAR0 window 1, 0x70 BAR0 window 2, 0x74/0x78 BAR0 second-core windows, 0x84 BAR2 window,
#   0x88 subsystem control (bit 16 set; bit 19 cleared on 4387 only).
FN1_WINDOW_REGS = (0x70, 0x74, 0x78, 0x80, 0x84)
FN1_SUBSYS_CTRL, SUBSYS_BITS = 0x88, (1 << 16) | (1 << 19)
DEVCTL_FLR, DEVCAP_FLR = 1 << 15, 1 << 28
MSI_ENABLE, MSI_MME, MSI_64BIT = 1 << 16, 0x7 << 20, 1 << 23
MSI_ADDR_MIN, MSI_ADDR_MAX = 0x00100000, 0xBFF00000      # dart-apcie0 ADT window (vm-base, vm-size)
FN1_CMD_QUIET = wu.CMD_MEM | wu.CMD_INTX_DIS             # 0x0402: decode, no DMA, no INTx
FLR_WAIT_S = 0.1                                         # PCIe: 100 ms after Initiate FLR


def _irange(b, n):
    from m1n1.utils import irange
    return irange(b, n)


def _TraceMode():
    from m1n1.hv.types import TraceMode
    return TraceMode


def make_run_dir():
    out = HERE / 'runs'
    out.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    return Path(tempfile.mkdtemp(prefix=f'run-{stamp}-', dir=str(out)))


def ecfg1(f, off):
    return wp.ecam_addr(f['ecam'], 1, 0, 1, off)


def bt_cfg_write_policy(off, val, cur, caps):
    """Value written to fn1 config `off` for a driver request, or None (refused).
    caps = {'pcie': off, 'msi': off or 0, 'msi64': bool}; cur = current dword at off."""
    pcie, msi = caps.get('pcie', 0), caps.get('msi', 0)
    if off in FN1_WINDOW_REGS:
        return val if (val & 0xFFF) == 0 and 0x18000000 <= val <= 0x1FFFF000 else None
    if off == FN1_SUBSYS_CTRL:
        return (cur & ~SUBSYS_BITS) | (val & SUBSYS_BITS)
    if off == 0x04:
        return (val & (wu.CMD_BME | wu.CMD_INTX_DIS)) | wu.CMD_MEM      # status half written as 0
    if pcie and off == pcie + 8:                                        # DevCtl: no NS/RO, no FLR bit
        return val & 0xFFFF & ~s259.DEVCTL_NS_RO & ~DEVCTL_FLR
    if pcie and off == pcie + 0x10:                                     # LinkCtl: ASPM bits only
        return (cur & 0xFFFF & ~wu.ASPM_MASK) | (val & wu.ASPM_MASK)
    if msi:
        data_off = msi + (0xC if caps.get('msi64') else 0x8)
        if off == msi:                                                  # enable bit only, one vector
            return (cur & ~(MSI_ENABLE | MSI_MME)) | (val & MSI_ENABLE)
        if off == msi + 4:
            return val if (val & 3) == 0 and MSI_ADDR_MIN <= val < MSI_ADDR_MAX else None
        if caps.get('msi64') and off == msi + 8:
            return val if val == 0 else None
        if off == data_off:
            return val & 0xFFFF
    return None


def bt_info(adt):
    """/arm-io/bluetooth: BD address (never logged) and both Taurus calibration blobs."""
    n = adt['/arm-io/bluetooth']
    addr = n.getprop('local-mac-address')
    addr = bytes(addr) if addr is not None and len(bytes(addr)) == 6 else None
    cal = n.getprop('bluetooth-taurus-calibration')
    calbf = n.getprop('bluetooth-taurus-calibration-bf')
    return {'bdaddr': addr, 'cal': bytes(cal) if cal is not None else b'',
            'calbf': bytes(calbf) if calbf is not None else b''}


class WifiBtS261(s259.WifiS259):
    def __init__(self, p, facts, smc_factory, *, bt_enabled=True, bt=None, **kw):
        super().__init__(p, facts, smc_factory, **kw)
        self.bt_enabled = bt_enabled and self.enabled
        self.bt_state, self.bt_error, self.bt_avail, self.bt_seq = BT_OFF, 0, 0, 0
        self.bt_ids = self.bt_devctl = self.bt_epcmd = self.bt_devcap = 0
        self.bt_window = 0
        self.bt_rid2sid = 0
        self.bt_caps = {}
        self.bt_cfg_off, self.bt_cfg_err = 0, CFG_OK
        self.bt_cfg_reads = self.bt_cfg_writes = self.bt_cfg_refused = 0
        self.bt_reset_result, self.bt_reset_count = RESET_NONE, 0
        self.bt_cmd_count, self.bt_last_cmd = 0, 0
        self.epoch = 0
        # S261b fence (2026-11-18 hardware: a chip power cycle under a running driver made a plain BAR2 read
        # raise a synchronous abort that the guest re-executed thousands of times -> Windows hung).  The fn1
        # BARs are covered by a HOOK that answers all-ones and drops writes whenever the chip may not decode
        # them (boot, power down/up, Wi-Fi chip-reset bracket, fn1 quiet).  Only the driver lifts it, with
        # ARM = the current generation, after the hypervisor re-checked the fn1 BARs.  Every fence bumps GEN.
        self.hv = None
        self.bt_gen, self.bt_fenced, self.bt_arm_result = 1, False, ARM_NONE
        self.fence_reads = self.fence_writes = 0
        self.fence_on_wifi_reset = True
        self.bt_regs = self._bt_regs(bt or {}, kw.get('wlan') or {})
        if not self.enabled:
            self.bt_state, self.bt_error = BT_FAILED, BT_ERR['WIFI_DISABLED']
        elif not bt_enabled:
            self.bt_state, self.bt_error = BT_FAILED, BT_ERR['DISABLED']

    @staticmethod
    def _bt_regs(info, wlan):
        a = info.get('bdaddr') or b''
        a = a if len(a) == 6 else bytes(6)
        regs = {R_BDADDR_LO: int.from_bytes(a[:4], 'little'), R_BDADDR_HI: int.from_bytes(a[4:], 'little')}
        board = (str(wlan.get('board') or '').encode('ascii', 'replace') + bytes(BOARD_LEN))[:BOARD_LEN]
        for i in range(0, BOARD_LEN, 4):
            regs[R_BOARD + i] = int.from_bytes(board[i:i + 4], 'little')
        for key, roff, base in (('cal', R_CAL_LEN, CAL_OFF), ('calbf', R_CALBF_LEN, CALBF_OFF)):
            blob = bytes(info.get(key) or b'')
            if len(blob) > CAL_MAX:
                blob = b''                                   # refuse a truncated blob (driver sees length 0)
            regs[roff] = len(blob)
            p = blob + bytes(-len(blob) % 4)
            for i in range(0, len(p), 4):
                regs[base + i] = int.from_bytes(p[i:i + 4], 'little')
        return regs

    def blog(self, msg):
        self.lines += 1
        if self.lines <= s259.LOG_BUDGET or msg.startswith(('READY', 'FAIL', 'OFF', 'BOOT')):
            self._out(f'{TAG_BT} {msg}'[:300])

    # ------------------------------------------------------------ chip bring-up = S259 + fn1
    # ------------------------------------------------------------ fence (S261b)
    def fence_install(self, hv, fence_on_wifi_reset=True):
        """Module time (before the guest runs): the fence starts closed; hv.start() maps it."""
        self.hv, self.fence_on_wifi_reset = hv, fence_on_wifi_reset
        self._fence(True, 'boot', now=False)

    def _fence(self, on, why, now=True):
        if self.hv is None:
            return
        try:
            if on and not self.bt_fenced:
                for b, n in BT_BAR_EMUL:
                    self.hv.add_tracer(_irange(b, n),
                                       FENCE_ID, _TraceMode().HOOK, read=self.fence_read, write=self.fence_write)
                self.bt_fenced = True
            elif not on and self.bt_fenced:
                for b, n in BT_BAR_EMUL:
                    self.hv.del_tracer(_irange(b, n), FENCE_ID)
                self.bt_fenced = False
            if on:
                self.bt_gen += 1
                self.bt_arm_result = ARM_NONE
            if now:
                self.hv.pt_update()                 # map it before this trapped access returns (tlbi inside)
            self.blog(f'fence {"ON" if on else "OFF"} ({why}) gen={self.bt_gen}')
        except Exception as e:
            self.blog(f'fence {"ON" if on else "OFF"} ({why}) raised {type(e).__name__}: {e}')

    def fence_read(self, addr, width):
        self.fence_reads += 1
        if self.fence_reads <= 8 or self.fence_reads % 1000 == 0:
            self.blog(f'fence read {addr:#x}/{width} -> all-ones (count {self.fence_reads}, gen {self.bt_gen})')
        return (1 << width) - 1 if width < 64 else 0xFFFFFFFFFFFFFFFF

    def fence_write(self, addr, value, width):
        self.fence_writes += 1
        if self.fence_writes <= 8 or self.fence_writes % 1000 == 0:
            self.blog(f'fence write {addr:#x}/{width} dropped (count {self.fence_writes}, gen {self.bt_gen})')

    def bt_arm(self, val):
        """Driver: lift the fence for this generation.  fn1 BARs/decode are re-checked (and rewritten if a
        chip reset cleared them) first; nothing is unfenced unless fn1 is UP and the generation matches."""
        if self.bt_state != BT_UP or self.up is None:
            self.bt_arm_result = ARM_NOT_UP
        elif val != self.bt_gen:
            self.bt_arm_result = ARM_STALE
        else:
            f, m = self.f, self.up.m
            try:
                with self.up.guarded() as g:
                    rb = (m.r32(ecfg1(f, 0x10)) & ~0xF, m.r32(ecfg1(f, 0x18)) & ~0xF, m.r32(ecfg1(f, 0x04)) & 0x2)
                    if rb != (FN1_BAR0_BUS, FN1_BAR2_BUS, 0x2):
                        m.w32(ecfg1(f, 0x10), FN1_BAR0_BUS)
                        m.w32(ecfg1(f, 0x14), 0)
                        m.w32(ecfg1(f, 0x18), FN1_BAR2_BUS)
                        m.w32(ecfg1(f, 0x1C), 0)
                        m.w32(ecfg1(f, 0x04), FN1_CMD_QUIET)
                        self.bt_epcmd = FN1_CMD_QUIET
                    rb2 = (m.r32(ecfg1(f, 0x10)) & ~0xF, m.r32(ecfg1(f, 0x18)) & ~0xF, m.r32(ecfg1(f, 0x04)) & 0x2)
                ok = rb2 == (FN1_BAR0_BUS, FN1_BAR2_BUS, 0x2) and not g['exc']
                self.blog(f'arm gen={val} fn1 before={tuple(hex(x) for x in rb)} after={tuple(hex(x) for x in rb2)} ok={ok}')
            except Exception as e:
                ok = False
                self.blog(f'arm raised {type(e).__name__}: {e}')
            if ok:
                self._fence(False, f'arm gen {val}')
                self.bt_arm_result = ARM_OK
            else:
                self.bt_arm_result = ARM_BARS
        self.blog(f'ARM {val} -> {self.bt_arm_result} (fn1 state {self.bt_state}, gen {self.bt_gen})')

    def _cfg_write(self, off, val):
        # Wi-Fi driver: LinkCtl (fn0 0xBC) is written right before and after its chip reset (DRIVER-SIDE.md).
        if self.fence_on_wifi_reset and off == wu.EP_LINKCTL_CFG and self.bt_enabled:
            self._fence(True, 'Wi-Fi chip reset bracket (fn0 0xBC write)')
        return super()._cfg_write(off, val)

    def _bring_up(self, up):
        self._fence(True, 'chip power up')
        self.bt_state = BT_OFF if self.bt_enabled else self.bt_state
        super()._bring_up(up)                               # S259: raises Fail -> Wi-Fi FAILED, chip off
        self.epoch += 1
        if self.bt_enabled:
            self._bt_setup_guarded(up)

    def _bt_setup_guarded(self, up):
        res = {}
        up.res['bt'] = res
        try:
            self._bt_setup(up, res)
            self.bt_state, self.bt_error, self.bt_avail = BT_UP, 0, 1
            self.blog(f'READY fn1 ids={self.bt_ids:#x} bar0={FN1_BAR0_BUS + CPU_OFFSET:#x} '
                      f'bar2={FN1_BAR2_BUS + CPU_OFFSET:#x} window={self.bt_window:#x} '
                      f'epcmd={self.bt_epcmd:#x} devctl={self.bt_devctl:#x} flr_cap={int(bool(self.bt_devcap & DEVCAP_FLR))} '
                      f'msi_cap={self.bt_caps.get("msi", 0):#x} rid2sid_valid={self.bt_rid2sid:#x} epoch={self.epoch}')
        except Fail as e:
            self.bt_state, self.bt_error = BT_FAILED, e.code
            res['failure'] = e.text
            self.blog(f'FAIL fn1 {BT_ERRORS.get(e.code)}: {e.text}')
            self._bt_undo(up, res)
        except Exception as e:
            self.bt_state, self.bt_error = BT_FAILED, BT_ERR['ERROR']
            res['failure'] = f'{type(e).__name__}: {e}'
            self.blog(f'FAIL fn1 ERROR: {type(e).__name__}: {e}')
            self._bt_undo(up, res)

    def _walk_caps(self, up, fn_cfg):
        caps, p, guard = {}, up.m.r32(fn_cfg(0x34)) & 0xFC, 0
        while 0x40 <= p < 0x100 and guard < 48:
            h = up.m.r32(fn_cfg(p))
            cid = h & 0xFF
            if cid == 0x10 and 'pcie' not in caps:
                caps['pcie'] = p
            elif cid == 0x05 and 'msi' not in caps:
                caps['msi'], caps['msi64'] = p, bool(h & MSI_64BIT)
            p, guard = (h >> 8) & 0xFC, guard + 1
        return caps

    def _bt_setup(self, up, res):
        f, m = self.f, up.m
        fn = (up.res.get('functions') or {}).get(1) or {}
        ids = int(fn.get('id', '0'), 16) if isinstance(fn, dict) else 0
        self.bt_ids = ids
        if ids != BT_ID:
            raise Fail(BT_ERR['NO_FN1'], f'fn1 id {ids:#x} (want {BT_ID:#x})')
        bars = {b['bar']: b for b in fn.get('bars', []) if isinstance(b, dict)}
        try:
            s0, s2 = int(bars[0]['size'], 16), int(bars[2]['size'], 16)
        except (KeyError, ValueError, TypeError):
            raise Fail(BT_ERR['FN1_LAYOUT'], f'fn1 BAR sizes not measured: {bars}')
        if (s0, s2) != (FN1_BAR0_SIZE, FN1_BAR2_SIZE) or bars[0]['type'] != 'mem64' or bars[2]['type'] != 'mem64':
            raise Fail(BT_ERR['FN1_LAYOUT'], f'fn1 BAR0 {s0:#x}/{bars[0]["type"]} BAR2 {s2:#x}/{bars[2]["type"]} '
                                             f'differ from the contract ({FN1_BAR0_SIZE:#x}/{FN1_BAR2_SIZE:#x} mem64)')

        def c1(o):
            return ecfg1(f, o)
        for o in (0x04, FN1_SUBSYS_CTRL) + FN1_WINDOW_REGS:   # exact fn1 addresses the driver may write
            m.allow(c1(o))
        m.cmd_regs = frozenset(set(m.cmd_regs) | {c1(0x04)})
        with up.checked('fn1 config save'):
            saved = {o: m.r32(c1(o)) for o in (0x04, 0x10, 0x14, 0x18, 0x1C)}
            caps = self._walk_caps(up, c1)
        res['saved'] = {hex(o): hex(v) for o, v in saved.items()}
        res['caps'] = {k: (hex(v) if isinstance(v, int) and not isinstance(v, bool) else v) for k, v in caps.items()}
        if saved[0x04] & 0x7 or saved[0x10] & ~0xF or saved[0x14] or saved[0x18] & ~0xF or saved[0x1C]:
            raise Fail(BT_ERR['FN1_ASSIGNED'], f'fn1 already decoding/assigned (cmd={saved[0x04]:#x} '
                                               f'bar0={saved[0x10]:#x} bar2={saved[0x18]:#x})')
        if not 0x40 <= caps.get('pcie', 0) <= 0xF0:
            raise Fail(BT_ERR['DEVCTL'], f'fn1 has no PCIe capability ({caps})')
        self.bt_caps = caps
        pcie = caps['pcie']
        for o in (pcie + 8, pcie + 0x10):
            m.allow(c1(o))
        if caps.get('msi'):
            for o in range(caps['msi'], caps['msi'] + 0x10, 4):
                m.allow(c1(o))
        up.bt_touched = True
        with up.checked('fn1 BARs'):
            m.w32(c1(0x10), FN1_BAR0_BUS)
            m.w32(c1(0x14), 0)
            m.w32(c1(0x18), FN1_BAR2_BUS)
            m.w32(c1(0x1C), 0)
            rb = {o: m.r32(c1(o)) for o in (0x10, 0x14, 0x18, 0x1C)}
        bad = [f'fn1 {o:#x}: want {w:#x} got {g:#x}' for o, g, w in
               ((0x10, rb[0x10] & ~0xF, FN1_BAR0_BUS), (0x14, rb[0x14], 0),
                (0x18, rb[0x18] & ~0xF, FN1_BAR2_BUS), (0x1C, rb[0x1C], 0)) if g != w]
        res['bars_readback'] = {hex(o): hex(v) for o, v in rb.items()}
        if bad:
            raise Fail(BT_ERR['FN1_BARS'], '; '.join(bad))
        with up.checked('root port window'):
            m.w32(up.rcfg(0x20), RP_WIN_S261)
            self.bt_window = m.r32(up.rcfg(0x20))
        res['window'] = hex(self.bt_window)
        if self.bt_window != RP_WIN_S261:
            raise Fail(BT_ERR['WINDOW'], f'root port 0x20 want {RP_WIN_S261:#x} got {self.bt_window:#x}')
        with up.checked('fn1 DevCtl'):
            dc = c1(pcie + 8)
            old = m.r32(dc) & 0xFFFF
            m.w32(dc, old & ~s259.DEVCTL_NS_RO & ~DEVCTL_FLR)
            new = m.r32(dc) & 0xFFFF
            self.bt_devcap = m.r32(c1(pcie + 4))
        self.bt_devctl = new
        res['devctl'] = {'before': hex(old), 'after': hex(new), 'devcap': hex(self.bt_devcap)}
        if new & s259.DEVCTL_NS_RO:
            raise Fail(BT_ERR['DEVCTL'], f'fn1 NoSnoop/RO still set ({new:#x})')
        with up.checked('fn1 command'):
            m.w32(c1(0x04), FN1_CMD_QUIET)
            cmd = m.r32(c1(0x04)) & 0xFFFF
        self.bt_epcmd = cmd
        res['epcmd'] = hex(cmd)
        if (cmd & 0x407) != FN1_CMD_QUIET:
            raise Fail(BT_ERR['CMD'], f'fn1 command read-back {cmd:#x} (want {FN1_CMD_QUIET:#x})')
        with up.guarded():
            r2s = [m.r32(f['port0'] + ws.PORT_RID2SID + 4 * i) for i in range(16)]   # read only
        self.bt_rid2sid = sum(1 << i for i, v in enumerate(r2s) if v & ws.RID2SID_VALID)
        res['port0_rid2sid_0_15'] = [hex(x) for x in r2s]
        with self.phase('s261-port1', 10):
            after = up.port1_snapshot()
        if after != up.res.get('port1_before'):
            raise Fail(BT_ERR['PORT1_CHANGED'], 'FL1100 port-1 registers changed during fn1 setup')

    def _bt_undo(self, up, res):
        """fn1 decode off, BARs cleared, window back to the S259 value.  Every step on its own."""
        if not getattr(up, 'bt_touched', False):
            return
        f, m = self.f, up.m
        steps = {}
        for name, addr, val in (('fn1 cmd off', ecfg1(f, 0x04), 0), ('fn1 bar0', ecfg1(f, 0x10), 0),
                                ('fn1 bar2', ecfg1(f, 0x18), 0), ('window', up.rcfg(0x20), RP_WIN_S259)):
            try:
                with up.guarded() as g:
                    m.w32(addr, val)
                steps[name] = 'ok' if not g['exc'] else f'exc {g["exc"]}'
            except Exception as e:
                steps[name] = f'{type(e).__name__}: {e}'
        res['undo'] = steps
        self.bt_window = RP_WIN_S259
        self.blog(f'fn1 undo {steps}')

    def power_down(self, why='CMD_POWER_DOWN'):
        self._fence(True, f'chip power down ({why})')     # before the chip goes away
        super().power_down(why)
        if self.bt_enabled and self.state != s259.STATE_UP:
            self.bt_state = BT_OFF
            self.blog(f'OFF fn1 (chip power down: {why})')

    def _fail_cleanup(self, up, text):
        self._fence(True, 'bring-up failed')
        super()._fail_cleanup(up, text)
        if self.bt_enabled:
            self.bt_state = BT_OFF

    # ------------------------------------------------------------ BT commands (ACPI) / FLR (driver)
    def bt_ensure(self):
        if not self.bt_enabled:
            return
        if self.state in (s259.STATE_OFF, s259.STATE_OFF_REQ):
            self.blog(f'ensure: chip state {self.state} -> S259 power up')
            self.power_up()
            if self.state != s259.STATE_UP:
                self.bt_state, self.bt_error = BT_FAILED, BT_ERR['CHIP_DOWN']
            return
        if self.state != s259.STATE_UP or self.up is None:
            self.bt_state, self.bt_error = BT_FAILED, BT_ERR['CHIP_DOWN']
            self.blog(f'ensure: chip failed (Wi-Fi state {self.state}, error {s259.ERRORS.get(self.error)})')
            return
        if self.bt_state == BT_UP:
            self.blog('ensure: fn1 already up (no change)')
        elif self.bt_state == BT_QUIET:
            self.bt_state = BT_UP
            self.blog('ensure: fn1 quiet -> up (bus master stays off until the driver sets it)')
        else:
            self._bt_setup_guarded(self.up)

    def bt_quiet(self):
        self._fence(True, 'fn1 quiet (_PS3)')
        if self.bt_state != BT_UP or self.up is None:
            self.blog(f'quiet: fn1 state {self.bt_state}, nothing to do')
            return
        f, m = self.f, self.up.m
        try:
            with self.up.guarded() as g:
                if self.bt_caps.get('msi'):
                    a = ecfg1(f, self.bt_caps['msi'])
                    m.w32(a, m.r32(a) & ~(MSI_ENABLE | MSI_MME))
                m.w32(ecfg1(f, 0x04), FN1_CMD_QUIET)
                self.bt_epcmd = m.r32(ecfg1(f, 0x04)) & 0xFFFF
            self.bt_state = BT_QUIET
            self.blog(f'OFF fn1 quiet (bus master off) epcmd={self.bt_epcmd:#x} exc={g["exc"]}')
        except Exception as e:
            self.bt_state, self.bt_error = BT_FAILED, BT_ERR['ERROR']
            self.blog(f'FAIL fn1 quiet {type(e).__name__}: {e}')

    def bt_reset(self):
        """Function level reset of fn1 (Linux probe does pci_reset_function first).  Bus master off,
        Initiate FLR, 100 ms, restore BARs/DevCtl, command = MEM|INTx-off.  Driver re-enables BME."""
        self.bt_reset_count += 1
        if self.bt_state != BT_UP or self.up is None:
            self.bt_reset_result = RESET_NOT_UP
            return
        if not self.bt_devcap & DEVCAP_FLR:
            self.bt_reset_result = RESET_NO_FLR
            self.blog(f'reset: fn1 DevCap {self.bt_devcap:#x} has no FLR')
            return
        f, up = self.f, self.up
        m, pcie = up.m, self.bt_caps['pcie']
        try:
            with up.checked('fn1 FLR'):
                m.w32(ecfg1(f, 0x04), FN1_CMD_QUIET)
                self.sleep(0.01)
                dc = ecfg1(f, pcie + 8)
                cur = m.r32(dc) & 0xFFFF
                m.w32(dc, (cur & ~s259.DEVCTL_NS_RO) | DEVCTL_FLR)
                self.sleep(FLR_WAIT_S)
                m.w32(ecfg1(f, 0x10), FN1_BAR0_BUS)
                m.w32(ecfg1(f, 0x14), 0)
                m.w32(ecfg1(f, 0x18), FN1_BAR2_BUS)
                m.w32(ecfg1(f, 0x1C), 0)
                m.w32(dc, cur & ~s259.DEVCTL_NS_RO & ~DEVCTL_FLR)
                m.w32(ecfg1(f, 0x04), FN1_CMD_QUIET)
                rb = (m.r32(ecfg1(f, 0x10)) & ~0xF, m.r32(ecfg1(f, 0x18)) & ~0xF,
                      m.r32(ecfg1(f, 0x04)) & 0x407, m.r32(dc) & 0xFFFF)
            self.bt_epcmd, self.bt_devctl = rb[2], rb[3]
            ok = rb[:3] == (FN1_BAR0_BUS, FN1_BAR2_BUS, FN1_CMD_QUIET) and not rb[3] & (s259.DEVCTL_NS_RO | DEVCTL_FLR)
            self.bt_reset_result = RESET_OK if ok else RESET_MISMATCH
            self.blog(f'reset: FLR done ok={ok} bar0={rb[0]:#x} bar2={rb[1]:#x} cmd={rb[2]:#x} devctl={rb[3]:#x}')
        except Exception as e:
            self.bt_reset_result = RESET_EXCEPTION
            self.blog(f'reset: FLR exception {type(e).__name__}: {e}')

    # ------------------------------------------------------------ BT control page
    def bt_reg_read(self, off):
        if off == R_CFG_DATA:
            return self._bt_cfg_read(self.bt_cfg_off)
        if off in self.bt_regs:
            return self.bt_regs[off] & 0xFFFFFFFF
        up = self.bt_state == BT_UP or self.bt_state == BT_QUIET
        v = {R_MAGIC: BT_MAGIC, R_VERSION: BT_VERSION, R_STATE: self.bt_state, R_ERROR: self.bt_error,
             R_CMD: self.bt_last_cmd, R_SEQ: self.bt_seq, R_AVAIL: self.bt_avail, R_IDS: self.bt_ids,
             R_CFG_OFF: self.bt_cfg_off, R_CFG_ERR: self.bt_cfg_err,
             R_BAR0: FN1_BAR0_BUS if up else 0, R_BAR2: FN1_BAR2_BUS if up else 0,
             R_CHIP_STATE: self.state, R_DEVCTL: self.bt_devctl, R_EPCMD: self.bt_epcmd,
             R_RESET: self.bt_reset_result, R_RESET_COUNT: self.bt_reset_count, R_EPOCH: self.epoch,
             R_RID2SID: self.bt_rid2sid, R_CMD_COUNT: self.bt_cmd_count, R_WINDOW: self.bt_window,
             R_DEVCAP: self.bt_devcap, R_MSICAP: self.bt_caps.get('msi', 0),
             R_GEN: self.bt_gen, R_ARM: self.bt_arm_result}.get(off, 0)
        return v & 0xFFFFFFFF

    def bt_reg_write(self, off, val):
        if off == R_CMD:
            self.bt_last_cmd, self.bt_cmd_count = val, self.bt_cmd_count + 1
            self.blog(f'CMD {val:#x} (fn1 state {self.bt_state}, chip state {self.state})')
            if val == BT_CMD_ENSURE:
                self.bt_ensure()
            elif val == BT_CMD_QUIET:
                self.bt_quiet()
            else:
                self.blog(f'CMD {val:#x} ignored (unknown)')
            self.bt_seq += 1
        elif off == R_CFG_OFF:
            self.bt_cfg_off = val & 0xFFF
            self.bt_cfg_err = CFG_OK if val == self.bt_cfg_off and not val & 3 else CFG_BAD_OFFSET
        elif off == R_CFG_DATA:
            self._bt_cfg_write(self.bt_cfg_off, val)
        elif off == R_RESET:
            if val == 1:
                self.bt_reset()
        elif off == R_ARM:
            self.bt_arm(val)
        else:
            self.blog(f'write {off:#x} = {val:#x} ignored (read-only)')

    def _bt_cfg_read(self, off):
        self.bt_cfg_reads += 1
        if self.bt_state not in (BT_UP, BT_QUIET) or self.up is None:
            self.bt_cfg_err = CFG_NOT_UP
            return 0xFFFFFFFF
        if off & 3 or off >= 0x1000:
            self.bt_cfg_err = CFG_BAD_OFFSET
            return 0xFFFFFFFF
        with self.up.guarded() as g:
            v = self.up.m.r32(ecfg1(self.f, off))
        self.bt_cfg_err = CFG_EXCEPTION if g['exc'] else CFG_OK
        return 0xFFFFFFFF if g['exc'] else v

    def _bt_cfg_write(self, off, val):
        self.bt_cfg_writes += 1
        if self.bt_state != BT_UP or self.up is None:
            self.bt_cfg_err = CFG_NOT_UP
            return
        if off & 3 or off >= 0x1000:
            self.bt_cfg_err = CFG_BAD_OFFSET
            return
        up, f = self.up, self.f
        caps = self.bt_caps
        cur = 0
        if off in (FN1_SUBSYS_CTRL, caps.get('pcie', -1) + 0x10, caps.get('msi', -1)):
            with up.guarded():
                cur = up.m.r32(ecfg1(f, off))
        v = bt_cfg_write_policy(off, val, cur, caps)
        if v is not None and caps.get('msi') and off == caps['msi'] and v & MSI_ENABLE:
            with up.guarded():
                lo = up.m.r32(ecfg1(f, caps['msi'] + 4))
                hi = up.m.r32(ecfg1(f, caps['msi'] + 8)) if caps.get('msi64') else 0
            if hi or lo & 3 or not MSI_ADDR_MIN <= lo < MSI_ADDR_MAX:
                v = None                                        # never enable MSI towards 0xFFFFF000 / unset
        if v is None:
            self.bt_cfg_refused += 1
            self.bt_cfg_err = CFG_NOT_ALLOWED
            if self.bt_cfg_refused <= 16:
                self.blog(f'fn1 config write refused off={off:#x} val={val:#x}')
            return
        with up.guarded() as g:
            up.m.w32(ecfg1(f, off), v)
        if off == 0x04:
            self.bt_epcmd = v & 0xFFFF
        if caps.get('pcie') and off == caps['pcie'] + 8:
            self.bt_devctl = v & 0xFFFF
        self.bt_cfg_err = CFG_EXCEPTION if g['exc'] else CFG_OK
        if self.bt_cfg_writes <= 64:
            self.blog(f'fn1 config write off={off:#x} val={val:#x} -> {v:#x} err={self.bt_cfg_err}')

    def bt_hook_read(self, addr, width):
        try:
            off = addr - BT_CTRL_IPA
            base = off & ~3
            if width == 64:
                return self.bt_reg_read(base) | (self.bt_reg_read(base + 4) << 32)
            v = self.bt_reg_read(base) >> (8 * (off & 3))
            return v & ((1 << width) - 1)
        except Exception as e:
            self.blog(f'hook read {addr:#x}/{width} raised {type(e).__name__}: {e}')
            return (1 << width) - 1 if width < 64 else 0xFFFFFFFFFFFFFFFF

    def bt_hook_write(self, addr, value, width):
        try:
            off = addr - BT_CTRL_IPA
            if isinstance(value, (list, tuple)):
                value = value[0]
            if width == 32 and not off & 3:
                self.bt_reg_write(off, value & 0xFFFFFFFF)
            else:
                self.blog(f'write {off:#x}/{width} ignored (32-bit aligned only)')
        except Exception as e:
            self.blog(f'hook write {addr:#x}/{width} raised {type(e).__name__}: {e}')
            self.bt_state, self.bt_error = BT_FAILED, BT_ERR['ERROR']
