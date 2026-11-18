/*
 * nwbc_core.h - hardware-independent core of the NWOAS BCM4378 Wi-Fi driver.
 *
 * Plain C99, no OS calls, no allocation.  Built into the Windows driver
 * (src/driver) and into the host unit tests (tests/run-tests.sh on the Mac).
 *
 * Everything chip/firmware-protocol related is re-implemented from OpenBSD
 * bwfm (sys/dev/ic/bwfm.c, bwfmreg.h, sys/dev/pci/if_bwfm_pci.c,
 * if_bwfm_pci.h) and cross-checked against Linux brcmfmac (pcie.c, chip.c,
 * msgbuf.c, firmware.c).  Both are ISC licensed:
 *
 *   Copyright (c) 2010-2016 Broadcom Corporation
 *   Copyright (c) 2016,2017 Patrick Wildt <patrick@blueri.se>
 *   Copyright (c) 2014 Broadcom Corporation (brcmfmac)
 *
 *   Permission to use, copy, modify, and/or distribute this software for any
 *   purpose with or without fee is hereby granted, provided that the above
 *   copyright notice and this permission notice appear in all copies.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *   WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *   ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * The same sequence was run on the Mac mini from the m1n1 proxy on
 * 2026-11-18 (nwoas_scripts/wifi-s257: PASS_FW_UP, PASS_SCAN with 11 BSS).
 * No GPL code is used.
 */
#ifndef NWBC_CORE_H
#define NWBC_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char      nb_u8;
typedef unsigned short     nb_u16;
#ifdef _MSC_VER
typedef unsigned long      nb_u32;   /* = ULONG (32-bit on Windows) */
typedef long               nb_s32;
#else
typedef unsigned int       nb_u32;   /* 32-bit on LP64 hosts */
typedef int                nb_s32;
#endif
typedef unsigned long long nb_u64;
typedef signed char        nb_s8;
typedef short              nb_s16;

/* ------------------------------------------------------------------ errors */
#define NB_OK              0
#define NB_E_ARG          -1   /* bad argument / buffer too small */
#define NB_E_RANGE        -2   /* value outside the allowed range */
#define NB_E_FORMAT       -3   /* malformed input */
#define NB_E_NOTFOUND     -4
#define NB_E_LOOP         -5   /* bounded walk exceeded */
#define NB_E_FULL         -6

/* ------------------------------------------------------------- byte order */
static __inline nb_u16 nb_ld16(const void *p) { const nb_u8 *b = (const nb_u8 *)p; return (nb_u16)(b[0] | (b[1] << 8)); }
static __inline nb_u32 nb_ld32(const void *p) { const nb_u8 *b = (const nb_u8 *)p; return (nb_u32)b[0] | ((nb_u32)b[1] << 8) | ((nb_u32)b[2] << 16) | ((nb_u32)b[3] << 24); }
static __inline nb_u16 nb_be16(const void *p) { const nb_u8 *b = (const nb_u8 *)p; return (nb_u16)((b[0] << 8) | b[1]); }
static __inline nb_u32 nb_be32(const void *p) { const nb_u8 *b = (const nb_u8 *)p; return ((nb_u32)b[0] << 24) | ((nb_u32)b[1] << 16) | ((nb_u32)b[2] << 8) | (nb_u32)b[3]; }
static __inline void nb_st16(void *p, nb_u32 v) { nb_u8 *b = (nb_u8 *)p; b[0] = (nb_u8)v; b[1] = (nb_u8)(v >> 8); }
static __inline void nb_st32(void *p, nb_u32 v) { nb_u8 *b = (nb_u8 *)p; b[0] = (nb_u8)v; b[1] = (nb_u8)(v >> 8); b[2] = (nb_u8)(v >> 16); b[3] = (nb_u8)(v >> 24); }
static __inline nb_u32 nb_align(nb_u32 v, nb_u32 a) { return (v + a - 1) / a * a; }

