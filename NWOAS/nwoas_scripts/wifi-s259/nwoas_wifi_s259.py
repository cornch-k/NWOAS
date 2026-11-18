# SPDX-License-Identifier: MIT
"""NWOAS S259: BCM4378 Wi-Fi (PCIe 1:0.0, apcie port 0) handed to Windows as ACPI\\NWOA4378.

Contract: research-20261118/wifi/CONTRACT-WIN.md.  Plan: nwoas_scripts/wifi-s259/README.md.

Who does what (all decided from code, see the contract):
  * launcher (-m wifi_module.py, after usb-s174/guest_map.py ran p.pcie_init(), before UEFI):
      boot_force_off(): PERST# low + SMC gP0d <- 0x800000 + link-down check, so UEFI meets port 0
      exactly as on every boot so far (chip unpowered, link down, bus 1 empty -> FL1100 stays at
      bus 0xC0000000).  Then install the control page (guest IPA CTRL_IPA, Python HOOK).
  * Windows ACPI (\\_SB.NWC0.WLN0, SSDT from fw-wifi-s259): _INI / _PS0 write CMD_POWER_UP,
      _PS3 writes CMD_POWER_DOWN into the control page.  That runs, inside the trapped write:
      power_up(): PERST# low, SMC gP0d <- 0x800001, port-0 PERST#/REFCLK/LTSSM (S240 code,
      hardware PASS_IDS 2026-11-15), config IDs, fixed BARs (S257 code, hardware PASS_FW_UP/
      PASS_SCAN 2026-11-18), chip-id read through BAR0, DevCtl NoSnoop/RO off, port-0 INTx
      unmask check, dart-apcie0 == dart-apcie1 check (UEFI programmed both with one table),
      then bus master on.  Firmware is NOT loaded here (the Windows driver owns load/rings/scan).
      power_down(): bus master off, PERST# low, SMC off, link-down check.  PORT_PERST_OFF is
      never cleared (dart-apcie0 lives inside the port-0 block and faults after that, S257).
  * the driver reads the control page (state, BAR bus addresses) and changes the PCI config
    registers it needs (BAR0 window 0x80, command, DevCtl, LinkCtl ASPM) only through it.

Every hardware write goes through the S240/S257 exact-address allowlist (RangeMmio).  FL1100
(port 1) registers and its ECAM function are never written; port-1 state is compared before/after.
Built and tested offline only (test_wifi_hv.py).  Not a boot or Wi-Fi result.
"""
import datetime
import json
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
for _d in (ROOT / 'nwoas_scripts' / 'wifi-s240', ROOT / 'nwoas_scripts' / 'wifi-s257'):
    if str(_d) not in sys.path:
        sys.path.insert(0, str(_d))
import wifi_probe as wp  # noqa: E402  (S240, hardware PASS_IDS 2026-11-15)
import wifi_up as wu     # noqa: E402  (S257, hardware PASS_FW_UP / PASS_SCAN 2026-11-18)
import wifi_scan as ws   # noqa: E402

TAG = '[s259]'

# ---------------------------------------------------------------- contract constants
# CONTRACT-WIN.md section 2.  fw-wifi-s259/build.py refuses to build when the SSDT differs.
CTRL_IPA, CTRL_SIZE = 0x348000000, 0x4000
BAR_BASE = 0xE0000000                        # same --bar-base default S257 used on hardware
BAR2_BUS, BAR2_SIZE = 0xE0000000, 0x800000  # TCM (S257 hardware readback 0x18 = 0xe0000004)
BAR0_BUS, BAR0_SIZE = 0xE0800000, 0x10000   # registers (0x10 = 0xe0800004)
WINDOW_BUS = (0xE0000000, 0xE08FFFFF)        # root port 0 memory window (0x120 = 0xe080e000)
CPU_OFFSET = wu.CPU_OFFSET                   # 0x600000000
RESERVED_RAM = (0xE0000000, 0x1000000)       # MemoryInitPeiLib S259 reservation (16 MiB)
# Guest accesses to the BARs are emulated by the C hypervisor (TraceMode.BYPASS, like the FL1100 BAR in
# usb-s174/guest_map.py), NOT passed through: Windows maps MMIO as Device-nGnRnE, stage 2 keeps the
# guest type (hv_vm.c PTE_MEMATTR_UNCHANGED, HCR_EL2.FWB=0), and an nGnRnE write to PCIe BAR space
# raises SError (Asahi docs hw/cpu/system-registers.md; hardware 2026-11-18 14:xx L2C_ERR_ADR
# 0x6e0800120/0x6e0800c34/0x6e0800408).  The hypervisor performs the access through m1n1's own MMU,
# which maps this PCIe range Device-nGnRE (memory.c mmu_remap_ranges, ADT pmap-io-ranges
# 0x6c0000000+0x40000000 flags 0x80000027).
BAR_EMUL = (WINDOW_BUS[0] + CPU_OFFSET, WINDOW_BUS[1] - WINDOW_BUS[0] + 1)   # 0x6e0000000 + 0x900000
DART0_BASE = ws.EXPECT_DART['base']          # /arm-io/dart-apcie0 (port 0)
DART1_BASE = 0x682008000                     # /arm-io/dart-apcie1 (port 1, FL1100) READ ONLY
WLAN_IRQ = 695                               # port-0 INTx aggregate (dts 3187, DSDT _PRT)

