#!/usr/bin/env python3
"""S240 Wi-Fi milestone 1: BCM4378 power-on + PCIe port-0 config-space probe.

Run ONLY against an idle, freshly booted m1n1 proxy (no chainload, no guest, nothing else on
the serial port).  Default mode prints the plan and touches nothing; `--run` executes:

  1. guards: serial port free (lsof), ADT compatible contains j274, ADT addresses equal the
     J274 values recorded offline (refuse on any mismatch)
  2. p.pcie_init()            (same proxy call as usb-s174/guest_map.py:7)
  3. SMC key gP0d <- 0x800001 (the gP0d write of proxyclient/experiments/pcie_enable_devices.py:13-18
                               and m1n1 C dcp.c smc_write_u32(..., 0x800001)).  That script ALSO
                               writes gP1a <- 1 (line 16); this probe deliberately does NOT
                               (open hypothesis, README section 6)
  4. port-0 only PERST#/REFCLK/LTSSM bring-up in the UEFI AppleSiliconPciPlatformDxe.c order
     (itself a port of Linux pcie-apple.c), plus an explicit controller-side reset (PORT_PERST
     bit 0 cleared) while the PERST# GPIO is asserted, because m1n1 pcie_init already set that
     bit (m1n1 src/pcie.c:592)
  5. read bus 1 dev 0 fn 0/1 config space: IDs, class, BAR sizes, PCIe DevCtl/LnkSta
  6. ALWAYS (finally), each step independently with its own time limit and error record:
     restore root-port bus numbers, assert PERST#, clear PORT_PERST_OFF, gP0d <- 0x800000,
     SMC quiesce, check link down, compare the port-1 (FL1100) snapshot

Never touches storage (no NVMe/ANS calls).  Never writes FL1100 (port 1) registers: every MMIO
write goes through an exact-address allowlist.  This is a hardware observation tool; a PASS here
is not a driver, not a Windows result, and not a boot result.
"""
import argparse
import contextlib
import datetime
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DEFAULT_PROXYCLIENT = ROOT / 'm1n1_windows' / 'proxyclient'
DEFAULT_DEVICE = '/dev/cu.usbmodemC07HL05SQ6NY1'
DEFAULT_EXTRA_PORTS = ['/dev/cu.usbmodemC07HL05SQ6NY3']

# J274 values. FACT: read offline from the Mac mini ADT dump
# experiments/tahoe-afk-20260906/current.adt with m1n1_windows/proxyclient/m1n1/adt.py.
EXPECT = {
    'ecam': 0x690000000,        # /arm-io/apcie reg 0
    'rc': 0x680000000,          # /arm-io/apcie reg 1
    'port0': 0x681000000,       # /arm-io/apcie reg 6
    'port0_phy': 0x680084000,   # /arm-io/apcie reg 8 (== rc + 0x84000, UEFI CORE_PHY_DEFAULT_BASE(0))
    'port1': 0x682000000,       # /arm-io/apcie reg 11 (FL1100 USB-A port; read-only here)
    'gpio': 0x23c100000,        # /arm-io/gpio reg 0
    'gpio_phandle': 106,        # /arm-io/gpio AAPL,phandle == function-perst phandle
    'perst_pin': 0x98,          # pci-bridge0 function-perst = 106:GPIO(0x98, 0x0)
    'port1_perst_pin': 0x99,    # pci-bridge1 function-perst = 106:GPIO(0x99, 0x0)
    'smc': 0x23e400000,         # /arm-io/smc reg 0
}

SMC_WIFI_KEY = 'gP0d'           # 'gP' + 2 hex digits of SMC GPIO 13 (experiments/smc.py:20-25)
SMC_GPIO_ON = 0x800001          # pcie_enable_devices.py:14, m1n1 dcp.c smc_write_u32(.., 0x800001)
SMC_GPIO_OFF = 0x800000         # experiments/smc.py:28 (commented 0x800000 | 0x0)
# get_key_info() returns (length, 4-char type, flags) (proxyclient m1n1/fw/smc.py:134-139).
# Only plain 32-bit integer-like types are accepted; anything else refuses before the write.
SMC_KEY_TYPES = ('ui32', 'hex_')

# Port registers (UEFI Include AppleSiliconPciPlatformDxe.h; same names as Linux pcie-apple.c)
PORT_LTSSMCTL, LTSSMCTL_START = 0x080, 0x1
PORT_LINKSTS, LINKSTS_UP, LINKSTS_BUSY = 0x208, 0x1, 0x4
PORT_APPCLK, APPCLK_EN, APPCLK_CGDIS = 0x800, 0x1, 0x100
PORT_STATUS, STATUS_READY = 0x804, 0x1
PORT_REFCLK, REFCLK_EN, REFCLK_CGDIS = 0x810, 0x1, 0x100
PORT_PERST, PERST_OFF = 0x814, 0x1
PHY_LANE_CFG, REFCLK0REQ, REFCLK1REQ, REFCLK0ACK, REFCLK1ACK, REFCLKEN = 0x0, 0x1, 0x2, 0x4, 0x8, 0x600
PHY_LANE_CTL, CFGACC = 0x4, 0x8000

# Apple GPIO pin register (AppleEmbeddedGpioControllerDxe.h:23-39)
GPIO_DATA, GPIO_MODE_MASK, GPIO_MODE_OUT, GPIO_PERIPH, GPIO_INPUT_EN = 0x1, 0xE, 0x2, 0x60, 0x200

