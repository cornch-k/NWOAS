#!/usr/bin/env python3
"""Offline tests for wifi_up.py: fake proxy + fake BCM4378 (backplane, EROM, CR4, TCM, OTP).
No serial port, no m1n1 proxy, no hardware. Uses the macOS firmware files in --fw-dir
(/usr/share/firmware/wifi/C-4378__s-B1 on this MacBook).

Run:  m1n1_windows/.venv-hv/bin/python3 -I -B nwoas_scripts/wifi-s257/test_wifi_up.py
"""
import contextlib
import struct
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'wifi-s240'))
import wifi_up as wu          # noqa: E402
import wifi_probe as wp       # noqa: E402
import wifi_scan as ws        # noqa: E402
import test_wifi_probe as tp  # noqa: E402

FW_DIR = wu.DEFAULT_FW_DIR
CPU = wu.CPU_OFFSET
HEAP = 0x10_0000_0000
SHARED = 0x400000
CONSOLE = 0x401000
CONBUF = 0x402000
RAMSIZE = 0x200000           # fake: 4 banks x 64 x 8 KiB
RINGINFO, RINGMEM, HTOD_MB, DTOH_MB = 0x404000, 0x404100, 0x405000, 0x405004
HEAP_SIZE = 0x800000
DART = ws.EXPECT_DART['base']
FAKE_MAC = bytes([2, 0, 0, 0, 0, 1])
FAKE_CAL = bytes(range(256)) * 8 + bytes(48)          # 2096 bytes like the ADT property


def otp_image(vendor='u', modrev='6.5'):
    board = f'M=RASP V={vendor} m={modrev}'.encode()
    t15 = b'\x08\x00\x00\x00' + b's=B1\x00' + board + b'\x00'
    raw = bytes([0x15, len(t15)]) + t15 + bytes([0x80, 4]) + b'\x01\x02\x03\x04' + b'\x00\x00'
    return raw + b'\xff' * (2 * wu.OTP_WORDS_4378 - len(raw))


class FakeChip:
    CORES = [(wu.CORE_CC, 60, 0x18000000, 0x18100000), (wu.CORE_PCIE2, 70, 0x18001000, 0x18101000),
             (wu.CORE_CR4, 9, 0x18002000, 0x18102000), (wu.CORE_GCI, 12, 0x18003000, 0x18103000),
             (wu.CORE_80211, 80, 0x18004000, 0x18104000)]

    def __init__(self, chipid=0x10034378, fw_comes_up=True, otp=None, lose_config=False, fw=None):
        self.chipid, self.fw_comes_up, self.lose_config = chipid, fw_comes_up, lose_config
        self.fw = fw                                  # FakeMsgbufFw or None
        self.bp = {wu.CHIP_BASE: chipid, wu.CHIP_BASE + wu.CC_EROMPTR: 0x18200000}
        erom = []
        for cid, rev, base, wrap in self.CORES:
            erom += [(cid << 8) | 0x1, (rev << 24) | (1 << 19) | 0x1,
                     base | (wu.DMP_SLAVE_SLAVE << 6) | 0x5, wrap | (wu.DMP_SLAVE_SWRAP << 6) | 0x5]
        erom.append(0xF)
        for i, w in enumerate(erom):
            self.bp[0x18200000 + 4 * i] = w
        for _, _, _, wrap in self.CORES:
            self.bp[wrap + wu.AGENT_IOCTL] = wu.IOCTL_CLK
            self.bp[wrap + wu.AGENT_RESET_CTL] = 0
        cr4 = 0x18002000
        self.bp[cr4 + wu.CR4_CAP] = 0x22
        self.bank = 0
        self.otp = otp or otp_image()
        self.tcm = bytearray(0x800000)
        self.started = False
        self.watchdogs = 0
        self.hw = None

    def bp_read(self, addr):
        if addr == 0x18001000 + wu.PCIE2_CONFIGDATA and self.hw is not None:   # config mirror
            return self.hw.fn[0]['cfg'].get(self.bp.get(0x18001000 + wu.PCIE2_CONFIGADDR, 0), 0)
        if addr == 0x18002000 + wu.CR4_BANKINFO:
            return 0x3F
        g = 0x18003000 + wu.OTP_BASE_4378
        if g <= addr < g + 2 * wu.OTP_WORDS_4378:
            o = addr - g
            return struct.unpack_from('<I', self.otp + b'\0\0', o)[0]
        return self.bp.get(addr, 0)

    def bp_write(self, addr, val, hw):
        if self.fw is not None and addr == 0x18001000 + ws.PCIE2_64_H2D_MB0:
            self.fw.doorbell()
            return
        if self.fw is not None and addr == 0x18001000 + ws.PCIE2_64_H2D_MB1:
            self.fw.hostready += 1
            self.fw.doorbell()
            return
        if addr == 0x18002000 + wu.CR4_BANKIDX:
            self.bank = val
            return
        if addr == 0x18000000 + wu.CC_WATCHDOG and val == 4:
            self.watchdogs += 1
            if self.lose_config:
                hw.fn[0]['cfg'].update({0x04: 0x00100000, 0x10: 0x4, 0x18: 0x4})
            return
        self.bp[addr] = val
        if addr == 0x18102000 + wu.AGENT_IOCTL:
            run = (val & wu.IOCTL_CLK) and not (val & (wu.IOCTL_CR4_CPUHALT | wu.IOCTL_FGC)) and \
                self.bp.get(0x18102000 + wu.AGENT_RESET_CTL, 0) == 0
            if run and not self.started:
                self.started = True
                self._boot()

    def _boot(self):
        rv = struct.unpack_from('<I', self.tcm, 0)[0]
        fw0 = struct.unpack_from('<I', self.tcm, wu.RAMBASE_4378)[0]
        # the last RAM word holds the NVRAM length token now (bwfm/brcmfmac order); the footer sits below the NVRAM
        tok = struct.unpack_from('<I', self.tcm, wu.RAMBASE_4378 + RAMSIZE - 4)[0]
        nvlen = 4 * (tok & 0xFFFF) + 4
        footer = struct.unpack_from('<II', self.tcm, wu.RAMBASE_4378 + RAMSIZE - nvlen - 8)
        if not self.fw_comes_up or rv != fw0 or footer != (wu.SEED_LEN, wu.SEED_MAGIC):
            return
        msg = b'\r\nRTE (PCIE-MSG_BUF) 18.20.511 on BCM4378 r3 @ 37.4/168/1000 MHz\r\n' \
              b'Firmware: BCM4378/3 wl0: Aug 11 2026 version 18.20.511.0.7.8.207 FWID 01-2606388a\r\n'
        for lo, n in ((SHARED, 0x80), (CONSOLE, 0x20), (CONBUF, 0x1000), (RINGINFO, 0x200), (HTOD_MB, 8)):
            self.tcm[lo:lo + n] = bytes(n)        # these fake structures sit inside the image area
        self.tcm[CONBUF:CONBUF + len(msg)] = msg
        struct.pack_into('<III', self.tcm, CONSOLE + 8, CONBUF, 0x1000, len(msg))
        struct.pack_into('<I', self.tcm, SHARED, 0x78810007)          # the real flags (2026-11-18)
        struct.pack_into('<I', self.tcm, SHARED + 0x14, CONSOLE)
        struct.pack_into('<I', self.tcm, SHARED + 0x20, 255 << 16)     # max_rxbufpost
        struct.pack_into('<III', self.tcm, SHARED + 0x28, HTOD_MB, DTOH_MB, RINGINFO)
        struct.pack_into('<IIIII', self.tcm, RINGINFO, RINGMEM, 0x406000, 0x406100, 0x406200, 0x406300)
        struct.pack_into('<HHH', self.tcm, RINGINFO + 0x34, 40, 42, 3)
        struct.pack_into('<I', self.tcm, wu.RAMBASE_4378 + RAMSIZE - 4, SHARED)

    def console(self, text):
        widx = struct.unpack_from('<I', self.tcm, CONSOLE + 0x10)[0]
        for ch in text:
            self.tcm[CONBUF + widx] = ch
            widx = (widx + 1) % 0x1000
        struct.pack_into('<I', self.tcm, CONSOLE + 0x10, widx)