/* ------------------------------------------------------ chip (bwfmreg.h) */
#define NB_CHIP_BASE            0x18000000u
#define NB_CC_CHIPID            0x000
#define NB_CC_CAPS              0x004
#define NB_CC_WATCHDOG          0x080
#define NB_CC_EROMPTR           0x0FC
#define NB_CORE_CC              0x800
#define NB_CORE_80211           0x812
#define NB_CORE_PMU             0x827
#define NB_CORE_PCIE2           0x83C
#define NB_CORE_CR4             0x83E
#define NB_CORE_GCI             0x840
#define NB_AGENT_IOCTL          0x408
#define NB_AGENT_RESET_CTL      0x800
#define NB_IOCTL_CLK            0x1
#define NB_IOCTL_FGC            0x2
#define NB_IOCTL_CR4_CPUHALT    0x20
#define NB_RESET_CTL_RESET      0x1
#define NB_D11_PHYCLOCKEN       0x4
#define NB_D11_PHYRESET         0x8
#define NB_CR4_CAP              0x004
#define NB_CR4_BANKIDX          0x040
#define NB_CR4_BANKINFO         0x044
#define NB_BRCM_CC_4378         0x4378
#define NB_RAMBASE_4378         0x352000u
#define NB_OTP_BASE_4378        0x1120u     /* in the GCI core */
#define NB_OTP_WORDS_4378       0x170u
#define NB_RAMSIZE_OFF          0x6C
#define NB_RAMSIZE_MAGIC        0x534D4152u /* "SMAR" */
#define NB_SEED_MAGIC           0xFEEDC0DEu
#define NB_SEED_LEN             0x100u

/* PCI config of the endpoint (if_bwfm_pci.h) */
#define NB_CFG_BAR0_WINDOW      0x80
#define NB_CFG_LINK_CTRL        0xBC        /* PCIe cap 0xAC + 0x10 on this chip (S240) */
#define NB_CFG_REG_BAR2_CONFIG  0x4E0
#define NB_BAR0_WINDOW_SIZE     0x1000u
#define NB_BAR0_PCIE_ENUM       0x2000u     /* PCIe2 core registers mirrored at BAR0+0x2000 */

/* PCIe2 core registers */
#define NB_PCIE2_INTMASK        0x024
#define NB_PCIE2_MAILBOXINT     0x048
#define NB_PCIE2_MAILBOXMASK    0x04C
#define NB_PCIE2_CONFIGADDR     0x120
#define NB_PCIE2_CONFIGDATA     0x124
#define NB_PCIE2_H2D_MB0        0x140
#define NB_PCIE2_H2D_MB1        0x144
#define NB_PCIE2_64_MAILBOXINT  0xC30
#define NB_PCIE2_64_MAILBOXMASK 0xC34
#define NB_PCIE2_64_H2D_MB0     0xA20
#define NB_PCIE2_64_H2D_MB1     0xA24
#define NB_PCIE2_MB_D2H_DB      0x00FF0000u /* rev < 64: D2H0..3 DB0/1 */
#define NB_PCIE2_MB_FN0         0x00000300u
#define NB_PCIE2_64_MB_D2H_DB   0x0000FFFFu /* rev >= 64 */

typedef struct nb_core {
    nb_u16 id;
    nb_u8  rev;
    nb_u32 base;
    nb_u32 wrap;
} nb_core;

typedef nb_u32 (*nb_rd32_fn)(void *ctx, nb_u32 addr);

#define NB_EROM_MAX_WORDS 4096
/* bwfm_chip_dmp_erom_scan: walk the enumeration ROM. *n = number of cores. */
int nb_erom_scan(nb_rd32_fn rd, void *ctx, nb_u32 erom, nb_core *out, int max, int *n);
const nb_core *nb_core_find(const nb_core *c, int n, nb_u16 id, int idx);
/* chipid register -> chip / rev / type */
static __inline nb_u32 nb_chipid_chip(nb_u32 r) { return r & 0xFFFF; }
static __inline nb_u32 nb_chipid_rev(nb_u32 r)  { return (r >> 16) & 0xF; }
static __inline nb_u32 nb_chipid_type(nb_u32 r) { return (r >> 28) & 0xF; }
/* CR4 bank info register -> bank bytes */
static __inline nb_u32 nb_cr4_bank_bytes(nb_u32 info) { return ((info & 0x7F) + 1) * ((info & 0x200) ? 1024u : 8192u); }
static __inline nb_u32 nb_cr4_nbanks(nb_u32 cap) { return (cap & 0xF) + ((cap >> 4) & 0xF); }