# m1n1 exception guard (proxy.py GUARD): SKIP=1, SILENT=0x100
GUARD_OFF, GUARD_SKIP_SILENT = 0, 0x101

WIFI_ID = 0x442514E4            # 14e4:4425 (recovered-j274.dts wifi@0,0)
BT_ID = 0x5F6914E4              # 14e4:5f69 (recovered-j274.dts bluetooth@0,1)
APPLE_VENDOR = 0x106B

PORT1_SNAPSHOT_OFFS = (PORT_LINKSTS, PORT_APPCLK, PORT_STATUS, PORT_REFCLK, PORT_PERST)

# Per-step time limits (s) of the power-down sequence. Each step has its own alarm, so a stall in
# one step cannot use up the time of the next one.
POWER_DOWN_STEP_SECONDS = {'root_bus_restore': 5, 'perst_gpio_assert': 5, 'perst_off_clear': 5,
                           'smc_off': 10, 'smc_stop': 10, 'link_check': 5, 'port1_snapshot': 10}

# --- Wi-Fi bridge-window prediction (HYPOTHESIS; pure arithmetic, no hardware access) ---
# FACT: our Mu T8103 build has the 32-bit PCI aperture at bus 0xC0000000, CPU = bus + 0x600000000
#   (apple_silicon_platforms_mu/Silicon/Apple/T810XFamilyPkg/T810XFamilyPkg.dsc.inc:85-87).
# FACT: the host bridge allocates bottom-up from the aperture base
#   (MU_BASECORE/MdeModulePkg/Bus/Pci/PciHostBridgeDxe/PciHostBridge.c:779-827, AllocateResource).
# FACT: children are ordered by descending alignment (PciBusDxe/PciResourceSupport.c:93-127) and a
#   64-bit non-prefetchable BAR behind a P2P bridge is degraded to MEM32 (same file :1164-1172).
# FACT: today root port 1 (FL1100) owns bus 0xC0000000..0xC00FFFFF (usb-s232 HANDOFF section 9).
# HYPOTHESIS: the real window is whatever UEFI assigns once Wi-Fi is powered before UEFI; it has to
#   be read back from config space (S235W-style ECAM dump). This only predicts it from BAR sizes.
LOW_MMIO_BUS_BASE = 0xC0000000
LOW_MMIO_CPU_OFFSET = 0x600000000
BRIDGE_MEM_GRANULARITY = 0x100000
FL1100_WINDOW_LEN = 0x100000


class Refusal(Exception):
    """A guard refused to continue. Nothing (or nothing more) was written."""


class PhaseTimeout(TimeoutError):
    pass


def ecam_addr(ecam, bus, dev, fn, off=0):
    return ecam + (bus << 20) + (dev << 15) + (fn << 12) + off


# ----------------------------------------------------------------------------- guards

def check_serial_free(ports, runner=subprocess.run):
    """Refuse unless lsof positively reports that nobody holds any of the ports."""
    for port in ports:
        try:
            r = runner(['/usr/sbin/lsof', '-t', port], capture_output=True, timeout=10)
        except (OSError, subprocess.TimeoutExpired) as e:
            raise Refusal(f'cannot verify serial ownership of {port}: {type(e).__name__}')
        if r.returncode != 1 or r.stdout or r.stderr:
            raise Refusal(f'serial port {port} is owned or ownership is uncertain '
                          f'(lsof rc={r.returncode}, pids={r.stdout!r})')


def _func(prop):
    if prop is None or isinstance(prop, str):
        raise Refusal(f'ADT function property missing/short: {prop!r}')
    return prop.phandle, list(prop.args)


def adt_facts(adt):
    """Extract everything the probe needs from a (real or fake) ADT object."""
    try:
        compat = [str(c) for c in adt.compatible]
        ap = adt['/arm-io/apcie']
        b0 = adt['/arm-io/apcie/pci-bridge0']
        b1 = adt['/arm-io/apcie/pci-bridge1']
        gpio = adt['/arm-io/gpio']
        p0, a0 = _func(b0.getprop('function-perst'))
        p1, a1 = _func(b1.getprop('function-perst'))
        wlan_compat = [str(c) for c in adt['/arm-io/apcie/pci-bridge0/wlan'].compatible]
        return {
            'compatible': compat,
            'ecam': ap.get_reg(0)[0], 'rc': ap.get_reg(1)[0],
            'port0': ap.get_reg(6)[0], 'port0_phy': ap.get_reg(8)[0], 'port1': ap.get_reg(11)[0],
            'gpio': gpio.get_reg(0)[0], 'gpio_phandle': gpio.getprop('AAPL,phandle'),
            'perst_phandle': p0, 'perst_pin': a0[0], 'perst_polarity': a0[1],
            'port1_perst_phandle': p1, 'port1_perst_pin': a1[0],
            'smc': adt['/arm-io/smc'].get_reg(0)[0],
            'wlan_compatible': wlan_compat,
            'perst_to_config_ms': int(b0.getprop('perst-to-config', 100) or 100),
            't_refclk_to_perst_us': int(b0.getprop('t-refclk-to-perst', 100) or 100),
        }
    except Refusal:
        raise
    except Exception as e:  # missing node/property => not the machine we know
        raise Refusal(f'ADT lookup failed: {type(e).__name__}: {e}')


