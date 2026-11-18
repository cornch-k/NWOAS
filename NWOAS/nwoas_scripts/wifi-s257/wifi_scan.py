# SPDX-License-Identifier: ISC
#
# NWOAS S257 Wi-Fi stage 3 (`wifi_up.py --stage scan`): after the firmware is up, set up the PCIe
# msgbuf protocol (common rings + host DMA index buffers) in m1n1-proxy heap memory that is mapped
# through the port-0 DART, talk to the firmware with ioctls/iovars, load CLM/txcap/calibration,
# bring the interface up and run ONE escan. No join, no association, no data frames.
#
# The protocol side is re-implemented in Python from OpenBSD bwfm (sys/dev/pci/if_bwfm_pci.c
# bwfm_pci_preinit/setup_ring/ring_*/msg_rx/msgbuf_*, sys/dev/ic/bwfm.c bwfm_preinit/bwfm_init/
# bwfm_scan_v0/v2/bwfm_rx_event_cb/bwfm_process_blob, bwfmreg.h, if_bwfm_pci.h), cross-checked
# against Linux brcmfmac pcie.c/msgbuf.h (both ISC). Their notices:
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
# DART t8020 register offsets and PTE format: m1n1 proxyclient m1n1/hw/dart8020.py (MIT); the same
# values are in our UEFI AppleDartIoMmuDxe.h. No GPL code (Linux apple-dart.c, pcie-apple.c) is copied.
"""S257 scan stage: DART + msgbuf rings + iovars + one passive escan (scan only, no join)."""
import re
import struct

# ---------------------------------------------------------------- DART (t8020)
DART_STREAM_COMMAND, DART_CMD_INVALIDATE, DART_CMD_BUSY = 0x20, 1 << 20, 1 << 2
DART_STREAM_SELECT = 0x34
DART_ERROR, DART_ERROR_FLAG = 0x40, 1 << 31
DART_ERROR_ADDR_LO, DART_ERROR_ADDR_HI = 0x50, 0x54
DART_CONFIG, DART_CONFIG_LOCK = 0x60, 1 << 15
DART_ENABLED_STREAMS = 0xFC
DART_TCR, DART_TTBR = 0x100, 0x200
DART_TCR_TRANSLATE, DART_TCR_BYPASS = 1 << 7, 1 << 8
DART_TTBR_VALID = 1 << 31
DART_NSTREAMS, DART_NTTBR = 16, 4
DART_PAGE = 0x4000
PTE_VALID, PTE_SP_PROT_DIS, PTE_SP_END_FULL = 0x1, 0x2, 0xFFF << 40
PTE_OFFSET_MASK = ((1 << 40) - 1) & ~(DART_PAGE - 1)          # OFFSET = bits 39:14
L1_SHIFT, L2_SHIFT, LX_MASK = 25, 14, 0x7FF
INVALIDATE_POLLS = 1000

# FACT (offline ADT experiments/tahoe-afk-20260906/current.adt): /arm-io/dart-apcie0, compatible
# dart,t8020, reg 0x681008000+0x4000, page-size 16384, vm-base 0x100000, vm-size 0xBFE00000; the
# pci-bridge0 function-dart_* properties and wlan iommu-parent (mapper-apcie0-wlan) point at it.
DART_PATH = '/arm-io/dart-apcie0'
EXPECT_DART = {'base': 0x681008000, 'size': 0x4000, 'compat': 'dart,t8020', 'page_size': 0x4000}
# IOVA the chip sees for our buffers. Inside the ADT vm window, far below the 32-bit MMIO window
# 0xC0000000.. (so the root port never takes a DMA address for a downstream BAR), 32 MiB aligned
# (one DART L1 entry = 32 MiB, so one L1 page + one L2 page describe every mapping).
IOVA_BASE = 0x10000000
IOVA_SPAN = 1 << L1_SHIFT

# ---------------------------------------------------------------- shared RAM (if_bwfm_pci.h)
SH_FLAGS, SH_CONSOLE, SH_MAX_RXBUFPOST, SH_RX_DATAOFFSET = 0x00, 0x14, 0x22, 0x24
SH_HTOD_MB, SH_DTOH_MB, SH_RING_INFO = 0x28, 0x2C, 0x30
SH_SCRATCH_LEN, SH_SCRATCH_LO, SH_SCRATCH_HI = 0x34, 0x38, 0x3C
SH_RINGUPD_LEN, SH_RINGUPD_LO, SH_RINGUPD_HI = 0x40, 0x44, 0x48
SH_HOST_CAP, SH_HOST_CAP2 = 0x54, 0x70
SH_WORDS = 0x78 // 4
FLAG_DMA_INDEX, FLAG_DMA_2B_IDX = 0x10000, 0x100000
FLAG_USE_MAILBOX, FLAG_HOSTRDY_DB1, FLAG_SHARED_DAR = 0x2000000, 0x10000000, 0x80000000
HOSTCAP_H2D_ENABLE_HOSTRDY, HOSTCAP_DS_NO_OOB_DW, HOSTCAP_H2D_DAR = 0x400, 0x1000, 0x10000
DEFAULT_MAX_RXBUFPOST = 255
SCRATCH_LEN, RINGUPD_LEN = 8, 1024                        # BWFM_DMA_D2H_SCRATCH/RINGUPD_BUF_LEN
D2H_DEV_D3_ACK, D2H_DEV_DS_ENTER_REQ, D2H_DEV_DS_EXIT_NOTE, D2H_DEV_FWHALT = 0x1, 0x2, 0x4, 0x10000000
H2D_HOST_DS_ACK = 0x2
# ring info (struct bwfm_pci_ringinfo): ringmem, 4 TCM index pointers, 4 host address pairs, counts
RI_RINGMEM, RI_TCM_IDX, RI_HOSTADDR, RI_COUNTS = 0x00, 0x04, 0x14, 0x34
RI_LEN = 0x3C
RING_MEM_SZ, RING_MAX_ITEM, RING_BASE_LO = 16, 0x04, 0x08

# PCIe2 core mailbox registers (if_bwfm_pci.h; brcmfmac uses the _64 set for core rev >= 64)
PCIE2_MAILBOXINT, PCIE2_MAILBOXMASK, PCIE2_H2D_MB0, PCIE2_H2D_MB1 = 0x48, 0x4C, 0x140, 0x144
PCIE2_64_MAILBOXINT, PCIE2_64_MAILBOXMASK, PCIE2_64_H2D_MB0, PCIE2_64_H2D_MB1 = 0xC30, 0xC34, 0xA20, 0xA24
BAR0_PCIE_ENUM_OFFSET = 0x2000                          # bwfm sc_pcie_ioh = BAR0 + 0x2000

# ---------------------------------------------------------------- msgbuf (if_bwfm_pci.h)
MSG_GEN_STATUS, MSG_RING_STATUS = 0x1, 0x2
MSG_IOCTLPTR_REQ, MSG_IOCTLPTR_REQ_ACK, MSG_IOCTLRESP_BUF_POST, MSG_IOCTL_CMPLT = 0x9, 0xA, 0xB, 0xC
MSG_EVENT_BUF_POST, MSG_WL_EVENT = 0xD, 0xE
MSG_TX_STATUS, MSG_RXBUF_POST, MSG_RX_CMPLT = 0x10, 0x11, 0x12
MSG_H2D_MAILBOX_DATA, MSG_D2H_MAILBOX_DATA = 0x23, 0x24
IOCTL_REQ_PKTID = 0xFFFE
IOCTL_MAX_LEN = 1518                                    # BWFM_DMA_H2D_IOCTL_BUF_LEN = ETHER_MAX_LEN
CTL_PKT_SIZE, RX_PKT_SIZE = 8192, 2048                  # MSGBUF_MAX_CTL_PKT_SIZE / MSGBUF_MAX_PKT_SIZE
N_IOCTLRESP, N_EVENT = 8, 8                             # bwfm if_rxr_init(..., 8, 8) for both
ID_IOCTLRESP, ID_EVENT, ID_RX = 0x1000, 0x2000, 0x3000
# (name, direction, index in its idx array, max items, item size v7, item size < v7)
RINGS = (('ctrl_submit', 'h2d', 0, 64, 40, 40), ('rxpost', 'h2d', 1, 1024, 32, 32),
         ('ctrl_complete', 'd2h', 0, 64, 24, 24), ('tx_complete', 'd2h', 1, 1024, 24, 16),
         ('rx_complete', 'd2h', 2, 1024, 40, 32))

# ---------------------------------------------------------------- dongle commands (bwfmreg.h)
C_GET_VERSION, C_UP, C_DOWN, C_GET_VAR, C_SET_VAR = 1, 2, 3, 262, 263
IO_TYPE_D11N, IO_TYPE_D11AC = 1, 2
E_ESCAN_RESULT = 69
E_STATUS_SUCCESS, E_STATUS_PARTIAL = 0, 8
E_STATUS_NAMES = {0: 'SUCCESS', 1: 'FAIL', 2: 'TIMEOUT', 3: 'NO_NETWORKS', 4: 'ABORT', 5: 'NO_ACK',
                  6: 'UNSOLICITED', 7: 'ATTEMPT', 8: 'PARTIAL', 9: 'NEWSCAN', 10: 'NEWASSOC',
                  11: '11HQUIET', 12: 'SUPPRESS', 13: 'NOCHANS', 15: 'CS_ABORT', 16: 'ERROR'}