/* ----------------------------------------------------------------- OTP */
typedef struct nb_otp {
    char module[18];   /* M= */
    char vendor[18];   /* V= */
    char modrev[18];   /* m= */
    char chiprev[18];  /* s= */
    int  tuples;
} nb_otp;
/* brcmf_pcie_parse_otp (tuple stepping as brcmfmac: next = i + 2 + len). */
int nb_otp_parse(const nb_u8 *otp, nb_u32 len, nb_otp *o);

/* ------------------------------------------------------- NVRAM / layout */
/* bwfm_nvram_convert + Asahi key whitespace strip.  mac may be NULL.
 * out gets the NUL separated blob padded to 4 plus the length token. */
int nb_nvram_convert(const nb_u8 *txt, nb_u32 len, const nb_u8 *mac,
                     nb_u8 *out, nb_u32 cap, nb_u32 *outlen, nb_u32 *entries);

typedef struct nb_ramlayout {
    nb_u32 rambase, ramsize, fw_end, seed, footer, nvram, shared_ptr, top;
} nb_ramlayout;
int    nb_ram_layout(nb_u32 rambase, nb_u32 ramsize, nb_u32 fwlen, nb_u32 nvlen, nb_ramlayout *l);
nb_u32 nb_fw_ramsize_override(const nb_u8 *fw, nb_u32 len);   /* 0 = none */

/* ---------------------------------------------------------- shared RAM */
#define NB_SH_FLAGS             0x00
#define NB_SH_CONSOLE           0x14
#define NB_SH_MAX_RXBUFPOST     0x22
#define NB_SH_RX_DATAOFFSET     0x24
#define NB_SH_HTOD_MB           0x28
#define NB_SH_DTOH_MB           0x2C
#define NB_SH_RING_INFO         0x30
#define NB_SH_SCRATCH_LEN       0x34
#define NB_SH_SCRATCH_LO        0x38
#define NB_SH_SCRATCH_HI        0x3C
#define NB_SH_RINGUPD_LEN       0x40
#define NB_SH_RINGUPD_LO        0x44
#define NB_SH_RINGUPD_HI        0x48
#define NB_SH_HOST_CAP          0x54
#define NB_SH_HOST_CAP2         0x70
#define NB_SH_WORDS             30
#define NB_RI_WORDS             15
#define NB_RI_HOSTADDR          0x14        /* 4 x {lo,hi}: h2d_w, h2d_r, d2h_w, d2h_r */
#define NB_RING_MEM_SZ          16
#define NB_RING_MAX_ITEM        0x04
#define NB_RING_BASE_LO         0x08
#define NB_FLAG_DMA_INDEX       0x10000u
#define NB_FLAG_DMA_2B_IDX      0x100000u
#define NB_FLAG_USE_MAILBOX     0x2000000u
#define NB_FLAG_HOSTRDY_DB1     0x10000000u
#define NB_FLAG_SHARED_DAR      0x80000000u
#define NB_HOSTCAP_H2D_ENABLE_HOSTRDY 0x400u
#define NB_HOSTCAP_DS_NO_OOB_DW 0x1000u
#define NB_HOSTCAP_H2D_DAR      0x10000u
#define NB_SCRATCH_LEN          8
#define NB_RINGUPD_LEN          1024
#define NB_CON_BUFADDR          0x08
#define NB_CON_BUFSIZE          0x0C
#define NB_CON_WRITEIDX         0x10
#define NB_D2H_DEV_D3_ACK       0x1u
#define NB_D2H_DEV_DS_ENTER_REQ 0x2u
#define NB_D2H_DEV_FWHALT       0x10000000u
#define NB_H2D_HOST_DS_ACK      0x2u

