/* nwbt_core.h - hardware independent logic of the NWOAS BCM4378 Bluetooth transport (nwbt4378.sys).
 *
 * No Windows headers, no I/O: every function works on plain byte buffers so it can be unit tested on
 * the Mac (tests/test_nwbt_core.c).  The driver (src/driver) does the MMIO, DMA memory and BthX side.
 *
 * Protocol facts (register offsets, ring geometry, message layouts, boot stages, OTP format, vendor
 * HCI commands) come from reading the Linux driver drivers/bluetooth/hci_bcm4377.c (Asahi Linux,
 * GPL-2.0-only OR MIT); this file is an independent implementation, no code was copied.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef NWBT_CORE_H
#define NWBT_CORE_H

#if defined(_KERNEL_MODE) || defined(_WDMDDK_) || defined(_NTDDK_)
/* kernel build (/kernel): the user-mode stdint.h clashes with the km CRT headers (macro warnings),
 * so the fixed-width types are declared here with the MSVC widths. */
#include <stddef.h>
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
#else
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- results */
#define NBT_OK            0
#define NBT_ERR_ARG      (-1)
#define NBT_ERR_FULL     (-2)
#define NBT_ERR_TOOBIG   (-3)
#define NBT_ERR_MSGID    (-4)
#define NBT_ERR_GEN      (-5)
#define NBT_ERR_FORMAT   (-6)
#define NBT_ERR_RING     (-7)
#define NBT_ERR_UNKNOWN  (-8)

/* ---------------------------------------------------------------- PCI config (fn1) */
#define NBT_CFG_BAR0_WINDOW1        0x80
#define NBT_CFG_BAR0_WINDOW2        0x70
#define NBT_CFG_BAR0_CORE2_WINDOW1  0x74
#define NBT_CFG_BAR0_CORE2_WINDOW2  0x78
#define NBT_CFG_BAR2_WINDOW         0x84
#define NBT_CFG_SUBSYS_CTRL         0x88
#define NBT_SUBSYS_CTRL_SET         (1u << 16)
#define NBT_4378_WINDOW1            0x18002000u
#define NBT_4378_WINDOW2            0x1810a000u
#define NBT_4378_CORE2_WINDOW1      0x18011000u
#define NBT_4378_CORE2_WINDOW2      0x18107000u
#define NBT_BAR2_WINDOW_DEFAULT     0x19000000u
#define NBT_CHIP_4378               0x4378u

/* ---------------------------------------------------------------- BAR0 / BAR2 registers */
#define NBT_BAR0_FW_DOORBELL        0x140
#define NBT_BAR0_RTI_CONTROL        0x144
#define NBT_BAR0_SLEEP_CONTROL      0x150
#define NBT_SLEEP_UNQUIESCE         0
#define NBT_SLEEP_AWAKE             2
#define NBT_SLEEP_QUIESCE           3
#define NBT_BAR0_DOORBELL           0x174
#define NBT_BAR0_HOST_WINDOW_LO     0x590
#define NBT_BAR0_HOST_WINDOW_HI     0x594
#define NBT_BAR0_HOST_WINDOW_SIZE   0x598
#define NBT_BAR0_OTP                0x4120
#define NBT_OTP_SIZE                0xe0
#define NBT_BAR2_BOOTSTAGE          0x200454
#define NBT_BAR2_FW_LO              0x200478
#define NBT_BAR2_FW_HI              0x20047c
#define NBT_BAR2_FW_SIZE            0x200480
#define NBT_BAR2_CONTEXT_LO         0x20048c
#define NBT_BAR2_CONTEXT_HI         0x200450
#define NBT_BAR2_RTI_STATUS         0x20045c
#define NBT_BAR2_RTI_WINDOW_LO      0x200494
#define NBT_BAR2_RTI_WINDOW_HI      0x200498
#define NBT_BAR2_RTI_WINDOW_SIZE    0x20049c
#define NBT_DMA_WINDOW              0xfffffe00u   /* chip DMA reaches [0, 0xfffffe00] only */
#define NBT_BOOTSTAGE_DONE          2
#define NBT_RTI_STATE1              1
#define NBT_RTI_STATE2              2