class FakeHW2(tp.FakeHW):
    def __init__(self, clock, chip=None, **kw):
        super().__init__(clock, **kw)
        for fn, (s0, s2) in ((0, (0x10000, 0x800000)), (1, (0x8000, 0x400000))):
            self.fn[fn]['sizes'] = {0: s0, 2: s2}
        self.chip = chip or FakeChip()
        self.chip.hw = self
        self.heap = bytearray(HEAP_SIZE)
        self.ep_cmd_history = []
        self.rp_cmd_history = []
        self.read16 = self._read16
        self.alloc_next = HEAP + 0x100000
        self.allocs, self.freed = {}, []
        self.cache_ops = 0
        # DART as seen on hardware 2026-11-18 12:13 (config 0x10000, enabled 0xffff, every TCR
        # 0x80080, error 0xf000400); TTBR values are made up (not logged on hardware)
        self.dart = {ws.DART_TCR + 4 * s: 0x80080 for s in range(16)}
        self.dart.update({ws.DART_TTBR + 16 * s + 4 * j: 0 for s in range(16) for j in range(4)})
        self.dart[ws.DART_TTBR + 16 * 3] = ws.DART_TTBR_VALID | 0x123
        self.dart.update({ws.DART_ENABLED_STREAMS: 0xFFFF, ws.DART_CONFIG: 0x10000, ws.DART_ERROR: 0xF000400,
                          ws.DART_ERROR_ADDR_LO: 0, ws.DART_ERROR_ADDR_HI: 0})
        self.dart_dead = False                        # set by PORT_PERST_OFF clear (port-0 core reset)
        self.dart_dead_access = 0                     # hardware: data abort / SError for each one
        self.dart_writes_with_bme = 0
        self.bme_stuck = False                        # fault: the endpoint keeps bus master on
        self.dart_initial = dict(self.dart)
        self.dart_invalidates = 0
        self.dart_lock = False
        self.bme_seen_with = []                       # DART state each time bus master went on
        self.dma_faults = []
        self.devctl_reg = 0x50                        # tp.FakeHW: PCIe cap at 0x48

    # ---- heap RAM
    def in_heap(self, a, n=4):
        return HEAP <= a and a + n <= HEAP + len(self.heap)

    def alloc(self, size):
        a = self.alloc_next
        self.alloc_next += (size + 0x3FFF) & ~0x3FFF
        assert self.alloc_next <= HEAP + len(self.heap)
        self.allocs[a] = size
        return a

    def free(self, a):
        self.freed.append(a)

    def memset32(self, dst, val, size):
        assert self.in_heap(dst, size)
        self.heap[dst - HEAP:dst - HEAP + size] = struct.pack('<I', val) * (size // 4)

    def dc_cvac(self, a, n):
        assert a % 64 == 0 and n % 64 == 0 and self.in_heap(a, n), (hex(a), n)
        self.cache_ops += 1

    def dc_civac(self, a, n):
        assert a % 64 == 0 and n % 64 == 0 and self.in_heap(a, n), (hex(a), n)
        self.cache_ops += 1

    # ---- DART model (t8020 subset): translation through stream 1 (Linux SID of RID 0x100)
    def dart_translate(self, iova, n, write):
        tcr = self.dart[ws.DART_TCR + 4 * 1]
        ttbr = self.dart[ws.DART_TTBR + 16 * 1]
        pa = None
        if tcr == ws.DART_TCR_TRANSLATE and ttbr & ws.DART_TTBR_VALID and iova >> 25 == (iova + n - 1) >> 25:
            l1pa = (ttbr & 0x7FFFFFFF) << 12

            def page(a):
                return bytes(self.heap[a - HEAP:a - HEAP + 0x4000]) if self.in_heap(a, 0x4000) else bytes(0x4000)
            pa = ws.walk_tables(page(l1pa), page, ttbr, iova)
            if pa is not None and ws.walk_tables(page(l1pa), page, ttbr, iova + n - 1) != pa + n - 1:
                pa = None
        if pa is None or not self.in_heap(pa, n):
            self.dma_faults.append((iova, n, write))
            self.dart[ws.DART_ERROR] = ws.DART_ERROR_FLAG | 0x4
            self.dart[ws.DART_ERROR_ADDR_LO] = iova & 0xFFFFFFFF
            return None
        return pa - HEAP

    def dma_ok(self):
        e = self.f['ecam']
        return bool(self.fn[0]['cfg'].get(0x04, 0) & wu.CMD_BME and
                    self.regs.get(wp.ecam_addr(e, 0, 0, 0, 0x04), 0) & wu.CMD_BME)

    def dma_read(self, iova, n):
        if not self.dma_ok():
            return None
        o = self.dart_translate(iova, n, False)
        return None if o is None else bytes(self.heap[o:o + n])

    def dma_write(self, iova, data):
        if not self.dma_ok():
            return False
        o = self.dart_translate(iova, len(data), True)
        if o is None:
            return False
        self.heap[o:o + len(data)] = data
        return True

    # decode of the endpoint BARs through root port 0 (window + MEM enable on both)
    def _bar(self, addr):
        e = self.f['ecam']
        rp = self.regs.get(wp.ecam_addr(e, 0, 0, 0, 0x20), 0)
        lo, hi = (rp & 0xFFF0) << 16, ((rp >> 16) & 0xFFF0) << 16 | 0xFFFFF
        cfg = self.fn[0]['cfg']
        if not (self.regs.get(wp.ecam_addr(e, 0, 0, 0, 0x04), 0) & 2 and cfg.get(0x04, 0) & 2):
            return None
        bus = addr - CPU
        if not lo <= bus <= hi:
            return None
        b0, b2 = cfg[0x10] & ~0xF, cfg[0x18] & ~0xF
        if b0 and b0 <= bus < b0 + 0x10000:
            return ('bar0', bus - b0)
        if b2 and b2 <= bus < b2 + 0x800000:
            return ('bar2', bus - b2)
        return None

    def _bar_access(self, addr, val=None, size=4):
        d = self._bar(addr)
        if d is None:
            if self.guard:
                self.exc_count += 1
            return 0xFFFFFFFF
        kind, off = d
        if kind == 'bar2':
            if val is None:
                return struct.unpack_from('<I', self.chip.tcm, off)[0]
            struct.pack_into('<I', self.chip.tcm, off, val & 0xFFFFFFFF)
            return None
        bpaddr = self.fn[0]['cfg'].get(0x80, 0x18000000) + (off & 0xFFF)
        if off >= 0x1000:
            return 0xFFFFFFFF if val is None else None
        if val is None:
            return self.chip.bp_read(bpaddr)
        self.chip.bp_write(bpaddr, val, self)

    def read32(self, addr):
        if self.in_heap(addr):
            return struct.unpack_from('<I', self.heap, addr - HEAP)[0]
        if CPU + 0xC0000000 <= addr < CPU + 0x100000000:
            return self._bar_access(addr)
        if DART <= addr < DART + 0x4000 and self.dart_dead:
            self.dart_dead_access += 1
            if self.guard:
                self.exc_count += 1
            return 0xABAD1DEA
        if DART <= addr < DART + 0x4000:
            return self.dart.get(addr - DART, 0) & ~ws.DART_CMD_BUSY if addr - DART == ws.DART_STREAM_COMMAND \
                else self.dart.get(addr - DART, 0) | (ws.DART_CONFIG_LOCK if addr - DART == ws.DART_CONFIG and self.dart_lock else 0)
        return super().read32(addr)

    def _read16(self, addr):
        return self._bar_access(addr) & 0xFFFF

    def write32(self, addr, val):
        if self.in_heap(addr):
            struct.pack_into('<I', self.heap, addr - HEAP, val)
            return
        if CPU + 0xC0000000 <= addr < CPU + 0x100000000:
            self.writes.append((addr, val))
            return self._bar_access(addr, val)
        e = self.f['ecam']
        if addr == self.f['port0'] + wp.PORT_PERST and not val & wp.PERST_OFF:
            self.dart_dead = True
        if DART <= addr < DART + 0x4000:
            self.writes.append((addr, val))
            if self.dart_dead:
                self.dart_dead_access += 1
                if self.guard:
                    self.exc_count += 1
                return
            if self.dma_ok():
                self.dart_writes_with_bme += 1
            off = addr - DART
            if self.dart_lock and off in self.dart and off >= ws.DART_TCR:
                return
            if off == ws.DART_STREAM_COMMAND and val & ws.DART_CMD_INVALIDATE:
                self.dart_invalidates += 1
                return
            if off == ws.DART_ERROR:
                self.dart[off] &= ~val
                return
            self.dart[off] = val
            return
        if addr == wp.ecam_addr(e, 0, 0, 0, 0x04):
            self.rp_cmd_history.append(val)
            if val & wu.CMD_BME:
                self.bme_seen_with.append(('rp', [self.dart[ws.DART_TCR + 4 * s] for s in range(16)]))
        if wp.ecam_addr(e, 1, 0, 0) <= addr < wp.ecam_addr(e, 1, 0, 0, 0x1000):
            reg = addr - wp.ecam_addr(e, 1, 0, 0)
            if reg in (0x04, 0x80, 0xBC, self.devctl_reg) and self._link_up():
                self.writes.append((addr, val))
                if reg == 0x04:
                    if self.bme_stuck and self.fn[0]['cfg'].get(0x04, 0) & wu.CMD_BME:
                        val |= wu.CMD_BME
                    self.ep_cmd_history.append(val)
                    if val & wu.CMD_BME:
                        self.bme_seen_with.append(('ep', [self.dart[ws.DART_TCR + 4 * s] for s in range(16)]))
                self.fn[0]['cfg'][reg] = val & 0xFFFF if reg in (0x04, self.devctl_reg) else val
                return
        return super().write32(addr, val)

    def memcpy32(self, dst, src, size):
        if HEAP <= src < HEAP + len(self.heap):
            for o in range(0, size, 4):
                self.write32(dst + o, struct.unpack_from('<I', self.heap, src - HEAP + o)[0])
        else:
            for o in range(0, size, 4):
                struct.pack_into('<I', self.heap, dst - HEAP + o, self.read32(src + o))


class FakeIface:
    def __init__(self, hw):
        self.hw = hw

    def writemem(self, addr, data):
        assert self.hw.in_heap(addr, len(data)), hex(addr)
        self.hw.heap[addr - HEAP:addr - HEAP + len(data)] = data

    def readmem(self, addr, n):
        assert self.hw.in_heap(addr, n), hex(addr)
        return bytes(self.hw.heap[addr - HEAP:addr - HEAP + n])


def fake_dma_factory(hw):
    def make(size):
        size = (size + 0x3FFF) & ~0x3FFF
        return ws.DmaArena(hw, FakeIface(hw), hw.alloc(size), size, hw.free)
    return make


def bss_info(bssid, ssid, chan, rssi):
    b = bytearray(140)
    struct.pack_into('<II', b, 0, 109, len(b))
    b[8:14] = bssid
    b[18] = len(ssid)
    b[19:19 + len(ssid)] = ssid
    struct.pack_into('<H', b, 72, chan | (0x2 << 11) | ((3 if chan > 14 else 0) << 14))
    struct.pack_into('<h', b, 78, rssi)
    b[88] = chan
    struct.pack_into('<HHI', b, 116, 128, 0, 12)
    return bytes(b)


def event_frame(etype, status, data=b''):
    eth = bytes(12) + struct.pack('>H', ws.ETHERTYPE_LINK_CTL)
    bcm = struct.pack('>HHB', 0x8001, 48 + len(data), 0) + ws.BRCM_OUI + struct.pack('>H', 1)
    msg = struct.pack('>HHIIIII', 2, 0, etype, status, 0, 0, len(data)) + bytes(6) + bytes(16) + b'\0\0'
    return eth + bcm + msg + data


class FakeMsgbufFw:
    """Just enough of the BCM4378 msgbuf firmware: reads the H2D rings through the fake DART,
    answers ioctls/iovars, emits escan events into posted event buffers."""
    APS = [(bytes([0x10, 0, 0, 0, 0, i]), f'net-{i}'.encode(), [1, 6, 11, 36, 149][i % 5], -40 - 3 * i)
           for i in range(10)]

    def __init__(self, mode='ok'):
        self.mode, self.hw = mode, None
        self.hostready = 0
        self.cs_r = self.rx_r = 0
        self.cc_w = 0
        self.resp_bufs, self.event_bufs, self.rx_posted = [], [], 0
        self.pending = []
        self.blobs, self.blob_parts = {}, {}
        self.vars, self.up, self.scans, self.cmds = {}, False, [], []
        self.mask = b''
        self.h2d_mb = []

    # memory helpers (TCM is the chip's own memory; host RAM only via DMA)
    def tcm32(self, a):
        return struct.unpack_from('<I', self.hw.chip.tcm, a)[0]

    def ring(self, k):
        d = RINGMEM + 16 * k
        nitem, itemsz = struct.unpack_from('<HH', self.hw.chip.tcm, d + 4)
        return (self.tcm32(d + 8) | self.tcm32(d + 12) << 32), nitem, itemsz

    def idx_addr(self, k):
        return self.tcm32(RINGINFO + 0x14 + 8 * k) | self.tcm32(RINGINFO + 0x18 + 8 * k) << 32

    def get_idx(self, arr, i):
        d = self.hw.dma_read(self.idx_addr(arr) + 4 * i, 4)
        return None if d is None else struct.unpack('<I', d)[0]

    def set_idx(self, arr, i, v):
        self.hw.dma_write(self.idx_addr(arr) + 4 * i, struct.pack('<I', v))

    def doorbell(self):
        hw = self.hw
        if self.mode == 'mute' or not hw.dma_ok() or not self.hostready:
            return
        base, nitem, sz = self.ring(0)
        w = self.get_idx(0, 0)
        while w is not None and self.cs_r != w:
            it = hw.dma_read(base + self.cs_r * sz, sz)
            self.cs_r = (self.cs_r + 1) % nitem
            if it is not None:
                self.handle(it)
        self.set_idx(1, 0, self.cs_r)
        base, nitem, sz = self.ring(1)
        w = self.get_idx(0, 1)
        while w is not None and self.rx_r != w:
            self.rx_posted += 1
            self.rx_r = (self.rx_r + 1) % nitem
        self.set_idx(1, 1, self.rx_r)
        self.flush()

    def handle(self, it):
        t, _i, _f, _r, rid = struct.unpack_from('<BBBBI', it, 0)
        if t in (ws.MSG_IOCTLRESP_BUF_POST, ws.MSG_EVENT_BUF_POST):
            ln, = struct.unpack_from('<H', it, 8)
            lo, hi = struct.unpack_from('<II', it, 16)
            (self.resp_bufs if t == ws.MSG_IOCTLRESP_BUF_POST else self.event_bufs).append((rid, lo | hi << 32, ln))
        elif t == ws.MSG_IOCTLPTR_REQ:
            cmd, trans, inlen, outlen = struct.unpack_from('<IHHH', it, 8)
            lo, hi = struct.unpack_from('<II', it, 24)
            data = self.hw.dma_read(lo | hi << 32, inlen) or b''
            st, resp = self.ioctl(cmd, data, outlen)
            self.pending.append(('ack', struct.pack('<BBBBIhH12x', ws.MSG_IOCTLPTR_REQ_ACK, 0, 0, 0, rid, 0, 0)))
            self.pending.append(('cmplt', (st, trans, cmd, resp)))
            if cmd == ws.C_SET_VAR and data.startswith(b'escan\0') and self.mode != 'trap':
                self.queue_scan()
        elif t == ws.MSG_H2D_MAILBOX_DATA:
            self.h2d_mb.append(struct.unpack_from('<I', it, 8)[0])

    def ioctl(self, cmd, data, outlen):
        self.cmds.append(cmd)
        if cmd == ws.C_GET_VERSION:
            return 0, struct.pack('<I', 2)
        if cmd in (ws.C_UP, ws.C_DOWN):
            self.up = cmd == ws.C_UP
            return 0, data[:outlen]
        name, _, payload = data.partition(b'\0')
        name = name.decode()
        if cmd == ws.C_GET_VAR:
            v = {'cur_etheraddr': FAKE_MAC, 'ver': b'wl0: Aug 11 2026 version 18.20.511.0.7.8.207\0',
                 'scan_ver': struct.pack('<HHH', 1, 6, 2), 'clmload_status': bytes(4),
                 'country': b'XZ\0\0' + bytes(4) + b'XZ\0\0'}.get(name)
            return (0, v.ljust(outlen, b'\0')[:outlen]) if v is not None else (-23, b'')
        if cmd == ws.C_SET_VAR:
            self.vars[name] = payload
            if name in ('clmload', 'txcapload', 'calload'):
                flag, typ, ln, crc = struct.unpack_from('<HHII', payload, 0)
                assert typ == ws.DLOAD_TYPE_CLM and flag & ws.DLOAD_HANDLER_VER_1 and ln == len(payload) - 12
                if flag & ws.DLOAD_FLAG_BEGIN:
                    self.blob_parts[name] = b''
                self.blob_parts[name] += payload[12:]
                if flag & ws.DLOAD_FLAG_END:
                    self.blobs[name] = self.blob_parts.pop(name)
            if name == 'event_msgs':
                self.mask = payload
            if name == 'escan':
                if self.mode == 'trap':
                    self.hw.chip.console(b'000002.000 TRAP 4(1c): pc 0x1234, lr 0x5678\n')
                self.scans.append(payload)
            return 0, data[:outlen]
        return -23, b''

    def queue_scan(self):
        if not self.up or not (len(self.mask) > 8 and self.mask[8] & (1 << 5)):
            return
        if self.mode == 'dart_fault':
            self.hw.dma_write(ws.IOVA_BASE + 0x4000, b'\xee' * 64)      # guard page -> DART fault
        aps = list(self.APS) + [(self.APS[0][0], self.APS[0][1], self.APS[0][2], -90)]   # duplicate
        for bssid, ssid, ch, rssi in aps:
            body = struct.pack('<IIHH', 12 + 140, 109, ws.ESCAN_SYNC_ID, 1) + bss_info(bssid, ssid, ch, rssi)
            self.pending.append(('event', event_frame(ws.E_ESCAN_RESULT, ws.E_STATUS_PARTIAL, body)))
        self.pending.append(('event', event_frame(ws.E_ESCAN_RESULT, ws.E_STATUS_SUCCESS,
                                                  struct.pack('<IIHH', 12, 109, ws.ESCAN_SYNC_ID, 0))))

    def emit(self, item):
        base, nitem, sz = self.ring(2)
        self.hw.dma_write(base + self.cc_w * sz, item.ljust(sz, b'\0'))
        self.cc_w = (self.cc_w + 1) % nitem
        self.set_idx(2, 0, self.cc_w)

    def flush(self):
        _b, nitem, _s = self.ring(2)
        while self.pending:
            r = self.get_idx(3, 0)
            if r is None or (self.cc_w + 1) % nitem == r:
                return
            kind, x = self.pending[0]
            if kind == 'ack':
                self.emit(x)
            elif kind == 'cmplt':
                if not self.resp_bufs:
                    return
                st, trans, cmd, resp = x
                rid, addr, ln = self.resp_bufs.pop(0)
                self.hw.dma_write(addr, resp[:ln])
                self.emit(struct.pack('<BBBBIhHHHII', ws.MSG_IOCTL_CMPLT, 0, 0, 0, rid, st, 0, len(resp), trans, cmd, 0))
            else:
                if not self.event_bufs:
                    return
                rid, addr, ln = self.event_bufs.pop(0)
                self.hw.dma_write(addr, x[:ln])
                self.emit(struct.pack('<BBBBIhHHH8x', ws.MSG_WL_EVENT, 0, 0, 0, rid, 0, 0, len(x), 0))
            self.pending.pop(0)


class FakeXfer:
    def __init__(self, hw):
        self.hw, self.buf, self.size = hw, HEAP, wu.XFER_CHUNK

    def put(self, data):
        self.hw.heap[:len(data)] = data
        return self.buf

    def get(self, n):
        return bytes(self.hw.heap[:n])

    def close(self):
        pass


def opts(**over):
    a = wu.parse_args(['--stage', over.pop('stage', 'fw'), '--hold-seconds', '0'])
    for k, v in over.items():
        setattr(a, k, v)
    return a


def run_up(chip=None, info=None, hw_setup=None, **optkw):
    clock = tp.Clock()
    hw = FakeHW2(clock, chip=chip)
    if chip is not None and chip.fw is not None:
        chip.fw.hw = hw
    if hw_setup:
        hw_setup(hw)
    smcs = []

    def factory(base):
        s = tp.FakeSmc(hw)
        smcs.append(s)
        return s
    info = info or fake_info()
    a = opts(**optkw)
    fs = wu.FwSet(a.fw_dir, info['board'], info['antenna'])
    logs = []
    up = wu.WifiUp(hw, tp.good_facts(), factory, a, lambda: FakeXfer(hw), info, fs,
                   dma_factory=fake_dma_factory(hw), sleep=clock.sleep,
                   clock=clock, phase=lambda n, s: contextlib.nullcontext(), log=logs.append)
    return up.run(), hw, smcs, logs


def fake_dart_info(**over):
    d = dict(ws.EXPECT_DART, path=ws.DART_PATH, vm_base=0x100000, vm_size=0xBFE00000, phandle=66,
             bridge0_dart_phandle=66)
    d.update(over)
    return d


def fake_info(**over):
    i = {'board': 'atlantisb', 'antenna': 'X3', 'mac': FAKE_MAC, 'cal_len': len(FAKE_CAL), 'cal': FAKE_CAL,
         'wifi_chipset': '4378', 'dart': fake_dart_info()}
    i.update(over)
    return i


@unittest.skipUnless(FW_DIR.is_dir(), 'macOS firmware dir not present')
class Stage2(unittest.TestCase):
    def assert_clean(self, res, hw, smcs):
        pd = res['power_down']
        self.assertTrue(pd['confirmed'], pd)
        self.assertEqual(pd['stage2']['errors'], {})
        self.assertFalse(hw.wifi_powered)
        self.assertEqual(smcs[0].written[-1], wp.SMC_GPIO_OFF)
        cfg = hw.fn[0]['cfg']
        self.assertEqual(cfg[0x04] & 0x7, 0, 'endpoint decode must be off at the end')
        for o in (0x10, 0x14, 0x18, 0x1C):
            self.assertEqual(cfg[o] & ~0xF, 0, f'BAR {o:#x} not cleared')
        e = hw.f['ecam']
        for o in (0x04, 0x20, 0x24, 0x28, 0x2C):
            self.assertEqual(hw.regs.get(wp.ecam_addr(e, 0, 0, 0, o), 0), 0, f'root port {o:#x} not restored')
        self.assertTrue(all(not v & wu.CMD_BME for v in hw.ep_cmd_history), 'bus master must never be set')
        allowed = wp.build_allowlist(hw.f) | {wp.ecam_addr(e, 0, 0, 0, o) for o in (0x04, 0x20, 0x24, 0x28, 0x2C)} \
            | {wp.ecam_addr(e, 1, 0, 0, o) for o in (0x04, 0x80, 0xBC)}
        for a, _ in hw.writes:
            if CPU + 0xC0000000 <= a < CPU + 0x100000000:
                continue
            self.assertIn(a, allowed, hex(a))

    def test_fw_up(self):
        res, hw, smcs, logs = run_up()
        self.assertEqual(res['verdict'], 'PASS_FW_UP', res.get('verdict'))
        self.assertEqual(res['chip']['chip'], '0x4378')
        self.assertEqual(res['chip']['fw_rev_dir'], 'B1')
        self.assertEqual(res['otp']['V'], 'u')
        self.assertTrue(res['files']['nvram']['path'].endswith('P-atlantisb-X3_M-RASP_V-u__m-6.5.txt'))
        self.assertTrue(res['firmware_readback_match'])
        self.assertEqual(res['shared']['version'], 7)
        self.assertIn('version 18.20.511', res['console']['version_lines'][0])
        self.assertEqual(hw.chip.watchdogs, 1)
        self.assertTrue(res['chip']['cfg_mirror']['match'], res['chip']['cfg_mirror'])
        self.assertTrue(any('fw-console' in ln for ln in logs))
        self.assertFalse(any('02:00:00:00:00:01' in ln for ln in logs), 'MAC must not be logged')
        self.assert_clean(res, hw, smcs)

    def test_nvram_layout_in_tcm(self):
        res, hw, smcs, _ = run_up()
        L = {k: int(v, 16) for k, v in res['ram_layout'].items()}
        t = hw.chip.tcm
        self.assertEqual(struct.unpack_from('<II', t, L['footer']), (wu.SEED_LEN, wu.SEED_MAGIC))
        nv = bytes(t[L['nvram']:L['top'] - 4])              # last word: token, then the shared pointer
        self.assertEqual(struct.unpack_from('<I', t, L['shared_ptr'])[0], SHARED)
        self.assertIn(b'macaddr=02:00:00:00:00:01\0', nv)
        blob, _ = wu.convert_nvram(b'a=1\nb=2\n', None)
        tok = struct.unpack_from('<I', blob, len(blob) - 4)[0]
        self.assertEqual(tok & 0xFFFF, (len(blob) - 4) // 4)
        self.assertEqual(tok >> 16, ~tok & 0xFFFF)

    def test_fw_not_up(self):
        res, hw, smcs, _ = run_up(chip=FakeChip(fw_comes_up=False))
        self.assertTrue(res['verdict'].startswith('FW_NOT_UP'), res['verdict'])
        self.assert_clean(res, hw, smcs)

    def test_chip_mismatch(self):
        res, hw, smcs, _ = run_up(chip=FakeChip(chipid=0x10034387))
        self.assertTrue(res['verdict'].startswith('CHIP_MISMATCH'), res['verdict'])
        self.assert_clean(res, hw, smcs)

    def test_stage_otp_no_tcm_writes(self):
        res, hw, smcs, _ = run_up(stage='otp')
        self.assertEqual(res['verdict'], 'PASS_OTP')
        self.assertFalse(any(hw.chip.tcm[wu.RAMBASE_4378:wu.RAMBASE_4378 + 64]))
        self.assertFalse(hw.chip.started)
        self.assert_clean(res, hw, smcs)

    def test_stage_chip_no_reset(self):
        res, hw, smcs, _ = run_up(stage='chip')
        self.assertEqual(res['verdict'], 'PASS_CHIP')
        self.assertEqual(hw.chip.watchdogs, 0)
        self.assertEqual(len(res['cores']), 5)
        self.assert_clean(res, hw, smcs)

    def test_config_lost_after_watchdog(self):
        res, hw, smcs, _ = run_up(chip=FakeChip(lose_config=True))
        self.assertTrue(res['config_lost']['watchdog'])
        self.assertEqual(res['verdict'], 'PASS_FW_UP', res['verdict'])
        self.assert_clean(res, hw, smcs)

    def test_vendor_m(self):
        res, hw, smcs, _ = run_up(chip=FakeChip(otp=otp_image('m', '7.1')))
        self.assertTrue(res['files']['nvram']['path'].endswith('_V-m__m-7.1.txt'))
        self.assertEqual(res['verdict'], 'PASS_FW_UP')

    def test_unknown_modrev_stops(self):
        res, hw, smcs, _ = run_up(chip=FakeChip(otp=otp_image('u', '9.9')))
        self.assertTrue(res['verdict'].startswith('NVRAM_NOT_FOUND'), res['verdict'])
        self.assertFalse(hw.chip.started)
        self.assert_clean(res, hw, smcs)

    def test_wrong_fw_dir_rev(self):
        res, hw, smcs, _ = run_up(fw_dir=Path('/usr/share/firmware/wifi/C-4378__s-B3'))
        self.assertTrue(res['verdict'].startswith('REFUSED'), res['verdict'])
        self.assert_clean(res, hw, smcs)


@unittest.skipUnless(FW_DIR.is_dir(), 'macOS firmware dir not present')
class ScanStage(unittest.TestCase):
    """Stage 3 against the fake msgbuf firmware: DMA only through the fake DART, bus master only
    after the DART translates, everything undone at the end."""

    def run_scan(self, mode='ok', **kw):
        fw = FakeMsgbufFw(mode)
        res, hw, smcs, logs = run_up(chip=FakeChip(fw=fw), stage='scan', **kw)
        return res, hw, smcs, logs, fw

    def assert_scan_clean(self, res, hw, smcs, dart_touched=True, dart_mode='restored'):
        pd = res['power_down']
        self.assertTrue(pd['confirmed'], pd)
        self.assertEqual(pd['stage2']['errors'], {})
        self.assertEqual(hw.dart_dead_access, 0, 'no DART access after the port-0 reset (SErrors on hardware)')
        self.assertEqual(hw.dart_writes_with_bme, 0, 'DART only written while bus master is off')
        if dart_touched:
            order = list(pd['stage2']['steps'])
            self.assertLess(order.index('dma_off'), order.index('dart_teardown'))
            self.assertLess(order.index('dart_teardown'), order.index('ep_quiesce'))
            self.assertLess(order.index('dart_teardown'), order.index('dma_free'))
        self.assertFalse(hw.wifi_powered)
        self.assertEqual(smcs[0].written[-1], wp.SMC_GPIO_OFF)
        cfg = hw.fn[0]['cfg']
        self.assertEqual(cfg[0x04] & 0x7, 0, 'endpoint decode/bus master must be off at the end')
        for o in (0x10, 0x14, 0x18, 0x1C):
            self.assertEqual(cfg[o] & ~0xF, 0)
        e = hw.f['ecam']
        self.assertEqual(hw.regs.get(wp.ecam_addr(e, 0, 0, 0, 0x04), 0), 0, 'root port command restored')
        if hw.ep_cmd_history:
            self.assertFalse(hw.ep_cmd_history[-1] & wu.CMD_BME)
        for who, tcrs in hw.bme_seen_with:
            self.assertTrue(all(t == ws.DART_TCR_TRANSLATE for t in tcrs), f'{who} bus master before DART: {tcrs}')
        if dart_mode == 'restored':
            for k, v in hw.dart_initial.items():
                if k >= ws.DART_TCR or k == ws.DART_ENABLED_STREAMS:
                    self.assertEqual(hw.dart[k], v, f'DART +{k:#x} not restored')
        elif dart_mode == 'blocked':
            for k, v in hw.dart.items():
                if k >= ws.DART_TCR:
                    self.assertEqual(v, 0, f'DART +{k:#x} not blocked')
        if dart_touched:
            self.assertEqual(pd['dart'], dart_mode)
            self.assertGreaterEqual(hw.dart_invalidates, 2)
        self.assertEqual(sorted(hw.freed), sorted(hw.allocs), 'every DMA allocation freed')
        allowed = wp.build_allowlist(hw.f) | {wp.ecam_addr(e, 0, 0, 0, o) for o in (0x04, 0x20, 0x24, 0x28, 0x2C)} \
            | {wp.ecam_addr(e, 1, 0, 0, o) for o in (0x04, 0x80, 0xBC, hw.devctl_reg)} | ws.Dart.write_addrs(DART) \
            | {hw.f['port0'] + ws.PORT_RID2SID}
        for a, _ in hw.writes:
            if CPU + 0xC0000000 <= a < CPU + 0x100000000:
                continue
            self.assertIn(a, allowed, hex(a))

    def test_scan_lists_ssids(self):
        res, hw, smcs, logs, fw = self.run_scan()
        self.assertEqual(res['verdict'], 'PASS_SCAN', res.get('verdict'))
        sc = res['scan']
        self.assertEqual(len(sc['bss']), 10, 'deduplicated by BSSID')
        self.assertEqual((sc['bss'][0]['ssid'], sc['bss'][0]['rssi'], sc['bss'][0]['channel']), ('net-0', -40, 1))
        self.assertEqual(sc['bss'][0]['band'], '2G')
        self.assertIn({'ssid': 'net-3', 'bssid': '10:00:00:00:00:03', 'channel': 36, 'band': '5G', 'rssi': -49},
                      sc['bss'])
        self.assertEqual(sc['escan']['status'], 'SUCCESS')
        self.assertEqual(sc['escan']['events'], 12, '12 events through 8 recycled event buffers')
        self.assertEqual(fw.blobs['clmload'], (FW_DIR / 'atlantisb-X3.clmb').read_bytes())
        self.assertEqual(fw.blobs['txcapload'], (FW_DIR / 'atlantisb-X3.txcb').read_bytes())
        self.assertEqual(fw.blobs['calload'], FAKE_CAL)
        self.assertEqual(fw.vars['mpc'], struct.pack('<I', 0))
        self.assertTrue(fw.mask[ws.E_ESCAN_RESULT // 8] & (1 << (ws.E_ESCAN_RESULT % 8)))
        self.assertEqual(len(fw.scans), 1)
        p = fw.scans[0]
        self.assertEqual(struct.unpack_from('<IHHHH', p, 0)[:2] + (struct.unpack_from('<H', p, 8)[0],), (2, 1, 2))
        self.assertEqual(struct.unpack_from('<I', p, 56)[0], ws.SCANTYPE_PASSIVE, 'passive: no probe requests')
        self.assertEqual(fw.cmds.count(ws.C_UP), 1)
        self.assertEqual(fw.cmds[-1], ws.C_DOWN, 'radio down before power-off')
        self.assertFalse(any('join' in k or 'ssid' in k or 'wsec' in k for k in fw.vars), 'no join/association iovar')
        self.assertLessEqual(set(fw.cmds), {ws.C_GET_VERSION, ws.C_UP, ws.C_DOWN, ws.C_GET_VAR, ws.C_SET_VAR})
        self.assertTrue(sc['mac_matches_adt'])
        self.assertEqual(fw.rx_posted, 32)
        self.assertEqual(sc['host_cap'], hex(7 | ws.HOSTCAP_DS_NO_OOB_DW | ws.HOSTCAP_H2D_ENABLE_HOSTRDY))
        self.assertTrue(any('net-9' in ln and '10:00:00:00:00:09' in ln for ln in logs), 'table printed')
        self.assertFalse(any('02:00:00:00:00:01' in ln for ln in logs), 'own MAC must not be logged')
        self.assertEqual(hw.fn[0]['cfg'][hw.devctl_reg] & 0x810, 0x810, 'DevCtl restored after the run')
        self.assertTrue(res['bar_layout']['bus_master'])
        self.assertFalse(hw.dma_faults)
        self.assert_scan_clean(res, hw, smcs)

    def test_scan_dart_fault_is_contained(self):
        res, hw, smcs, logs, fw = self.run_scan('dart_fault')
        self.assertEqual(res['verdict'], 'PASS_SCAN', res.get('verdict'))
        self.assertTrue(hw.dma_faults)
        self.assertIn('dart_fault', res['scan'])
        self.assertTrue(all(hw.heap[o] != 0xEE for o in range(0x100000, 0x200000, 0x1000)))
        self.assert_scan_clean(res, hw, smcs)

    def test_saved_bypass_stream_is_not_restored(self):
        def setup(h):
            h.dart[ws.DART_TCR] = ws.DART_TCR_BYPASS
            h.dart_initial = dict(h.dart)
        res, hw, smcs, logs, fw = self.run_scan(hw_setup=setup)
        self.assertEqual(res['verdict'], 'PASS_SCAN', res.get('verdict'))
        self.assertEqual(res['scan']['dart_saved_bypass_streams'], [0])
        self.assert_scan_clean(res, hw, smcs, dart_mode='blocked')

    def test_bus_master_stuck_leaves_dart_blocked(self):
        res, hw, smcs, logs, fw = self.run_scan(hw_setup=lambda h: setattr(h, 'bme_stuck', True))
        pd = res['power_down']
        self.assertIn('dma_off', pd['stage2']['errors'])
        self.assertEqual(pd['dart'], 'blocked')
        self.assertFalse(pd['confirmed'], 'bus master not verified off -> not confirmed')
        self.assertTrue(all(v == 0 for k, v in hw.dart.items() if k >= ws.DART_TCR))
        self.assertEqual(hw.dart_dead_access, 0)

    def test_dart_inaccessible_stops_before_bus_master(self):
        res, hw, smcs, logs, fw = self.run_scan(hw_setup=lambda h: setattr(h, 'dart_dead', True))
        self.assertTrue(res['verdict'].startswith('ERROR'), res['verdict'])
        self.assertFalse(any(v & wu.CMD_BME for v in hw.ep_cmd_history + hw.rp_cmd_history))
        self.assertFalse(any(DART <= a < DART + 0x4000 for a, _ in hw.writes), 'no DART write attempted')
        self.assertEqual(res['power_down']['dart'], 'untouched')
        self.assertTrue(res['power_down']['confirmed'], res['power_down'])
        self.assertEqual(sorted(hw.freed), sorted(hw.allocs))

    def test_scan_firmware_silent(self):
        res, hw, smcs, logs, fw = self.run_scan('mute')
        self.assertTrue(res['verdict'].startswith('IOCTL_NO_REPLY'), res['verdict'])
        self.assert_scan_clean(res, hw, smcs)

    def test_scan_firmware_trap(self):
        res, hw, smcs, logs, fw = self.run_scan('trap')
        self.assertTrue(res['verdict'].startswith('FW_TRAP'), res['verdict'])
        self.assertTrue(res['scan']['console_trap_lines'])
        self.assert_scan_clean(res, hw, smcs)

    def test_scan_dart_locked_no_bus_master(self):
        res, hw, smcs, logs, fw = self.run_scan(hw_setup=lambda h: setattr(h, 'dart_lock', True))
        self.assertTrue(res['verdict'].startswith('DART_LOCKED'), res['verdict'])
        self.assertFalse(any(v & wu.CMD_BME for v in hw.ep_cmd_history + hw.rp_cmd_history))
        self.assertFalse(any(a >= DART + ws.DART_TCR and a < DART + 0x4000 for a, _ in hw.writes))
        self.assert_scan_clean(res, hw, smcs, dart_touched=False)

    def test_scan_refuses_unknown_dart(self):
        res, hw, smcs, logs = run_up(chip=FakeChip(fw=FakeMsgbufFw()), stage='scan',
                                     info=fake_info(dart=fake_dart_info(base=0x682008000)))
        self.assertTrue(res['verdict'].startswith('REFUSED'), res['verdict'])
        self.assertEqual(hw.writes, [])
        self.assertTrue(res['power_down']['nothing_was_done'])

    def test_active_scan_option(self):
        res, hw, smcs, logs, fw = self.run_scan(scan_type='active', country='KR', no_cal=True, rid2sid='map')
        self.assertEqual(res['verdict'], 'PASS_SCAN')
        rs = hw.f['port0'] + ws.PORT_RID2SID
        self.assertIn((rs, ws.RID2SID_VALID | (1 << 16) | 0x100), hw.writes)
        self.assertEqual(hw.regs.get(rs, 0), 0, 'RID2SID entry restored')
        self.assertEqual(struct.unpack_from('<I', fw.scans[0], 56)[0], ws.SCANTYPE_ACTIVE)
        self.assertEqual(fw.vars['country'][:4], b'KR\0\0')
        self.assertNotIn('calload', fw.blobs)
        self.assert_scan_clean(res, hw, smcs)


class ScanUnits(unittest.TestCase):
    def test_layout_pages_and_guards(self):
        L = ws.dma_layout(42, 3, 4, 32, 7)
        regs = sorted(L['regions'].values())
        mapped = set(L['pages'])
        for (o, s), (o2, _s2) in zip(regs, regs[1:]):
            self.assertEqual(o % ws.DART_PAGE, 0)
            end = o + ((s + ws.DART_PAGE - 1) // ws.DART_PAGE) * ws.DART_PAGE
            self.assertGreaterEqual(o2, end + ws.DART_PAGE)
            self.assertNotIn(end, mapped, 'guard page after each buffer is unmapped')
        self.assertLessEqual(L['size'], ws.IOVA_SPAN)
        self.assertEqual(L['rings']['tx_complete'][2], 24)
        self.assertEqual(L['rings']['rx_complete'][2], 40)
        self.assertEqual(ws.dma_layout(42, 3, 4, 0, 6)['rings']['rx_complete'][2], 32)

    def test_tables_walk(self):
        maps = [(ws.IOVA_BASE + 0x8000, HEAP + 0x40000), (ws.IOVA_BASE + 0x10000, HEAP + 0x80000)]
        l1, l2, ttbr, idx = ws.build_tables(HEAP + 0x200000, HEAP + 0x204000, maps)
        self.assertEqual(idx, ws.IOVA_BASE >> 25)
        look = lambda a: l2 if a == HEAP + 0x204000 else bytes(0x4000)
        self.assertEqual(ws.walk_tables(l1, look, ttbr, ws.IOVA_BASE + 0x8010), HEAP + 0x40010)
        self.assertIsNone(ws.walk_tables(l1, look, ttbr, ws.IOVA_BASE + 0xC000))
        with self.assertRaises(ValueError):
            ws.build_tables(HEAP + 0x200000, HEAP + 0x204000, [(ws.IOVA_BASE, HEAP), (ws.IOVA_BASE + ws.IOVA_SPAN, HEAP)])

    def test_escan_params(self):
        self.assertEqual(len(ws.escan_params(2)), 80)
        self.assertEqual(len(ws.escan_params(0)), 72)
        self.assertEqual(struct.unpack_from('<IHH', ws.escan_params(0), 0), (1, 1, ws.ESCAN_SYNC_ID))

    def test_dload_chunks(self):
        blob = bytes(range(256)) * 133 + b'xyz'
        ch = ws.dload_chunks(blob)
        self.assertEqual(len(ch), (len(blob) + 1399) // 1400)
        f0, f1 = struct.unpack_from('<H', ch[0])[0], struct.unpack_from('<H', ch[-1])[0]
        self.assertTrue(f0 & ws.DLOAD_FLAG_BEGIN and not f0 & ws.DLOAD_FLAG_END)
        self.assertTrue(f1 & ws.DLOAD_FLAG_END and not f1 & ws.DLOAD_FLAG_BEGIN)
        self.assertEqual(b''.join(c[12:] for c in ch), blob)
        one = ws.dload_chunks(b'a' * 304)
        self.assertEqual(struct.unpack_from('<H', one[0])[0] & 0x6, 0x6)

    def test_bss_parse_and_dedup(self):
        rows = [ws.parse_bss(bss_info(bytes([1, 2, 3, 4, 5, 6]), b'cafe', 149, -61)),
                ws.parse_bss(bss_info(bytes([1, 2, 3, 4, 5, 6]), b'cafe', 149, -70)),
                ws.parse_bss(bss_info(bytes([1, 2, 3, 4, 5, 7]), b'', 6, -50))]
        self.assertEqual((rows[0]['channel'], rows[0]['band'], rows[0]['rssi']), (149, '5G', -61))
        t = ws.bss_table(rows)
        self.assertEqual([(r['bssid'][-2:], r['rssi']) for r in t], [('07', -50), ('06', -61)])
        self.assertEqual(ws.show_ssid(b''), '<hidden>')
        self.assertEqual(ws.show_ssid(b'a\x01b'), 'a?b')
        ev = ws.parse_event(event_frame(69, 8, b'abc'))
        self.assertEqual((ev['type'], ev['status'], ev['data']), (69, 8, b'abc'))
        self.assertIsNone(ws.parse_event(b'\0' * 80))

    def test_bus_master_guard(self):
        clock = tp.Clock()
        hw = FakeHW2(clock)
        m = wu.RangeMmio(hw, {0x1000})
        m.cmd_regs = frozenset({0x1000})
        with self.assertRaises(wp.Refusal):
            m.w32(0x1000, 0x6)
        m.w32(0x1000, 0x2)
        m.bme_ok = True
        m.w32(0x1000, 0x6)

    def test_dart_info_check(self):
        self.assertIsNone(ws.check_dart_info(fake_dart_info()))
        self.assertIn('bridge0', ws.check_dart_info(fake_dart_info(bridge0_dart_phandle=1)) or '')
        self.assertIn('vm range', ws.check_dart_info(fake_dart_info(vm_size=0x1000)) or '')

    def test_scan_args(self):
        for bad in (['--scan-seconds', '40'], ['--country', 'kr'], ['--rxbufs', '999']):
            with self.assertRaises(SystemExit):
                wu.parse_args(['--stage', 'scan'] + bad)


@unittest.skipUnless(FW_DIR.is_dir(), 'macOS firmware dir not present')
class MainGlue(unittest.TestCase):
    def test_main_fw_exit0(self):
        import tempfile
        import types
        clock = tp.Clock()
        hw = FakeHW2(clock)
        info = {'board': 'atlantisb', 'antenna': 'X3', 'mac': None, 'cal_len': 2096, 'wifi_chipset': '4378'}
        lsof = lambda *a, **k: types.SimpleNamespace(returncode=1, stdout=b'', stderr=b'')
        conn = lambda a: (hw, tp.good_facts(), lambda b: tp.FakeSmc(hw), lambda: FakeXfer(hw), info, lambda: None)
        logs = []
        with tempfile.TemporaryDirectory() as d:
            rc = wu.main(['--run', '--stage', 'fw', '--hold-seconds', '0', '--out-dir', d], runner=lsof,
                         connect=conn, phase=lambda n, s: contextlib.nullcontext(), log=logs.append)
            out = list(Path(d).glob('run-*/result.json'))
            self.assertEqual(len(out), 1)
        self.assertEqual(rc, 0, logs[-3:])
        self.assertTrue(any('S257 verdict PASS_FW_UP' in ln for ln in logs))

    def test_main_scan_exit0(self):
        import tempfile
        import types
        clock = tp.Clock()
        fw = FakeMsgbufFw()
        hw = FakeHW2(clock, chip=FakeChip(fw=fw))
        fw.hw = hw
        lsof = lambda *a, **k: types.SimpleNamespace(returncode=1, stdout=b'', stderr=b'')
        conn = lambda a: (hw, tp.good_facts(), lambda b: tp.FakeSmc(hw), lambda: FakeXfer(hw), fake_info(),
                          lambda: None, fake_dma_factory(hw))
        logs = []
        with tempfile.TemporaryDirectory() as d:
            rc = wu.main(['--run', '--stage', 'scan', '--hold-seconds', '0', '--out-dir', d], runner=lsof,
                         connect=conn, phase=lambda n, s: contextlib.nullcontext(), log=logs.append)
            out = list(Path(d).glob('run-*/result.json'))
            self.assertEqual(len(out), 1)
            txt = out[0].read_text()
        self.assertEqual(rc, 0, logs[-3:])
        self.assertTrue(any('S257 verdict PASS_SCAN' in ln for ln in logs))
        self.assertNotIn('02:00:00:00:00:01', txt, 'own MAC not in result.json')
        self.assertNotIn('"cal"', txt)


class Units(unittest.TestCase):
    def test_convert_nvram(self):
        blob, n = wu.convert_nvram(b'# c\r\nkey =1\nslice/1/x =2\n\nlast=3', None)
        self.assertEqual(n, 3)
        body = blob[:-4]
        self.assertTrue(body.startswith(b'key=1\0slice/1/x=2\0last=3\0'))
        self.assertEqual(len(body) % 4, 0)

    def test_parse_otp(self):
        o = wu.parse_otp(otp_image('m', '6.11'))
        self.assertEqual((o['M'], o['V'], o['m'], o['s']), ('RASP', 'm', '6.11', 'B1'))

    def test_bar_base_guard(self):
        with self.assertRaises(SystemExit):
            wu.parse_args(['--bar-base', '0xFFF80000'])


if __name__ == '__main__':
    unittest.main(verbosity=1)