typedef struct nb_shared {
    nb_u32 flags;
    nb_u32 version;
    nb_u32 idx_sz;            /* 0 = indices in TCM, else 2 or 4 bytes in host RAM */
    nb_u32 max_rxbufpost;
    nb_u32 rx_dataoffset;
    nb_u32 htod_mb, dtoh_mb;
    nb_u32 ring_info;
    nb_u32 ringmem;
    nb_u32 tcm_idx[4];        /* h2d_w, h2d_r, d2h_w, d2h_r (TCM index mode) */
    nb_u32 max_flowrings, max_sub, max_compl;
    nb_u32 console;
    int    mb_via_ctl;
    int    hostrdy_db1;
    int    shared_dar;
} nb_shared;
/* words = NB_SH_WORDS words at the shared address, ri = NB_RI_WORDS words of
 * ring info (read from words[NB_SH_RING_INFO/4]).  [lo,hi) = TCM range. */
int    nb_shared_parse(const nb_u32 *words, const nb_u32 *ri, nb_u32 lo, nb_u32 hi, nb_shared *s);
nb_u32 nb_host_cap(const nb_shared *s);

/* ------------------------------------------------------------ DMA layout */
#define NB_RING_CTRL_SUBMIT     0
#define NB_RING_RXPOST          1
#define NB_RING_CTRL_COMPLETE   2
#define NB_RING_TX_COMPLETE     3
#define NB_RING_RX_COMPLETE     4
#define NB_NUM_COMMON_RINGS     5
#define NB_MAX_FLOWRINGS        4
#define NB_FLOWRING_ITEMS       512
#define NB_FLOWRING_ITEMSZ      48
#define NB_IOCTL_MAX_LEN        1518        /* BWFM_DMA_H2D_IOCTL_BUF_LEN */
#define NB_CTL_PKT_SIZE         8192
#define NB_RX_PKT_SIZE          2048
#define NB_N_IOCTLRESP          8
#define NB_N_EVENT              8

typedef struct nb_ringdesc {
    nb_u32 off;               /* offset of the ring in the control arena */
    nb_u16 nitem, itemsz;
    nb_u16 is_h2d, idx;       /* index slot in its idx array */
} nb_ringdesc;

typedef struct nb_dmalayout {
    nb_u32 idx_off, idx_stride;           /* 4 arrays: h2d_w, h2d_r, d2h_w, d2h_r */
    nb_ringdesc ring[NB_NUM_COMMON_RINGS];
    nb_ringdesc flow[NB_MAX_FLOWRINGS];
    nb_u32 scratch_off, ringupd_off, ioctl_req_off, ioctl_resp_off, event_off;
    nb_u32 size;                          /* control arena bytes */
} nb_dmalayout;
int nb_dma_layout(const nb_shared *s, nb_u32 align, nb_dmalayout *l);
/* offset of an index word in the host index arena: which 0..3 (h2d_w, h2d_r, d2h_w, d2h_r) */
static __inline nb_u32 nb_idx_off(const nb_dmalayout *l, const nb_shared *s, nb_u32 which, nb_u32 slot)
{ return l->idx_off + which * l->idx_stride + slot * s->idx_sz; }

/* ring arithmetic (bwfm_pci_ring_write_reserve / ring_read_avail) */
nb_u32 nb_ring_write_space(nb_u32 r, nb_u32 w, nb_u32 n);     /* slots the producer may fill */
nb_u32 nb_ring_read_avail(nb_u32 r, nb_u32 w, nb_u32 n, nb_u32 *contig);

/* --------------------------------------------------------- msgbuf items */
#define NB_MSG_GEN_STATUS            0x01
#define NB_MSG_RING_STATUS           0x02
#define NB_MSG_FLOW_RING_CREATE      0x03
#define NB_MSG_FLOW_RING_CREATE_CMPLT 0x04
#define NB_MSG_FLOW_RING_DELETE      0x05
#define NB_MSG_FLOW_RING_DELETE_CMPLT 0x06
#define NB_MSG_IOCTLPTR_REQ          0x09
#define NB_MSG_IOCTLPTR_REQ_ACK      0x0A
#define NB_MSG_IOCTLRESP_BUF_POST    0x0B
#define NB_MSG_IOCTL_CMPLT           0x0C
#define NB_MSG_EVENT_BUF_POST        0x0D
#define NB_MSG_WL_EVENT              0x0E
#define NB_MSG_TX_POST               0x0F
#define NB_MSG_TX_STATUS             0x10
#define NB_MSG_RXBUF_POST            0x11
#define NB_MSG_RX_CMPLT              0x12
#define NB_MSG_H2D_MAILBOX_DATA      0x23
#define NB_MSG_D2H_MAILBOX_DATA      0x24
#define NB_IOCTL_REQ_PKTID           0xFFFE
#define NB_TXF_802_3                 0x01
#define NB_TXF_PRIO_SHIFT            5