MAGIC, VERSION = 0x4657574E, 1               # 'NWWF'
R_MAGIC, R_VERSION, R_STATE, R_ERROR, R_CMD, R_SEQ, R_AVAIL, R_IDS = 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C
R_CFG_OFF, R_CFG_DATA, R_CFG_ERR = 0x20, 0x24, 0x28
R_BAR0, R_BAR2, R_LINKSTS, R_DEVCTL, R_EPCMD, R_DART_TTBR, R_DART_CHECK = 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44
R_CHIPID, R_BAR0WIN, R_CMD_COUNT = 0x48, 0x4C, 0x50
# ADT /arm-io/wlan data the driver cannot get on Windows (read-only, valid in every STATE):
R_MAC_LO, R_MAC_HI, R_CAL_LEN, R_ANTENNA, R_BOARD, BOARD_LEN = 0x60, 0x64, 0x68, 0x6C, 0x70, 16
CAL_OFF, CAL_MAX = 0x1000, 0x1000            # wifi-calibration-msf bytes at page offset 0x1000

STATE_OFF, STATE_UP, STATE_OFF_REQ, STATE_FAILED = 0, 1, 2, 3
CMD_POWER_UP, CMD_POWER_DOWN = 1, 2
CFG_OK, CFG_NOT_UP, CFG_NOT_ALLOWED, CFG_BAD_OFFSET, CFG_EXCEPTION = 0, 1, 2, 3, 4

ERRORS = {0: 'NONE', 1: 'DISABLED', 2: 'RP0_BAD', 3: 'PORT_NOT_READY', 4: 'DART0_OFFLINE',
          5: 'LINK_ALREADY_UP', 6: 'SMC_KEY', 7: 'SMC_ERROR', 8: 'LINK_DOWN', 9: 'NO_DEVICE',
          10: 'BAR_SETUP', 11: 'BAR_LAYOUT', 12: 'CHIP_ID', 13: 'DEVCTL', 14: 'DART_MISMATCH',
          15: 'BUS_MASTER', 16: 'PORT1_CHANGED', 17: 'ERROR', 18: 'POWER_OFF_UNCONFIRMED',
          19: 'INTX_MASK'}
ERR = {v: k for k, v in ERRORS.items()}

INTMSK, INTMSKCLR, INTX_BITS = 0x104, 0x10C, 0xF   # PORT_INTMSK / PORT_INTMSKCLR (UEFI .h), INTx 0..3
DEVCTL_NS_RO = 0x0810                              # No Snoop (11) | Relaxed Ordering (4)
EP_CMD_UP = wu.CMD_MEM | wu.CMD_BME                # 0x0006, like XHC0 _INI for the FL1100
EP_CMD_QUIET = wu.CMD_MEM | wu.CMD_INTX_DIS        # 0x0402, S257 value while no DMA is allowed
LOG_BUDGET = 4000                                  # lines per launcher session (bounded log)
STEP_BUDGET = 160                                  # steps kept per event JSON


class Fail(Exception):
    def __init__(self, code, text):
        super().__init__(f'{code}: {text}')
        self.code, self.text = code, text


def cfg_write_policy(off, val, cur, devctl_off):
    """Value written to endpoint config `off` for a driver request, or None if not allowed.
    cur = current dword at off (needed for LinkCtl).  Upper 16 bits of 0x04 / DevCtl dwords are
    status (write-1-to-clear): always written as 0, so a driver write never clears status."""
    if off == wu.BAR0_WINDOW_CFG:
        return val & 0xFFFFF000 if val & 0xFFF == 0 else None
    if off == 0x04:
        return (val & (wu.CMD_BME | wu.CMD_INTX_DIS)) | wu.CMD_MEM   # memory decode stays on
    if devctl_off and off == devctl_off:
        return val & 0xFFFF & ~DEVCTL_NS_RO
    if off == wu.EP_LINKCTL_CFG:
        return (cur & 0xFFFF & ~wu.ASPM_MASK) | (val & wu.ASPM_MASK)
    return None