def validate_facts(f):
    if not any('j274' in c.lower() for c in f['compatible']):
        raise Refusal(f'wrong target, compatible={f["compatible"]}')
    for k, v in EXPECT.items():
        if f.get(k) != v:
            raise Refusal(f'ADT {k}={f.get(k)!r} differs from recorded J274 value {v:#x}')
    if f['perst_phandle'] != f['gpio_phandle'] or f['port1_perst_phandle'] != f['gpio_phandle']:
        raise Refusal('function-perst does not point at /arm-io/gpio')
    if not any('bcm4378' in c for c in f['wlan_compatible']):
        raise Refusal(f'pci-bridge0/wlan is not bcm4378: {f["wlan_compatible"]}')
    if not (1 <= f['perst_to_config_ms'] <= 1000 and 1 <= f['t_refclk_to_perst_us'] <= 10000):
        raise Refusal('ADT reset timing out of range')


# ----------------------------------------------------------------------------- MMIO

class GuardedMmio:
    """read32 anywhere the probe asks; write32 only to an exact-address allowlist."""

    def __init__(self, p, allowed):
        self.p = p
        self.allowed = frozenset(allowed)
        self.writes = []

    def r32(self, addr):
        return self.p.read32(addr) & 0xFFFFFFFF

    def w32(self, addr, val):
        if addr not in self.allowed:
            raise Refusal(f'write to {addr:#x} is outside the S240 allowlist (blocked)')
        self.writes.append((addr, val & 0xFFFFFFFF))
        self.p.write32(addr, val & 0xFFFFFFFF)

    def rmw(self, addr, clear, set_):
        old = self.r32(addr)
        new = (old & ~clear) | set_
        self.w32(addr, new)
        return old, self.r32(addr)


def build_allowlist(f):
    port0, phy, ecam = f['port0'], f['port0_phy'], f['ecam']
    allowed = {port0 + o for o in (PORT_LTSSMCTL, PORT_APPCLK, PORT_REFCLK, PORT_PERST)}
    allowed |= {phy + PHY_LANE_CFG, phy + PHY_LANE_CTL}
    allowed.add(f['gpio'] + 4 * f['perst_pin'])
    allowed.add(ecam_addr(ecam, 0, 0, 0, 0x18))                  # root port 0 bus numbers only
    for fn in (0, 1):                                            # endpoint BARs (sizing+restore)
        allowed |= {ecam_addr(ecam, 1, 0, fn, 0x10 + 4 * i) for i in range(6)}
    return allowed


def _align_up(v, a):
    return (v + a - 1) // a * a


def predict_wifi_window(functions):
    """HYPOTHESIS: where UEFI would put the root-port-0 (Wi-Fi) memory window once Wi-Fi is
    powered before UEFI, from the BAR sizes measured here.  See the constants above for the
    rules used.  Guest RAM in the low window has guest-physical == bus address, so this window
    must be kept out of guest RAM exactly like the FL1100 one (usb-s236 badmemorylist)."""
    mem, pmem64, io = [], [], []
    for fn in sorted(functions):
        info = functions[fn]
        if not info.get('present', True):
            continue
        bars = info.get('bars')
        if not isinstance(bars, list):
            return {'available': False, 'hypothesis': True,
                    'reason': f'fn{fn} BAR sizes not measured ({bars})'}
        for b in bars:
            size = int(b['size'], 16)
            if not size:
                continue
            if b['type'] == 'io':
                io.append(size)
            elif b['type'] == 'mem64' and b.get('prefetch'):
                pmem64.append(size)
            else:
                mem.append(size)
    out = {'available': True, 'hypothesis': True,
           'note': 'prediction from EDK2 allocation rules; the real window must be read from config '
                   'space after UEFI enumerates with Wi-Fi powered',
           'mem32_bar_sizes': [hex(s) for s in sorted(mem, reverse=True)],
           'pmem64_bar_sizes': [hex(s) for s in pmem64], 'io_bar_sizes': [hex(s) for s in io]}
    if pmem64:
        out['pmem64_note'] = ('64-bit prefetchable BARs go to the 64-bit aperture at 0x400000000 '
                              '(T810XFamilyPkg.dsc.inc:92), which is not in the low guest-RAM window')
    if io:
        out['io_note'] = 'I/O BARs cannot be assigned (no I/O aperture, AppleSiliconPciHostBridgeLib.c)'
    if not mem:
        out['wifi_window_bus'] = None
        return out
    off = 0
    for s in sorted(mem, reverse=True):
        off = _align_up(off, s) + s
    length = _align_up(off, BRIDGE_MEM_GRANULARITY)
    align = max(max(mem), BRIDGE_MEM_GRANULARITY)
    if align > FL1100_WINDOW_LEN:
        wifi = LOW_MMIO_BUS_BASE
        fl = _align_up(wifi + length, FL1100_WINDOW_LEN)
        order = 'Wi-Fi window first (larger alignment)'
    else:
        fl = LOW_MMIO_BUS_BASE
        wifi = fl + FL1100_WINDOW_LEN
        order = 'equal 1 MiB alignment: order follows enumeration/insertion (extra hypothesis)'
    out.update({
        'window_alignment': hex(align), 'window_length': hex(length), 'order': order,
        'wifi_window_bus': [hex(wifi), hex(wifi + length - 1)],
        'wifi_window_cpu': [hex(wifi + LOW_MMIO_CPU_OFFSET), hex(wifi + length - 1 + LOW_MMIO_CPU_OFFSET)],
        'wifi_badmemorylist_pfns': [hex(wifi >> 12), hex((wifi + length - 1) >> 12)],
        'fl1100_window_bus': [hex(fl), hex(fl + FL1100_WINDOW_LEN - 1)],
        'fl1100_moves': fl != LOW_MMIO_BUS_BASE,
    })
    return out