/* encoders: write one item (zero-filled to itemsz) and return its size, 0 on error */
nb_u32 nb_enc_ioctl_req(nb_u8 *it, nb_u32 itemsz, nb_u32 cmd, nb_u16 trans_id, nb_u16 len, nb_u64 iova);
nb_u32 nb_enc_buf_post(nb_u8 *it, nb_u32 itemsz, nb_u8 type, nb_u32 pktid, nb_u16 len, nb_u64 iova);
nb_u32 nb_enc_rxbuf_post(nb_u8 *it, nb_u32 itemsz, nb_u32 pktid, nb_u16 len, nb_u64 iova);
nb_u32 nb_enc_tx_post(nb_u8 *it, nb_u32 itemsz, nb_u8 ifidx, nb_u32 req_id, const nb_u8 *ethhdr,
                      nb_u8 prio, nb_u64 data_iova, nb_u16 data_len);
nb_u32 nb_enc_flowring_create(nb_u8 *it, nb_u32 itemsz, nb_u8 ifidx, nb_u16 flow_ring_id, const nb_u8 *da,
                              const nb_u8 *sa, nb_u8 tid, nb_u16 max_items, nb_u16 len_item, nb_u64 iova);
nb_u32 nb_enc_flowring_delete(nb_u8 *it, nb_u32 itemsz, nb_u8 ifidx, nb_u16 flow_ring_id);
nb_u32 nb_enc_h2d_mailbox(nb_u8 *it, nb_u32 itemsz, nb_u32 data);

typedef struct nb_msg {
    nb_u8  type, ifidx, flags;
    nb_u32 req_id;
    nb_s16 status;
    nb_u16 flow_ring_id;
    nb_u16 resp_len, trans_id;   /* IOCTL_CMPLT */
    nb_u32 cmd;
    nb_u16 event_len;            /* WL_EVENT */
    nb_u16 data_len, data_offset, rx_flags; /* RX_CMPLT */
    nb_u32 mb_data;              /* D2H_MAILBOX_DATA */
} nb_msg;
int nb_dec_msg(const nb_u8 *it, nb_u32 len, nb_msg *m);

/* ------------------------------------------------------ dongle commands */
#define NB_C_GET_VERSION        1
#define NB_C_UP                 2
#define NB_C_DOWN               3
#define NB_C_SET_INFRA          20
#define NB_C_GET_BSSID          23
#define NB_C_SET_SSID           26
#define NB_C_GET_CHANNEL        29
#define NB_C_DISASSOC           52
#define NB_C_SET_PM             86
#define NB_C_SET_AP             118
#define NB_C_GET_RSSI           127
#define NB_C_GET_RATE           12      /* brcmfmac BRCMF_C_GET_RATE: 500 kb/s units */
#define NB_C_SET_SCAN_CHANNEL_TIME 185
#define NB_C_SET_SCAN_UNASSOC_TIME 187
#define NB_C_GET_VAR            262
#define NB_C_SET_VAR            263
#define NB_C_SET_SCAN_PASSIVE_TIME 258
#define NB_PM_CAM               0
#define NB_PM_FAST_PS           2

/* iovar request buffer: "name\0" + payload.  Returns total length, 0 if it does not fit. */
nb_u32 nb_iovar(nb_u8 *out, nb_u32 cap, const char *name, const void *payload, nb_u32 plen);