def setup_bars_post_uefi(up):
    """S257 WifiUp._setup_bars with the same writes, but checked against the state UEFI leaves.

    Hardware 2026-11-18 12:56 (run-20261118-125628-ynmpoboc): UEFI's PciBusDxe + the DART EBS hook
    leave root port 0 COMMAND = 0x0007 (I/O | MEM | BME); on the D4 proxy it was 0x0000.  S257 keeps
    every bit except BME (rp_cmd = saved & ~BME | MEM = 0x0003) and then required (cmd & 7) == MEM,
    which only holds when I/O decode was off -> 'BAR_SETUP_FAILED (config read-back differs)' with
    every register actually correct.  Here the I/O bit is left as UEFI set it (no I/O aperture exists,
    nothing behind port 0 decodes I/O) and each register is compared with the exact value written;
    a mismatch names the register with expected and read values."""
    bars = {b['bar']: b for b in up.res['functions'][0].get('bars', []) if isinstance(b, dict)}
    try:
        s0, s2 = int(bars[0]['size'], 16), int(bars[2]['size'], 16)
    except (KeyError, ValueError, TypeError):
        raise Fail(ERR['BAR_SETUP'], f'BAR0/BAR2 sizes not measured: {bars}')
    for name, s, i in (('BAR0', s0, 0), ('BAR2', s2, 2)):
        if s & (s - 1) or bars[i]['type'] != 'mem64':
            raise Fail(ERR['BAR_SETUP'], f'{name} size {s:#x} type {bars[i]["type"]} not a mem64 power of two')
    bar2_bus = wu.align_up(up.o.bar_base, s2)
    bar0_bus = wu.align_up(bar2_bus + s2, s0)
    win_lo, win_hi = bar2_bus & ~0xFFFFF, wu.align_up(bar0_bus + s0, 0x100000) - 1
    if (bar0_bus, bar2_bus, s0, s2, win_lo, win_hi) != (BAR0_BUS, BAR2_BUS, BAR0_SIZE, BAR2_SIZE) + WINDOW_BUS:
        raise Fail(ERR['BAR_LAYOUT'], f'layout bar0={bar0_bus:#x}/{s0:#x} bar2={bar2_bus:#x}/{s2:#x} '
                                      f'window={win_lo:#x}-{win_hi:#x} differs from the contract')
    up.bar0_bus, up.bar2_bus, up.bar0_size, up.bar2_size = bar0_bus, bar2_bus, s0, s2
    up.bar0_cpu, up.bar2_cpu = bar0_bus + CPU_OFFSET, bar2_bus + CPU_OFFSET
    m = up.m
    with up.checked('config save'):
        saved = {'rp': {o: m.r32(up.rcfg(o)) for o in (0x04, 0x18, 0x20, 0x24, 0x28, 0x2C)},
                 'ep': {o: m.r32(up.ecfg(o)) for o in (0x04, 0x10, 0x14, 0x18, 0x1C, wu.BAR0_WINDOW_CFG)}}
    up.res['config_saved'] = {k: {hex(o): hex(v) for o, v in d.items()} for k, d in saved.items()}
    ep = saved['ep']
    if ep[0x04] & 0x7 or (ep[0x10] & ~0xF) or ep[0x14] or (ep[0x18] & ~0xF) or ep[0x1C]:
        raise Fail(ERR['BAR_SETUP'], f'endpoint already decoding/assigned (cmd={ep[0x04]:#x} bar0={ep[0x10]:#x})')
    if (saved['rp'][0x18] & 0xFFFF00) != 0x010100:
        raise Fail(ERR['BAR_SETUP'], f'root port 0 bus numbers {saved["rp"][0x18]:#x} (want sec 1 sub 1)')
    up.saved, up.ep_touched = saved, True
    rp_cmd = (saved['rp'][0x04] & 0xFFFF & ~wu.CMD_BME) | wu.CMD_MEM
    ep_cmd = wu.CMD_MEM | wu.CMD_INTX_DIS
    mem_window = ((win_hi >> 16) & 0xFFF0) << 16 | ((win_lo >> 16) & 0xFFF0)
    with up.checked('BAR setup'):
        m.w32(up.rcfg(0x20), mem_window)
        m.w32(up.rcfg(0x24), 0x0000FFF0)                  # prefetchable window disabled (base > limit)
        m.w32(up.rcfg(0x28), 0)
        m.w32(up.rcfg(0x2C), 0)
        m.w32(up.rcfg(0x04), rp_cmd)                      # memory forwarding, no bus master yet
        m.w32(up.ecfg(0x10), bar0_bus)
        m.w32(up.ecfg(0x14), 0)
        m.w32(up.ecfg(0x18), bar2_bus)
        m.w32(up.ecfg(0x1C), 0)
        m.w32(up.ecfg(0x04), ep_cmd)                      # memory decode, NO bus master, INTx off
        rb = {o: m.r32(up.ecfg(o)) for o in (0x04, 0x10, 0x14, 0x18, 0x1C)}
        rp = {o: m.r32(up.rcfg(o)) for o in (0x04, 0x20, 0x24, 0x28, 0x2C)}
    # (name, read value after mask, expected): BAR low nibble = read-only type bits (64-bit = 0x4);
    # 0x24 low nibbles = read-only 64-bit-capable bits of the prefetchable base/limit.
    checks = [('ep 0x04 cmd', rb[0x04] & 0x0407, ep_cmd), ('ep 0x10 bar0', rb[0x10] & ~0xF, bar0_bus),
              ('ep 0x14 bar0 hi', rb[0x14], 0), ('ep 0x18 bar2', rb[0x18] & ~0xF, bar2_bus),
              ('ep 0x1c bar2 hi', rb[0x1C], 0), ('rp 0x04 cmd', rp[0x04] & 0xFFFF, rp_cmd),
              ('rp 0x20 window', rp[0x20], mem_window), ('rp 0x24 pref', rp[0x24] & 0xFFF0FFF0, 0x0000FFF0),
              ('rp 0x28 pref base hi', rp[0x28], 0), ('rp 0x2c pref limit hi', rp[0x2C], 0)]
    bad = [f'{n}: want {w:#x} got {g:#x}' for n, g, w in checks if g != w]
    up.res['bar_layout'] = {'bar0_bus': hex(bar0_bus), 'bar0_cpu': hex(up.bar0_cpu), 'bar0_size': hex(s0),
                            'bar2_bus': hex(bar2_bus), 'bar2_cpu': hex(up.bar2_cpu), 'bar2_size': hex(s2),
                            'rootport0_window_bus': [hex(win_lo), hex(win_hi)],
                            'expected': {n: hex(w) for n, _, w in checks},
                            'readback': {n: hex(g) for n, g, _ in checks}, 'mismatch': bad, 'bus_master': False}
    up.log2('BARs assigned', bar0_cpu=up.bar0_cpu, bar2_cpu=up.bar2_cpu, window=f'{win_lo:#x}-{win_hi:#x}',
            rp_cmd=rp[0x04] & 0xFFFF, ep_cmd=rb[0x04] & 0xFFFF, ok=not bad)
    for line in bad:
        up.log2('BAR read-back mismatch', detail=line)
    if bad:
        raise Fail(ERR['BAR_SETUP'], 'config read-back differs: ' + '; '.join(bad))
    m.ranges = [(up.bar0_cpu, s0), (up.bar2_cpu, s2)]