uint32_t nbt_doorbell_value(uint8_t doorbell, uint16_t value);

/* ---------------------------------------------------------------- rings */
enum { NBT_XR_CONTROL = 0, NBT_XR_HCI_H2D = 1, NBT_XR_HCI_D2H = 2, NBT_XR_SCO_H2D = 3,
       NBT_XR_SCO_D2H = 4, NBT_XR_ACL_H2D = 5, NBT_XR_ACL_D2H = 6, NBT_N_XR_USED = 7 };
enum { NBT_CR_CONTROL_ACK = 0, NBT_CR_HCI_ACL_ACK = 1, NBT_CR_HCI_ACL_EVENT = 2, NBT_CR_SCO_ACK = 3,
       NBT_CR_SCO_EVENT = 4, NBT_N_CR_USED = 5 };
enum { NBT_DB_CONTROL = 0, NBT_DB_HCI_H2D = 1, NBT_DB_HCI_D2H = 2, NBT_DB_ACL_H2D = 3,
       NBT_DB_ACL_D2H = 4, NBT_DB_SCO = 6 };

#define NBT_N_XFER_RINGS       9        /* the firmware wants exactly these counts in the context */
#define NBT_N_COMPLETION_RINGS 6
#define NBT_MAX_RING           256
#define NBT_RING_N             128
#define NBT_ENTRY_SIZE         16       /* transfer and completion ring entries */
#define NBT_CONTROL_MSG_SIZE   0x34
#define NBT_CONTEXT_SIZE       0x68
#define NBT_PERIPH_INFO_SIZE   0x20
#define NBT_MAX_INPLACE        (4 * 0xff)
#define NBT_MAX_EVENT_PAYLOAD  264      /* 255 + 2 header bytes (event), rounded up to 4 */
#define NBT_MAX_SCO_PAYLOAD    260      /* 255 + 3, rounded up to 4 */
#define NBT_MAX_ACL_PAYLOAD    1032     /* 1024 + 4 + 4 header bytes */
#define NBT_D2H_POSTED         0xf      /* entries offered to the device on a d2h ring */

#define NBT_XF_MAPPED          0x01     /* transfer/completion entry flags */
#define NBT_XF_IN_FOOTER       0x02

#define NBT_RF_VIRTUAL         0x01     /* transfer ring kind */
#define NBT_RF_SYNC            0x02
#define NBT_RF_D2H_BUFS        0x04
#define NBT_RF_WAIT            0x08

typedef struct {
    uint8_t  id, doorbell, completion, flags;
    uint16_t n_entries;
    uint16_t payload_size;              /* in-place bytes after each entry */
    uint16_t mapped_size;               /* out-of-place payload slot size */
    uint8_t  generation;
    uint8_t  enabled;
    uint32_t ring_off;                  /* offset in the DMA block (unused for virtual rings) */
    uint32_t payload_off;               /* mapped payload slots */
    uint32_t msgids[NBT_MAX_RING / 32]; /* message ids in flight */
} nbt_xring;

typedef struct {
    uint8_t  id, enabled;
    uint16_t n_entries;
    uint16_t payload_size;
    uint16_t delay;
    uint32_t ring_off;
    uint32_t xfer_mask;                 /* transfer rings that may appear in this completion ring */
} nbt_cring;