/* CLM / txcap / calibration download chunk (bwfm_process_blob) */
#define NB_DLOAD_MAX_LEN        1400
#define NB_DLOAD_HDR_LEN        12
#define NB_DLOAD_FLAG_BEGIN     0x2
#define NB_DLOAD_FLAG_END       0x4
#define NB_DLOAD_HANDLER_VER    0x1000
#define NB_DLOAD_TYPE_CLM       2
nb_u32 nb_dload_count(nb_u32 len);
/* chunk index i of blob -> out (header + data); returns bytes, 0 on error */
nb_u32 nb_dload_chunk(const nb_u8 *blob, nb_u32 len, nb_u32 i, nb_u8 *out, nb_u32 cap);

/* ------------------------------------------------------------- events */
#define NB_E_SET_SSID           0
#define NB_E_AUTH               3
#define NB_E_DEAUTH             5
#define NB_E_DEAUTH_IND         6
#define NB_E_ASSOC              7
#define NB_E_REASSOC            9
#define NB_E_DISASSOC           11
#define NB_E_DISASSOC_IND       12
#define NB_E_LINK               16
#define NB_E_ROAM               19
#define NB_E_EAPOL_MSG          25
#define NB_E_IF                 54
#define NB_E_ESCAN_RESULT       69
#define NB_E_LAST               139
#define NB_E_STATUS_SUCCESS     0
#define NB_E_STATUS_FAIL        1
#define NB_E_STATUS_TIMEOUT     2
#define NB_E_STATUS_NO_NETWORKS 3
#define NB_E_STATUS_ABORT       4
#define NB_E_STATUS_UNSOLICITED 6
#define NB_E_STATUS_PARTIAL     8
#define NB_EVENT_LINK_UP        0x1        /* event flags bit for E_LINK */
#define NB_EVENT_MASK_LEN       18
#define NB_EVENT_HDR_LEN        (14 + 10 + 48)
#define NB_ETHERTYPE_LINK_CTL   0x886C
#define NB_ETHERTYPE_EAPOL      0x888E

typedef struct nb_event {
    nb_u32 type, status, reason, auth, datalen;
    nb_u16 flags;
    nb_u8  addr[6];
    nb_u8  ifidx;
    const nb_u8 *data;          /* points into the frame */
} nb_event;
/* 1 = firmware event parsed, 0 = not an event frame, <0 malformed */
int  nb_event_parse(const nb_u8 *f, nb_u32 len, nb_event *e);
void nb_event_mask(nb_u8 *mask, nb_u32 len, const nb_u32 *ev, nb_u32 n);

/* ---------------------------------------------------------------- scan */
#define NB_SCANTYPE_ACTIVE      0
#define NB_SCANTYPE_PASSIVE     1
#define NB_ESCAN_SYNC_ID        0x5257
#define NB_IO_TYPE_D11N         1
#define NB_IO_TYPE_D11AC        2
#define NB_BSS_INFO_FIXED       128
/* escan request (version 1 = bwfm_escan_params_v0, version 2 = _v2).
 * ssid may be NULL.  scan_ver 0/1 -> v1 layout, 2/3 -> v2 layout. */
nb_u32 nb_escan_params(nb_u8 *out, nb_u32 cap, int scan_ver, int scan_type, nb_u16 sync_id,
                       const nb_u8 *ssid, nb_u32 ssid_len);

typedef struct nb_escan_hdr {
    nb_u32 buflen, version;
    nb_u16 sync_id, count;
} nb_escan_hdr;

typedef struct nb_bss {
    nb_u8  bssid[6];
    nb_u16 beacon_period, capability;
    nb_u8  ssid_len;
    nb_u8  ssid[32];
    nb_u16 chanspec;
    nb_s16 rssi;
    nb_u8  channel;
    nb_u8  band;                /* 2 = 2.4 GHz, 5 = 5 GHz, 6 = 6 GHz, 0 unknown */
    nb_u16 bw_mhz;
    nb_u32 freq_mhz;
    const nb_u8 *ies;
    nb_u32 ies_len;
} nb_bss;
int nb_escan_hdr_parse(const nb_u8 *body, nb_u32 len, nb_escan_hdr *h);
/* iterate the bss_info records of an escan result body: *off starts at 12.
 * Returns 1 = one record in *b, 0 = end, <0 malformed. */