# ----------------------------------------------------------------------------- probe

class WifiProbe:
    def __init__(self, p, facts, smc_factory, opts, sleep=time.sleep, clock=time.monotonic,
                 phase=None, log=print):
        self.p, self.f, self.smc_factory, self.o = p, facts, smc_factory, opts
        self.sleep, self.clock, self.log = sleep, clock, log
        self.phase = phase or (lambda name, seconds: contextlib.nullcontext())
        self.m = None
        self.res = {'experiment': 'S240-wifi-m1', 'writes_to_storage': False,
                    'hardware_success_claim': False, 'steps': [], 'notes': []}
        self.smc = None
        self.smc_on_attempted = False
        self.port0_touched = False
        self.root_bus_saved = None

    # -- helpers
    def step(self, msg, **kw):
        self.res['steps'].append(dict(msg=msg, **kw))
        extra = ' '.join(f'{k}={v:#x}' if isinstance(v, int) and not isinstance(v, bool) else f'{k}={v}'
                         for k, v in kw.items())
        self.log(f'[s240] {msg} {extra}'.rstrip())

    def poll(self, addr, mask, want, timeout_ms, interval_ms=0.1):
        end = self.clock() + timeout_ms / 1000.0
        while True:
            v = self.m.r32(addr)
            if (v & mask) == want:
                return True, v
            if self.clock() >= end:
                return False, v
            self.sleep(interval_ms / 1000.0)

    @contextlib.contextmanager
    def guarded(self):
        """m1n1 exception guard around config-space access; reports exceptions taken."""
        before = self.p.get_exc_count()
        self.p.set_exc_guard(GUARD_SKIP_SILENT)
        box = {'exc': 0}
        try:
            yield box
        except BaseException:
            try:  # best effort; never mask the original error
                self.p.set_exc_guard(GUARD_OFF)
            except Exception:
                pass
            raise
        self.p.set_exc_guard(GUARD_OFF)
        box['exc'] = self.p.get_exc_count() - before

    def cfg(self, bus, dev, fn, off):
        return self.m.r32(ecam_addr(self.f['ecam'], bus, dev, fn, off))

    def port1_snapshot(self):
        f = self.f
        snap = {f'port1+{o:#x}': self.m.r32(f['port1'] + o) for o in PORT1_SNAPSHOT_OFFS}
        snap['gpio_port1_perst'] = self.m.r32(f['gpio'] + 4 * f['port1_perst_pin'])
        with self.guarded() as g:
            snap['rootport1_id'] = self.cfg(0, 1, 0, 0x00)
            snap['rootport1_bus'] = self.cfg(0, 1, 0, 0x18)
        snap['exc'] = g['exc']
        return snap

    # -- main sequence
    def run(self):
        try:
            validate_facts(self.f)
        except Refusal as e:
            self.res['verdict'] = f'REFUSED: {e}'
            self.res['power_down'] = {'confirmed': True, 'nothing_was_done': True}
            return self.res
        self.m = GuardedMmio(self.p, build_allowlist(self.f))
        try:
            self._sequence()
        except Refusal as e:
            self.res['verdict'] = f'REFUSED: {e}'
        except Exception as e:
            self.res['verdict'] = f'ERROR: {type(e).__name__}: {e}'
        finally:
            self._power_down()
            self.res['mmio_writes'] = [[hex(a), hex(v)] for a, v in self.m.writes]
        return self.res

    def _sequence(self):
        f, o = self.f, self.o
        with self.phase('pcie_init', 20):
            rc = self.p.pcie_init()
        self.step('pcie_init', rc=rc)
        if rc != 0:
            raise Refusal(f'pcie_init returned {rc:#x}; nothing powered')

        with self.phase('preflight', 15):
            with self.guarded() as g:
                rp_id = self.cfg(0, 0, 0, 0x00)
                rp_hdr = self.cfg(0, 0, 0, 0x0C)
            if g['exc'] or (rp_id & 0xFFFF) != APPLE_VENDOR or ((rp_hdr >> 16) & 0x7F) != 1:
                raise Refusal(f'root port 0 config unexpected (id={rp_id:#x} hdr={rp_hdr:#x} exc={g["exc"]})')
            st = self.m.r32(f['port0'] + PORT_STATUS)
            link0 = self.m.r32(f['port0'] + PORT_LINKSTS)
            self.res['port1_before'] = self.port1_snapshot()
        self.step('root port 0', id=rp_id, port_status=st, linksts=link0)
        if not (st & STATUS_READY):
            raise Refusal('port 0 not READY after pcie_init')
        link_was_up = bool(link0 & LINKSTS_UP)
        self.res['link_was_up_before_power'] = link_was_up
        if link_was_up:
            self.res['notes'].append('port 0 link was already up before SMC power-on (not a fresh proxy?)')

        with self.phase('smc-on', 20):
            self.smc = self.smc_factory(f['smc'])
            self.smc.start()
            info = self.smc.key_info(SMC_WIFI_KEY)
            self.step('smc key info', key=SMC_WIFI_KEY, info=str(info))
            if (not isinstance(info, (tuple, list)) or len(info) < 2 or info[0] != 4
                    or info[1] not in SMC_KEY_TYPES):
                raise Refusal(f'SMC key {SMC_WIFI_KEY} info {info!r} is not a 4-byte '
                              f'{"/".join(SMC_KEY_TYPES)} key')
            self.smc_on_attempted = True
            self.smc.write32(SMC_WIFI_KEY, SMC_GPIO_ON)
        self.step('smc write', key=SMC_WIFI_KEY, value=SMC_GPIO_ON)
        self.sleep(o.power_settle_ms / 1000.0)

        if not link_was_up:
            with self.phase('port0-bringup', 15):
                self._bring_up_port0()

        with self.phase('link-wait', 10):
            up, ls = self.poll(f['port0'] + PORT_LINKSTS, LINKSTS_UP, LINKSTS_UP, o.link_timeout_ms)
        self.step('port 0 link', up=up, linksts=ls)
        self.res['link_up'] = up
        if not up:
            self.res['verdict'] = 'LINK_DOWN'
            return

        with self.phase('config-read', 30):
            self._read_endpoint()

    def _bring_up_port0(self):
        """UEFI AppleSiliconPciePlatformDxeSetupPciePort, port 0 only, T8103 branch."""
        f, m = self.f, self.m
        port, phy = f['port0'], f['port0_phy']
        gpio_reg = f['gpio'] + 4 * f['perst_pin']
        self.port0_touched = True
        m.rmw(port + PORT_APPCLK, 0, APPCLK_EN)
        orig_gpio = m.r32(gpio_reg)
        self.res['perst_gpio_original'] = hex(orig_gpio)
        self._perst(assert_=True)
        # m1n1 pcie_init already set PORT_PERST bit 0 (= RESET_DIS, m1n1 src/pcie.c:592), so the
        # later PERST_OFF set alone would be a no-op. Clear it here so the controller-side reset
        # really is asserted around REFCLK setup, as in a cold UEFI/Linux bring-up.
        # HYPOTHESIS: whether this matters for link training is not known.
        before, after = m.rmw(port + PORT_PERST, PERST_OFF, 0)
        self.res['port0_perst_reg'] = {'before': hex(before), 'after_clear': hex(after)}
        self.step('port 0 controller reset asserted', before=before, after=after)
        m.rmw(phy + PHY_LANE_CTL, 0, CFGACC)
        m.rmw(phy + PHY_LANE_CFG, 0, REFCLK0REQ)
        ok, v = self.poll(phy + PHY_LANE_CFG, REFCLK0ACK, REFCLK0ACK, 50)
        if not ok:
            raise RuntimeError(f'REFCLK0ACK timeout (lane_cfg={v:#x})')
        m.rmw(phy + PHY_LANE_CFG, 0, REFCLK1REQ)
        ok, v = self.poll(phy + PHY_LANE_CFG, REFCLK1ACK, REFCLK1ACK, 50)
        if not ok:
            raise RuntimeError(f'REFCLK1ACK timeout (lane_cfg={v:#x})')
        m.rmw(phy + PHY_LANE_CTL, CFGACC, 0)
        m.rmw(phy + PHY_LANE_CFG, 0, REFCLKEN)
        m.rmw(port + PORT_REFCLK, 0, REFCLK_EN)
        self.sleep(f['t_refclk_to_perst_us'] / 1e6)
        m.rmw(port + PORT_PERST, 0, PERST_OFF)
        self._perst(assert_=False)
        self.sleep(f['perst_to_config_ms'] / 1000.0)
        ok, v = self.poll(port + PORT_STATUS, STATUS_READY, STATUS_READY, 250)
        if not ok:
            raise RuntimeError(f'port 0 READY timeout (status={v:#x})')
        m.w32(port + PORT_LTSSMCTL, LTSSMCTL_START)
        self.step('port 0 LTSSM started')

    def _perst(self, assert_):
        """PERST# is active-low (dts reset-gpios flag 1): assert = drive 0."""
        reg = self.f['gpio'] + 4 * self.f['perst_pin']
        data = 0 if assert_ else GPIO_DATA
        self.m.rmw(reg, GPIO_PERIPH | GPIO_MODE_MASK | GPIO_INPUT_EN | GPIO_DATA, GPIO_MODE_OUT | data)

    def _read_endpoint(self):
        f, m, o = self.f, self.m, self.o
        port = f['port0']
        # clear clock-gating disables like UEFI/Linux after link-up
        m.rmw(port + PORT_REFCLK, REFCLK_CGDIS, 0)
        m.rmw(port + PORT_APPCLK, APPCLK_CGDIS, 0)
        bus_addr = ecam_addr(f['ecam'], 0, 0, 0, 0x18)
        with self.guarded() as g:
            orig = m.r32(bus_addr)
            self.root_bus_saved = orig
            want = (orig & 0xFF000000) | (1 << 16) | (1 << 8)
            if orig != want:
                m.w32(bus_addr, want)
            self.res['rootport0_bus'] = {'original': hex(orig), 'set': hex(want)}
            self.res['rootport0_pcie'] = self._pcie_cap(0, 0, 0)
        if g['exc']:
            raise RuntimeError(f'{g["exc"]} exception(s) touching root port 0 config')

        # CRS / not-ready retry on the vendor ID, bounded
        end = self.clock() + o.config_retry_ms / 1000.0
        while True:
            with self.guarded() as g:
                id0 = self.cfg(1, 0, 0, 0x00)
            if g['exc'] == 0 and id0 not in (0xFFFFFFFF, 0) and (id0 & 0xFFFF) != 0x0001:
                break
            if self.clock() >= end:
                self.res['verdict'] = f'NO_CONFIG_RESPONSE (id={id0:#x}, exc={g["exc"]})'
                return
            self.sleep(0.01)

        fns = {}
        for fn in (0, 1):
            fns[fn] = self._read_function(fn)
        self.res['functions'] = fns
        self.res['bridge_window_prediction'] = predict_wifi_window(fns)
        ok = fns[0]['id'] == hex(WIFI_ID) and fns[1]['id'] == hex(BT_ID)
        self.res['verdict'] = 'PASS_IDS' if ok else 'ID_MISMATCH'

    def _read_function(self, fn):
        o = self.o
        with self.guarded() as g:
            fid = self.cfg(1, 0, fn, 0x00)
            cmdsts = self.cfg(1, 0, fn, 0x04)
            clsrev = self.cfg(1, 0, fn, 0x08)
            hdr = self.cfg(1, 0, fn, 0x0C)
            subsys = self.cfg(1, 0, fn, 0x2C)
            info = {'id': hex(fid), 'vendor': hex(fid & 0xFFFF), 'device': hex(fid >> 16),
                    'class': hex(clsrev >> 8), 'revision': hex(clsrev & 0xFF),
                    'header_type': hex((hdr >> 16) & 0xFF), 'command': hex(cmdsts & 0xFFFF),
                    'status': hex(cmdsts >> 16), 'subsystem': hex(subsys)}
            if fid == 0xFFFFFFFF:
                info['present'] = False
                return info
            info['present'] = True
            info['pcie'] = self._pcie_cap(1, 0, fn)
        read_exc = g['exc']
        info['exceptions_during_read'] = read_exc
        if not o.size_bars:
            info['bars'] = 'skipped (--no-bar-sizing)'
        elif read_exc:
            info['bars'] = 'skipped (exception during config read)'
        elif cmdsts & 0x3:
            info['bars'] = 'skipped (memory/io decode already enabled)'
        elif (hdr >> 16) & 0x7F != 0:
            info['bars'] = 'skipped (not a type-0 header)'
        else:
            with self.guarded() as g2:
                info['bars'] = self._size_bars(fn)
            info['exceptions_during_sizing'] = g2['exc']
        self.step(f'bus1 fn{fn}', id=fid, class_rev=clsrev)
        return info

    def _size_bars(self, fn):
        m = self.m
        bars, i = [], 0
        while i < 6:
            a = ecam_addr(self.f['ecam'], 1, 0, fn, 0x10 + 4 * i)
            orig = m.r32(a)
            if orig & 1:                                     # I/O BAR
                m.w32(a, 0xFFFFFFFF); v = m.r32(a); m.w32(a, orig)
                size = ((~(v & ~0x3)) + 1) & 0xFFFF if v & ~0x3 else 0
                bars.append({'bar': i, 'type': 'io', 'size': hex(size), 'orig': hex(orig)})
                i += 1
                continue
            kind = (orig >> 1) & 0x3
            if kind == 2 and i < 5:                          # 64-bit memory BAR
                ah = a + 4
                orig_hi = m.r32(ah)
                m.w32(a, 0xFFFFFFFF); m.w32(ah, 0xFFFFFFFF)
                lo, hi = m.r32(a), m.r32(ah)
                m.w32(a, orig); m.w32(ah, orig_hi)
                mask = (hi << 32) | (lo & ~0xF)
                size = ((~mask) + 1) & (2 ** 64 - 1) if mask else 0
                bars.append({'bar': i, 'type': 'mem64', 'prefetch': bool(orig & 8), 'size': hex(size),
                             'orig': hex((orig_hi << 32) | orig),
                             'restored': m.r32(a) == orig and m.r32(ah) == orig_hi})
                i += 2
                continue
            m.w32(a, 0xFFFFFFFF); v = m.r32(a); m.w32(a, orig)
            size = ((~(v & ~0xF)) + 1) & 0xFFFFFFFF if v & ~0xF else 0
            bars.append({'bar': i, 'type': 'mem32', 'prefetch': bool(orig & 8), 'size': hex(size),
                         'orig': hex(orig), 'restored': m.r32(a) == orig})
            i += 1
        return bars

    def _pcie_cap(self, bus, dev, fn):
        ptr = self.cfg(bus, dev, fn, 0x34) & 0xFC
        for _ in range(48):
            if ptr < 0x40:
                return None
            v = self.cfg(bus, dev, fn, ptr)
            if v & 0xFF == 0x10:
                devctl = self.cfg(bus, dev, fn, ptr + 8) & 0xFFFF
                lnk = self.cfg(bus, dev, fn, ptr + 0x10)
                lnksta = lnk >> 16
                return {'cap_offset': hex(ptr), 'devctl': hex(devctl),
                        'relaxed_ordering': bool(devctl & 0x10), 'no_snoop': bool(devctl & 0x800),
                        'link_speed_code': lnksta & 0xF, 'link_width': (lnksta >> 4) & 0x3F}
            ptr = (v >> 8) & 0xFC
        return None

    def _pd_step(self, pd, name, fn):
        """Run one power-down step on its own: own time limit, own error record. A failure here
        never prevents the following steps from being attempted."""
        try:
            with self.phase(f'power-down:{name}', POWER_DOWN_STEP_SECONDS[name]):
                value = fn()
        except Exception as e:
            pd['steps'][name] = 'error'
            pd['errors'][name] = f'{type(e).__name__}: {e}'
            return False, None
        pd['steps'][name] = 'ok'
        return True, value

    def _restore_root_bus(self):
        with self.guarded():
            self.m.w32(ecam_addr(self.f['ecam'], 0, 0, 0, 0x18), self.root_bus_saved)

    def _power_down(self):
        f = self.f
        pd = {'steps': {}, 'errors': {}, 'perst_asserted': False, 'controller_reset_asserted': False,
              'smc_off_written': False, 'smc_stopped': False}
        powered = self.port0_touched or self.smc_on_attempted
        required = []
        if self.root_bus_saved is not None:
            required.append('root_bus_restore')
            pd['root_bus_restored'], _ = self._pd_step(pd, 'root_bus_restore', self._restore_root_bus)
        if powered:
            required += ['perst_gpio_assert', 'perst_off_clear']
            pd['perst_asserted'], _ = self._pd_step(pd, 'perst_gpio_assert',
                                                    lambda: self._perst(assert_=True))
            pd['controller_reset_asserted'], _ = self._pd_step(
                pd, 'perst_off_clear', lambda: self.m.rmw(f['port0'] + PORT_PERST, PERST_OFF, 0))
        if self.smc is not None:
            if self.smc_on_attempted:
                required.append('smc_off')
                pd['smc_off_written'], _ = self._pd_step(
                    pd, 'smc_off', lambda: self.smc.write32(SMC_WIFI_KEY, SMC_GPIO_OFF))
            required.append('smc_stop')
            pd['smc_stopped'], _ = self._pd_step(pd, 'smc_stop', self.smc.stop)
        if powered:
            required.append('link_check')
            ok, r = self._pd_step(pd, 'link_check',
                                  lambda: self.poll(f['port0'] + PORT_LINKSTS, LINKSTS_UP, 0, 200))
            pd['link_down_after'] = r[0] if ok else None
            if ok:
                pd['linksts_after'] = hex(r[1])
        if 'port1_before' in self.res:
            ok, after = self._pd_step(pd, 'port1_snapshot', self.port1_snapshot)
            if ok:
                self.res['port1_after'] = after
                pd['port1_unchanged'] = after == self.res['port1_before']
            else:
                pd['port1_unchanged'] = None        # unknown: snapshot could not be taken
        pd['required_steps'] = required
        # Confirmed only if every required step succeeded AND (when anything was powered) the
        # link was actually seen down afterwards. A link that stays up is NOT a power-down.
        pd['confirmed'] = (all(pd['steps'].get(s) == 'ok' for s in required)
                           and (not powered or pd.get('link_down_after') is True))
        self.res['power_down'] = pd
        self.step('power down', confirmed=pd['confirmed'], errors=sorted(pd['errors']) or 'none',
                  port1_unchanged=pd.get('port1_unchanged', 'n/a'))