/* shared ring index block (16-bit indices, little endian) */
#define NBT_RS_CHEAD(i)   (2u * (i))
#define NBT_RS_CTAIL(i)   (2u * (NBT_N_COMPLETION_RINGS + (i)))
#define NBT_RS_XHEAD(i)   (2u * (2 * NBT_N_COMPLETION_RINGS + (i)))
#define NBT_RS_XTAIL(i)   (2u * (2 * NBT_N_COMPLETION_RINGS + NBT_N_XFER_RINGS + (i)))
#define NBT_RS_SIZE       (2u * (2 * NBT_N_COMPLETION_RINGS + 2 * NBT_N_XFER_RINGS))

typedef struct {
    nbt_xring x[NBT_N_XR_USED];
    nbt_cring c[NBT_N_CR_USED];
    uint32_t  ctx_off, state_off, periph_off;
    uint32_t  size;                     /* whole DMA block, page multiple */
} nbt_layout;

/* Ring geometry (same as the Linux driver) and offsets inside one DMA block; every region 4 KiB aligned. */
int nbt_layout_init(nbt_layout *L);
/* Context ("converged IPC") structure for a block at bus address base. */
void nbt_context_build(uint8_t ctx[NBT_CONTEXT_SIZE], const nbt_layout *L, uint64_t base);
void nbt_msg_create_cring(uint8_t m[NBT_CONTROL_MSG_SIZE], const nbt_cring *c, uint64_t base);
void nbt_msg_create_xring(uint8_t m[NBT_CONTROL_MSG_SIZE], const nbt_xring *r, uint64_t base);
void nbt_msg_destroy_cring(uint8_t m[NBT_CONTROL_MSG_SIZE], uint8_t id);
void nbt_msg_destroy_xring(uint8_t m[NBT_CONTROL_MSG_SIZE], uint8_t id);

uint16_t nbt_rs_get(const uint8_t *state, uint32_t off);
void nbt_rs_set(uint8_t *state, uint32_t off, uint16_t v);

/* Put one message on a host->device transfer ring.  blk = DMA block, base = its bus address,
 * state = ring index block.  Returns the message id (>= 0) and *db = new head for the doorbell. */
int nbt_xring_put(nbt_xring *r, uint8_t *blk, uint64_t base, uint8_t *state,
                  const uint8_t *data, uint32_t len, uint16_t *db);
/* Acknowledgement of raw message id (generation << 8 | id).  Returns the id or an error. */
int nbt_xring_ack(nbt_xring *r, uint16_t raw);
int nbt_xring_busy(const nbt_xring *r, int msgid);
uint32_t nbt_xring_inflight(const nbt_xring *r);
/* Before a create message: head = tail = 0, generation + 1, no message in flight. */
void nbt_xring_reset(nbt_xring *r, uint8_t *state);
/* d2h buffer ring: entry i points at payload slot i (called after the create message). */
void nbt_xring_fill_d2h(nbt_xring *r, uint8_t *blk, uint64_t base);
/* After a device->host message on a virtual / d2h ring: head + 1, returns the doorbell value. */
uint16_t nbt_xring_advance(const nbt_xring *r, uint8_t *state);

typedef struct {
    uint8_t  flags;
    uint16_t xring;                     /* transfer ring this entry belongs to */
    uint16_t msgid;                     /* raw */
    uint32_t len;
    uint32_t data_off;                  /* offset in the DMA block of the in-place data */
} nbt_centry;

int nbt_cring_entry(const nbt_cring *c, const uint8_t *blk, uint16_t pos, nbt_centry *out);
/* Payload of a device->host message: in-place in the completion entry or in a mapped slot. */
int nbt_d2h_payload(const nbt_xring *r, const nbt_cring *c, const nbt_centry *e, uint32_t *off, uint32_t *len);