int nb_escan_next(const nb_u8 *body, nb_u32 len, nb_u32 *off, int io_type, nb_bss *b);
int nb_bss_parse(const nb_u8 *rec, nb_u32 len, int io_type, nb_bss *b);
void nb_chanspec_decode(nb_u16 cs, int io_type, nb_u8 *chan, nb_u8 *band, nb_u16 *bw);
nb_u32 nb_chan_freq(nb_u8 band, nb_u8 chan);
/* probe-response frame body (no 802.11 header): timestamp(8)=0, interval, capability, IEs */
nb_u32 nb_bss_probe_body(const nb_bss *b, nb_u8 *out, nb_u32 cap);
/* find information element id (and for 221, the 4-byte OUI+type) in an IE list */
const nb_u8 *nb_ie_find(const nb_u8 *ies, nb_u32 len, nb_u8 id, const nb_u8 *oui4);

/* ------------------------------------------------------------- connect */
#define NB_AUTH_OPEN            0
#define NB_MFP_NONE             0
#define NB_MFP_CAPABLE          1
#define NB_WPA_AUTH_DISABLED    0x0000u
#define NB_WPA_AUTH_WPA_PSK     0x0004u
#define NB_WPA_AUTH_WPA2_PSK    0x0080u
#define NB_WPA_AUTH_WPA2_PSK_SHA256 0x8000u
#define NB_WSEC_NONE            0x0u
#define NB_WSEC_WEP             0x1u
#define NB_WSEC_TKIP            0x2u
#define NB_WSEC_AES             0x4u
#define NB_CRYPTO_ALGO_OFF      0
#define NB_CRYPTO_ALGO_WEP1     1
#define NB_CRYPTO_ALGO_TKIP     2
#define NB_CRYPTO_ALGO_WEP128   3
#define NB_CRYPTO_ALGO_AES_CCM  4
#define NB_WSEC_PRIMARY_KEY     0x2u
#define NB_WSEC_KEY_LEN         164
#define NB_EXT_JOIN_LEN         68          /* without chanspec list */
#define NB_JOIN_LEN             48

/* our own cipher/akm names (the driver maps WDI enums onto these) */
#define NB_CIPHER_NONE  0
#define NB_CIPHER_WEP40 1
#define NB_CIPHER_TKIP  2
#define NB_CIPHER_CCMP  4
#define NB_CIPHER_WEP104 5
#define NB_AKM_NONE     0
#define NB_AKM_PSK      2    /* 00-0F-AC:2 */
#define NB_AKM_PSK256   6    /* 00-0F-AC:6 */

nb_u32 nb_ext_join_params(nb_u8 *out, nb_u32 cap, const nb_u8 *ssid, nb_u32 ssid_len,
                          const nb_u8 *bssid, nb_u16 chanspec);
nb_u32 nb_join_params(nb_u8 *out, nb_u32 cap, const nb_u8 *ssid, nb_u32 ssid_len,
                      const nb_u8 *bssid, nb_u16 chanspec);
/* rsc = 6-byte receive sequence counter (little endian PN) or NULL */
nb_u32 nb_wsec_key(nb_u8 *out, nb_u32 cap, nb_u32 index, int cipher, int pairwise,
                   const nb_u8 *key, nb_u32 keylen, const nb_u8 *peer, const nb_u8 *rsc);
nb_u32 nb_wsec_for_cipher(int cipher);
nb_u32 nb_wpa_auth_for(int rsn, int akm);
/* parse an RSN IE (id 48): group cipher suite type, first pairwise, first AKM, caps */
int nb_rsn_parse(const nb_u8 *ie, nb_u32 *group, nb_u32 *pairwise, nb_u32 *akm, nb_u16 *caps);
/* every 00-0F-AC pairwise suite type of an RSN IE as a bit mask (bit n = suite type n, n < 32):
 * diagnostics only (s258m).  NB_OK or NB_E_FORMAT. */