EVENT_MASK_LENS = (18, 16, 20, 24)                      # bwfm BWFM_EVENT_MASK_LEN = 18 first
ETHERTYPE_LINK_CTL, BRCM_OUI, BRCM_SUBTYPE_EVENT = 0x886C, b'\x00\x10\x18', 1
EVENT_HDR_LEN = 14 + 10 + 48                            # ether_header + bwfm_ethhdr + bwfm_event_msg
DLOAD_FLAG_BEGIN, DLOAD_FLAG_END, DLOAD_HANDLER_VER_1, DLOAD_TYPE_CLM = 0x2, 0x4, 0x1000, 2
DLOAD_MAX_LEN = 1400
ESCAN_REQ_VERSION, ESCAN_REQ_VERSION_V2, ESCAN_ACTION_START = 1, 2, 1
SCANTYPE_ACTIVE, SCANTYPE_PASSIVE, BSSTYPE_ANY = 0, 1, 2
ESCAN_SYNC_ID = 0x5257
BSS_INFO_FIXED = 128                                    # sizeof(struct bwfm_bss_info) incl. padding
MAX_BSS = 256
MAX_TABLE_ROWS = 100

IOCTL_TIMEOUT_S = 3.0                                   # bwfm sleeps 5 s, brcmfmac 2 s
POLL_S = 0.01
HEALTH_EVERY_S = 0.5
MAX_SCAN_CONSOLE_LINES = 200
MAX_MSG_LOG = 40
TRAP_RE = re.compile(rb'\b(TRAP|ASSERT|FWHALT)\b')
# Port-0 RID->SID table (register offsets/bits: our UEFI AppleSiliconPciPlatformDxe.h PORT_RID2SID
# 0x828, VALID bit 31, SID shift 16). Read and logged always; written only with --rid2sid map.
PORT_RID2SID, RID2SID_VALID, RID2SID_SID_SHIFT = 0x828, 1 << 31, 16
WIFI_RID, LINUX_WIFI_SID = 0x100, 1                   # bus 1 dev 0 fn 0; t8103.dtsi iommu-map SID 1


class FwDead(Exception):
    """The firmware trapped / halted / restarted (seen on the console, mailbox or shared pointer)."""


class IoctlTimeout(Exception):
    pass


def align_up(v, a):
    return (v + a - 1) // a * a


# ---------------------------------------------------------------- ADT (no hardware)

def dart_info(adt):
    """Port-0 DART from the ADT, plus the structural link pci-bridge0 -> this DART."""
    d = adt[DART_PATH]
    base, size = d.get_reg(0)
    out = {'path': DART_PATH, 'base': base, 'size': size, 'compat': str(d.compatible[0]),
           'page_size': int(d.getprop('page-size') or 0), 'vm_base': int(d.getprop('vm-base') or 0),
           'vm_size': int(d.getprop('vm-size') or 0), 'phandle': d.getprop('AAPL,phandle')}
    b0 = adt['/arm-io/apcie/pci-bridge0']
    f = b0.getprop('function-dart_self')
    out['bridge0_dart_phandle'] = getattr(f, 'phandle', None)
    return out


def check_dart_info(d):
    """Refusal text (or None) for a DART description that is not the recorded J274 one."""
    if not isinstance(d, dict) or 'error' in d:
        return f'ADT DART lookup failed: {d}'
    for k, v in EXPECT_DART.items():
        if d.get(k) != v:
            return f'ADT {DART_PATH} {k}={d.get(k)!r} differs from recorded {v!r}'
    if d.get('bridge0_dart_phandle') != d.get('phandle'):
        return f'pci-bridge0 function-dart_self does not point at {DART_PATH}'
    lo, hi = d.get('vm_base', 0), d.get('vm_base', 0) + d.get('vm_size', 0)
    if not (lo <= IOVA_BASE and IOVA_BASE + IOVA_SPAN <= hi):
        return f'IOVA window {IOVA_BASE:#x}+{IOVA_SPAN:#x} outside the ADT vm range {lo:#x}..{hi:#x}'
    return None


# ---------------------------------------------------------------- DMA layout (pure)

def dma_layout(max_sub, max_compl, idx_sz, n_rxbuf, shared_version):
    """Arena layout: every buffer starts on its own 16 KiB DART page and is followed by one
    unmapped guard page, so a run-over faults in the DART instead of reaching the next buffer.
    Returns {'regions': {name: (offset, size)}, 'pages': [mapped page offsets], 'size': arena bytes,
    'idx': {array: offset}, 'rings': {name: (offset, nitem, itemsz)}, ...}."""
    v7 = shared_version >= 7
    regions, pages = {}, []
    off = 0

    def take(name, size):
        nonlocal off
        regions[name] = (off, size)
        n = align_up(max(size, 1), DART_PAGE) // DART_PAGE
        pages.extend(off + i * DART_PAGE for i in range(n))
        off += (n + 1) * DART_PAGE                       # + guard page
        return regions[name][0]

    idx = {}
    if idx_sz:
        stride = align_up(max(max_sub, max_compl) * idx_sz, 256)
        base = take('idx', 4 * stride)
        for i, name in enumerate(('h2d_w', 'h2d_r', 'd2h_w', 'd2h_r')):
            idx[name] = base + i * stride
    rings = {}
    for name, _d, _i, nitem, sz7, sz6 in RINGS:
        itemsz = sz7 if v7 else sz6
        rings[name] = (take(f'ring_{name}', nitem * itemsz), nitem, itemsz)
    fw = take('fw_scratch', 256 + RINGUPD_LEN)
    req = take('ioctl_req', IOCTL_MAX_LEN)
    resp = take('ioctl_resp', N_IOCTLRESP * CTL_PKT_SIZE)
    ev = take('event', N_EVENT * CTL_PKT_SIZE)
    rx = take('rxbuf', n_rxbuf * RX_PKT_SIZE) if n_rxbuf else None
    size = off
    if size > IOVA_SPAN:
        raise ValueError(f'DMA arena {size:#x} larger than one DART L1 entry')
    return {'regions': regions, 'pages': pages, 'size': size, 'idx': idx, 'idx_sz': idx_sz,
            'rings': rings, 'scratch': fw, 'ringupd': fw + 256, 'ioctl_req': req, 'ioctl_resp': resp,
            'event': ev, 'rxbuf': rx, 'n_rxbuf': n_rxbuf}


def build_tables(l1_pa, l2_pa, maps):
    """One L1 page and one L2 page for (iova, pa) 16 KiB mappings that all live in one 32 MiB L1
    slot. PTE format = m1n1 dart8020 PTE_T8020 (SP_START 0, SP_END 0xfff, OFFSET, SP_PROT_DIS, VALID).
    Returns (l1 bytes, l2 bytes, TTBR value, l1 index)."""
    if not maps:
        raise ValueError('nothing to map')
    l1i = {(iova >> L1_SHIFT) & LX_MASK for iova, _ in maps}
    if len(l1i) != 1 or any(iova >> 36 for iova, _ in maps):
        raise ValueError('mappings must share one L1 slot below 64 GiB (TTBR0)')
    for a in [l1_pa, l2_pa] + [x for m in maps for x in m]:
        if a & (DART_PAGE - 1):
            raise ValueError(f'unaligned address {a:#x}')
    l1 = [0] * (LX_MASK + 1)
    l2 = [0] * (LX_MASK + 1)
    (i1,) = l1i
    l1[i1] = (l2_pa & PTE_OFFSET_MASK) | PTE_SP_PROT_DIS | PTE_VALID
    for iova, pa in maps:
        i2 = (iova >> L2_SHIFT) & LX_MASK
        if l2[i2]:
            raise ValueError(f'IOVA {iova:#x} mapped twice')
        l2[i2] = PTE_SP_END_FULL | (pa & PTE_OFFSET_MASK) | PTE_SP_PROT_DIS | PTE_VALID
    ttbr = DART_TTBR_VALID | (l1_pa >> 12)
    if (l1_pa >> 12) >> 31:
        raise ValueError('L1 table above the TTBR address range')
    return struct.pack('<2048Q', *l1), struct.pack('<2048Q', *l2), ttbr, i1


def walk_tables(l1_bytes, l2_lookup, ttbr, iova):
    """Software walk of the tables as the DART would do it (for the self-check and the fake DART)."""
    if not ttbr & DART_TTBR_VALID:
        return None
    e1 = struct.unpack_from('<Q', l1_bytes, 8 * ((iova >> L1_SHIFT) & LX_MASK))[0]
    if not e1 & PTE_VALID:
        return None
    l2 = l2_lookup(e1 & PTE_OFFSET_MASK)
    e2 = struct.unpack_from('<Q', l2, 8 * ((iova >> L2_SHIFT) & LX_MASK))[0]
    if not e2 & PTE_VALID:
        return None
    return (e2 & PTE_OFFSET_MASK) | (iova & (DART_PAGE - 1))