# ----------------------------------------------------------------------------- real proxy glue

class RealSmc:
    """Thin adapter over proxyclient m1n1.fw.smc.SMCClient (endpoint 0x20)."""

    def __init__(self, u, base):
        from m1n1.fw.smc import SMCClient
        self.client = SMCClient(u, base, None)
        self.ep = None

    def start(self):
        self.client.start()
        self.client.start_ep(0x20)
        self.ep = self.client.smcep

    def key_info(self, key):
        return self.ep.get_key_info(key)

    def write32(self, key, value):
        self.ep.write32(key, value)

    def stop(self):
        self.client.stop()        # AP->0x10 then IOP->0x10 == C rtkit_quiesce (smc_shutdown)


@contextlib.contextmanager
def alarm_phase(name, seconds):
    def handler(*_):
        raise PhaseTimeout(f'phase {name} exceeded {seconds}s')
    old = signal.signal(signal.SIGALRM, handler)
    signal.alarm(seconds)
    try:
        yield
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, old)


def connect_real(args):
    if str(args.proxyclient) not in sys.path:
        sys.path.insert(0, str(args.proxyclient))
    from m1n1.proxy import UartInterface, M1N1Proxy
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    with alarm_phase('connect', 30):
        iface = UartInterface(args.device)
        p = M1N1Proxy(iface, debug=False)
        bootstrap_port(iface, p)
        u = ProxyUtils(p)
        adt = u.adt
        facts = adt_facts(adt)
    return p, facts, (lambda base: RealSmc(u, base)), iface.dev.close