int nb_rsn_pairwise_mask(const nb_u8 *ie, nb_u32 *mask);
/* build an RSN IE with one pairwise and one AKM suite (00-0F-AC) */
nb_u32 nb_rsn_build(nb_u8 *out, nb_u32 cap, nb_u32 group, nb_u32 pairwise, nb_u32 akm, nb_u16 caps);
/* "assoc_info" iovar reply -> request/response IE lengths */
int nb_assoc_info_parse(const nb_u8 *b, nb_u32 len, nb_u32 *req_len, nb_u32 *resp_len);

/* ----------------------------------------------------------- data path */
#define NB_FRAME_8023   1
#define NB_FRAME_80211  2
/* classify a frame the OS handed us (802.11 data frame with LLC/SNAP, or Ethernet II) */
int nb_frame_kind(const nb_u8 *f, nb_u32 len);
/* 802.11 data frame (to-DS, LLC/SNAP) -> Ethernet II.  Returns length, 0 on error. */
nb_u32 nb_80211_to_8023(const nb_u8 *in, nb_u32 len, nb_u8 *out, nb_u32 cap);
/* Ethernet II -> 802.11 from-DS data frame with LLC/SNAP (for RxFrameFormat=1).
 * prot = 1 sets the Protected Frame bit: the frame was received encrypted and the firmware
 * already decrypted it (the native 802.11 convention; the OS drops unprotected frames, EAPOL
 * included, once a pairwise key is installed). */
nb_u32 nb_8023_to_80211(const nb_u8 *eth, nb_u32 len, const nb_u8 *bssid, nb_u8 *out, nb_u32 cap);
nb_u32 nb_8023_to_80211_p(const nb_u8 *eth, nb_u32 len, const nb_u8 *bssid, int prot, nb_u8 *out, nb_u32 cap);

/* EAPOL-Key message of an Ethernet EAPOL frame, from the Key Information flags only:
 * 1..4 = pairwise 4-way message 1..4, 5/6 = group key message 1/2, 0 = other EAPOL, -1 = not EAPOL */
#define NB_EAPOL_OTHER 0
#define NB_EAPOL_G1    5
#define NB_EAPOL_G2    6
int nb_eapol_key_msg(const nb_u8 *eth, nb_u32 len);

/* E_EAPOL_MSG (25): the firmware hands an EAPOL frame to the host inside an event
 * (hardware 2026-11-18 16:10: four of them one second apart = AP retransmitting 1/4, and no
 * RX completion at all).  The event data is either a whole Ethernet frame (ethertype 0x888E)
 * or the bare 802.1X frame (version, type, length, body).  Builds the Ethernet frame for the
 * host stack: DA = our MAC, SA = the event's peer address when the data has no header.
 * Returns the frame length (0 = not EAPOL) and *fmt = 1 (Ethernet) / 2 (bare 802.1X). */
nb_u32 nb_eapol_from_event(const nb_u8 *data, nb_u32 len, const nb_u8 *own, const nb_u8 *peer,
                           nb_u8 *out, nb_u32 cap, int *fmt);

/* --------------------------------------------------------- board file */
/* nwoas-bcm4378-board.bin: "NWWL", u32 version 1, u32 flags (bit0 = MAC),
 * mac[6], pad[2], u32 cal_len, u32 cal_crc32, cal[cal_len]. */
#define NB_BOARD_MAGIC  0x4C57574Eu      /* "NWWL" little endian */
#define NB_BOARD_HDR    28
typedef struct nb_board {
    int have_mac;
    nb_u8 mac[6];
    const nb_u8 *cal;
    nb_u32 cal_len;
} nb_board;
int    nb_board_parse(const nb_u8 *f, nb_u32 len, nb_board *b);
nb_u32 nb_board_build(nb_u8 *out, nb_u32 cap, const nb_u8 *mac, const nb_u8 *cal, nb_u32 cal_len);
nb_u32 nb_crc32(const nb_u8 *p, nb_u32 n);

/* ------------------------------------------------------- small helpers */
/* NVRAM file name for this board: P-<board>_M-<module>_V-<vendor>.txt (the
 * package keeps one file per vendor; same-vendor versions are identical). */
int nb_nvram_name(char *out, nb_u32 cap, const char *board, const nb_otp *o);

#ifdef __cplusplus
}
#endif
#endif
