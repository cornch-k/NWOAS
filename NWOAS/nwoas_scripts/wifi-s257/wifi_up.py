#!/usr/bin/env python3
# SPDX-License-Identifier: ISC
#
# NWOAS S257 Wi-Fi stage 2: BCM4378 off -> firmware running and answering, from the idle (D4)
# m1n1 proxy, BEFORE any chainload / hypervisor / UEFI / Windows.
#
# The chip-side sequence (backplane window, EROM scan, AI core reset, CR4 halt/start, watchdog
# reset, OTP tuple parse, NVRAM conversion, random-seed footer, shared-RAM wait, console ring) is
# re-implemented here in Python from OpenBSD bwfm (sys/dev/ic/bwfm.c, sys/dev/pci/if_bwfm_pci.c),
# cross-checked against Linux brcmfmac pcie.c/chip.c/firmware.c (both ISC). Their notices:
#
#   Copyright (c) 2010-2016 Broadcom Corporation
#   Copyright (c) 2016,2017 Patrick Wildt <patrick@blueri.se>
#   Copyright (c) 2014 Broadcom Corporation (brcmfmac)
#
#   Permission to use, copy, modify, and/or distribute this software for any
#   purpose with or without fee is hereby granted, provided that the above
#   copyright notice and this permission notice appear in all copies.
#
#   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
#   WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
#   MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
#   ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
#   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
#   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
#   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
# No GPL code (Linux pcie-apple.c, AppleSiliconPciPlatformDxe.c) is copied; the port-0 power/link
# part is imported unchanged from nwoas_scripts/wifi-s240/wifi_probe.py, which passed on the
# Mac mini on 2026-11-15 (wifi-s240/hw/README.md, verdict PASS_IDS).
"""S257 Wi-Fi stage 2: BCM4378 power -> PCIe link -> chip ID -> OTP -> firmware in TCM -> running.

Modes (nothing touches hardware unless --run is given):
  (default)      print the plan
  --check-files  offline: resolve and convert the firmware/NVRAM from --fw-dir with the offline
                 ADT dump (--adt) and optional --otp-* values; prints sizes, hashes, RAM layout
  --run          talk to an idle, freshly booted m1n1 proxy. --stage picks how far to go:
                   ids  = S240 stage 1 only (power, link, config IDs, BAR sizes)
                   chip = + assign BARs, read chip ID + core list through BAR0 (no core resets)
                   otp  = + CR4 halt, chip watchdog reset, TCM size, OTP read, NVRAM choice  [default]
                   fw   = + write firmware/NVRAM/seed to TCM, start CR4, wait for the shared-RAM
                          pointer, read the firmware console (version string)
                   scan = + msgbuf rings/index buffers in proxy-heap RAM mapped ONLY through the
                          port-0 DART (all 16 streams -> one table holding just our pages), then
                          bus master on, CLM/txcap/calibration, mpc 0, UP, one escan (passive by
                          default), SSID table. No join/association, no data frames (wifi_scan.py)
ALWAYS (finally, each step on its own): [scan: bus master off + read-back first, then DART
restored as found only if that read-back is clean and no saved stream was bypass, else every
stream blocked; DMA memory freed - all BEFORE the port power-down, after which the DART faults],
endpoint decode off, BARs/windows restored, then the S240 power-down (PERST# low,
gP0d <- 0x800000, SMC quiesce, link-down check, FL1100 port-1 compare).
Stages ids..fw: bus mastering stays OFF on the endpoint and root port the whole time (the chip
cannot DMA into Mac RAM). Interrupt-disable is set on the endpoint. No storage access.
A PASS is a hardware observation from the proxy, not a Windows/driver/boot result.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
S240 = ROOT / 'nwoas_scripts' / 'wifi-s240'
if str(S240) not in sys.path:
    sys.path.insert(0, str(S240))
import wifi_probe as wp  # noqa: E402  (stage-1 code, hardware-validated 2026-11-15)
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
import wifi_scan as ws  # noqa: E402  (stage 3: DART + msgbuf + escan)

# FACT: sha256 of the wifi_probe.py that produced PASS_IDS on the Mac mini (wifi-s240/hw/README.md).
PROBE_SHA256 = 'dab5251d35f68f93c117f35057770af5e63076f91a2808fcd4118c444f6a9de1'

# 2026-11-18: AAPL,phandle numbers are assigned by iBoot when it builds the ADT and differ
# between OS firmware versions (recorded 106 came from the macOS 26 iBoot dump; the 13.5
# stub iBoot gives 102).  The absolute number is not a hardware fact, so it is not compared.
# The structural check stays in wp.validate_facts: both function-perst phandles must equal
# /arm-io/gpio's phandle.  Every other recorded value (addresses, pins) is still compared.
def validate_facts(f):
    g = dict(f)
    if g.get('gpio_phandle') is not None and g.get('perst_phandle') == g['gpio_phandle'] \
            and g.get('port1_perst_phandle') == g['gpio_phandle']:
        live = g['gpio_phandle']
        g['gpio_phandle'] = g['perst_phandle'] = g['port1_perst_phandle'] = wp.EXPECT['gpio_phandle']
        if live != wp.EXPECT['gpio_phandle']:
            print(f'{TAG} note: ADT gpio phandle {live} (recorded {wp.EXPECT["gpio_phandle"]}), perst points at it')
    wp.validate_facts(g)
TAG = '[s257]'
DEFAULT_FW_DIR = Path('/usr/share/firmware/wifi/C-4378__s-B1')
DEFAULT_ADT = ROOT / 'experiments' / 'tahoe-afk-20260906' / 'current.adt'
STAGES = ('ids', 'chip', 'otp', 'fw', 'scan')

# ---------------------------------------------------------------- BCM chip constants (bwfmreg.h,
# if_bwfm_pci.h; same values in brcmfmac)
CHIP_BASE = 0x18000000
CC_CHIPID, CC_CAPS, CC_WATCHDOG, CC_EROMPTR, CC_CAPS_EXT = 0x000, 0x004, 0x080, 0x0FC, 0x0AC
CORE_CC, CORE_80211, CORE_PMU, CORE_PCIE2, CORE_CR4, CORE_GCI = 0x800, 0x812, 0x827, 0x83C, 0x83E, 0x840
CORE_NAMES = {0x800: 'chipcommon', 0x812: '80211', 0x827: 'pmu', 0x83C: 'pcie2', 0x83E: 'arm-cr4',
              0x840: 'gci', 0x80E: 'socram', 0x849: 'sysmem', 0x847: 'arm-ca7', 0x82A: 'arm-cm3'}
AGENT_IOCTL, AGENT_RESET_CTL = 0x408, 0x800
IOCTL_CLK, IOCTL_FGC, IOCTL_CR4_CPUHALT = 0x1, 0x2, 0x20
RESET_CTL_RESET = 0x1
D11_PHYCLOCKEN, D11_PHYRESET = 0x4, 0x8
CR4_CAP, CR4_BANKIDX, CR4_BANKINFO = 0x004, 0x040, 0x044
DMP_DESC_MASK, DMP_COMPONENT, DMP_MASTER_PORT, DMP_ADDRESS, DMP_ADDR_GT32, DMP_EOT = 0xF, 0x1, 0x3, 0x5, 0x8, 0xF
DMP_SLAVE_SLAVE, DMP_SLAVE_SWRAP, DMP_SLAVE_MWRAP = 0, 2, 3
DMP_SIZE_4K, DMP_SIZE_8K, DMP_SIZE_DESC = 0, 1, 3
EROM_MAX_WORDS = 4096                     # bound on the descriptor walk
BAR0_WINDOW_CFG, BAR0_WINDOW_SIZE = 0x80, 0x1000
EP_LINKCTL_CFG, ASPM_MASK = 0xBC, 0x3     # BWFM_PCI_CFGREG_LINK_STATUS_CTRL (cap 0xAC + 0x10, S240)
PCIE2_CONFIGADDR, PCIE2_CONFIGDATA = 0x120, 0x124
PCIE2_MAILBOXINT, PCIE2_MAILBOXINT64 = 0x048, 0xC30
PCIE2_CFG_RESTORE = (0x004, 0x04C, 0x058, 0x05C, 0x060, 0x064, 0x0DC, 0x228, 0x248, 0x4E0, 0x4F4)
BRCM_CC_4378 = 0x4378
RAMBASE_4378 = 0x352000                   # bwfm_chip_tcm_rambase (bwfm.c), brcmfmac chip.c
REV_TO_FWDIR = {3: 'B1', 5: 'B3'}         # brcmfmac: rev 3 = 4378B1, rev 5 = 4378B3; bwfm: <=3 -> b1
OTP_BASE_4378, OTP_WORDS_4378 = 0x1120, 0x170   # GCI core (bwfm_pci_read_otp, brcmf_pcie_read_otp)
RAMSIZE_OFF, RAMSIZE_MAGIC = 0x6C, 0x534D4152
SEED_MAGIC, SEED_LEN = 0xFEEDC0DE, 0x100
SHARED_INFO, SHARED_CONSOLE = 0x00, 0x14
SHARED_MIN_VER, SHARED_MAX_VER = 5, 7
CON_BUFADDR, CON_BUFSIZE, CON_WRITEIDX = 0x08, 0x0C, 0x10
FW_POLL_MS = 50

# ---------------------------------------------------------------- PCI addresses
LOW_BUS_MIN, LOW_BUS_END = 0xC0000000, 0x100000000     # 32-bit window, CPU = bus + 0x600000000
MSI_GUARD = 0xFFF00000                                  # MSI doorbell 0xFFFFF000 lives above this
CPU_OFFSET = wp.LOW_MMIO_CPU_OFFSET
DEFAULT_BAR_BASE = 0xE0000000           # away from 0xC0000000 where UEFI later puts FL1100
CMD_MEM, CMD_BME, CMD_INTX_DIS = 0x2, 0x4, 0x400

XFER_CHUNK = 0x40000
MAX_CONSOLE_LINES = 150
MAX_LINE = 200

POWER_DOWN2_STEP_SECONDS = {'s257_resync': 5, 'ep_quiesce': 5, 'ep_bars_clear': 5,
                            'ep_window_restore': 5, 'rp_window_restore': 5, 'rp_cmd_restore': 5,
                            'dma_off': 5, 'devctl_restore': 5, 'rid2sid_restore': 5, 'dart_teardown': 10,
                            'dma_free': 5}


class StageStop(Exception):
    """An observed failure that ends the stage with a verdict (not a script error)."""

    def __init__(self, verdict):
        super().__init__(verdict)
        self.verdict = verdict


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def align_up(v, a):
    return (v + a - 1) // a * a


# ---------------------------------------------------------------- ADT / files (no hardware)

def wlan_info(adt):
    """Board, antenna, MAC (never logged), calibration size from the ADT /arm-io/wlan node."""
    w = adt['/arm-io/wlan']
    board = str(w.getprop('module-instance') or '').strip('\0').strip()
    raw = w.getprop('wifi-antenna-sku-info')
    if isinstance(raw, (list, tuple)):
        fmt = '<' + ('Q' if len(raw) == 2 else 'I' if len(raw) == 4 else 'B') * len(raw)
        raw = struct.pack(fmt, *[int(x) for x in raw])
    raw = bytes(raw or b'')
    ant = raw[8:16].split(b'\0')[0].decode('ascii', 'replace') if len(raw) >= 10 else ''
    mac = w.getprop('local-mac-address')
    mac = bytes(mac) if mac is not None and len(bytes(mac)) == 6 else None
    cal = w.getprop('wifi-calibration-msf')
    chipset = None
    try:
        chipset = str(adt['/product'].getprop('wifi-chipset')).strip('\0')
    except Exception:
        pass
    cal = bytes(cal) if cal is not None else b''
    return {'board': board, 'antenna': ant, 'mac': mac, 'cal_len': len(cal), 'cal': cal,
            'wifi_chipset': chipset}


def convert_nvram(data, mac=None):
    """Text NVRAM -> firmware blob: bwfm_nvram_convert (NUL-separated, comments/CR dropped, macaddr
    appended, NUL pad to 4, length token) + Asahi asahi_firmware/wifi.py key whitespace strip."""
    out = []
    for line in data.replace(b'\r', b'').split(b'\n'):
        if not line or line.startswith(b'#'):
            continue
        if b'=' in line:
            k, v = line.split(b'=', 1)
            line = k.strip() + b'=' + v
        out.append(line)
    if mac is not None and not any(x.startswith(b'macaddr=') for x in out):
        out.append(b'macaddr=' + ':'.join(f'{c:02x}' for c in mac).encode())
    blob = b'\0'.join(out)
    blob += b'\0' * (align_up(len(blob) + 1, 4) - len(blob))
    token = (len(blob) // 4) & 0xFFFF
    token |= (~token & 0xFFFF) << 16
    return blob + struct.pack('<I', token), len(out)


def image_version(fw):
    m = re.search(rb'<FW-TAG>[ -~]{0,240}', fw[-8192:])
    if not m:
        m = re.search(rb'Version[=:][ -~]{0,160}', fw[-8192:])
    return m.group().decode('ascii', 'replace') if m else None


def fw_ramsize_override(fw):
    if len(fw) >= RAMSIZE_OFF + 8:
        magic, size = struct.unpack_from('<II', fw, RAMSIZE_OFF)
        if magic == RAMSIZE_MAGIC:
            return size
    return None


class FwSet:
    """macOS firmware names (/usr/share/firmware/wifi/C-4378__s-<rev>/):
       <board>-<ant>.trx|.clmb|.txcb (symlinks) and P-<board>-<ant>_M-<module>_V-<vendor>__m-<modrev>.txt"""

    def __init__(self, fw_dir, board, ant):
        self.dir, self.board, self.ant = Path(fw_dir), board, ant

    def _pick(self, names):
        for n in names:
            p = self.dir / n
            if p.is_file():
                return p
        return None

    def blob(self, ext):
        return self._pick([f'{self.board}-{self.ant}.{ext}', f'{self.board}.{ext}'])

    def nvram_candidates(self):
        if not self.dir.is_dir():
            return []
        return sorted(p.name for p in self.dir.glob(f'P-{self.board}*_M-*.txt'))

    def nvram(self, module, vendor, modrev):
        names = []
        for b in (f'{self.board}-{self.ant}', self.board):
            names.append(f'P-{b}_M-{module}_V-{vendor}__m-{modrev}.txt')
        return self._pick(names), names

    def describe(self, path):
        if path is None:
            return None
        data = path.read_bytes()
        return {'path': str(path), 'real': str(path.resolve()), 'size': len(data), 'sha256': sha256(data)}


def preflight_files(a, info):
    """Refuse before any hardware access if the firmware set cannot be resolved."""
    if info['board'] != a.board:
        raise wp.Refusal(f'ADT module-instance {info["board"]!r} != expected board {a.board!r}')
    if not re.fullmatch(r'[A-Z0-9]{1,4}', info['antenna'] or ''):
        raise wp.Refusal(f'ADT antenna SKU unreadable ({info["antenna"]!r})')
    fs = FwSet(a.fw_dir, info['board'], info['antenna'])
    trx = fs.blob('trx')
    if trx is None:
        raise wp.Refusal(f'no {info["board"]}-{info["antenna"]}.trx in {a.fw_dir}')
    if a.nvram is None and not fs.nvram_candidates():
        raise wp.Refusal(f'no P-{info["board"]}*.txt NVRAM in {a.fw_dir}')
    if getattr(a, 'stage', None) == 'scan':
        for ext in ('clmb', 'txcb'):
            if fs.blob(ext) is None:
                raise wp.Refusal(f'scan stage needs {info["board"]}-{info["antenna"]}.{ext} in {a.fw_dir}')
        why = ws.check_dart_info(info.get('dart'))
        if why:
            raise wp.Refusal(why)
    return fs


# ---------------------------------------------------------------- MMIO with BAR ranges

class RangeMmio(wp.GuardedMmio):
    """S240 exact-address allowlist plus the two assigned endpoint BAR ranges (filled in once the
    BARs are programmed). Range writes are counted, not listed (there are thousands)."""

    def __init__(self, p, allowed):
        super().__init__(p, allowed)
        self.ranges = []
        self.range_writes = 0
        self.late = set()          # exact addresses allowed once their offset is known (DevCtl)
        self.cmd_regs = frozenset()  # endpoint/root-port command registers: bus-master value guard
        self.bme_ok = False        # set only by the scan stage after the DART is translating

    def allow(self, addr):
        self.late.add(addr)

    def _in_ranges(self, addr, size=4):
        return any(b <= addr and addr + size <= b + s for b, s in self.ranges)

    def w32(self, addr, val):
        if addr in self.cmd_regs and val & CMD_BME and not self.bme_ok:
            raise wp.Refusal(f'bus master bit in a write to {addr:#x} before the DART is set up (blocked)')
        if addr in self.allowed:
            return super().w32(addr, val)
        if addr in self.late:
            self.writes.append((addr, val & 0xFFFFFFFF))
            self.p.write32(addr, val & 0xFFFFFFFF)
            return None
        if not self._in_ranges(addr):
            raise wp.Refusal(f'write to {addr:#x} is outside the S257 allowlist/BAR ranges (blocked)')
        self.range_writes += 1
        self.p.write32(addr, val & 0xFFFFFFFF)

    def r16(self, addr):
        return self.p.read16(addr) & 0xFFFF

    def copy_to_dev(self, dst, src_heap, size):
        if not self._in_ranges(dst, size):
            raise wp.Refusal(f'bulk write {dst:#x}+{size:#x} outside the BAR ranges (blocked)')
        self.range_writes += size // 4
        self.p.memcpy32(dst, src_heap, size)

    def copy_from_dev(self, dst_heap, src, size):
        if not self._in_ranges(src, size):
            raise wp.Refusal(f'bulk read {src:#x}+{size:#x} outside the BAR ranges (blocked)')
        self.p.memcpy32(dst_heap, src, size)


class Xfer:
    """Host <-> proxy RAM buffer (ProxyUtils heap + iface.writemem/readmem)."""

    def __init__(self, u, size=XFER_CHUNK):
        self.u, self.iface = u, u.iface
        self.size = size
        self.buf = u.memalign(0x4000, size)

    def put(self, data):
        assert len(data) <= self.size and len(data) % 4 == 0
        self.iface.writemem(self.buf, data)
        return self.buf

    def get(self, n):
        return self.iface.readmem(self.buf, n)

    def close(self):
        try:
            self.u.free(self.buf)
        except Exception:
            pass


# ---------------------------------------------------------------- the probe

class WifiUp(wp.WifiProbe):
    def __init__(self, p, facts, smc_factory, opts, xfer_factory, info, fwset, dma_factory=None, **kw):
        super().__init__(p, facts, smc_factory, opts, **kw)
        self.xfer_factory, self.info, self.fs = xfer_factory, info, fwset
        self.dma_factory = dma_factory
        self.scan = None
        self.res.update({'experiment': 'S257-wifi-up', 'stage_requested': opts.stage})
        self.saved = None          # config values read before the first stage-2 write
        self.ep_touched = False
        self.cur_page = None
        self.cores = []
        self.chip = {}
        self.xfer = None

    # ---- logging
    def log2(self, msg, **kw):
        extra = ' '.join(f'{k}={v:#x}' if isinstance(v, int) and not isinstance(v, bool) else f'{k}={v}'
                         for k, v in kw.items())
        line = f'{TAG} {msg} {extra}'.rstrip()
        self.res['steps'].append({'msg': msg, 's257': True, **{k: (hex(v) if isinstance(v, int)
                                  and not isinstance(v, bool) else v) for k, v in kw.items()}})
        self.log(line[:400])

    # ---- config space helpers
    def ecfg(self, off):
        return wp.ecam_addr(self.f['ecam'], 1, 0, 0, off)

    def rcfg(self, off):
        return wp.ecam_addr(self.f['ecam'], 0, 0, 0, off)

    def extra_allow(self):
        e = self.f['ecam']
        a = {wp.ecam_addr(e, 0, 0, 0, o) for o in (0x04, 0x20, 0x24, 0x28, 0x2C)}
        a |= {wp.ecam_addr(e, 1, 0, 0, o) for o in (0x04, BAR0_WINDOW_CFG, EP_LINKCTL_CFG)}
        if self.o.stage == 'scan':
            a |= ws.Dart.write_addrs(self.info['dart']['base'])
        return a

    # ---- backplane access through the BAR0 window (bwfm_pci_buscore_read/write)
    def _bp(self, addr):
        page = addr & ~(BAR0_WINDOW_SIZE - 1)
        if page != self.cur_page:
            self.m.w32(self.ecfg(BAR0_WINDOW_CFG), page)
            if self.m.r32(self.ecfg(BAR0_WINDOW_CFG)) != page:
                self.m.w32(self.ecfg(BAR0_WINDOW_CFG), page)
            self.cur_page = page
        return self.bar0_cpu + (addr & (BAR0_WINDOW_SIZE - 1))

    def bp_r32(self, addr):
        return self.m.r32(self._bp(addr))

    def bp_w32(self, addr, val):
        self.m.w32(self._bp(addr), val)

    def bp_r16(self, addr):
        return self.m.r16(self._bp(addr))

    def tcm(self, addr):
        if not 0 <= addr < self.bar2_size:
            raise RuntimeError(f'TCM address {addr:#x} outside BAR2 ({self.bar2_size:#x})')
        return self.bar2_cpu + addr

    def tcm_r32(self, addr):
        return self.m.r32(self.tcm(addr))

    def tcm_w32(self, addr, val):
        self.m.w32(self.tcm(addr), val)

    def checked(self, what):
        """Guarded block that turns any m1n1 exception into a hard stop."""
        outer = self

        class _C:
            def __enter__(self_):
                self_.cm = outer.guarded()
                self_.g = self_.cm.__enter__()
                return self_.g

            def __exit__(self_, et, ev, tb):
                r = self_.cm.__exit__(et, ev, tb)
                if et is None and self_.g['exc']:
                    outer.res.setdefault('exceptions', []).append({'where': what, 'count': self_.g['exc']})
                    raise RuntimeError(f'{self_.g["exc"]} m1n1 exception(s) during {what}')
                return r
        return _C()

    # ---- main
    def run(self):
        try:
            validate_facts(self.f)
        except wp.Refusal as e:
            self.res['verdict'] = f'REFUSED: {e}'
            self.res['power_down'] = {'confirmed': True, 'nothing_was_done': True}
            return self.res
        if self.o.stage == 'scan':
            why = ws.check_dart_info(self.info.get('dart'))
            if why or self.dma_factory is None:
                self.res['verdict'] = f'REFUSED: {why or "no DMA memory allocator"}'
                self.res['power_down'] = {'confirmed': True, 'nothing_was_done': True}
                return self.res
        self.m = RangeMmio(self.p, wp.build_allowlist(self.f) | self.extra_allow())
        self.m.cmd_regs = frozenset({self.ecfg(0x04), self.rcfg(0x04)})
        self.res['controller_reset_option'] = bool(self.o.controller_reset)
        try:
            self._sequence()
        except StageStop as e:
            self.res['verdict'] = e.verdict
        except wp.Refusal as e:
            self.res['verdict'] = f'REFUSED: {e}'
        except Exception as e:
            self.res['verdict'] = f'ERROR: {type(e).__name__}: {e}'
        finally:
            if self.xfer is not None:
                self.xfer.close()
            self._power_down()
            self.res['mmio_writes'] = [[hex(a), hex(v)] for a, v in self.m.writes[:300]]
            self.res['mmio_writes_total'] = len(self.m.writes)
            self.res['bar_range_writes'] = self.m.range_writes
        return self.res

    def _sequence(self):
        super()._sequence()                                   # S240: power, link, IDs, BAR sizes
        if self.res.get('verdict') != 'PASS_IDS' or self.o.stage == 'ids':
            return
        with self.phase('s257-bars', 20):
            self._setup_bars()
        with self.phase('s257-chip', 60):
            self._chip_id_and_cores()
        if self.o.stage == 'chip':
            self.res['verdict'] = 'PASS_CHIP'
            return
        with self.phase('s257-reset-otp', 90):
            self._reset_ramsize_otp()
        self._choose_files()
        if self.o.stage == 'otp':
            self.res['verdict'] = 'PASS_OTP'
            return
        with self.phase('s257-fw-load', 240):
            self._load_firmware()
        with self.phase('s257-fw-start', 30):
            self._start_and_wait()
        with self.phase('s257-console', 20 + self.o.hold_seconds):
            self._console_and_hold()
        if self.o.stage != 'scan':
            self.res['verdict'] = 'PASS_FW_UP'
            return
        self.res['fw_up'] = True
        self.scan = ws.ScanRunner(self, StageStop)
        rows = self.scan.run(self.phase)
        self.res['verdict'] = 'PASS_SCAN' if rows else \
            f'SCAN_NO_RESULTS (escan {self.res["scan"]["escan"]["status"]}, {self.res["scan"]["escan"]["events"]} events)'

    # ---- stage 2a: BARs and windows (standard PCI, nothing chip-specific)
    def _setup_bars(self):
        bars = {b['bar']: b for b in self.res['functions'][0].get('bars', []) if isinstance(b, dict)}
        try:
            s0, s2 = int(bars[0]['size'], 16), int(bars[2]['size'], 16)
        except (KeyError, ValueError, TypeError):
            raise StageStop(f'BAR_SETUP_FAILED (BAR0/BAR2 sizes not measured: {bars})')
        for name, s, lo, hi in (('BAR0', s0, 0x1000, 0x100000), ('BAR2', s2, 0x100000, 0x1000000)):
            if s & (s - 1) or not lo <= s <= hi or bars[0 if name == 'BAR0' else 2]['type'] != 'mem64':
                raise StageStop(f'BAR_SETUP_FAILED ({name} size {s:#x} not a mem64 power of two in [{lo:#x},{hi:#x}])')
        base = self.o.bar_base
        bar2_bus = align_up(base, s2)
        bar0_bus = align_up(bar2_bus + s2, s0)
        win_lo = bar2_bus & ~0xFFFFF
        win_hi = align_up(bar0_bus + s0, 0x100000) - 1
        if not (LOW_BUS_MIN <= win_lo and win_hi < MSI_GUARD):
            raise wp.Refusal(f'BAR layout {win_lo:#x}..{win_hi:#x} leaves the 32-bit window or hits MSI')
        self.bar0_bus, self.bar2_bus, self.bar0_size, self.bar2_size = bar0_bus, bar2_bus, s0, s2
        self.bar0_cpu, self.bar2_cpu = bar0_bus + CPU_OFFSET, bar2_bus + CPU_OFFSET
        with self.checked('config save'):
            saved = {'rp': {o: self.m.r32(self.rcfg(o)) for o in (0x04, 0x20, 0x24, 0x28, 0x2C)},
                     'ep': {o: self.m.r32(self.ecfg(o)) for o in (0x04, 0x10, 0x14, 0x18, 0x1C, BAR0_WINDOW_CFG)}}
        self.res['config_saved'] = {k: {hex(o): hex(v) for o, v in d.items()} for k, d in saved.items()}
        ep = saved['ep']
        if ep[0x04] & 0x7 or (ep[0x10] & ~0xF) or ep[0x14] or (ep[0x18] & ~0xF) or ep[0x1C]:
            raise wp.Refusal(f'endpoint already decoding/assigned (cmd={ep[0x04]:#x}); not a fresh proxy?')
        self.saved = saved
        self.ep_touched = True
        rp_cmd = (saved['rp'][0x04] & 0xFFFF & ~CMD_BME) | CMD_MEM
        ep_cmd = CMD_MEM | CMD_INTX_DIS
        mem_window = ((win_hi >> 16) & 0xFFF0) << 16 | ((win_lo >> 16) & 0xFFF0)
        with self.checked('BAR setup'):
            self.m.w32(self.rcfg(0x20), mem_window)
            self.m.w32(self.rcfg(0x24), 0x0000FFF0)          # prefetchable window disabled (base > limit)
            self.m.w32(self.rcfg(0x28), 0)
            self.m.w32(self.rcfg(0x2C), 0)
            self.m.w32(self.rcfg(0x04), rp_cmd)              # memory forwarding, no bus master
            self.m.w32(self.ecfg(0x10), bar0_bus)
            self.m.w32(self.ecfg(0x14), 0)
            self.m.w32(self.ecfg(0x18), bar2_bus)
            self.m.w32(self.ecfg(0x1C), 0)
            self.m.w32(self.ecfg(0x04), ep_cmd)              # memory decode, NO bus master, INTx off
            rb = {o: self.m.r32(self.ecfg(o)) for o in (0x04, 0x10, 0x14, 0x18, 0x1C)}
            rp = {o: self.m.r32(self.rcfg(o)) for o in (0x04, 0x20)}
        ok = ((rb[0x10] & ~0xF) == bar0_bus and rb[0x14] == 0 and (rb[0x18] & ~0xF) == bar2_bus and rb[0x1C] == 0
              and (rb[0x04] & 0x7) == CMD_MEM and rp[0x20] == mem_window and (rp[0x04] & 0x7) == CMD_MEM)
        self.res['bar_layout'] = {'bar0_bus': hex(bar0_bus), 'bar0_cpu': hex(self.bar0_cpu), 'bar0_size': hex(s0),
                                  'bar2_bus': hex(bar2_bus), 'bar2_cpu': hex(self.bar2_cpu), 'bar2_size': hex(s2),
                                  'rootport0_window_bus': [hex(win_lo), hex(win_hi)],
                                  'readback': {hex(o): hex(v) for o, v in {**rb, **{0x100 + o: v for o, v in rp.items()}}.items()},
                                  'bus_master': False}
        self.log2('BARs assigned', bar0_cpu=self.bar0_cpu, bar2_cpu=self.bar2_cpu, window=f'{win_lo:#x}-{win_hi:#x}',
                  ep_cmd=rb[0x04] & 0xFFFF, ok=ok)
        if not ok:
            raise StageStop('BAR_SETUP_FAILED (config read-back differs)')
        self.m.ranges = [(self.bar0_cpu, s0), (self.bar2_cpu, s2)]

    def _recheck_bars(self, where):
        """After a chip watchdog reset: config must still decode our BARs (re-program if not)."""
        with self.checked(f'config recheck ({where})'):
            rb = {o: self.m.r32(self.ecfg(o)) for o in (0x04, 0x10, 0x18)}
            lost = (rb[0x04] & CMD_MEM) == 0 or (rb[0x10] & ~0xF) != self.bar0_bus or (rb[0x18] & ~0xF) != self.bar2_bus
            if lost:
                self.m.w32(self.ecfg(0x10), self.bar0_bus)
                self.m.w32(self.ecfg(0x14), 0)
                self.m.w32(self.ecfg(0x18), self.bar2_bus)
                self.m.w32(self.ecfg(0x1C), 0)
                self.m.w32(self.ecfg(0x04), CMD_MEM | CMD_INTX_DIS)
        self.cur_page = None
        self.log2(f'config after {where}', lost=lost, cmd=rb[0x04] & 0xFFFF)
        self.res.setdefault('config_lost', {})[where] = lost

    # ---- stage 2b: chip ID and cores (bwfm_chip_attach up to the EROM scan)
    def _chip_id_and_cores(self):
        with self.checked('chip id'):
            cid = self.bp_r32(CHIP_BASE + CC_CHIPID)
        chip, rev, typ = cid & 0xFFFF, (cid >> 16) & 0xF, (cid >> 28) & 0xF
        self.chip.update({'chipid_reg': hex(cid), 'chip': hex(chip), 'rev': rev, 'socitype': typ})
        self.res['chip'] = self.chip
        self.log2('chip id', reg=cid, chip=chip, rev=rev, socitype=typ)
        if chip != BRCM_CC_4378 or typ != 1:
            raise StageStop(f'CHIP_MISMATCH (chipid {cid:#x}: chip {chip:#x} rev {rev} type {typ})')
        want = REV_TO_FWDIR.get(rev)
        self.chip['fw_rev_dir'] = want
        if want is None:
            raise StageStop(f'CHIP_MISMATCH (unknown 4378 revision {rev})')
        with self.checked('EROM scan'):
            self.cores = self._erom_scan()
            for c in self.cores:
                c['up'] = self._ai_isup(c)
        self.res['cores'] = [{'id': hex(c['id']), 'name': CORE_NAMES.get(c['id'], '?'), 'rev': c['rev'],
                              'base': hex(c['base']), 'wrap': hex(c['wrap']), 'up': c['up']} for c in self.cores]
        for c in self.cores[:40]:
            self.log2('core', id=c['id'], name=CORE_NAMES.get(c['id'], '?'), rev=c['rev'], base=c['base'],
                      wrap=c['wrap'], up=c['up'])
        for need in (CORE_CC, CORE_PCIE2, CORE_CR4, CORE_GCI):
            if self.core(need) is None:
                raise StageStop(f'CHIP_MISMATCH (core {CORE_NAMES[need]} not in EROM)')
        self.chip['pcie2_rev'] = self.core(CORE_PCIE2)['rev']
        self.chip['pcireg64'] = self.chip['pcie2_rev'] >= 64
        # Observation for the Windows design (not fatal): can the chip's own config space be read
        # through the PCIe2 core CONFIGADDR/CONFIGDATA pair inside BAR0 (bwfm uses the same pair
        # for 0x4e0)? If yes, a driver on an ACPI (non-PCI) device node needs no ECAM access for
        # the BAR0 window register. HYPOTHESIS until this reads back equal on hardware.
        p2 = self.core(CORE_PCIE2)['base']
        with self.checked('PCIe2 config mirror'):
            mirror = {}
            for off in (0x00, BAR0_WINDOW_CFG):
                self.bp_w32(p2 + PCIE2_CONFIGADDR, off)
                mirror[off] = self.bp_r32(p2 + PCIE2_CONFIGDATA)
            ecam = {off: self.m.r32(self.ecfg(off)) for off in (0x00, BAR0_WINDOW_CFG)}
        self.chip['cfg_mirror'] = {'id_mirror': hex(mirror[0]), 'id_ecam': hex(ecam[0]),
                                   'win_mirror': hex(mirror[BAR0_WINDOW_CFG]), 'win_ecam': hex(ecam[BAR0_WINDOW_CFG]),
                                   'match': mirror == ecam}
        self.log2('config mirror via PCIe2 core', id=mirror[0], window=mirror[BAR0_WINDOW_CFG], match=mirror == ecam)
        dir_rev = re.search(r'__s-([A-Z]\d)$', Path(self.o.fw_dir).name)
        if dir_rev and dir_rev.group(1) != want:
            raise wp.Refusal(f'--fw-dir {self.o.fw_dir} is for {dir_rev.group(1)}, chip rev {rev} needs {want}')

    def core(self, cid, idx=0):
        for c in self.cores:
            if c['id'] == cid:
                if idx == 0:
                    return c
                idx -= 1
        return None

    def _erom_scan(self):
        """bwfm_chip_dmp_erom_scan + bwfm_chip_dmp_get_regaddr (bounded)."""
        erom = self.bp_r32(CHIP_BASE + CC_EROMPTR)
        start, cores, steps = erom, [], 0

        def rd():
            nonlocal steps
            steps += 1
            if steps > EROM_MAX_WORDS:
                raise RuntimeError(f'EROM walk exceeded {EROM_MAX_WORDS} words (start {start:#x})')
            return self.bp_r32(state['e'])
        state = {'e': erom}
        typ = 0
        while typ != DMP_EOT:
            val = rd(); state['e'] += 4
            typ = val & DMP_DESC_MASK
            if typ != DMP_COMPONENT:
                continue
            cid = (val >> 8) & 0xFFF
            val = rd(); state['e'] += 4
            if (val & DMP_DESC_MASK) != DMP_COMPONENT:
                raise RuntimeError('EROM: not a component descriptor')
            nmw, nsw, rev = (val >> 14) & 0x1F, (val >> 19) & 0x1F, (val >> 24) & 0xFF
            if nmw + nsw == 0 and cid not in (CORE_PMU, CORE_GCI):
                continue
            ok, base, wrap = self._erom_regaddr(rd, state)
            if not ok:
                continue
            cores.append({'id': cid, 'rev': rev, 'base': base, 'wrap': wrap})
        self.chip['erom'] = {'start': hex(start), 'words_read': steps}
        return cores

    def _erom_regaddr(self, rd, state):
        base = wrap = 0
        val = rd()
        typ = val & DMP_DESC_MASK
        if typ == DMP_MASTER_PORT:
            wraptype = DMP_SLAVE_MWRAP
            state['e'] += 4
        elif (typ & ~DMP_ADDR_GT32) == DMP_ADDRESS:
            wraptype = DMP_SLAVE_SWRAP
        else:
            return False, 0, 0
        while base == 0 or wrap == 0:
            while True:
                val = rd()
                typ = val & DMP_DESC_MASK
                if typ == DMP_COMPONENT:
                    return True, base, wrap
                if typ == DMP_EOT:
                    return False, 0, 0
                state['e'] += 4
                if (typ & ~DMP_ADDR_GT32) == DMP_ADDRESS:
                    break
            if typ & DMP_ADDR_GT32:
                state['e'] += 4
            sztype = (val >> 4) & 0x3
            if sztype == DMP_SIZE_DESC:
                v2 = rd()
                state['e'] += 8 if (v2 & DMP_DESC_MASK) & DMP_ADDR_GT32 else 4
            if sztype not in (DMP_SIZE_4K, DMP_SIZE_8K):
                continue
            stype = (val >> 6) & 0x3
            if base == 0 and stype == DMP_SLAVE_SLAVE:
                base = val & 0xFFFFF000
            if wrap == 0 and stype == wraptype:
                wrap = val & 0xFFFFF000
        return True, base, wrap

    # ---- AI core control (bwfm_chip_ai_*)
    def _ai_isup(self, c):
        ioctl = self.bp_r32(c['wrap'] + AGENT_IOCTL)
        reset = self.bp_r32(c['wrap'] + AGENT_RESET_CTL)
        return (ioctl & (IOCTL_FGC | IOCTL_CLK)) == IOCTL_CLK and (reset & RESET_CTL_RESET) == 0

    def _ai_disable(self, c, prereset, reset):
        w = c['wrap']
        if (self.bp_r32(w + AGENT_RESET_CTL) & RESET_CTL_RESET) == 0:
            self.bp_w32(w + AGENT_IOCTL, prereset | IOCTL_FGC | IOCTL_CLK)
            self.bp_r32(w + AGENT_IOCTL)
            self.bp_w32(w + AGENT_RESET_CTL, RESET_CTL_RESET)
            self.sleep(20e-6)
            for _ in range(300):
                if self.bp_r32(w + AGENT_RESET_CTL) == RESET_CTL_RESET:
                    break
            else:
                self.log2('core reset timeout (disable)', id=c['id'])
        self.bp_w32(w + AGENT_IOCTL, reset | IOCTL_FGC | IOCTL_CLK)
        self.bp_r32(w + AGENT_IOCTL)

    def _ai_reset(self, c, prereset, reset, postreset):
        w = c['wrap']
        self._ai_disable(c, prereset, reset)
        for _ in range(50):
            if (self.bp_r32(w + AGENT_RESET_CTL) & RESET_CTL_RESET) == 0:
                break
            self.bp_w32(w + AGENT_RESET_CTL, 0)
            self.sleep(60e-6)
        else:
            self.log2('core reset timeout (release)', id=c['id'])
        self.bp_w32(w + AGENT_IOCTL, postreset | IOCTL_CLK)
        self.bp_r32(w + AGENT_IOCTL)

    def _cr4_passive(self):
        cr4 = self.core(CORE_CR4)
        val = self.bp_r32(cr4['wrap'] + AGENT_IOCTL)
        self._ai_reset(cr4, val & IOCTL_CR4_CPUHALT, IOCTL_CR4_CPUHALT, IOCTL_CR4_CPUHALT)
        i = 0
        while True:
            d11 = self.core(CORE_80211, i)
            if d11 is None:
                break
            self._ai_disable(d11, D11_PHYRESET | D11_PHYCLOCKEN, D11_PHYCLOCKEN)
            i += 1

    def _cr4_active(self, rstvec):
        self.tcm_w32(0, rstvec)                               # bwfm_pci_buscore_activate
        self._ai_reset(self.core(CORE_CR4), IOCTL_CR4_CPUHALT, 0, 0)

    # ---- stage 2c: halt, chip reset, RAM size, OTP
    def _reset_ramsize_otp(self):
        cc, pcie2 = self.core(CORE_CC), self.core(CORE_PCIE2)
        mbox = PCIE2_MAILBOXINT64 if self.chip['pcireg64'] else PCIE2_MAILBOXINT
        with self.checked('CR4 halt'):
            self._cr4_passive()
        self.log2('CR4 halted (passive), 802.11 cores disabled')
        # bwfm_pci_buscore_reset: ASPM off, chipcommon watchdog = 4, 100 ms, ASPM back
        with self.checked('watchdog reset'):
            lnk = self.m.r32(self.ecfg(EP_LINKCTL_CFG))
            self.m.w32(self.ecfg(EP_LINKCTL_CFG), lnk & ~ASPM_MASK)
            self.bp_w32(cc['base'] + CC_WATCHDOG, 4)
        self.sleep(0.1)
        self.cur_page = None
        with self.checked('after watchdog'):
            self.m.w32(self.ecfg(EP_LINKCTL_CFG), lnk)
        self._recheck_bars('watchdog')
        with self.checked('post-reset'):
            if pcie2['rev'] <= 13:
                for off in PCIE2_CFG_RESTORE:
                    self.bp_w32(pcie2['base'] + PCIE2_CONFIGADDR, off)
                    self.bp_w32(pcie2['base'] + PCIE2_CONFIGDATA, self.bp_r32(pcie2['base'] + PCIE2_CONFIGDATA))
            st = self.bp_r32(pcie2['base'] + mbox)
            if st != 0xFFFFFFFF:
                self.bp_w32(pcie2['base'] + mbox, st)
            cid = self.bp_r32(CHIP_BASE + CC_CHIPID)
            self._cr4_passive()
        self.log2('chip watchdog reset done', linkctl=lnk & 0xFFFF, mailbox=st, chipid=cid)
        if cid & 0xFFFF != BRCM_CC_4378:
            raise StageStop(f'CHIP_MISMATCH (chip id {cid:#x} after watchdog)')
        with self.checked('TCM size'):
            cr4 = self.core(CORE_CR4)
            cap = self.bp_r32(cr4['base'] + CR4_CAP)
            nbanks = (cap & 0xF) + ((cap >> 4) & 0xF)
            ramsize, banks = 0, []
            for i in range(nbanks):
                self.bp_w32(cr4['base'] + CR4_BANKIDX, i)
                info = self.bp_r32(cr4['base'] + CR4_BANKINFO)
                blk = 1024 if info & 0x200 else 8192
                ramsize += ((info & 0x7F) + 1) * blk
                banks.append(hex(info))
            caps = self.bp_r32(cc['base'] + CC_CAPS)
        self.chip.update({'cr4_cap': hex(cap), 'banks': banks, 'ramsize_banks': hex(ramsize),
                          'rambase': hex(RAMBASE_4378), 'cc_caps': hex(caps)})
        self.ramsize = ramsize
        self.log2('TCM', rambase=RAMBASE_4378, ramsize=ramsize, banks=nbanks)
        if not ramsize or RAMBASE_4378 + ramsize > self.bar2_size:
            raise StageStop(f'TCM_LAYOUT_BAD (rambase {RAMBASE_4378:#x} + ramsize {ramsize:#x} vs BAR2 {self.bar2_size:#x})')
        # OTP shadow in the GCI core (bwfm_pci_read_otp / brcmf_pcie_read_otp, 4378 branch)
        gci = self.core(CORE_GCI)
        with self.checked('OTP read'):
            words = [self.bp_r16(gci['base'] + OTP_BASE_4378 + 2 * i) for i in range(OTP_WORDS_4378)]
        otp = b''.join(struct.pack('<H', w) for w in words)
        self.res['otp_sha256'] = sha256(otp)
        self.otp = parse_otp(otp)
        self.res['otp'] = {k: v for k, v in self.otp.items() if k != 'raw_tuples'}
        self.log2('OTP', module=self.otp.get('M'), vendor=self.otp.get('V'), modrev=self.otp.get('m'),
                  chiprev=self.otp.get('s'), tuples=len(self.otp.get('raw_tuples', [])))
        with self.checked('PCIe2 BAR2 config rewrite'):
            # bwfm_pci_preinit: CONFIGADDR 0x4e0 read/write back (also brcmf_pcie_attach)
            self.bp_w32(pcie2['base'] + PCIE2_CONFIGADDR, 0x4E0)
            v = self.bp_r32(pcie2['base'] + PCIE2_CONFIGDATA)
            self.bp_w32(pcie2['base'] + PCIE2_CONFIGDATA, v)

    # ---- file choice (host side)
    def _choose_files(self):
        fs = self.fs
        if self.o.nvram:
            nv = Path(self.o.nvram)
            tried = [str(nv)]
        else:
            M, V, mr = self.otp.get('M'), self.otp.get('V'), self.otp.get('m')
            if not (M and V and mr):
                raise StageStop(f'OTP_INCOMPLETE (M={M!r} V={V!r} m={mr!r}); give --nvram explicitly')
            nv, tried = fs.nvram(M, V, mr)
        if nv is None or not Path(nv).is_file():
            self.res['nvram_tried'] = tried
            raise StageStop(f'NVRAM_NOT_FOUND (tried {tried})')
        self.files = {'trx': fs.blob('trx'), 'nvram': Path(nv), 'clmb': fs.blob('clmb'), 'txcb': fs.blob('txcb')}
        self.res['files'] = {k: fs.describe(v) for k, v in self.files.items()}
        for k, d in self.res['files'].items():
            if d:
                self.log2(f'file {k}', name=Path(d['path']).name, size=d['size'], sha256=d['sha256'][:16])
        self.fw = self.files['trx'].read_bytes()
        self.nvram_blob, n = convert_nvram(self.files['nvram'].read_bytes(),
                                           None if self.o.no_macaddr else self.info['mac'])
        self.res['nvram_blob'] = {'len': len(self.nvram_blob), 'entries': n,
                                  'macaddr_appended': not self.o.no_macaddr and self.info['mac'] is not None}
        self.res['image_version'] = image_version(self.fw)
        self.log2('firmware image', version=self.res['image_version'])
        over = fw_ramsize_override(self.fw)
        if over:
            self.log2('firmware SMAR ramsize override', ramsize=over)
            self.ramsize = over
        self.layout = ram_layout(RAMBASE_4378, self.ramsize, len(self.fw), len(self.nvram_blob))
        self.res['ram_layout'] = {k: hex(v) for k, v in self.layout.items()}
        if self.layout['fw_end'] > self.layout['seed']:
            raise StageStop(f'TCM_LAYOUT_BAD (firmware end {self.layout["fw_end"]:#x} > seed {self.layout["seed"]:#x})')

    # ---- stage 2d: firmware into TCM (bwfm_pci_load_microcode)
    def _load_firmware(self):
        L = self.layout
        self.xfer = self.xfer_factory()
        fw = self.fw + b'\0' * (align_up(len(self.fw), 4) - len(self.fw))
        t0 = self.clock()
        with self.checked('firmware write'):
            for off in range(0, len(fw), XFER_CHUNK):
                chunk = fw[off:off + XFER_CHUNK]
                self.m.copy_to_dev(self.tcm(L['rambase'] + off), self.xfer.put(chunk), len(chunk))
        self.log2('firmware written', bytes=len(fw), seconds=round(self.clock() - t0, 2))
        if self.o.verify:
            h = hashlib.sha256()
            with self.checked('firmware read-back'):
                for off in range(0, len(fw), XFER_CHUNK):
                    n = min(XFER_CHUNK, len(fw) - off)
                    self.m.copy_from_dev(self.xfer.buf, self.tcm(L['rambase'] + off), n)
                    h.update(self.xfer.get(n))
            same = h.hexdigest() == sha256(fw)
            self.res['firmware_readback_match'] = same
            self.log2('firmware read-back', match=same)
            if not same:
                raise StageStop('FW_READBACK_MISMATCH')
        seed = os.urandom(SEED_LEN)
        with self.checked('NVRAM/seed write'):
            self.tcm_w32(L['shared_ptr'], 0)                  # firmware replaces this once up
            self.m.copy_to_dev(self.tcm(L['nvram']), self.xfer.put(self.nvram_blob), len(self.nvram_blob))
            self.m.copy_to_dev(self.tcm(L['footer']), self.xfer.put(struct.pack('<II', SEED_LEN, SEED_MAGIC)), 8)
            self.m.copy_to_dev(self.tcm(L['seed']), self.xfer.put(seed), SEED_LEN)
            written = self.tcm_r32(L['shared_ptr'])
            self.m.copy_from_dev(self.xfer.buf, self.tcm(L['nvram']), len(self.nvram_blob))
            nv_back = self.xfer.get(len(self.nvram_blob))
        self.shared_written = written
        self.res['nvram_readback_match'] = nv_back == self.nvram_blob
        self.log2('NVRAM + seed written', nvram_at=L['nvram'], seed_at=L['seed'], last_word_before_start=written,
                  nvram_match=nv_back == self.nvram_blob)
        if nv_back != self.nvram_blob:
            raise StageStop('NVRAM_READBACK_MISMATCH')

    def _start_and_wait(self):
        L = self.layout
        rstvec = struct.unpack_from('<I', self.fw, 0)[0]
        with self.checked('CR4 start'):
            self._cr4_active(rstvec)
        self.log2('CR4 released', rstvec=rstvec)
        shared, polls = self.shared_written, 0
        t0 = self.clock()
        limit = self.o.fw_timeout_ms // FW_POLL_MS
        while polls < limit:
            self.sleep(FW_POLL_MS / 1000.0)
            polls += 1
            with self.checked('shared wait'):
                shared = self.tcm_r32(L['shared_ptr'])
            if shared != self.shared_written:
                break
        ms = int((self.clock() - t0) * 1000)
        self.res['shared'] = {'addr': hex(shared), 'polls': polls, 'ms': ms}
        self.log2('shared RAM pointer', value=shared, ms=ms, changed=shared != self.shared_written)
        if shared == self.shared_written:
            raise StageStop(f'FW_NOT_UP (last RAM word unchanged after {ms} ms)')
        if not L['rambase'] <= shared < L['rambase'] + self.ramsize:
            raise StageStop(f'FW_BAD_SHARED ({shared:#x} outside TCM)')
        with self.checked('shared info'):
            flags = self.tcm_r32(shared + SHARED_INFO)
            con = self.tcm_r32(shared + SHARED_CONSOLE)
        ver = flags & 0xFF
        self.shared_addr, self.console_base = shared, con
        self.res['shared'].update({'flags': hex(flags), 'version': ver, 'console_struct': hex(con)})
        self.log2('shared info', flags=flags, version=ver, console=con)
        if not SHARED_MIN_VER <= ver <= SHARED_MAX_VER:
            raise StageStop(f'FW_BAD_SHARED (shared version {ver}, flags {flags:#x})')

    def _read_console(self):
        L = self.layout
        con = self.console_base
        top = L['rambase'] + self.ramsize
        if not L['rambase'] <= con < top:
            return None
        with self.checked('console struct'):
            buf, size, widx = (self.tcm_r32(con + o) for o in (CON_BUFADDR, CON_BUFSIZE, CON_WRITEIDX))
        if not (L['rambase'] <= buf and buf + size <= top and 0 < size <= 0x10000 and widx < size):
            return {'buf': hex(buf), 'size': hex(size), 'widx': hex(widx), 'valid': False}
        a0, a1 = buf & ~3, align_up(buf + size, 4)
        with self.checked('console read'):
            self.m.copy_from_dev(self.xfer.buf, self.tcm(a0), a1 - a0)
            raw = self.xfer.get(a1 - a0)[buf - a0: buf - a0 + size]
        ring = raw[widx:] + raw[:widx]
        return {'buf': hex(buf), 'size': hex(size), 'widx': widx, 'valid': True, 'ring': ring}

    def _console_and_hold(self):
        c = self._read_console()
        if not c or not c.get('valid'):
            self.res['console'] = {k: v for k, v in (c or {}).items() if k != 'ring'} or None
            self.log2('console unavailable', info=self.res['console'])
            return
        text = c['ring'].replace(b'\r', b'').split(b'\0')
        text = b'\n'.join(t for t in text if t).decode('ascii', 'replace')
        lines = [ln[:MAX_LINE] for ln in text.split('\n') if ln.strip()]
        shown = lines[-MAX_CONSOLE_LINES:]
        for ln in shown:
            self.log(f'{TAG} fw-console | {ln}')
        vers = [ln for ln in lines if 'Firmware:' in ln or 'version' in ln.lower()][:3]
        self.res['console'] = {'buf': c['buf'], 'size': c['size'], 'lines': len(lines), 'version_lines': vers,
                               'tail': shown[-40:]}
        self.log2('firmware version (console)', text=vers[0] if vers else None)
        widx0, grew = c['widx'], 0
        end = self.clock() + self.o.hold_seconds
        while self.clock() < end:
            self.sleep(1.0)
            c2 = self._read_console()
            if c2 and c2.get('valid'):
                grew += (c2['widx'] - widx0) % int(c2['size'], 16)
                widx0 = c2['widx']
        if self.o.hold_seconds:
            with self.checked('hold check'):
                still = self.tcm_r32(self.layout['shared_ptr'])
            self.res['hold'] = {'seconds': self.o.hold_seconds, 'console_bytes': grew,
                                'shared_ptr_unchanged': still == self.shared_addr}
            self.log2('hold', seconds=self.o.hold_seconds, console_bytes=grew, shared_ptr_same=still == self.shared_addr)

    # ---- power-down: stage-2 undo first (fixed values), then the S240 sequence
    def _pd2(self, pd, name, fn):
        try:
            with self.phase(f'power-down:{name}', POWER_DOWN2_STEP_SECONDS[name]):
                fn()
        except Exception as e:
            pd['steps'][name] = 'error'
            pd['errors'][name] = f'{type(e).__name__}: {e}'
            return False
        pd['steps'][name] = 'ok'
        return True

    def _scan_dart_undo(self, sc, pd2):
        """DART undo + DMA memory release, BEFORE the S240 power-down (the DART faults after
        PORT_PERST_OFF is cleared) and AFTER bus master is off. Restore only if the bus-master-off
        read-back succeeded; otherwise block every stream. Memory is released only when the DART
        is known to no longer point at our tables."""
        quiet = not sc.bme_attempted or pd2['steps'].get('dma_off') == 'ok'
        box = {'mode': 'untouched'}
        ok = True
        if sc.dart_touched:
            ok = self._pd2(pd2, 'dart_teardown', lambda: box.update(mode=sc.dart_teardown(quiet)))
            if not ok:
                box['mode'] = 'error'
        sc.res['dart_teardown_mode'] = box['mode']
        if ok:
            self._pd2(pd2, 'dma_free', sc.free)
        else:
            pd2['steps']['dma_free'] = 'skipped (DART may still point at our tables)'

    def _cfg_writes(self, pairs):
        with self.guarded():
            for addr, val in pairs:
                self.m.w32(addr, val)

    def _power_down(self):
        pd2 = {'steps': {}, 'errors': {}}
        sc = self.scan
        if self.ep_touched and self.saved:
            s = self.saved
            self._pd2(pd2, 's257_resync', lambda: wp.resync_proxy(self.p, self.sleep))
            if sc is not None and sc.bme_attempted:        # bus master off before anything else
                self._pd2(pd2, 'dma_off', sc.dma_off)
            if sc is not None and sc.devctl_saved is not None:
                self._pd2(pd2, 'devctl_restore', sc.devctl_restore)
            if sc is not None and sc.rid2sid_saved is not None:
                self._pd2(pd2, 'rid2sid_restore', sc.rid2sid_restore)
            if sc is not None:
                self._scan_dart_undo(sc, pd2)
            self._pd2(pd2, 'ep_quiesce', lambda: self._cfg_writes([(self.ecfg(0x04), 0)]))
            self._pd2(pd2, 'ep_bars_clear', lambda: self._cfg_writes(
                [(self.ecfg(o), 0) for o in (0x10, 0x14, 0x18, 0x1C)]))
            self._pd2(pd2, 'ep_window_restore', lambda: self._cfg_writes(
                [(self.ecfg(BAR0_WINDOW_CFG), s['ep'][BAR0_WINDOW_CFG])]))
            self._pd2(pd2, 'rp_window_restore', lambda: self._cfg_writes(
                [(self.rcfg(o), s['rp'][o]) for o in (0x20, 0x24, 0x28, 0x2C)]))
            self._pd2(pd2, 'rp_cmd_restore', lambda: self._cfg_writes([(self.rcfg(0x04), s['rp'][0x04] & 0xFFFF)]))
            self.m.ranges = []
        super()._power_down()
        pd = self.res['power_down']
        if sc is not None:
            pd['dart'] = self.res.get('scan', {}).get('dart_teardown_mode', 'untouched')
        pd['stage2'] = pd2
        if self.ep_touched:
            pd['confirmed_s240'] = pd['confirmed']
            pd['confirmed'] = pd['confirmed'] and not pd2['errors']
            self.log2('stage-2 undo', errors=sorted(pd2['errors']) or 'none', confirmed=pd['confirmed'])


def parse_otp(otp):
    """OTP tuples: [type, length, data[length]], next tuple at +2+length (brcmf_pcie_parse_otp;
    bwfm steps by `length` only, brcmfmac is followed here). Type 0x15 = system vendor: LE32 header
    8, then a chip-params string and a board-params string of space-separated X=value fields
    (M = module, V = vendor, m = module revision; s = chip revision where present)."""
    out = {'raw_tuples': []}
    i = 0
    while i < len(otp) - 1:
        t, n = otp[i], otp[i + 1]
        if t == 0 or i + 2 + n > len(otp):
            break
        out['raw_tuples'].append((t, n))
        data = otp[i + 2:i + 2 + n]
        if t == 0x15 and len(data) >= 6 and data[:4] == b'\x08\x00\x00\x00':
            for field in re.split(rb'[ \x00\xff]', data[4:]):
                if 3 <= len(field) <= 17 and field[1:2] == b'=' and field[:1] in b'sMmV':
                    out.setdefault(field[:1].decode(), field[2:].decode('ascii', 'replace'))
        i += 2 + n
    return out


def ram_layout(rambase, ramsize, fw_len, nv_len):
    top = rambase + ramsize
    nvram = top - nv_len
    footer = nvram - 8
    seed = footer - SEED_LEN
    return {'rambase': rambase, 'ramsize': ramsize, 'fw_end': rambase + align_up(fw_len, 4),
            'seed': seed, 'footer': footer, 'nvram': nvram, 'shared_ptr': top - 4, 'top': top}


# ---------------------------------------------------------------- glue

def connect_real(a):
    if str(a.proxyclient) not in sys.path:
        sys.path.insert(0, str(a.proxyclient))
    from m1n1.proxy import UartInterface, M1N1Proxy
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    with wp.alarm_phase('connect', 30):
        iface = UartInterface(a.device)
        p = M1N1Proxy(iface, debug=False)
        bootstrap_port(iface, p)
        u = ProxyUtils(p)
        facts = wp.adt_facts(u.adt)
        info = wlan_info(u.adt)
        try:
            info['dart'] = ws.dart_info(u.adt)
        except Exception as e:
            info['dart'] = {'error': f'{type(e).__name__}: {e}'}

    def dma_factory(size):
        size = align_up(size, ws.DART_PAGE)
        return ws.DmaArena(p, iface, u.memalign(ws.DART_PAGE, size), size, u.free)
    return (p, facts, (lambda base: wp.RealSmc(u, base)), (lambda: Xfer(u)), info, iface.dev.close,
            dma_factory)


def load_offline_adt(path, proxyclient):
    if str(proxyclient) not in sys.path:
        sys.path.insert(0, str(proxyclient))
    from m1n1.adt import load_adt
    return load_adt(Path(path).read_bytes())


PLAN = """S257 plan (nothing executed). On a fresh idle m1n1 proxy, --run --stage {stage}:
  S240 (imported, hardware-validated): lsof guard, ADT j274 guard, pcie_init, SMC gP0d <- 0x800001,
     port-0 PERST#/REFCLK/LTSSM, read 1:0.0/1:0.1 IDs, size BARs
  chip : root-port-0 window {base:#x}.. (bus; CPU +0x600000000), BAR2 (TCM) then BAR0 (regs),
         endpoint cmd = MEM | INTx-disable (NO bus master), chip ID via BAR0 window, EROM core list,
         read check of the chip's config mirror (PCIe2 CONFIGADDR/DATA) against ECAM
  otp  : CR4 halt + 802.11 cores off, chipcommon watchdog reset, re-check config, TCM size from
         CR4 banks, OTP (GCI+0x1120, 0x170 words) -> NVRAM file name
  fw   : firmware {trx} -> TCM 0x352000 (read-back {verify}), NVRAM + seed footer + 256 random
         bytes at the top of TCM, last word = 0, CR4 start, wait <= {fwto} ms for the shared-RAM
         pointer, read shared version + firmware console, hold {hold} s
  finally: endpoint cmd 0, BARs 0, windows/cmd restored, then S240 power-down (PERST# low,
         gP0d <- 0x800000, SMC quiesce, link-down check, FL1100 port-1 compare)
  scan : + read the msgbuf shared struct/ring info; DMA arena from the proxy heap, every buffer on
         its own 16 KiB page + guard page at IOVA 0x10000000..; port-0 DART (ADT /arm-io/dart-apcie0)
         saved, all 16 streams -> one L1/L2 table holding only those pages; ring descriptors and
         host index addresses into TCM; DevCtl No Snoop/RO off; THEN bus master on (EP + root port);
         host-ready doorbell; post 8 ioctl + 8 event + {rxbufs} rx buffers; GET_VERSION,
         cur_etheraddr (compared, not logged), ver, clmload, txcapload, calload ({cal}), mpc 0,
         {country}, event_msgs = ESCAN_RESULT only, UP, scan_ver, escan ({scantype}, all channels,
         no SSID) for <= {scansec} s, DOWN, deduplicated SSID/BSSID/channel/RSSI table.
         No join/association, no data frames.
         finally (scan): bus master off + read-back first; then, still before the port power-down
         (the DART is inside the port-0 block and faults after PORT_PERST_OFF is cleared), the
         DART is restored as found (or left blocked if bus master was not verified off or a saved
         stream was bypass) and the DMA memory is freed.
  fw stage: CLM/txcap are only checked (they go over the message rings = the scan stage).
  Afterwards reboot m1n1 (tools/reboot.py) before any launcher."""


def parse_args(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--run', action='store_true', help='talk to the proxy (default: print plan)')
    ap.add_argument('--check-files', action='store_true', help='offline firmware/NVRAM check, no proxy')
    ap.add_argument('--stage', choices=STAGES, default='otp')
    ap.add_argument('--device', default=os.environ.get('M1N1DEVICE', wp.DEFAULT_DEVICE))
    ap.add_argument('--extra-port', action='append', default=None)
    ap.add_argument('--proxyclient', type=Path, default=wp.DEFAULT_PROXYCLIENT)
    ap.add_argument('--fw-dir', type=Path, default=DEFAULT_FW_DIR)
    ap.add_argument('--board', default='atlantisb', help='expected ADT module-instance (J274)')
    ap.add_argument('--nvram', type=Path, default=None, help='explicit NVRAM .txt (skips the OTP choice)')
    ap.add_argument('--no-macaddr', action='store_true', help='do not append the ADT MAC to the NVRAM')
    ap.add_argument('--adt', type=Path, default=DEFAULT_ADT, help='offline ADT dump for --check-files')
    ap.add_argument('--otp-module', default='RASP')
    ap.add_argument('--otp-vendor', default=None, help='--check-files only (u or m on J274)')
    ap.add_argument('--otp-modrev', default=None, help='--check-files only')
    ap.add_argument('--bar-base', type=lambda s: int(s, 0), default=DEFAULT_BAR_BASE)
    ap.add_argument('--fw-timeout-ms', type=int, default=5000)
    ap.add_argument('--hold-seconds', type=int, default=3)
    ap.add_argument('--no-verify', dest='verify', action='store_false')
    ap.add_argument('--scan-seconds', type=int, default=15, help='scan stage: wait for escan results')
    ap.add_argument('--scan-type', choices=('passive', 'active'), default='passive',
                    help='scan stage: passive = listen only (no probe requests sent)')
    ap.add_argument('--country', default=None, help='scan stage: set the firmware country (e.g. KR)')
    ap.add_argument('--no-cal', action='store_true', help='scan stage: do not send ADT wifi-calibration-msf')
    ap.add_argument('--rxbufs', type=int, default=32, help='scan stage: RX data buffers posted')
    ap.add_argument('--rid2sid', choices=('leave', 'map'), default='leave',
                    help='scan stage: leave = only read/log the port-0 RID->SID table; map = entry 0 '
                         'RID 0x100 -> SID 1 (Linux t8103 iommu-map), restored at the end')
    ap.add_argument('--doorbell', choices=('auto', 'bwfm'), default='auto',
                    help='scan stage: auto = brcmfmac (BAR0 window, _64 regs for PCIe2 rev >= 64); '
                         'bwfm = OpenBSD (BAR0+0x2000)')
    ap.add_argument('--allow-probe-change', action='store_true')
    ap.add_argument('--out-dir', type=Path, default=HERE / 'runs')
    # S240 options (same meaning and limits)
    ap.add_argument('--power-settle-ms', type=int, default=100)
    ap.add_argument('--link-timeout-ms', type=int, default=1000)
    ap.add_argument('--config-retry-ms', type=int, default=1000)
    ap.add_argument('--controller-reset', action='store_true')
    a = ap.parse_args(argv)
    a.size_bars = True                                         # stage 2 needs the measured sizes
    if a.extra_port is None:
        a.extra_port = list(wp.DEFAULT_EXTRA_PORTS)
    for name, lo, hi in (('power_settle_ms', 0, 2000), ('link_timeout_ms', 10, 5000),
                         ('config_retry_ms', 10, 5000), ('fw_timeout_ms', 500, 10000),
                         ('hold_seconds', 0, 60), ('scan_seconds', 5, 30), ('rxbufs', 0, 256)):
        if not lo <= getattr(a, name) <= hi:
            ap.error(f'{name} must be in [{lo}, {hi}]')
    if a.country is not None and not re.fullmatch(r'[A-Z]{2}', a.country):
        ap.error('--country must be two capital letters')
    if not (LOW_BUS_MIN <= a.bar_base < MSI_GUARD and a.bar_base % 0x800000 == 0):
        ap.error('--bar-base must be 8 MiB aligned inside [0xC0000000, 0xFFF00000)')
    return a


EXIT_PASS = {'PASS_IDS': 'ids', 'PASS_CHIP': 'chip', 'PASS_OTP': 'otp', 'PASS_FW_UP': 'fw', 'PASS_SCAN': 'scan'}


def check_files(a, log=print):
    adt = load_offline_adt(a.adt, a.proxyclient)
    info = wlan_info(adt)
    log(f'{TAG} ADT {a.adt.name}: board={info["board"]} antenna={info["antenna"]} '
        f'mac={"present" if info["mac"] else "missing"} cal_len={info["cal_len"]} chipset={info["wifi_chipset"]}')
    try:
        info['dart'] = ws.dart_info(adt)
    except Exception as e:
        info['dart'] = {'error': f'{type(e).__name__}: {e}'}
    d = info['dart']
    log(f'{TAG} ADT DART: {json.dumps({k: (hex(v) if isinstance(v, int) else v) for k, v in d.items()})} '
        f'check={ws.check_dart_info(d) or "ok"}')
    fs = preflight_files(a, info)
    log(f'{TAG} NVRAM candidates in {a.fw_dir}: {len(fs.nvram_candidates())}')
    out = {}
    for ext in ('trx', 'clmb', 'txcb'):
        out[ext] = fs.describe(fs.blob(ext))
        log(f'{TAG} {ext}: {json.dumps(out[ext])}')
    fw = fs.blob('trx').read_bytes()
    log(f'{TAG} image version: {image_version(fw)}  rstvec={struct.unpack_from("<I", fw)[0]:#x} '
        f'SMAR={fw_ramsize_override(fw)}')
    vendors = [a.otp_vendor] if a.otp_vendor else sorted({re.search(r'_V-([^_]+)__', n).group(1)
                                                          for n in fs.nvram_candidates() if '_V-' in n})
    for v in vendors:
        mods = [a.otp_modrev] if a.otp_modrev else sorted({re.search(r'__m-(.+)\.txt$', n).group(1)
                                                           for n in fs.nvram_candidates() if f'_V-{v}__' in n})
        for mr in mods:
            p, _ = fs.nvram(a.otp_module, v, mr)
            if p is None:
                log(f'{TAG} nvram M={a.otp_module} V={v} m={mr}: not found')
                continue
            blob, n = convert_nvram(p.read_bytes(), None)          # MAC left out offline
            sized, _ = convert_nvram(p.read_bytes(), bytes(6) if info['mac'] else None)  # dummy MAC: length only
            need = align_up(len(fw), 4) + SEED_LEN + 8 + len(sized)
            log(f'{TAG} nvram M={a.otp_module} V={v} m={mr}: {p.name} -> {p.resolve().name} '
                f'entries={n} blob(no mac)={len(blob)} blob(with mac)={len(sized)} sha256(no mac)={sha256(blob)[:16]} '
                f'TCM needed from rambase={need:#x} (the chip ramsize is read on hardware)')
    L = ws.dma_layout(42, 3, 4, min(a.rxbufs, 255), 7)        # example counts; real ones from shared RAM
    log(f'{TAG} scan DMA layout (example: 42 submit / 3 completion rings, 4-byte indices): arena '
        f'{L["size"]:#x} bytes, {len(L["pages"])} mapped 16 KiB pages at IOVA {ws.IOVA_BASE:#x}+')
    for k, (o, s) in L['regions'].items():
        log(f'{TAG}   {k:<20} iova {ws.IOVA_BASE + o:#011x} size {s:#x}')
    for ext in ('clmb', 'txcb'):
        b = fs.blob(ext)
        if b:
            log(f'{TAG} {ext}: {len(ws.dload_chunks(b.read_bytes()))} download chunks of <= {ws.DLOAD_MAX_LEN} bytes')
    log(f'{TAG} calibration: ADT wifi-calibration-msf {info["cal_len"]} bytes -> calload '
        f'{len(ws.dload_chunks(info["cal"]))} chunks (contents not logged)')
    log(f'{TAG} CLM/txcap/calibration are sent only by --stage scan (over the message rings).')
    return 0


def main(argv=None, runner=subprocess.run, connect=None, phase=wp.alarm_phase, log=print):
    a = parse_args(argv)
    if a.check_files:
        try:
            return check_files(a, log)
        except wp.Refusal as e:
            log(f'STOP: {e}')
            return wp.EXIT_REFUSED
    if not a.run:
        trx = FwSet(a.fw_dir, a.board, 'X3').blob('trx')
        log(PLAN.format(stage=a.stage, base=a.bar_base, trx=trx.name if trx else f'(missing in {a.fw_dir})',
                        verify='on' if a.verify else 'off', fwto=a.fw_timeout_ms, hold=a.hold_seconds,
                        rxbufs=a.rxbufs, cal='off' if a.no_cal else 'ADT wifi-calibration-msf',
                        country=f'country {a.country}' if a.country else 'country left at the firmware default',
                        scantype=a.scan_type, scansec=a.scan_seconds))
        return 0
    probe_sha = sha256((S240 / 'wifi_probe.py').read_bytes())
    if probe_sha != PROBE_SHA256 and not a.allow_probe_change:
        log(f'STOP: wifi-s240/wifi_probe.py sha256 {probe_sha} is not the hardware-validated {PROBE_SHA256}')
        return wp.EXIT_REFUSED
    ports = [a.device] + a.extra_port
    try:
        wp.check_serial_free(ports, runner)
    except wp.Refusal as e:
        log(f'STOP: {e}')
        return wp.EXIT_REFUSED
    try:
        out = wp.make_run_dir(a.out_dir)
    except OSError as e:
        log(f'STOP: cannot create a run directory under {a.out_dir} ({e}); nothing was touched')
        return wp.EXIT_REFUSED
    base_res = {'experiment': 'S257-wifi-up', 'writes_to_storage': False, 'hardware_success_claim': False,
                'stage_requested': a.stage, 'probe_sha256': probe_sha}
    connect = connect or connect_real
    close = None
    try:
        try:
            p, facts, smc_factory, xfer_factory, info, close, *more = connect(a)
            dma_factory = more[0] if more else None
            fs = preflight_files(a, info) if a.stage in ('otp', 'fw', 'scan') else \
                FwSet(a.fw_dir, info['board'], info['antenna'])
        except wp.Refusal as e:
            log(f'STOP: {e}')
            wp.write_result(out, dict(base_res, verdict=f'REFUSED: {e}',
                                      power_down={'confirmed': True, 'nothing_was_done': True}), log)
            return wp.EXIT_REFUSED
        except Exception as e:
            log(f'STOP: proxy connect failed: {type(e).__name__}: {e}. No power-on step was attempted.')
            wp.write_result(out, dict(base_res, verdict=f'CONNECT_FAILED: {type(e).__name__}: {e}',
                                      power_down={'confirmed': True, 'nothing_was_done': True}), log)
            return wp.EXIT_CONNECT
        log(f'{TAG} target board={info["board"]} antenna={info["antenna"]} cal_len={info["cal_len"]} '
            f'stage={a.stage} fw_dir={a.fw_dir}')
        up = WifiUp(p, facts, smc_factory, a, xfer_factory, info, fs, dma_factory=dma_factory,
                    phase=phase, log=log)
        res = up.run()
    finally:
        if close:
            close()
    res.update({k: v for k, v in base_res.items() if k not in res})
    res['facts'] = {k: (hex(v) if isinstance(v, int) else v) for k, v in facts.items()}
    res['wlan'] = {k: v for k, v in info.items() if k not in ('mac', 'cal')}
    if isinstance(res['wlan'].get('dart'), dict):
        res['wlan']['dart'] = {k: (hex(v) if isinstance(v, int) else v) for k, v in res['wlan']['dart'].items()}
    summary = {k: res.get(k) for k in ('verdict', 'chip', 'otp', 'shared', 'image_version', 'power_down')}
    if 'scan' in res:
        summary['scan'] = {k: res['scan'].get(k) for k in ('shared', 'host_cap', 'blobs', 'escan', 'counts',
                                                          'dart_fault', 'dart_error_at_teardown')}
    log(json.dumps(summary, indent=1, default=str)[:8000])
    wp.write_result(out, res, log)
    v = res.get('verdict', 'ERROR')
    pd = res['power_down']
    if not pd['confirmed']:
        log(f'STOP: power-down NOT confirmed (errors={sorted(pd.get("errors", {}))}, '
            f'stage2={sorted(pd.get("stage2", {}).get("errors", {}))}, link_down_after={pd.get("link_down_after")}). '
            'Power-cycle the Mac mini before anything else; if the power button does not respond, pull mains power.')
        return wp.EXIT_POWER_UNCONFIRMED
    if 'port1_before' in res and pd.get('port1_unchanged') is not True:
        log('STOP: FL1100 port-1 state changed or could not be re-read. Reboot m1n1 before any launcher.')
        return wp.EXIT_PORT1
    if v.startswith('REFUSED'):
        return wp.EXIT_REFUSED
    log(f'S257 verdict {v} (requested stage {a.stage}). Proxy-side hardware observation only; '
        'not a Windows, driver or boot result.')
    if EXIT_PASS.get(v) == a.stage:
        return 0
    if v.startswith('ERROR'):
        return wp.EXIT_ERROR
    return 1


if __name__ == '__main__':
    sys.exit(main())