PLAN = """S240 plan (nothing executed; add --run on a fresh idle m1n1 proxy):
  guards : lsof on {ports}; ADT j274 + recorded addresses; FL1100 port 1 never written
  1 pcie_init (proxy)            2 SMC {key} <- {on:#x}  (settle {settle} ms)
  3 port 0 PERST#/REFCLK/LTSSM   4 read 1:0.0 and 1:0.1 config, size BARs (restore)
  5 finally (each step on its own, own time limit): restore root bus regs, PERST# low,
    PORT_PERST_OFF clear, SMC {key} <- {off:#x}, SMC quiesce, link-down check, port-1 compare
  then power-cycle the Mac mini before any launcher."""


def parse_args(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--run', action='store_true', help='actually talk to the proxy (default: print plan)')
    ap.add_argument('--device', default=os.environ.get('M1N1DEVICE', DEFAULT_DEVICE))
    ap.add_argument('--extra-port', action='append', default=None,
                    help='more serial ports that must be free (default: the diagnostic CDC)')
    ap.add_argument('--proxyclient', type=Path, default=DEFAULT_PROXYCLIENT)
    ap.add_argument('--power-settle-ms', type=int, default=100)
    ap.add_argument('--link-timeout-ms', type=int, default=1000)
    ap.add_argument('--config-retry-ms', type=int, default=1000)
    ap.add_argument('--no-bar-sizing', dest='size_bars', action='store_false')
    ap.add_argument('--out-dir', type=Path, default=HERE)
    a = ap.parse_args(argv)
    if a.extra_port is None:
        a.extra_port = list(DEFAULT_EXTRA_PORTS)
    for name, lo, hi in (('power_settle_ms', 0, 2000), ('link_timeout_ms', 10, 5000),
                         ('config_retry_ms', 10, 5000)):
        if not lo <= getattr(a, name) <= hi:
            ap.error(f'{name} must be in [{lo}, {hi}]')
    return a


EXIT = {'PASS_IDS': 0, 'LINK_DOWN': 1, 'ID_MISMATCH': 1, 'NO_CONFIG_RESPONSE': 1}
EXIT_ERROR, EXIT_REFUSED, EXIT_POWER_UNCONFIRMED, EXIT_PORT1, EXIT_CONNECT = 2, 3, 4, 5, 6


def make_run_dir(out_dir, now=None):
    """Unique run directory, created BEFORE any proxy access (a same-second second run gets its
    own directory; an unwritable out-dir refuses before anything is touched)."""
    stamp = (now or datetime.datetime.now()).strftime('%Y%m%d-%H%M%S')
    out_dir.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=f'run-{stamp}-', dir=str(out_dir)))