/* ---------------------------------------------------------------- HCI */
#define NBT_HCI_EVT_CMD_COMPLETE  0x0e
#define NBT_HCI_EVT_CMD_STATUS    0x0f
#define NBT_HCI_EVT_LE_META       0x3e
#define NBT_LE_EXT_ADV_REPORT     0x0d
#define NBT_OP_RESET              0x0c03
#define NBT_OP_READ_LOCAL_VERSION 0x1001
#define NBT_OP_READ_LOCAL_CMDS    0x1002
#define NBT_OP_READ_BD_ADDR       0x1009
#define NBT_OP_LE_READ_FEATURES   0x2003
#define NBT_OP_BCM_SET_BDADDR     0xfc01
#define NBT_OP_BCM_CALIBRATION    0xfd97
#define NBT_OP_BCM_PTB            0xfe0d
#define NBT_CAL_CHUNK             0xe6
#define NBT_CAL_PARAM_LEN         (1 + 2 + NBT_CAL_CHUNK)   /* 0xe9 */
#define NBT_PTB_CHUNK             0xcf
#define NBT_PTB_PARAM_LEN         (2 + NBT_PTB_CHUNK)       /* 0xd1 */

uint32_t nbt_hci_cmd(uint8_t *out, uint32_t cap, uint16_t opcode, const uint8_t *params, uint32_t plen);
/* Command Complete / Command Status: opcode, status; for Complete ret points at the return parameters
 * (status first).  Returns 1 for Complete, 2 for Status, 0 for any other event, < 0 malformed. */
int nbt_hci_cmd_done(const uint8_t *evt, uint32_t len, uint16_t *opcode, uint8_t *status,
                     uint32_t *ret_off, uint32_t *ret_len);
uint32_t nbt_chunks(uint32_t len, uint32_t chunk);
int nbt_cal_chunk(const uint8_t *blob, uint32_t len, uint32_t idx, uint8_t params[NBT_CAL_PARAM_LEN]);
int nbt_ptb_chunk(const uint8_t *blob, uint32_t len, uint32_t idx, uint8_t params[NBT_PTB_PARAM_LEN]);
/* Read_BD_ADDR result (little endian) that only means "not programmed" (93:76:00:xx:<chip>). */
int nbt_bdaddr_is_default(const uint8_t le[6], uint16_t chip);
/* ADT local-mac-address is in display order; HCI wants little endian. */
void nbt_bdaddr_from_adt(const uint8_t adt[6], uint8_t le[6]);

#define NBT_FIX_MWS        0x1   /* Read Local Supported Commands: hide Get MWS Transport Layer Config */
#define NBT_FIX_LE_CODED   0x2   /* LE Read Local Supported Features: hide LE Coded PHY */
#define NBT_FIX_EXTADV_PHY 0x4   /* LE Extended Advertising Report: clear junk in the PHY bytes */
/* Applies the enabled fixes to one event in place; returns the fixes that changed something. */
uint32_t nbt_fix_event(uint8_t *evt, uint32_t len, uint32_t enabled);

/* ---------------------------------------------------------------- OTP / files */
int nbt_otp_parse(const uint8_t *otp, uint32_t n, char stepping[16], char vendor[16]);
/* Packaged file name "bt4378<stepping>-atlantisb-<vendor>.<ext>" (only b1 + m/u are packaged). */
int nbt_fw_name(char *out, uint32_t cap, const char *stepping, const char *vendor, const char *ext);

/* ---------------------------------------------------------------- packet FIFO */
typedef struct {
    uint8_t *buf;
    uint32_t cap, rd, wr, used;
    uint32_t count, drops;
} nbt_fifo;
void nbt_fifo_init(nbt_fifo *f, uint8_t *mem, uint32_t cap);
int nbt_fifo_push(nbt_fifo *f, uint8_t type, const uint8_t *data, uint32_t len);
int nbt_fifo_peek(const nbt_fifo *f, uint8_t *type, uint32_t *len);
int nbt_fifo_pop(nbt_fifo *f, uint8_t *out, uint32_t cap, uint8_t *type, uint32_t *len);
/* Discards the oldest packet (one that can never fit a reader's buffer). */
int nbt_fifo_drop(nbt_fifo *f);

#ifdef __cplusplus
}
#endif
#endif