# ---------------------------------------------------------------- message packing (pure)

def pack_ioctl_req(cmd, trans_id, length, iova):
    return struct.pack('<BBBBIIHHH6xII8x', MSG_IOCTLPTR_REQ, 0, 0, 0, IOCTL_REQ_PKTID, cmd, trans_id,
                       length, length, iova & 0xFFFFFFFF, iova >> 32)


def pack_buf_post(msgtype, pktid, length, iova):
    """IOCTLRESP_BUF_POST / EVENT_BUF_POST (struct msgbuf_rx_ioctl_resp_or_event, 40 bytes)."""
    return struct.pack('<BBBBIH6xII16x', msgtype, 0, 0, 0, pktid, length, iova & 0xFFFFFFFF, iova >> 32)


def pack_rxbuf_post(pktid, length, iova):
    """RXBUF_POST (struct msgbuf_rx_bufpost, 32 bytes): no metadata buffer."""
    return struct.pack('<BBBBIHHIIIII', MSG_RXBUF_POST, 0, 0, 0, pktid, 0, length, 0, 0, 0,
                       iova & 0xFFFFFFFF, iova >> 32)


def pack_h2d_mailbox(data):
    return struct.pack('<BBBBII28x', MSG_H2D_MAILBOX_DATA, 0xFF, 0, 0, 0, data)


def dload_chunks(blob, max_len=DLOAD_MAX_LEN):
    """bwfm_process_blob: [flag u16, type u16, len u32, crc u32] + data, BEGIN/END flags."""
    out, off = [], 0
    while off < len(blob):
        n = min(max_len, len(blob) - off)
        flag = DLOAD_HANDLER_VER_1 | (DLOAD_FLAG_BEGIN if off == 0 else 0) | \
            (DLOAD_FLAG_END if off + n >= len(blob) else 0)
        out.append(struct.pack('<HHII', flag, DLOAD_TYPE_CLM, n, 0) + blob[off:off + n])
        off += n
    return out


def escan_params(scan_ver_major, scan_type=SCANTYPE_PASSIVE, sync_id=ESCAN_SYNC_ID):
    """bwfm_scan_v0 / bwfm_scan_v2: no SSID, no channel list (= every channel the CLM allows),
    broadcast BSSID, any BSS type, firmware default dwell/probe values (-1)."""
    m1 = 0xFFFFFFFF
    ssid = bytes(36)
    bssid = b'\xff' * 6
    if scan_ver_major in (2, 3):
        # HYPOTHESIS for 3: brcmfmac keeps the v2 layout and uses the pad byte as ssid_type
        body = struct.pack('<HH', scan_ver_major, 80) + ssid + bssid + struct.pack(
            '<BBIIIIII', BSSTYPE_ANY, 0, scan_type, m1, m1, m1, m1, 0)
        return struct.pack('<IHH', ESCAN_REQ_VERSION_V2, ESCAN_ACTION_START, sync_id) + body
    body = ssid + bssid + struct.pack('<BBIIIII', BSSTYPE_ANY, scan_type, m1, m1, m1, m1, 0)
    return struct.pack('<IHH', ESCAN_REQ_VERSION, ESCAN_ACTION_START, sync_id) + body