def write_result(out, res, log):
    """Write result.json; if that fails, print the whole result so the log still has it."""
    try:
        (out / 'result.json').write_text(json.dumps(res, indent=2) + '\n')
    except OSError as e:
        log(f'WARNING: could not write {out / "result.json"} ({type(e).__name__}: {e}); '
            'full result follows on the RESULT_JSON line')
        log('RESULT_JSON ' + json.dumps(res))
        return False
    log(f'OUTPUT {out / "result.json"}')
    return True


def main(argv=None, runner=subprocess.run, connect=None, phase=alarm_phase, log=print):
    a = parse_args(argv)
    ports = [a.device] + a.extra_port
    if not a.run:
        log(PLAN.format(ports=ports, key=SMC_WIFI_KEY, on=SMC_GPIO_ON, off=SMC_GPIO_OFF,
                        settle=a.power_settle_ms))
        return 0
    try:
        check_serial_free(ports, runner)
    except Refusal as e:
        log(f'STOP: {e}')
        return EXIT_REFUSED
    try:
        out = make_run_dir(a.out_dir)
    except OSError as e:
        log(f'STOP: cannot create a run directory under {a.out_dir} ({type(e).__name__}: {e}); '
            'nothing was touched')
        return EXIT_REFUSED
    connect = connect or connect_real
    close = None
    base_res = {'experiment': 'S240-wifi-m1', 'writes_to_storage': False, 'hardware_success_claim': False}
    try:
        try:
            p, facts, smc_factory, close = connect(a)
        except Refusal as e:
            log(f'STOP: {e}')
            write_result(out, dict(base_res, verdict=f'REFUSED: {e}',
                                   power_down={'confirmed': True, 'nothing_was_done': True}), log)
            return EXIT_REFUSED
        except Exception as e:  # no power-on step was attempted
            log(f'STOP: proxy connect failed: {type(e).__name__}: {e}. No power-on step was attempted.')
            write_result(out, dict(base_res, verdict=f'CONNECT_FAILED: {type(e).__name__}: {e}',
                                   power_down={'confirmed': True, 'nothing_was_done': True}), log)
            return EXIT_CONNECT
        probe = WifiProbe(p, facts, smc_factory, a, phase=phase, log=log)
        res = probe.run()
    finally:
        if close:
            close()
    res['facts'] = {k: (hex(v) if isinstance(v, int) else v) for k, v in facts.items()}
    log(json.dumps({k: res.get(k) for k in ('verdict', 'functions', 'link_up', 'power_down',
                                            'bridge_window_prediction')}, indent=2))
    write_result(out, res, log)
    v = res.get('verdict', 'ERROR')
    pd = res['power_down']
    if not pd['confirmed']:
        log(f'STOP: power-down NOT confirmed (errors={sorted(pd.get("errors", {}))}, '
            f'link_down_after={pd.get("link_down_after")}). Power-cycle the Mac mini before anything '
            'else; if the power button does not respond, pull mains power.')
        return EXIT_POWER_UNCONFIRMED
    if 'port1_before' in res and pd.get('port1_unchanged') is not True:
        log(f'STOP: FL1100 port-1 state changed or could not be re-read '
            f'(port1_unchanged={pd.get("port1_unchanged")}). Power-cycle the Mac mini before any launcher.')
        return EXIT_PORT1
    if v.startswith('REFUSED'):
        return EXIT_REFUSED
    code = EXIT.get(v.split(' ')[0], EXIT_ERROR)
    log(f'S240 verdict {v}. This is a config-space observation only, not a driver or boot result.')
    return code


if __name__ == '__main__':
    sys.exit(main())