class WifiS259:
    def __init__(self, p, facts, smc_factory, *, enabled=True, sleep=time.sleep, clock=time.monotonic,
                 phase=wp.alarm_phase, log=print, run_dir=None, resync=None, wlan=None):
        self.p, self.f, self.smc_factory = p, facts, smc_factory
        self.wlan = self._wlan_regs(wlan or {})
        self.enabled = enabled
        self.sleep, self.clock, self.phase = sleep, clock, phase
        self._out = log
        self.lines = 0
        self.run_dir = run_dir
        # Only a proxy NOP here.  wp.resync_proxy() also drains the serial device, which is right on
        # an idle proxy (S240) but could eat hypervisor event bytes while the guest is running.
        self.resync = resync or self.p.nop
        self.state, self.error, self.avail, self.seq = STATE_OFF, 0, 0, 0
        self.first_up_done = False
        self.last_cmd, self.cmd_count = 0, 0
        self.ids = self.chipid = self.linksts = self.devctl = self.epcmd = 0
        self.dart_ttbr = self.dart_check = 0
        self.cfg_off, self.cfg_err = 0, CFG_OK
        self.cfg_reads = self.cfg_writes = self.cfg_refused = 0
        self.up = None
        self.devctl_off = 0
        self.events = []
        if not enabled:
            self.state, self.error = STATE_FAILED, ERR['DISABLED']

    @staticmethod
    def _wlan_regs(info):
        mac = info.get('mac') or b''
        mac = mac if len(mac) == 6 else bytes(6)
        cal = bytes(info.get('cal') or b'')[:CAL_MAX]
        ant = (str(info.get('antenna') or '').encode('ascii', 'replace') + bytes(4))[:4]
        board = (str(info.get('board') or '').encode('ascii', 'replace') + bytes(BOARD_LEN))[:BOARD_LEN]
        regs = {R_MAC_LO: int.from_bytes(mac[:4], 'little'), R_MAC_HI: int.from_bytes(mac[4:], 'little'),
                R_CAL_LEN: len(cal), R_ANTENNA: int.from_bytes(ant, 'little')}
        for i in range(0, BOARD_LEN, 4):
            regs[R_BOARD + i] = int.from_bytes(board[i:i + 4], 'little')
        calp = cal + bytes(-len(cal) % 4)
        for i in range(0, len(calp), 4):
            regs[CAL_OFF + i] = int.from_bytes(calp[i:i + 4], 'little')
        return regs

    # ------------------------------------------------------------ logging / records
    def log(self, msg):
        self.lines += 1
        if self.lines <= LOG_BUDGET or msg.startswith(('READY', 'FAIL', 'OFF', 'BOOT')):
            self._out(f'{TAG} {msg}'[:300])

    def record(self, name, res):
        rec = {'event': name, 'time': datetime.datetime.now().isoformat(timespec='seconds'),
               'state': self.state, 'error': ERRORS.get(self.error, self.error), 'seq': self.seq,
               'hardware_success_claim': False}
        rec.update({k: v for k, v in res.items() if k not in ('steps', 'mmio_writes')})
        rec['steps'] = res.get('steps', [])[:STEP_BUDGET]
        rec['mmio_writes'] = res.get('mmio_writes', [])[:200]
        self.events.append(rec)
        if self.run_dir is not None:
            try:
                (Path(self.run_dir) / f'event-{len(self.events):03d}-{name}.json').write_text(
                    json.dumps(rec, indent=1, default=str) + '\n')
            except OSError as e:
                self.log(f'WARNING event json not written: {e}')
        return rec

    # ------------------------------------------------------------ S240/S257 objects
    def _new_up(self):
        o = wu.parse_args(['--stage', 'chip', '--bar-base', hex(BAR_BASE)])
        up = wu.WifiUp(self.p, self.f, self.smc_factory, o, None, {}, None, sleep=self.sleep,
                       clock=self.clock, phase=self.phase, log=self.log)
        allowed = wp.build_allowlist(self.f) | up.extra_allow()
        allowed |= {self.f['port0'] + INTMSKCLR} | ws.Dart.write_addrs(DART0_BASE)
        up.m = wu.RangeMmio(self.p, allowed)
        up.m.cmd_regs = frozenset({up.ecfg(0x04), up.rcfg(0x04)})
        return up

    def _step(self, up, name, fn, seconds, pd):
        try:
            with self.phase(f's259-{name}', seconds):
                v = fn()
            pd['steps'][name] = 'ok'
            return True, v
        except Exception as e:
            pd['steps'][name] = 'error'
            pd['errors'][name] = f'{type(e).__name__}: {e}'
            return False, None

    def _link_up(self, up):
        return bool(up.m.r32(self.f['port0'] + wp.PORT_LINKSTS) & wp.LINKSTS_UP)

    # ------------------------------------------------------------ power off (shared)
    def _power_off(self, up, why, bme_off=True):
        """Fixed-value steps, each on its own.  Never clears PORT_PERST_OFF (dart-apcie0 sits in
        the port-0 block and faults after it, S257 2026-11-18), never touches port 1."""
        f = self.f
        pd = {'why': why, 'steps': {}, 'errors': {}}
        self._step(up, 'resync', self.resync, 5, pd)
        up.m.bme_ok = False
        if bme_off:
            def quiet():
                with up.guarded() as g:
                    if self._link_up(up):
                        up.m.w32(up.ecfg(0x04), EP_CMD_QUIET)
                        self.sleep(0.01)                 # let in-flight requests drain
                return g['exc']
            self._step(up, 'bme_off', quiet, 5, pd)
        gpio = f['gpio'] + 4 * f['perst_pin']
        self._step(up, 'perst_assert', lambda: up.m.w32(gpio, wp.PD_GPIO_PERST_ASSERTED), 5, pd)

        def smc_off():
            if up.smc is None:
                up.smc = self.smc_factory(f['smc'])
                up.smc.start()
                info = up.smc.key_info(wp.SMC_WIFI_KEY)
                if not isinstance(info, (tuple, list)) or len(info) < 2 or info[0] != 4 \
                        or info[1] not in wp.SMC_KEY_TYPES:
                    raise Fail(ERR['SMC_KEY'], f'SMC key info {info!r}')
            up.smc.write32(wp.SMC_WIFI_KEY, wp.SMC_GPIO_OFF)
        self._step(up, 'smc_off', smc_off, 20, pd)
        if up.smc is not None:
            self._step(up, 'smc_stop', up.smc.stop, 10, pd)
            up.smc = None
        ok, r = self._step(up, 'link_check',
                           lambda: up.poll(f['port0'] + wp.PORT_LINKSTS, wp.LINKSTS_UP, 0, 200), 5, pd)
        pd['link_down_after'] = bool(ok and r[0])
        if 'port1_before' in up.res:
            ok, after = self._step(up, 'port1_snapshot', up.port1_snapshot, 10, pd)
            pd['port1_unchanged'] = (after == up.res['port1_before']) if ok else None
        pd['confirmed'] = not pd['errors'] and pd['link_down_after'] and pd.get('port1_unchanged', True) is not False
        up.res['power_down'] = pd
        up.m.ranges = []
        self.log(f'power off ({why}) confirmed={pd["confirmed"]} errors={sorted(pd["errors"]) or "none"} '
                 f'port1_unchanged={pd.get("port1_unchanged", "n/a")}')
        return pd

    # ------------------------------------------------------------ launcher time (before UEFI)
    def boot_force_off(self):
        """Run once from the launcher module, before the guest (UEFI) starts.  Raises when the chip
        may stay powered with the link up: UEFI would then enumerate it and move the FL1100 BAR."""
        up = self._new_up()
        f = self.f
        with up.guarded() as g:
            pre = {'port_status': up.m.r32(f['port0'] + wp.PORT_STATUS),
                   'port_perst': up.m.r32(f['port0'] + wp.PORT_PERST),
                   'linksts': up.m.r32(f['port0'] + wp.PORT_LINKSTS),
                   'rp0_id': up.cfg(0, 0, 0, 0x00), 'rp0_bus': up.cfg(0, 0, 0, 0x18)}
        pre['exc'] = g['exc']
        up.res['boot_pre'] = {k: hex(v) for k, v in pre.items()}
        up.res['port1_before'] = up.port1_snapshot()
        self.log('BOOT port0 ' + ' '.join(f'{k}={v:#x}' for k, v in pre.items()))
        if not pre['port_perst'] & wp.PERST_OFF:
            self.log('BOOT note: port 0 PORT_PERST_OFF clear (pcie_init not run?) - dart-apcie0 not reachable')
        pd = self._power_off(up, 'boot force-off', bme_off=False)
        self.record('boot-force-off', up.res)
        if not pd['confirmed']:
            if not pd['link_down_after']:
                raise RuntimeError(f'{TAG} STOP boot force-off: port 0 link still up after power off '
                                   f'(UEFI would enumerate Wi-Fi and move USB-A) errors={pd["errors"]}')
            raise RuntimeError(f'{TAG} STOP boot force-off not confirmed errors={pd["errors"]} '
                               f'(chip power state unknown; set NWOAS_WIFI=0 to boot without Wi-Fi)')
        self.state = STATE_OFF if self.enabled else STATE_FAILED
        self.log(f'BOOT force-off confirmed; Wi-Fi {"armed" if self.enabled else "DISABLED (NWOAS_WIFI=0)"}; '
                 f'control page {CTRL_IPA:#x}+{CTRL_SIZE:#x}')
        return pd

    # ------------------------------------------------------------ guest time (inside the hook)
    def power_up(self):
        if not self.enabled:
            return
        if self.state == STATE_UP and self.up is not None:
            try:
                with self.up.guarded() as g:
                    ok = self._link_up(self.up) and (self.up.m.r32(self.up.ecfg(0x10)) & ~0xF) == BAR0_BUS
                if ok and not g['exc']:
                    self.log('power up: already up (no change)')
                    return
            except Exception as e:
                self.log(f'power up: up-check failed {type(e).__name__}: {e}')
            self.log('power up: link/BAR lost while UP -> full power cycle')
            self._power_off(self.up, 'lost link')
            self.up = None
        up = self._new_up()
        f = self.f
        t0 = self.clock()
        try:
            self._bring_up(up)
            self.up, self.state, self.error = up, STATE_UP, 0
            if not self.first_up_done:
                self.avail = 1
        except Fail as e:
            self.error = e.code
            self._fail_cleanup(up, e.text)
        except (wu.StageStop, wp.Refusal) as e:
            self.error = ERR['BAR_SETUP'] if 'BAR' in str(e) else ERR['ERROR']
            self._fail_cleanup(up, str(e))
        except Exception as e:
            self.error = ERR['ERROR']
            self._fail_cleanup(up, f'{type(e).__name__}: {e}')
        finally:
            self.first_up_done = True
        up.res['elapsed_s'] = round(self.clock() - t0, 3)
        self.record('power-up', up.res)
        if self.state == STATE_UP:
            self.log(f'READY ids={self.ids:#x} chip={self.chipid:#x} bar0={BAR0_BUS + CPU_OFFSET:#x} '
                     f'bar2={BAR2_BUS + CPU_OFFSET:#x} devctl={self.devctl:#x} epcmd={self.epcmd:#x} '
                     f'dart_check={self.dart_check} irq={WLAN_IRQ} t={up.res["elapsed_s"]}s')
        else:
            self.log(f'FAIL power up error={ERRORS.get(self.error)} t={up.res["elapsed_s"]}s')

    def _fail_cleanup(self, up, text):
        self.state = STATE_FAILED
        up.res['failure'] = text
        self.log(f'FAIL {ERRORS.get(self.error)}: {text}')
        pd = self._power_off(up, 'bring-up failed')
        if not pd['confirmed']:
            up.res['failure_power_off_unconfirmed'] = True
        self.up = None

    def _bring_up(self, up):
        f, m = self.f, up.m
        with self.phase('s259-preflight', 15):
            with up.guarded() as g:
                rp_id, rp_hdr = up.cfg(0, 0, 0, 0x00), up.cfg(0, 0, 0, 0x0C)
            if g['exc'] or (rp_id & 0xFFFF) != wp.APPLE_VENDOR or ((rp_hdr >> 16) & 0x7F) != 1:
                raise Fail(ERR['RP0_BAD'], f'root port 0 id={rp_id:#x} hdr={rp_hdr:#x} exc={g["exc"]}')
            st, perst = m.r32(f['port0'] + wp.PORT_STATUS), m.r32(f['port0'] + wp.PORT_PERST)
            link0 = m.r32(f['port0'] + wp.PORT_LINKSTS)
            up.res['pre'] = {'port_status': hex(st), 'port_perst': hex(perst), 'linksts': hex(link0)}
            if not st & wp.STATUS_READY:
                raise Fail(ERR['PORT_NOT_READY'], f'port 0 status {st:#x}')
            if not perst & wp.PERST_OFF:
                raise Fail(ERR['DART0_OFFLINE'], f'PORT_PERST {perst:#x}: port-0 block (and dart-apcie0) in reset')
            up.res['port1_before'] = up.port1_snapshot()
            if link0 & wp.LINKSTS_UP:
                raise Fail(ERR['LINK_ALREADY_UP'], f'link up before our power-on (linksts {link0:#x})')
        with self.phase('s259-smc-on', 20):
            up._perst(assert_=True)                      # UEFI left PERST# deasserted on an unpowered chip
            try:
                up.smc = self.smc_factory(f['smc'])
                up.smc.start()
                info = up.smc.key_info(wp.SMC_WIFI_KEY)
            except Exception as e:
                raise Fail(ERR['SMC_ERROR'], f'SMC start/key info: {type(e).__name__}: {e}')
            if not isinstance(info, (tuple, list)) or len(info) < 2 or info[0] != 4 or info[1] not in wp.SMC_KEY_TYPES:
                raise Fail(ERR['SMC_KEY'], f'SMC key {wp.SMC_WIFI_KEY} info {info!r}')
            up.smc_on_attempted = True
            up.smc.write32(wp.SMC_WIFI_KEY, wp.SMC_GPIO_ON)
            up.step('smc write', key=wp.SMC_WIFI_KEY, value=wp.SMC_GPIO_ON)
        self.sleep(up.o.power_settle_ms / 1000.0)
        with self.phase('s259-port0-bringup', 15):
            up._bring_up_port0()
        with self.phase('s259-link-wait', 10):
            ok, ls = up.poll(f['port0'] + wp.PORT_LINKSTS, wp.LINKSTS_UP, wp.LINKSTS_UP, up.o.link_timeout_ms)
        self.linksts = ls
        up.step('port 0 link', up=ok, linksts=ls)
        if not ok:
            raise Fail(ERR['LINK_DOWN'], f'port 0 link did not come up (linksts {ls:#x})')
        with self.phase('s259-config', 30):
            up._read_endpoint()
        if up.res.get('verdict') != 'PASS_IDS':
            raise Fail(ERR['NO_DEVICE'], f'endpoint: {up.res.get("verdict")}')
        self.ids = int(up.res['functions'][0]['id'], 16)
        with self.phase('s259-bars', 20):
            setup_bars_post_uefi(up)
        if (up.bar0_bus, up.bar2_bus, up.bar0_size, up.bar2_size) != (BAR0_BUS, BAR2_BUS, BAR0_SIZE, BAR2_SIZE):
            raise Fail(ERR['BAR_LAYOUT'], f'BAR layout {up.res.get("bar_layout")} differs from the contract')
        with self.phase('s259-chip-id', 10):
            with up.checked('chip id'):
                up.cur_page = None
                cid = up.bp_r32(wu.CHIP_BASE + wu.CC_CHIPID)
        self.chipid = cid
        up.res['chipid_reg'] = hex(cid)
        if (cid & 0xFFFF) != wu.BRCM_CC_4378 or (cid >> 28) & 0xF != 1:
            raise Fail(ERR['CHIP_ID'], f'chip id register {cid:#x}')
        with self.phase('s259-devctl', 10):
            pcie = up.res['functions'][0].get('pcie') or {}
            cap = int(pcie.get('cap_offset', '0'), 16)
            if not 0x40 <= cap <= 0xF0:
                raise Fail(ERR['DEVCTL'], f'no PCIe capability ({cap:#x})')
            self.devctl_off = cap + 8
            dc = up.ecfg(self.devctl_off)
            m.allow(dc)
            with up.checked('DevCtl'):
                old = m.r32(dc) & 0xFFFF
                m.w32(dc, old & ~DEVCTL_NS_RO)
                new = m.r32(dc) & 0xFFFF
            self.devctl = new
            up.res['devctl'] = {'offset': hex(self.devctl_off), 'before': hex(old), 'after': hex(new)}
            up.step('DevCtl', before=old, after=new)
            if new & DEVCTL_NS_RO:
                raise Fail(ERR['DEVCTL'], f'NoSnoop/RO still set ({new:#x})')
        with self.phase('s259-intx', 5):
            msk = m.r32(f['port0'] + INTMSK)
            if msk & INTX_BITS:
                m.w32(f['port0'] + INTMSKCLR, INTX_BITS)  # UEFI does the same in SetupPciePort
            msk2 = m.r32(f['port0'] + INTMSK)
            up.res['port0_intmsk'] = {'before': hex(msk), 'after': hex(msk2)}
            if msk2 & INTX_BITS:
                raise Fail(ERR['INTX_MASK'], f'port 0 INTx still masked ({msk2:#x})')
        with self.phase('s259-dart', 15):
            self._dart_check(up)
        with self.phase('s259-bus-master', 10):
            m.bme_ok = True
            with up.checked('bus master on'):
                rp_cmd = (up.saved['rp'][0x04] & 0xFFFF) | wu.CMD_MEM | wu.CMD_BME   # I/O bit as UEFI left it
                m.w32(up.rcfg(0x04), rp_cmd)
                m.w32(up.ecfg(0x04), EP_CMD_UP)
                rb = (m.r32(up.rcfg(0x04)) & 0xFFFF, m.r32(up.ecfg(0x04)) & 0xFFFF)
            self.epcmd = rb[1]
            up.res['bus_master'] = {'rp_cmd': hex(rb[0]), 'ep_cmd': hex(rb[1])}
            up.step('bus master ON (dart-apcie0 == dart-apcie1 table)', rp_cmd=rb[0], ep_cmd=rb[1])
            if (rb[0] & 0x6) != 0x6 or (rb[1] & 0x406) != 0x6:
                raise Fail(ERR['BUS_MASTER'], f'command read-back rp {rb[0]:#x} ep {rb[1]:#x}')
        with self.phase('s259-smc-stop', 10):
            up.smc.stop()                                 # same as m1n1 dcp.c: write, then smc_shutdown
            up.smc = None
        with self.phase('s259-port1', 10):
            after = up.port1_snapshot()
        up.res['port1_after'] = after
        if after != up.res['port1_before']:
            raise Fail(ERR['PORT1_CHANGED'], 'FL1100 port-1 registers changed during bring-up')

    def _dart_check(self, up):
        """UEFI SetupApcieDarts points all 16 streams of all three apcie DARTs at one L1 table and
        AppleDartIoMmuDxe's ExitBootServices handler fills it (low 4 GiB -> backing, high RAM 1:1).
        dart-apcie1 carries the working USB-A DMA, so dart-apcie0 must hold the same TTBR/TCR.
        If it does not, copy dart-apcie1's values (the design's 'same table' rule); never write
        dart-apcie1.  Bus master stays off until this passes."""
        d0, d1 = ws.Dart(up.m, DART0_BASE), ws.Dart(up.m, DART1_BASE)
        with up.checked('DART read'):
            s0, s1 = d0.snapshot(), d1.snapshot()
        up.res['dart0_before'], up.res['dart1'] = ws.hexsnap(s0), ws.hexsnap(s1)

        def same(a, b):
            return all(a['ttbr'][s][0] == b['ttbr'][s][0] and a['tcr'][s] == b['tcr'][s] for s in range(16)) \
                and a['enabled'] == b['enabled']
        ttbr = s1['ttbr'][0][0]
        good1 = (ttbr & ws.DART_TTBR_VALID) and all(s1['ttbr'][s][0] == ttbr for s in range(16)) \
            and all(t & ws.DART_TCR_TRANSLATE and not t & ws.DART_TCR_BYPASS for t in s1['tcr']) \
            and s1['enabled'] == 0xFFFF
        if not good1:
            raise Fail(ERR['DART_MISMATCH'], f'dart-apcie1 not in the UEFI shape: {ws.hexsnap(s1)}')
        if s0['config'] & ws.DART_CONFIG_LOCK:
            raise Fail(ERR['DART_MISMATCH'], 'dart-apcie0 locked')
        self.dart_ttbr = ttbr
        if same(s0, s1):
            self.dart_check = 1
            up.step('dart-apcie0 == dart-apcie1 (UEFI table)', ttbr=ttbr)
            return
        up.step('dart-apcie0 differs from dart-apcie1 -> copy', ttbr0=s0['ttbr'][0][0], ttbr1=ttbr)
        with up.checked('DART copy'):
            for s in range(16):
                d0.w(ws.DART_TCR + 4 * s, 0)
            for s in range(16):
                for j in range(ws.DART_NTTBR):
                    d0.w(ws.DART_TTBR + 16 * s + 4 * j, s1['ttbr'][s][j])
            d0.w(ws.DART_ENABLED_STREAMS, s1['enabled'])
            for s in range(16):
                d0.w(ws.DART_TCR + 4 * s, s1['tcr'][s])
            d0.invalidate()
            s0b = d0.snapshot()
        up.res['dart0_after'] = ws.hexsnap(s0b)
        if not same(s0b, s1):
            raise Fail(ERR['DART_MISMATCH'], 'dart-apcie0 copy did not read back')
        self.dart_check = 2

    def power_down(self, why='CMD_POWER_DOWN'):
        if self.state != STATE_UP or self.up is None:
            self.log(f'power down ({why}): state {self.state}, nothing powered')
            return
        pd = self._power_off(self.up, why)
        self.record('power-down', self.up.res)
        self.up = None
        if pd['confirmed']:
            self.state, self.error = STATE_OFF_REQ, 0
            self.log(f'OFF ({why}) confirmed')
        else:
            self.state, self.error = STATE_FAILED, ERR['POWER_OFF_UNCONFIRMED']
            self.log(f'FAIL power down unconfirmed {pd["errors"]}')

    # ------------------------------------------------------------ control page
    def reg_read(self, off):
        if off == R_CFG_DATA:
            return self._cfg_read(self.cfg_off)
        if off == R_BAR0WIN:
            return self._cfg_read(wu.BAR0_WINDOW_CFG)
        if off in self.wlan:
            return self.wlan[off] & 0xFFFFFFFF
        v = {R_MAGIC: MAGIC, R_VERSION: VERSION, R_STATE: self.state, R_ERROR: self.error,
             R_CMD: self.last_cmd, R_SEQ: self.seq, R_AVAIL: self.avail, R_IDS: self.ids,
             R_CFG_OFF: self.cfg_off, R_CFG_ERR: self.cfg_err,
             R_BAR0: BAR0_BUS if self.state == STATE_UP else 0,
             R_BAR2: BAR2_BUS if self.state == STATE_UP else 0,
             R_LINKSTS: self.linksts, R_DEVCTL: self.devctl, R_EPCMD: self.epcmd,
             R_DART_TTBR: self.dart_ttbr, R_DART_CHECK: self.dart_check, R_CHIPID: self.chipid,
             R_CMD_COUNT: self.cmd_count}.get(off, 0)
        return v & 0xFFFFFFFF

    def reg_write(self, off, val):
        if off == R_CMD:
            self.last_cmd, self.cmd_count = val, self.cmd_count + 1
            self.log(f'CMD {val:#x} (state {self.state})')
            if val == CMD_POWER_UP:
                self.power_up()
            elif val == CMD_POWER_DOWN:
                self.power_down()
            else:
                self.log(f'CMD {val:#x} ignored (unknown)')
            self.seq += 1
        elif off == R_CFG_OFF:
            self.cfg_off = val & 0xFFF
            self.cfg_err = CFG_OK if val == self.cfg_off and not val & 3 else CFG_BAD_OFFSET
        elif off == R_CFG_DATA:
            self._cfg_write(self.cfg_off, val)
        elif off == R_BAR0WIN:
            self._cfg_write(wu.BAR0_WINDOW_CFG, val)
        else:
            self.log(f'write {off:#x} = {val:#x} ignored (read-only)')

    def _cfg_read(self, off):
        self.cfg_reads += 1
        if self.state != STATE_UP or self.up is None:
            self.cfg_err = CFG_NOT_UP
            return 0xFFFFFFFF
        if off & 3 or off >= 0x1000:
            self.cfg_err = CFG_BAD_OFFSET
            return 0xFFFFFFFF
        with self.up.guarded() as g:
            v = self.up.m.r32(self.up.ecfg(off))
        self.cfg_err = CFG_EXCEPTION if g['exc'] else CFG_OK
        return 0xFFFFFFFF if g['exc'] else v

    def _cfg_write(self, off, val):
        self.cfg_writes += 1
        if self.state != STATE_UP or self.up is None:
            self.cfg_err = CFG_NOT_UP
            return
        if off & 3 or off >= 0x1000:
            self.cfg_err = CFG_BAD_OFFSET
            return
        up = self.up
        cur = 0
        if off == wu.EP_LINKCTL_CFG:
            with up.guarded():
                cur = up.m.r32(up.ecfg(off))
        v = cfg_write_policy(off, val, cur, self.devctl_off)
        if v is None:
            self.cfg_refused += 1
            self.cfg_err = CFG_NOT_ALLOWED
            if self.cfg_refused <= 16:
                self.log(f'config write refused off={off:#x} val={val:#x}')
            return
        with up.guarded() as g:
            up.m.w32(up.ecfg(off), v)
        if off == 0x04:
            self.epcmd = v & 0xFFFF
        if off == self.devctl_off:
            self.devctl = v & 0xFFFF
        self.cfg_err = CFG_EXCEPTION if g['exc'] else CFG_OK
        if self.cfg_writes <= 16 or off != wu.BAR0_WINDOW_CFG:
            self.log(f'config write off={off:#x} val={val:#x} -> {v:#x} err={self.cfg_err}')

    # HOOK callbacks (m1n1 hv: read(addr, width_bits) -> int, write(addr, value, width_bits)).
    # They must never raise: an exception would drop the hypervisor into its debug shell.
    def hook_read(self, addr, width):
        try:
            off = addr - CTRL_IPA
            base = off & ~3
            if width == 64:
                return self.reg_read(base) | (self.reg_read(base + 4) << 32)
            v = self.reg_read(base) >> (8 * (off & 3))
            return v & ((1 << width) - 1)
        except Exception as e:
            self.log(f'hook read {addr:#x}/{width} raised {type(e).__name__}: {e}')
            return (1 << width) - 1 if width < 64 else 0xFFFFFFFFFFFFFFFF

    def hook_write(self, addr, value, width):
        try:
            off = addr - CTRL_IPA
            if isinstance(value, (list, tuple)):
                value = value[0]
            if width == 32 and not off & 3:
                self.reg_write(off, value & 0xFFFFFFFF)
            elif width == 64 and not off & 7:
                self.reg_write(off, value & 0xFFFFFFFF)
                self.reg_write(off + 4, (value >> 32) & 0xFFFFFFFF)
            else:
                self.log(f'write {off:#x}/{width} ignored (32-bit aligned only)')
        except Exception as e:
            self.log(f'hook write {addr:#x}/{width} raised {type(e).__name__}: {e}')
            self.state, self.error = STATE_FAILED, ERR['ERROR']

    def summary(self):
        return {'state': self.state, 'error': ERRORS.get(self.error), 'avail': self.avail, 'seq': self.seq,
                'cfg_reads': self.cfg_reads, 'cfg_writes': self.cfg_writes, 'cfg_refused': self.cfg_refused}


def make_run_dir():
    out = HERE / 'runs'
    out.mkdir(parents=True, exist_ok=True)
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    return Path(tempfile.mkdtemp(prefix=f'run-{stamp}-', dir=str(out)))