def event_mask(length, events=(E_ESCAN_RESULT,)):
    m = bytearray(length)
    for e in events:
        m[e // 8] |= 1 << (e % 8)
    return bytes(m)


def parse_event(data):
    """Firmware event frame (ether header + BRCM header + big-endian bwfm_event_msg + data)."""
    if len(data) < EVENT_HDR_LEN:
        return None
    if struct.unpack_from('>H', data, 12)[0] != ETHERTYPE_LINK_CTL or data[19:22] != BRCM_OUI \
            or struct.unpack_from('>H', data, 22)[0] != BRCM_SUBTYPE_EVENT:
        return None
    ver, flags, etype, status, reason, auth, dlen = struct.unpack_from('>HHIIIII', data, 24)
    body = data[EVENT_HDR_LEN:EVENT_HDR_LEN + dlen]
    return {'type': etype, 'status': status, 'reason': reason, 'datalen': dlen, 'data': body,
            'ifidx': data[EVENT_HDR_LEN - 2]}


def parse_bss(b, io_type=IO_TYPE_D11AC):
    """struct bwfm_bss_info: bssid @8, ssid_len @18, ssid @19, chanspec @72, rssi @78, ctl_ch @88."""
    if len(b) < BSS_INFO_FIXED:
        return None
    ver, length = struct.unpack_from('<II', b, 0)
    ssid_len = min(b[18], 32)
    chanspec = struct.unpack_from('<H', b, 72)[0]
    rssi = struct.unpack_from('<h', b, 78)[0]
    ctl = b[88]
    chan = ctl or (chanspec & 0xFF)
    if io_type == IO_TYPE_D11N:
        band = {0x1: '5G', 0x2: '2G'}.get((chanspec >> 12) & 0x3, '?')
    else:
        band = {0: '2G', 3: '5G'}.get((chanspec >> 14) & 0x3, '?')
    return {'bssid': ':'.join(f'{x:02x}' for x in b[8:14]), 'ssid': bytes(b[19:19 + ssid_len]),
            'channel': chan, 'band': band, 'rssi': rssi, 'chanspec': chanspec, 'version': ver,
            'length': length}


def parse_escan(body, io_type=IO_TYPE_D11AC):
    """struct bwfm_escan_results {buflen, version, sync_id, bss_count} + length-prefixed bss_info."""
    if len(body) < 12:
        return None, []
    buflen, ver, sync_id, count = struct.unpack_from('<IIHH', body, 0)
    out, off = [], 12
    end = min(len(body), max(buflen, 12))
    for _ in range(min(count, 64)):
        if off + BSS_INFO_FIXED > end:
            break
        length = struct.unpack_from('<I', body, off + 4)[0]
        if length < BSS_INFO_FIXED or off + length > end:
            break
        row = parse_bss(body[off:off + length], io_type)
        if row:
            out.append(row)
        off += length
    return sync_id, out


def show_ssid(raw):
    if not raw:
        return '<hidden>'
    s = raw.decode('utf-8', 'replace')
    return ''.join(c if c.isprintable() else '?' for c in s)[:32]


def bss_table(rows):
    """Deduplicated by BSSID (strongest RSSI kept), strongest first."""
    best = {}
    for r in rows:
        k = r['bssid']
        if k not in best or r['rssi'] > best[k]['rssi']:
            best[k] = r
    return sorted(best.values(), key=lambda r: (-r['rssi'], r['bssid']))


# ---------------------------------------------------------------- host memory for DMA

class DmaArena:
    """A block of m1n1-proxy heap RAM (physical address `pa`). Every access is bounds-checked; writes
    are followed by a data-cache clean and reads are preceded by clean+invalidate (to the point of
    coherency), so the result is right whether or not the PCIe path snoops the CPU caches."""

    LINE = 64                  # m1n1 CACHE_LINE_SIZE; its *_range ops step from the given address

    def __init__(self, p, iface, pa, size, release=None):
        if pa & (DART_PAGE - 1) or size % DART_PAGE:
            raise ValueError('DMA arena must be whole DART pages')
        self.p, self.iface, self.pa, self.size, self._release = p, iface, pa, size, release
        self.p.memset32(pa, 0, size)
        self._clean(pa, size)

    def _lines(self, a, n):
        lo = a & ~(self.LINE - 1)
        return lo, align_up(a + max(n, 1), self.LINE) - lo

    def _clean(self, a, n):
        self.p.dc_cvac(*self._lines(a, n))

    def _inval(self, a, n):
        self.p.dc_civac(*self._lines(a, n))

    def _chk(self, off, n):
        if not (0 <= off and n >= 0 and off + n <= self.size):
            raise ValueError(f'DMA arena access {off:#x}+{n:#x} outside {self.size:#x}')
        return self.pa + off

    def write(self, off, data):
        a = self._chk(off, len(data))
        self.iface.writemem(a, bytes(data))
        self._clean(a, len(data))

    def read(self, off, n):
        a = self._chk(off, n)
        self._inval(a, n)
        return bytes(self.iface.readmem(a, n))

    def zero(self, off=0, n=None):
        n = self.size - off if n is None else n
        a = self._chk(off, n)
        self.p.memset32(a, 0, n)
        self._clean(a, n)

    def w32(self, off, v):
        a = self._chk(off, 4)
        self.p.write32(a, v & 0xFFFFFFFF)
        self._clean(a, 4)

    def r32(self, off):
        a = self._chk(off, 4)
        self._inval(a, 4)
        return self.p.read32(a) & 0xFFFFFFFF

    def close(self):
        if self._release is not None:
            rel, self._release = self._release, None
            rel(self.pa)


# ---------------------------------------------------------------- DART register access

class Dart:
    """Port-0 DART: every write goes through the probe's exact-address allowlist."""

    def __init__(self, m, base):
        self.m, self.base = m, base

    @staticmethod
    def write_addrs(base):
        a = {base + o for o in (DART_STREAM_COMMAND, DART_STREAM_SELECT, DART_ERROR, DART_ENABLED_STREAMS)}
        a |= {base + DART_TCR + 4 * s for s in range(DART_NSTREAMS)}
        a |= {base + DART_TTBR + 16 * s + 4 * j for s in range(DART_NSTREAMS) for j in range(DART_NTTBR)}
        return a

    def r(self, off):
        return self.m.r32(self.base + off)

    def w(self, off, v):
        self.m.w32(self.base + off, v)

    def snapshot(self):
        return {'config': self.r(DART_CONFIG), 'enabled': self.r(DART_ENABLED_STREAMS),
                'error': self.r(DART_ERROR), 'error_addr': (self.r(DART_ERROR_ADDR_HI) << 32) | self.r(DART_ERROR_ADDR_LO),
                'tcr': [self.r(DART_TCR + 4 * s) for s in range(DART_NSTREAMS)],
                'ttbr': [[self.r(DART_TTBR + 16 * s + 4 * j) for j in range(DART_NTTBR)]
                         for s in range(DART_NSTREAMS)]}

    def invalidate(self):
        self.w(DART_STREAM_SELECT, (1 << DART_NSTREAMS) - 1)
        self.w(DART_STREAM_COMMAND, DART_CMD_INVALIDATE)
        for i in range(INVALIDATE_POLLS):
            if not self.r(DART_STREAM_COMMAND) & DART_CMD_BUSY:
                return i
        raise RuntimeError('DART stream invalidate stayed busy')

    def block_all(self):
        """Translation off for every stream (TCR 0, TTBRs invalid): no DMA can be translated."""
        for s in range(DART_NSTREAMS):
            self.w(DART_TCR + 4 * s, 0)
        for s in range(DART_NSTREAMS):
            for j in range(DART_NTTBR):
                self.w(DART_TTBR + 16 * s + 4 * j, 0)
        self.invalidate()

    def program(self, ttbr):
        """All 16 streams of this DART -> our one table. The PCIe RID->SID map of port 0 is not
        programmed by m1n1 (Linux maps 0x100 -> SID 1, UEFI never maps it), so the stream the chip's
        requests land on is not known; every stream of dart-apcie0 belongs to port 0 (Wi-Fi fn0 +
        BT fn1, and fn1 never gets bus master), the same choice as our UEFI SetupApcieDarts."""
        self.block_all()
        for s in range(DART_NSTREAMS):
            self.w(DART_TTBR + 16 * s, ttbr)
        self.w(DART_ENABLED_STREAMS, (1 << DART_NSTREAMS) - 1)
        self.w(DART_ERROR, 0xFFFFFFFF)
        for s in range(DART_NSTREAMS):
            self.w(DART_TCR + 4 * s, DART_TCR_TRANSLATE)
        self.invalidate()

    def restore(self, snap):
        self.block_all()
        for s in range(DART_NSTREAMS):
            for j in range(DART_NTTBR):
                self.w(DART_TTBR + 16 * s + 4 * j, snap['ttbr'][s][j])
        self.w(DART_ENABLED_STREAMS, snap['enabled'])
        for s in range(DART_NSTREAMS):
            self.w(DART_TCR + 4 * s, snap['tcr'][s])
        self.invalidate()


def hexsnap(s):
    return {'config': hex(s['config']), 'enabled': hex(s['enabled']), 'error': hex(s['error']),
            'error_addr': hex(s['error_addr']), 'tcr': [hex(x) for x in s['tcr']],
            'ttbr': [[hex(x) for x in t] for t in s['ttbr']]}


# ---------------------------------------------------------------- rings

class Ring:
    def __init__(self, name, direction, index, off, nitem, itemsz, iova):
        self.name, self.dir, self.index = name, direction, index
        self.off, self.nitem, self.itemsz, self.iova = off, nitem, itemsz, iova
        self.w_ptr = self.r_ptr = 0


# ---------------------------------------------------------------- the stage

class ScanRunner:
    """Runs after WifiUp has seen the shared RAM pointer. Uses the probe's guarded MMIO helpers."""

    def __init__(self, up, stop_cls):
        self.up, self.o = up, up.o
        self._stop_cls = stop_cls                       # wifi_up.StageStop of the running module
        self.res = up.res.setdefault('scan', {})
        self.arena = self.l1 = self.l2 = None
        self.dart = self.dart_saved = None
        self.dart_touched = False
        self.bme_attempted = False
        self.devctl_saved = None
        self.rid2sid_saved = None
        self.rings = {}
        self.trans_id = 0
        self.pending = None
        self.bufs = {}
        self.counts = {}
        self.msg_log = 0
        self.escan_rows, self.escan_status, self.escan_events = [], None, 0
        self.io_type = IO_TYPE_D11AC
        self.con = None
        self.con_lines = 0
        self.dead = None
        self.dart_fault = None
        self.next_health = 0.0

    # ---- small helpers
    def log(self, msg, **kw):
        self.up.log2(msg, **kw)

    def count(self, k, n=1):
        self.counts[k] = self.counts.get(k, 0) + n

    def stop(self, verdict):
        raise self._stop_cls(verdict)

    # ---- 1. shared RAM and ring info (TCM reads only)
    def read_shared(self):
        up = self.up
        sh = up.shared_addr
        lo, hi = up.layout['rambase'], up.layout['rambase'] + up.ramsize
        with up.checked('shared struct'):
            words = [up.tcm_r32(sh + 4 * i) for i in range(SH_WORDS)]
        flags = words[0]
        ver = flags & 0xFF
        ri = words[SH_RING_INFO // 4]
        self.res['shared_words'] = [hex(w) for w in words]
        if not (lo <= ri and ri + RI_LEN <= hi and ri % 4 == 0):
            self.stop(f'SCAN_BAD_SHARED (ring info pointer {ri:#x} outside TCM)')
        with up.checked('ring info'):
            rw = [up.tcm_r32(ri + 4 * i) for i in range(RI_LEN // 4)]
        if ver >= 6:
            max_flow, max_sub, max_compl = rw[13] & 0xFFFF, rw[13] >> 16, rw[14] & 0xFFFF
        else:
            max_sub = rw[13] & 0xFFFF
            max_flow, max_compl = max_sub - 2, 3
        idx_sz = (2 if flags & FLAG_DMA_2B_IDX else 4) if flags & FLAG_DMA_INDEX else 0
        sh_info = {
            'flags': flags, 'version': ver, 'idx_sz': idx_sz,
            'max_rxbufpost': (words[SH_MAX_RXBUFPOST // 4] >> 16) or DEFAULT_MAX_RXBUFPOST,
            'rx_dataoffset': words[SH_RX_DATAOFFSET // 4], 'htod_mb': words[SH_HTOD_MB // 4],
            'dtoh_mb': words[SH_DTOH_MB // 4], 'ring_info': ri, 'ringmem': rw[0],
            'tcm_idx': rw[1:5], 'max_flowrings': max_flow, 'max_sub': max_sub, 'max_compl': max_compl,
            'mb_via_ctl': ver >= 6 and not flags & FLAG_USE_MAILBOX,
            'hostrdy_db1': bool(flags & FLAG_HOSTRDY_DB1), 'shared_dar': bool(flags & FLAG_SHARED_DAR)}
        self.sh = sh_info
        self.res['shared'] = {k: (hex(v) if isinstance(v, int) and not isinstance(v, bool) else
                                  [hex(x) for x in v] if isinstance(v, list) else v) for k, v in sh_info.items()}
        self.log('msgbuf shared', flags=flags, version=ver, idx_sz=idx_sz, max_sub=max_sub, max_compl=max_compl,
                 max_flow=max_flow, max_rxbufpost=sh_info['max_rxbufpost'], rx_dataoffset=sh_info['rx_dataoffset'],
                 ringmem=rw[0], mb_via_ctl=sh_info['mb_via_ctl'])
        if not (2 <= max_sub <= 1024 and 3 <= max_compl <= 1024 and 0 <= max_flow <= 512):
            self.stop(f'SCAN_BAD_SHARED (ring counts sub={max_sub} compl={max_compl} flow={max_flow})')
        if not (lo <= rw[0] and rw[0] + RING_MEM_SZ * len(RINGS) <= hi):
            self.stop(f'SCAN_BAD_SHARED (ring memory {rw[0]:#x} outside TCM)')
        if sh_info['rx_dataoffset'] > 256:
            self.stop(f'SCAN_BAD_SHARED (rx_dataoffset {sh_info["rx_dataoffset"]:#x})')
        for k in ('htod_mb', 'dtoh_mb'):
            v = sh_info[k]
            if v and not (lo <= v < hi):
                self.stop(f'SCAN_BAD_SHARED ({k} {v:#x} outside TCM)')
        if not idx_sz:
            for v in rw[1:5]:
                if not (lo <= v < hi):
                    self.stop(f'SCAN_BAD_SHARED (TCM index pointer {v:#x} outside TCM)')

    # ---- 2. host memory, DART, TCM ring descriptors, then (only then) bus master
    def setup_dma(self):
        up, sh = self.up, self.sh
        n_rx = min(self.o.rxbufs, sh['max_rxbufpost'])
        L = dma_layout(sh['max_sub'], sh['max_compl'], sh['idx_sz'], n_rx, sh['version'])
        self.L = L
        self.res['dma_layout'] = {'iova_base': hex(IOVA_BASE), 'arena_size': hex(L['size']),
                                  'mapped_pages': len(L['pages']),
                                  'regions': {k: [hex(IOVA_BASE + o), hex(s)] for k, (o, s) in L['regions'].items()}}
        self.arena = up.dma_factory(L['size'])
        self.l1 = up.dma_factory(DART_PAGE)
        self.l2 = up.dma_factory(DART_PAGE)
        maps = [(IOVA_BASE + o, self.arena.pa + o) for o in L['pages']]
        l1b, l2b, ttbr, l1i = build_tables(self.l1.pa, self.l2.pa, maps)
        self.l1.write(0, l1b)
        self.l2.write(0, l2b)
        back1, back2 = self.l1.read(0, DART_PAGE), self.l2.read(0, DART_PAGE)
        bad = [hex(iova) for iova, pa in maps
               if walk_tables(back1, lambda a: back2 if a == self.l2.pa else bytes(DART_PAGE), ttbr, iova) != pa]
        self.res['dma_layout'].update({'arena_pa': hex(self.arena.pa), 'l1_pa': hex(self.l1.pa),
                                       'l2_pa': hex(self.l2.pa), 'ttbr': hex(ttbr), 'l1_index': l1i,
                                       'table_walk_bad': bad})
        self.log('DMA arena', pa=self.arena.pa, size=L['size'], iova=IOVA_BASE, pages=len(L['pages']),
                 l1=self.l1.pa, l2=self.l2.pa, walk_ok=not bad)
        if bad:
            self.stop(f'DART_TABLE_BAD (software walk differs for {bad[:4]})')
        for name, d, i, *_ in RINGS:
            off, nitem, itemsz = L['rings'][name]
            self.rings[name] = Ring(name, d, i, off, nitem, itemsz, IOVA_BASE + off)
        self.bufs = {}
        for i in range(N_IOCTLRESP):
            self.bufs[ID_IOCTLRESP + i] = ('resp', L['ioctl_resp'] + i * CTL_PKT_SIZE, CTL_PKT_SIZE)
        for i in range(N_EVENT):
            self.bufs[ID_EVENT + i] = ('event', L['event'] + i * CTL_PKT_SIZE, CTL_PKT_SIZE)
        for i in range(L['n_rxbuf']):
            self.bufs[ID_RX + i] = ('rx', L['rxbuf'] + i * RX_PKT_SIZE, RX_PKT_SIZE)
        self._write_tcm_ring_info()
        self._program_dart(ttbr)
        self._devctl_and_bus_master()

    def _write_tcm_ring_info(self):
        up, sh, L = self.up, self.sh, self.L
        ri = sh['ring_info']
        with up.checked('TCM ring descriptors'):
            if sh['idx_sz']:
                for k, name in enumerate(('h2d_w', 'h2d_r', 'd2h_w', 'd2h_r')):
                    a = IOVA_BASE + L['idx'][name]
                    up.tcm_w32(ri + RI_HOSTADDR + 8 * k, a & 0xFFFFFFFF)
                    up.tcm_w32(ri + RI_HOSTADDR + 8 * k + 4, a >> 32)
            for k, (name, *_r) in enumerate(RINGS):
                r = self.rings[name]
                d = sh['ringmem'] + RING_MEM_SZ * k
                up.tcm_w32(d + RING_MAX_ITEM, r.nitem | (r.itemsz << 16))
                up.tcm_w32(d + RING_BASE_LO, r.iova & 0xFFFFFFFF)
                up.tcm_w32(d + RING_BASE_LO + 4, r.iova >> 32)
                if not sh['idx_sz']:                      # TCM indices start at 0 too
                    self._set_idx(r, 'w', 0)
                    self._set_idx(r, 'r', 0)
            sa, ua = IOVA_BASE + L['scratch'], IOVA_BASE + L['ringupd']
            s = up.shared_addr
            for off, v in ((SH_SCRATCH_LO, sa & 0xFFFFFFFF), (SH_SCRATCH_HI, sa >> 32), (SH_SCRATCH_LEN, SCRATCH_LEN),
                           (SH_RINGUPD_LO, ua & 0xFFFFFFFF), (SH_RINGUPD_HI, ua >> 32), (SH_RINGUPD_LEN, RINGUPD_LEN)):
                up.tcm_w32(s + off, v)
            host_cap = None
            if sh['version'] >= 6:                       # bwfm_pci_preinit
                host_cap = sh['version'] | HOSTCAP_DS_NO_OOB_DW
                if sh['hostrdy_db1']:
                    host_cap |= HOSTCAP_H2D_ENABLE_HOSTRDY
                if sh['shared_dar']:
                    host_cap |= HOSTCAP_H2D_DAR
                up.tcm_w32(s + SH_HOST_CAP, host_cap)
                up.tcm_w32(s + SH_HOST_CAP2, 0)
            back = [up.tcm_r32(sh['ringmem'] + RING_MEM_SZ * k + o) for k in range(len(RINGS)) for o in (4, 8, 12)]
        want = [w for name, *_r in RINGS for w in (self.rings[name].nitem | (self.rings[name].itemsz << 16),
                                                   self.rings[name].iova & 0xFFFFFFFF, self.rings[name].iova >> 32)]
        self.res['host_cap'] = hex(host_cap) if host_cap is not None else None
        self.log('TCM ring descriptors written', host_cap=host_cap, readback_ok=back == want)
        if back != want:
            self.stop('SCAN_TCM_READBACK_MISMATCH (ring descriptors)')

    def _program_dart(self, ttbr):
        up = self.up
        self.dart = Dart(up.m, up.info['dart']['base'])
        with up.checked('DART snapshot'):
            snap = self.dart.snapshot()
            rid2sid = [up.m.r32(up.f['port0'] + 0x828 + 4 * i) for i in range(16)]   # read only
        self.dart_saved = snap
        self.res['dart_before'] = hexsnap(snap)
        self.res['port0_rid2sid_0_15'] = [hex(x) for x in rid2sid]
        self.log('DART before', config=snap['config'], enabled=snap['enabled'], error=snap['error'],
                 tcr=','.join(f'{x:x}' for x in snap['tcr']))
        if snap['config'] & DART_CONFIG_LOCK:
            self.stop(f'DART_LOCKED (CONFIG {snap["config"]:#x}); nothing mapped, bus master stays off')
        self.dart_touched = True
        with up.checked('DART program'):
            self.dart.program(ttbr)
            after = self.dart.snapshot()
        ok = all(t == DART_TCR_TRANSLATE for t in after['tcr']) and all(
            t[0] == ttbr and t[1:] == [0, 0, 0] for t in after['ttbr'])
        self.res['dart_programmed'] = hexsnap(after)
        self.log('DART programmed (16 streams -> one table)', ttbr=ttbr, enabled=after['enabled'], ok=ok)
        if not ok:
            self.stop('DART_PROGRAM_FAILED (TCR/TTBR read-back differs); bus master stays off')
        if self.o.rid2sid == 'map':
            a = up.f['port0'] + PORT_RID2SID
            want = RID2SID_VALID | (LINUX_WIFI_SID << RID2SID_SID_SHIFT) | WIFI_RID
            up.m.allow(a)
            with up.checked('RID2SID map'):
                self.rid2sid_saved = (a, rid2sid[0])
                up.m.w32(a, want)
                back = up.m.r32(a)
            self.res['rid2sid_written'] = {'entry0': hex(want), 'readback': hex(back)}
            self.log('RID2SID entry 0 (--rid2sid map)', value=want, readback=back)

    def _devctl_and_bus_master(self):
        up = self.up
        pcie = (up.res.get('functions', {}).get(0) or {}).get('pcie') or {}
        cap = int(pcie.get('cap_offset', '0'), 16) if pcie else 0
        if not 0x40 <= cap <= 0xF0:
            self.stop(f'SCAN_NO_PCIE_CAP (cap offset {cap:#x})')
        dc = up.ecfg(cap + 8)
        up.m.allow(dc)
        with up.checked('DevCtl'):
            old = up.m.r32(dc) & 0xFFFF
            self.devctl_saved = (dc, old)
            # wifi.md 4.6 (USB-A lesson): No Snoop (bit 11) and Relaxed Ordering (bit 4) off before DMA.
            # Upper half (DevSta, write-1-to-clear) written as 0 = unchanged.
            up.m.w32(dc, old & ~0x0810)
            new = up.m.r32(dc) & 0xFFFF
        self.log('DevCtl', before=old, after=new)
        if new & 0x0810:
            self.stop(f'DEVCTL_NOT_CLEARED ({new:#x})')
        rp_cmd = (up.saved['rp'][0x04] & 0xFFFF & ~0x7) | 0x2 | 0x4
        ep_cmd = 0x2 | 0x4 | 0x400
        self.bme_attempted = True
        up.m.bme_ok = True
        with up.checked('bus master on'):
            up.m.w32(up.rcfg(0x04), rp_cmd)
            up.m.w32(up.ecfg(0x04), ep_cmd)
            rb = (up.m.r32(up.rcfg(0x04)) & 0xFFFF, up.m.r32(up.ecfg(0x04)) & 0xFFFF)
        up.res['bar_layout']['bus_master'] = True
        self.log('bus master ON (DART translating, only our pages mapped)', rp_cmd=rb[0], ep_cmd=rb[1])
        if (rb[0] & 0x6) != 0x6 or (rb[1] & 0x6) != 0x6:
            self.stop(f'BUS_MASTER_FAILED (rp {rb[0]:#x} ep {rb[1]:#x})')

    def dma_off(self):
        """Power-down step 1 (fixed values): endpoint and root port bus master off, read back.
        Raises if either bit is still set, so the DART is then left blocked, never restored."""
        up = self.up
        up.m.bme_ok = False
        with up.checked('bus master off'):
            up.m.w32(up.ecfg(0x04), 0x2 | 0x400)
            up.m.w32(up.rcfg(0x04), (up.saved['rp'][0x04] & 0xFFFF & ~0x4) | 0x2)
            rb = (up.m.r32(up.ecfg(0x04)) & 0xFFFF, up.m.r32(up.rcfg(0x04)) & 0xFFFF)
        up.sleep(0.01)                                   # let in-flight requests drain
        self.res['bus_master_off_readback'] = [hex(x) for x in rb]
        if rb[0] & 0x4 or rb[1] & 0x4:
            raise RuntimeError(f'bus master still on after the off write (ep {rb[0]:#x}, rp {rb[1]:#x})')

    def rid2sid_restore(self):
        a, old = self.rid2sid_saved
        with self.up.guarded():
            self.up.m.w32(a, old)

    def devctl_restore(self):
        dc, old = self.devctl_saved
        with self.up.guarded():
            self.up.m.w32(dc, old)

    def dart_teardown(self, chip_quiet):
        """Runs BEFORE the S240 power-down: the DART (0x681008000) sits inside the port-0 block
        (0x681000000), and once the power-down clears PORT_PERST_OFF its registers fault (hardware
        2026-11-18 12:13: data aborts at 0x681008040/54/50, then 73 SErrors on the writes).
        Restores the saved state only when bus master is verified off (`chip_quiet`) and no saved
        stream was in bypass; otherwise every stream is left blocked (TCR 0, TTBRs invalid)."""
        if self.dart is None or not self.dart_touched:
            return 'untouched'
        with self.up.checked('DART teardown read'):      # one fault here = stop before any write
            err = self.dart.r(DART_ERROR)
            ea = (self.dart.r(DART_ERROR_ADDR_HI) << 32) | self.dart.r(DART_ERROR_ADDR_LO)
            cfg = self.dart.r(DART_CONFIG)
        self.res['dart_error_at_teardown'] = {'error': hex(err), 'addr': hex(ea), 'config': hex(cfg)}
        bypass = [s for s, t in enumerate(self.dart_saved['tcr']) if t & DART_TCR_BYPASS]
        restore = chip_quiet and not bypass
        if bypass:
            self.res['dart_saved_bypass_streams'] = bypass
        with self.up.checked('DART teardown write'):
            if restore:
                self.dart.restore(self.dart_saved)
                mode = 'restored'
            else:
                self.dart.block_all()
                mode = 'blocked'
            after = self.dart.snapshot()
        self.res['dart_after'] = hexsnap(after)
        if mode == 'restored':
            same = after['tcr'] == self.dart_saved['tcr'] and after['ttbr'] == self.dart_saved['ttbr']
            if not same:
                raise RuntimeError('DART registers differ from the saved values after restore')
        elif any(after['tcr']) or any(any(t) for t in after['ttbr']):
            raise RuntimeError('DART not fully blocked after block_all')
        for t in (self.l1, self.l2):
            if t is not None:
                t.zero()                                   # our PTEs gone too
        self.dart_touched = False
        return mode

    def free(self):
        for a in (self.arena, self.l1, self.l2):
            if a is not None:
                a.close()
        self.arena = self.l1 = self.l2 = None

    # ---- index access (DMA index buffer in host RAM, or TCM when the firmware has no DMA_INDEX)
    def _idx_addr(self, r, which):
        key = f'{r.dir}_{which}'
        if self.sh['idx_sz']:
            return ('host', self.L['idx'][key] + r.index * self.sh['idx_sz'])
        base = self.sh['tcm_idx'][('h2d_w', 'h2d_r', 'd2h_w', 'd2h_r').index(key)]
        return ('tcm', base + r.index * 4)

    def _set_idx(self, r, which, v):
        kind, a = self._idx_addr(r, which)
        if kind == 'tcm':
            self.up.tcm_w32(a, v)
        elif self.sh['idx_sz'] == 4:
            self.arena.w32(a, v)
        else:
            w = self.arena.r32(a & ~3)
            sh = 8 * (a & 2)
            self.arena.w32(a & ~3, (w & ~(0xFFFF << sh)) | ((v & 0xFFFF) << sh))

    def _get_idx(self, r, which):
        kind, a = self._idx_addr(r, which)
        if kind == 'tcm':
            return self.up.tcm_r32(a) & 0xFFFF
        w = self.arena.r32(a & ~3)
        return (w >> (8 * (a & 2))) & 0xFFFF if self.sh['idx_sz'] == 2 else w & 0xFFFF

    # ---- doorbells / mailbox
    def _pcie2(self):
        return self.up.core(0x83C)['base']

    def _mb_regs(self):
        reg64 = self.up.chip.get('pcireg64')
        if self.o.doorbell == 'bwfm':        # bwfm: BAR0+0x2000 enumeration space, _64 only if DAR
            dar = self.sh['shared_dar']
            return ('enum', PCIE2_64_H2D_MB0 if dar else PCIE2_H2D_MB0, PCIE2_64_H2D_MB1 if dar else PCIE2_H2D_MB1,
                    PCIE2_64_MAILBOXINT if reg64 else PCIE2_MAILBOXINT)
        return ('window', PCIE2_64_H2D_MB0 if reg64 else PCIE2_H2D_MB0, PCIE2_64_H2D_MB1 if reg64 else PCIE2_H2D_MB1,
                PCIE2_64_MAILBOXINT if reg64 else PCIE2_MAILBOXINT)

    def _reg_w(self, kind, off, v):
        if kind == 'enum':
            self.up.m.w32(self.up.bar0_cpu + BAR0_PCIE_ENUM_OFFSET + off, v)
        else:
            self.up.bp_w32(self._pcie2() + off, v)

    def _reg_r(self, kind, off):
        if kind == 'enum':
            return self.up.m.r32(self.up.bar0_cpu + BAR0_PCIE_ENUM_OFFSET + off)
        return self.up.bp_r32(self._pcie2() + off)

    def doorbell(self):
        kind, mb0, _mb1, _mi = self._mb_regs()
        self._reg_w(kind, mb0, 1)

    # ---- ring operations
    def _h2d_submit(self, r, items):
        """Write items into an H2D ring, publish the write index, ring the doorbell."""
        r.r_ptr = self._get_idx(r, 'r')
        if r.r_ptr >= r.nitem:
            raise FwDead(f'{r.name} read index {r.r_ptr} >= {r.nitem}')
        for it in items:
            avail = (r.r_ptr - r.w_ptr) if r.r_ptr > r.w_ptr else r.r_ptr + (r.nitem - r.w_ptr)
            if avail <= 1:
                raise RuntimeError(f'{r.name} ring full (r={r.r_ptr} w={r.w_ptr})')
            self.arena.write(r.off + r.w_ptr * r.itemsz, it.ljust(r.itemsz, b'\0'))
            r.w_ptr = (r.w_ptr + 1) % r.nitem
        self._set_idx(r, 'w', r.w_ptr)
        self.doorbell()

    def _d2h_take(self, r, w=None):
        w = self._get_idx(r, 'w') if w is None else w
        if w >= r.nitem:
            raise FwDead(f'{r.name} write index {w} >= {r.nitem}')
        out = []
        while w != r.r_ptr:
            n = (w - r.r_ptr) if w > r.r_ptr else r.nitem - r.r_ptr
            off = self.sh['rx_dataoffset']
            raw = self.arena.read(r.off + r.r_ptr * r.itemsz, n * r.itemsz)
            if off:
                raw += bytes(off)
            out += [raw[i * r.itemsz + off:(i + 1) * r.itemsz + off] for i in range(n)]
            r.r_ptr = (r.r_ptr + n) % r.nitem
            self._set_idx(r, 'r', r.r_ptr)
        return out

    def post_buffers(self, kinds=('resp', 'event', 'rx'), ids=None):
        ctl, rxp = [], []
        for pid, (kind, off, size) in sorted(self.bufs.items()):
            if (ids is not None and pid not in ids) or kind not in kinds:
                continue
            iova = IOVA_BASE + off
            if kind == 'resp':
                ctl.append(pack_buf_post(MSG_IOCTLRESP_BUF_POST, pid, size, iova))
            elif kind == 'event':
                ctl.append(pack_buf_post(MSG_EVENT_BUF_POST, pid, size, iova))
            else:
                rxp.append(pack_rxbuf_post(pid, size, iova))
        if ctl:
            self._h2d_submit(self.rings['ctrl_submit'], ctl)
        if rxp:
            self._h2d_submit(self.rings['rxpost'], rxp)

    def _buf_data(self, pid, n, offset=0):
        b = self.bufs.get(pid)
        if b is None:
            self.count('bad_pktid')
            return None
        kind, off, size = b
        n = max(0, min(n, size - offset))
        return self.arena.read(off + offset, n) if n else b''

    # ---- message dispatch
    def pump(self):
        """Drain the three D2H rings once. Returns the number of messages handled."""
        handled = 0
        reposts = set()
        with self.up.checked('ring poll'):
            ws_ = [None] * 3
            if self.sh['idx_sz']:                         # all D2H write indices in one read
                raw = self.arena.read(self.L['idx']['d2h_w'], 3 * self.sh['idx_sz'])
                fmt = '<3I' if self.sh['idx_sz'] == 4 else '<3H'
                ws_ = [v & 0xFFFF for v in struct.unpack(fmt, raw)]
            for name in ('ctrl_complete', 'rx_complete', 'tx_complete'):
                r = self.rings[name]
                for msg in self._d2h_take(r, ws_[r.index]):
                    handled += 1
                    self._dispatch(name, msg, reposts)
            if reposts:
                self.post_buffers(ids=reposts)
        return handled

    def _note(self, text, **kw):
        if self.msg_log < MAX_MSG_LOG:
            self.msg_log += 1
            self.log(text, **kw)

    def _dispatch(self, ring, msg, reposts):
        t, ifidx, flags, _r, req_id = struct.unpack_from('<BBBBI', msg, 0)
        self.count(f'msg_{t:#x}')
        if t == MSG_IOCTLPTR_REQ_ACK:
            st = struct.unpack_from('<h', msg, 8)[0]
            if self.pending is not None and st:
                self.pending['ack_status'] = st
        elif t == MSG_IOCTL_CMPLT:
            st, _flow, resp_len, trans_id, cmd = struct.unpack_from('<hHHHI', msg, 8)
            data = self._buf_data(req_id, resp_len) if req_id in self.bufs else None
            if req_id in self.bufs:
                reposts.add(req_id)
            if self.pending is not None and trans_id == self.pending['trans_id']:
                self.pending.update(done=True, status=st, data=data or b'', resp_len=resp_len)
            else:
                self._note('unexpected ioctl completion', trans_id=trans_id, cmd=cmd, status=st)
        elif t == MSG_WL_EVENT:
            st, _flow, dlen = struct.unpack_from('<hHH', msg, 8)
            data = self._buf_data(req_id, dlen, self.sh['rx_dataoffset']) if req_id in self.bufs else None
            if req_id in self.bufs:
                reposts.add(req_id)
            if data:
                self._event(data)
        elif t == MSG_RX_CMPLT:
            if req_id in self.bufs:
                reposts.add(req_id)
        elif t == MSG_D2H_MAILBOX_DATA:
            self._mb_data(struct.unpack_from('<I', msg, 12)[0], 'ring')
        elif t in (MSG_GEN_STATUS, MSG_RING_STATUS):
            st, ring_id = struct.unpack_from('<hH', msg, 8)
            self._note('firmware status message', type=t, status=st, ring_id=ring_id)
        elif t == MSG_TX_STATUS:
            pass
        else:
            self._note('unhandled firmware message', ring=ring, type=t, request_id=req_id)

    def _mb_data(self, v, where):
        self.count('d2h_mb_data')
        self._note('D2H mailbox data', value=v, where=where)
        if v & D2H_DEV_FWHALT:
            raise FwDead(f'D2H mailbox FWHALT ({v:#x})')
        if v & D2H_DEV_DS_ENTER_REQ:                   # brcmfmac acks deep-sleep requests
            if self.sh['mb_via_ctl']:
                self._h2d_submit(self.rings['ctrl_submit'], [pack_h2d_mailbox(H2D_HOST_DS_ACK)])
            elif self.sh['htod_mb']:
                # the SBMBX config-space doorbell (0x98) is not allowlisted: TCM word only
                self.up.tcm_w32(self.sh['htod_mb'], H2D_HOST_DS_ACK)
                self._note('deep-sleep ack written to TCM only (no SBMBX config write)')

    def _event(self, data):
        ev = parse_event(data)
        if ev is None:
            self.count('bad_event')
            return
        self.count(f'event_{ev["type"]}')
        if ev['type'] != E_ESCAN_RESULT:
            return
        self.escan_events += 1
        if ev['status'] == E_STATUS_PARTIAL:
            sync, rows = parse_escan(ev['data'], self.io_type)
            if sync is not None and sync != ESCAN_SYNC_ID:
                self.count('escan_other_sync')
                return
            room = MAX_BSS - len(self.escan_rows)
            self.escan_rows += rows[:max(room, 0)]
        else:
            self.escan_status = ev['status']

    # ---- firmware health (console, mailbox data, shared pointer, DART error)
    def health(self, force=False):
        now = self.up.clock()
        if not force and now < self.next_health:
            return
        self.next_health = now + HEALTH_EVERY_S
        up = self.up
        with up.checked('health'):
            sp = up.tcm_r32(up.layout['shared_ptr'])
            dt = up.tcm_r32(self.sh['dtoh_mb']) if self.sh['dtoh_mb'] else 0
            if dt:
                up.tcm_w32(self.sh['dtoh_mb'], 0)
            kind, _mb0, _mb1, mi = self._mb_regs()
            st = self._reg_r(kind, mi)
            if st and st != 0xFFFFFFFF:
                self._reg_w(kind, mi, st)               # ack (write-1-to-clear)
            derr = self.dart.r(DART_ERROR) if self.dart is not None else 0
            if derr & DART_ERROR_FLAG and self.dart_fault is None:
                ea = (self.dart.r(DART_ERROR_ADDR_HI) << 32) | self.dart.r(DART_ERROR_ADDR_LO)
                self.dart_fault = {'error': hex(derr), 'addr': hex(ea)}
        self._console()
        if self.dart_fault and not self.res.get('dart_fault'):
            self.res['dart_fault'] = self.dart_fault
            self.log('DART fault (chip DMA outside our map, blocked by the DART)', **self.dart_fault)
        if sp != up.shared_addr and not self.res.get('shared_ptr_changed'):
            # recorded, not fatal: neither bwfm nor brcmfmac re-checks this word after start
            self.res['shared_ptr_changed'] = hex(sp)
            self.log('note: last TCM word changed after start', was=up.shared_addr, now=sp)
        if dt:
            self._mb_data(dt, 'tcm')

    def _console(self):
        up = self.up
        if self.con is None:
            c = up._read_console()
            if not c or not c.get('valid'):
                return
            self.con = {'buf': int(c['buf'], 16), 'size': int(c['size'], 16), 'r': c['widx'], 'part': b''}
            return
        con = self.con
        with up.checked('console poll'):
            w = up.tcm_r32(up.console_base + 0x10)
        if w >= con['size'] or w == con['r']:
            return
        r = con['r']
        spans = [(r, w)] if w > r else [(r, con['size']), (0, w)]
        data = b''
        with up.checked('console read'):
            for a, b in spans:
                lo, hi = (con['buf'] + a) & ~3, align_up(con['buf'] + b, 4)
                up.m.copy_from_dev(up.xfer.buf, up.tcm(lo), hi - lo)
                data += up.xfer.get(hi - lo)[con['buf'] + a - lo:con['buf'] + b - lo]
        con['r'] = w
        text = (con['part'] + data).replace(b'\r', b'')
        *lines, con['part'] = text.split(b'\n')
        con['part'] = con['part'][-400:]
        for ln in lines:
            s = ln.decode('ascii', 'replace').strip('\0').strip()
            if not s:
                continue
            if self.con_lines < MAX_SCAN_CONSOLE_LINES:
                self.con_lines += 1
                up.log(f'[s257] fw-console | {s[:200]}')
            if TRAP_RE.search(s.encode('ascii', 'replace')):
                self.res.setdefault('console_trap_lines', []).append(s[:200])
                raise FwDead(f'console: {s[:120]}')

    # ---- ioctl round trip
    def ioctl(self, cmd, data=b'', out_len=0, timeout=IOCTL_TIMEOUT_S):
        n = max(len(data), out_len)
        if not 0 < n <= IOCTL_MAX_LEN:
            raise ValueError(f'ioctl buffer length {n} outside 1..{IOCTL_MAX_LEN}')
        self.arena.write(self.L['ioctl_req'], bytes(data).ljust(n, b'\0'))
        self.trans_id = (self.trans_id + 1) & 0xFFFF
        self.pending = {'trans_id': self.trans_id, 'cmd': cmd, 'done': False}
        with self.up.checked('ioctl submit'):
            self._h2d_submit(self.rings['ctrl_submit'],
                             [pack_ioctl_req(cmd, self.trans_id, n, IOVA_BASE + self.L['ioctl_req'])])
        end = self.up.clock() + timeout
        try:
            while not self.pending['done']:
                busy = self.pump()
                self.health()
                if self.pending['done']:
                    break
                if self.up.clock() >= end:
                    self.count('ioctl_timeout')
                    raise IoctlTimeout(f'no completion for ioctl {cmd} within {timeout} s')
                if not busy:
                    self.up.sleep(POLL_S)
            p = self.pending
            if p.get('ack_status'):
                return p['ack_status'], b''
            return p['status'], p['data']
        finally:
            self.pending = None

    def get_int(self, cmd):
        st, d = self.ioctl(cmd, bytes(4), 4)
        return st, (struct.unpack_from('<I', d.ljust(4, b'\0'))[0] if not st else None)

    def set_int(self, cmd, v):
        return self.ioctl(cmd, struct.pack('<I', v & 0xFFFFFFFF))[0]

    def get_var(self, name, n):
        q = name.encode() + b'\0'
        st, d = self.ioctl(C_GET_VAR, q + bytes(n), len(q) + n)
        return st, (d[:n] if not st else None)

    def set_var(self, name, payload):
        return self.ioctl(C_SET_VAR, name.encode() + b'\0' + bytes(payload))[0]

    # ---- 3. protocol start and firmware configuration
    def start_protocol(self):
        up = self.up
        kind, _mb0, mb1, mi = self._mb_regs()
        with up.checked('host ready'):
            mask = PCIE2_64_MAILBOXMASK if up.chip.get('pcireg64') else PCIE2_MAILBOXMASK
            self._reg_w(kind, mask, 0)                   # we poll; no interrupt wanted
            st = self._reg_r(kind, mi)
            if st and st != 0xFFFFFFFF:
                self._reg_w(kind, mi, st)
            if self.sh['hostrdy_db1']:
                self._reg_w(kind, mb1, 1)                # bwfm_pci_hostready / brcmfmac_pcie_hostready
            self.post_buffers()
        self.log('host ready + buffers posted', doorbell=f'{kind}:{_mb0:#x}', hostready=self.sh['hostrdy_db1'],
                 ioctl_resp=N_IOCTLRESP, event=N_EVENT, rx=self.L['n_rxbuf'])
        self.up.sleep(0.05)
        self.pump()
        self.health(force=True)

    def _blob(self, var, blob, required=False):
        if not blob:
            self.res['blobs'][var] = 'skipped (empty)'
            return None
        chunks = dload_chunks(blob)
        for i, ch in enumerate(chunks):
            st = self.set_var(var, ch)
            if st:
                self.res['blobs'][var] = f'error {st} at chunk {i}/{len(chunks)}'
                self.log(f'{var} failed', status=st, chunk=i, chunks=len(chunks))
                return st
        self.res['blobs'][var] = f'ok ({len(blob)} bytes, {len(chunks)} chunks)'
        self.log(f'{var} ok', bytes=len(blob), chunks=len(chunks))
        return 0

    def init_firmware(self):
        up, r = self.up, self.res
        r['blobs'] = {}
        try:
            st, io = self.get_int(C_GET_VERSION)
        except IoctlTimeout:
            self.stop('IOCTL_NO_REPLY (first ioctl GET_VERSION got no completion: rings/doorbell/DMA path)')
        r['io_type'] = io
        self.io_type = io if io in (IO_TYPE_D11N, IO_TYPE_D11AC) else IO_TYPE_D11AC
        self.log('first ioctl answered (GET_VERSION)', status=st, io_type=io)
        st, mac = self.get_var('cur_etheraddr', 6)
        r['mac_matches_adt'] = (mac == up.info.get('mac')) if mac is not None else f'error {st}'
        self.log('cur_etheraddr', status=st, matches_adt=r['mac_matches_adt'])   # value not logged
        st, ver = self.get_var('ver', 256)
        if ver is not None:
            r['fw_ver'] = ver.split(b'\0')[0].decode('ascii', 'replace').strip()[:200]
            self.log('firmware ver', text=r['fw_ver'])
        f = up.files
        self._blob('clmload', f['clmb'].read_bytes() if f.get('clmb') else b'')
        s2, v = self.get_var('clmload_status', 4)
        r['blobs']['clmload_status'] = struct.unpack('<I', v)[0] if v is not None else f'error {s2}'
        self._blob('txcapload', f['txcb'].read_bytes() if f.get('txcb') else b'')
        cal = up.info.get('cal') or b''
        if self.o.no_cal:
            r['blobs']['calload'] = 'skipped (--no-cal)'
        else:
            self._blob('calload', cal)
        r['mpc'] = self.set_var('mpc', struct.pack('<I', 0))
        if self.o.country:
            cc = self.o.country.encode()
            r['country_set'] = self.set_var('country', cc.ljust(4, b'\0') + struct.pack('<i', -1) + cc.ljust(4, b'\0'))
        st, c = self.get_var('country', 12)
        r['country'] = c[:4].split(b'\0')[0].decode('ascii', 'replace') if c is not None else f'error {st}'
        r['event_mask'] = None
        for n in EVENT_MASK_LENS:
            st = self.set_var('event_msgs', event_mask(n))
            if not st:
                r['event_mask'] = f'{n} bytes, ESCAN_RESULT only'
                break
        self.log('settings', mpc=r['mpc'], country=r['country'], event_mask=r['event_mask'],
                 blobs=';'.join(f'{k}={v}' for k, v in r['blobs'].items()))
        st = self.set_int(C_UP, 0)
        r['up'] = st
        self.log('interface UP', status=st)
        if st:
            self.stop(f'UP_FAILED (status {st})')
        st, sv = self.get_var('scan_ver', 8)
        major = struct.unpack_from('<H', sv, 4)[0] if sv is not None else 0
        r['scan_ver'] = {'status': st, 'raw': sv.hex() if sv else None, 'major': major}
        self.scan_major = major if major in (2, 3) else (2 if sv is not None and any(sv) else 0)

    # ---- 4. escan
    def escan(self):
        up, r = self.up, self.res
        scan_type = SCANTYPE_ACTIVE if self.o.scan_type == 'active' else SCANTYPE_PASSIVE
        tried = []
        for major in ([self.scan_major] + ([0] if self.scan_major else [])):
            st = self.set_var('escan', escan_params(major, scan_type))
            tried.append({'params_version': major or 1, 'status': st})
            if not st:
                break
        r['escan_request'] = tried
        self.log('escan requested', tried=tried, scan_type=self.o.scan_type)
        if tried[-1]['status']:
            self.stop(f'ESCAN_REFUSED ({tried})')
        t0 = up.clock()
        end = t0 + self.o.scan_seconds
        while self.escan_status is None and up.clock() < end:
            busy = self.pump()
            self.health()
            if not busy:
                up.sleep(POLL_S)
        self.health(force=True)
        r['escan'] = {'status': E_STATUS_NAMES.get(self.escan_status, self.escan_status)
                      if self.escan_status is not None else 'TIMEOUT (no final event)',
                      'events': self.escan_events, 'raw_rows': len(self.escan_rows),
                      'seconds': round(up.clock() - t0, 2)}

    def down(self):
        try:
            self.res['down'] = self.set_int(C_DOWN, 0)
        except Exception as e:                           # best effort; power-down follows anyway
            self.res['down'] = f'{type(e).__name__}: {e}'

    def report(self):
        rows = bss_table(self.escan_rows)
        self.res['bss'] = [{'ssid': show_ssid(x['ssid']), 'bssid': x['bssid'], 'channel': x['channel'],
                            'band': x['band'], 'rssi': x['rssi']} for x in rows]
        log = self.up.log
        log(f'[s257] scan result: {len(rows)} BSS (status {self.res["escan"]["status"]}, '
            f'{self.res["escan"]["events"]} events, {self.res["escan"]["seconds"]} s)')
        log(f'[s257] {"SSID":<32}  {"BSSID":<17}  {"CH":>3}  {"BAND":<4}  {"RSSI":>5}')
        for x in self.res['bss'][:MAX_TABLE_ROWS]:
            log(f'[s257] {x["ssid"]:<32}  {x["bssid"]:<17}  {x["channel"]:>3}  {x["band"]:<4}  {x["rssi"]:>5}')
        if len(rows) > MAX_TABLE_ROWS:
            log(f'[s257] ... {len(rows) - MAX_TABLE_ROWS} more in result.json')
        return rows

    def run(self, phase):
        with phase('s257-scan-dma', 60):
            self.read_shared()
            self.setup_dma()
        try:
            with phase('s257-scan-init', 150):
                self.start_protocol()
                self.init_firmware()
            with phase('s257-scan', self.o.scan_seconds + 30):
                self.escan()
            with phase('s257-scan-down', 15):
                self.down()
        except FwDead as e:
            self.dead = str(e)
            self._partial_report()
            self.stop(f'FW_TRAP ({e})')
        except IoctlTimeout as e:
            self._partial_report()
            self.stop(f'IOCTL_TIMEOUT ({e})')
        finally:
            self.res['counts'] = dict(self.counts)
            if self.dart_fault:
                self.res['dart_fault'] = self.dart_fault
        return self.report()

    def _partial_report(self):
        if self.escan_rows:
            self.res.setdefault('escan', {'status': 'INTERRUPTED', 'events': self.escan_events,
                                          'raw_rows': len(self.escan_rows), 'seconds': None})
            self.report()
