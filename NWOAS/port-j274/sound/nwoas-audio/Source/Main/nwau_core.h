/*
 * NWOAS Mac mini (J274) internal speaker - hardware independent core logic.
 *
 * This file is plain C with no OS dependency so that it can be compiled and
 * unit tested on the build Mac (tests/test_nwau_core.c) and linked unchanged
 * into the Windows kernel driver (nwau_hw.cpp).
 *
 * Contents:
 *   - the S256 window header contract (PLAN-S256.md) and its validation
 *   - the expected ACPI memory resources (order fixed by the contract)
 *   - 16-bit stereo -> mono 24-bit-in-32 (msb aligned) conversion
 *   - Windows volume (dB, 16.16 fixed point) -> TAS5770L reg 0x05 mapping
 *   - ring / ADMAC descriptor bookkeeping
 *   - the amplifier write gate (every amp register write is checked here)
 *   - the speaker thermal model (V/I sense -> power -> two stage RC model ->
 *     volume floor) and its fail-safe health checks
 *
 * No GPL code is used.  Register facts come from the MIT-licensed m1n1
 * proxyclient (hw/admac.py, hw/mca.py, hw/i2c.py) and the NWOAS S255 tone test.
 * The thermal model parameters are the published J274 numbers of Asahi
 * speakersafetyd (conf/apple/j274.conf); the model equations (first order
 * coil + magnet RC network, attenuation ramp over the last t_window degrees)
 * are re-derived here from those facts and written from scratch in integer
 * arithmetic (no floating point in the kernel driver).
 */
#ifndef NWAU_CORE_H
#define NWAU_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
typedef unsigned __int8  nwu8;
typedef unsigned __int16 nwu16;
typedef signed __int16   nws16;
typedef unsigned __int32 nwu32;
typedef signed __int32   nws32;
typedef unsigned __int64 nwu64;
typedef signed __int64   nws64;
#else
#include <stdint.h>
typedef uint8_t  nwu8;
typedef uint16_t nwu16;
typedef int16_t  nws16;
typedef uint32_t nwu32;
typedef int32_t  nws32;
typedef uint64_t nwu64;
typedef int64_t  nws64;
#endif

/* ---- contract constants (PLAN-S256.md) ---------------------------------- */
#define NWAU_HDR_MAGIC          0x5541574eu   /* 'NWAU' little endian */
#define NWAU_HDR_VERSION        1u            /* v1: playback only (0x30 bytes) */
#define NWAU_HDR_VERSION_SENSE  2u            /* v2: + V/I sense ring and protection fields (0x4c bytes) */
#define NWAU_HDR_WORDS_V1       12u
#define NWAU_HDR_WORDS          19u           /* words read by the driver (v1 leaves 12..18 zero) */
#define NWAU_HDR_STATUS_READY   0x1u
#define NWAU_HDR_STATUS_SENSE   0x2u          /* v2: launcher prepared the sense path (RX serdes, ADMAC RX, amp TX slots) */
#define NWAU_RING_OFFSET        0x1000u       /* ring starts at window + 0x1000 */
#define NWAU_WINDOW_BYTES       0x40000u
#define NWAU_SAMPLE_RATE        48000u
#define NWAU_FORMAT_MONO24IN32  1u

#define NWAU_NUM_RESOURCES      5u
#define NWAU_RES_I2C            0u
#define NWAU_RES_ADMAC          1u
#define NWAU_RES_MCA_CLUSTER    2u
#define NWAU_RES_MCA_SWITCH1    3u
#define NWAU_RES_WINDOW         4u

#define NWAU_ADMAC_CHANNELS     24u           /* ADT #dma-channels = 12 pairs */

/* ---- V/I sense ring (header v2) ------------------------------------------ */
#define NWAU_SENSE_FORMAT_IV16  2u            /* one 32-bit word per frame: bits 15:0 = I (s16), 31:16 = V (s16) */
#define NWAU_SENSE_CHUNK_BYTES  0x2000u       /* one RX descriptor = 2048 frames (42.7 ms); 4 in flight = 171 ms
                                                 of capture across a hypervisor stall */
#define NWAU_SENSE_CHUNK_FRAMES (NWAU_SENSE_CHUNK_BYTES / 4u)
#define NWAU_SENSE_MIN_BYTES    (8u * NWAU_SENSE_CHUNK_BYTES)
#define NWAU_SENSE_MARKER       0x5a5aa5a5u   /* written into an RX chunk before it is queued */
#define NWAU_MCA_CLUSTERS       6u            /* MCA cluster block 0x18000 / 0x4000 */

/* ---- data path tuning ---------------------------------------------------- */
#define NWAU_CHUNK_BYTES        0x2000u       /* one ADMAC descriptor = 2048 mono frames (42.7 ms); the ADMAC
                                                 ring holds 4, so 171 ms of audio survive a stall in which the
                                                 guest does not run (m1n1 stops all CPUs for proxy traps) */
#define NWAU_MAX_INFLIGHT       4u            /* ADMAC descriptor ring depth */
#define NWAU_MIN_INFLIGHT       2u            /* below this, pad the current chunk with silence */
#define NWAU_MAX_LEAD_BYTES     (8u * NWAU_CHUNK_BYTES) /* written but not yet queued (~170 ms) */
#define NWAU_MIN_RING_BYTES     (16u * NWAU_CHUNK_BYTES)

/* ---- amplifier (TAS5770L, TAS2770 register layout) ----------------------- */
#define NWAU_AMP_REG_PWR_CTRL   0x02u
#define NWAU_AMP_REG_GAIN       0x03u
#define NWAU_AMP_REG_VOLUME     0x05u
#define NWAU_AMP_PWR_ACTIVE     0x0cu         /* active, V/I sense off */
#define NWAU_AMP_PWR_MUTE       0x0du
#define NWAU_AMP_PWR_SHUTDOWN   0x0eu
#define NWAU_AMP_PWR_ACTIVE_SNS 0x00u         /* active, V and I sense on (bits 3:2 = 0) */
#define NWAU_AMP_PWR_MUTE_SNS   0x01u         /* mute, sense on */
#define NWAU_AMP_PWR_SHDN_SNS   0x02u         /* software shutdown, sense on: the sense bits are only
                                                 changed while the amp is in shutdown */
#define NWAU_AMP_GAIN_FIXED     0x00u         /* lowest analog gain (11 dB): static mode */
#define NWAU_AMP_GAIN_STATIC    NWAU_AMP_GAIN_FIXED
#define NWAU_PROT_GAIN          0x14u         /* 21 dB (11 + 0.5 * 20), J274 speaker-config / Asahi gain 20.
                                                 Only while the thermal protection is healthy. */
#define NWAU_AMP_GAIN_UNKNOWN   0xffu         /* gate shadow after a failed / unverified gain write */
/* amp sense transmit config (TAS2770 layout, written while in shutdown):
 *   0x0e TDM_CFG_REG4 = 0x21: TX keeper on, unused TX slots driven 0 (zero fill),
 *        TX offset 0 (same as the RX offset 0 the launcher uses), TX edge bit 1  [hypothesis: edge]
 *   0x0f TDM_CFG_REG5 = 0x42: V sense transmit on, slot 2
 *   0x10 TDM_CFG_REG6 = 0x40: I sense transmit on, slot 0
 * Slots are 8-bit units: I = frame bits 0..15, V = bits 16..31 (Asahi DT imon 0 / vmon 2,
 * speakersafetyd j274 is_chan 0 / vs_chan 1 on a 16 x 16-bit capture). */
#define NWAU_AMP_REG_TDM4       0x0eu
#define NWAU_AMP_SNS_TDM4       0x21u
#define NWAU_AMP_SNS_TDM5       0x42u
#define NWAU_AMP_SNS_TDM6       0x40u
#define NWAU_VOL_HARD_FLOOR     0x0cu         /* -6 dB: driver refuses a header cap louder than this.
                                                 Amp gain 0x00 (11 dB, forced) -> +5 dB full scale.
                                                 speakersafetyd j274 model: continuous limit
                                                 (140-46)/(40+60) = 0.94 W; full-scale sine at 11 dB
                                                 into 4.6 ohm = 2.74 W -> -4.6 dB needed; -6 dB keeps
                                                 1.4 dB margin for a continuous full-scale sine. */
#define NWAU_VOL_REG_MAX        0xc9u         /* -100.5 dB */
#define NWAU_VOL_REG_DEFAULT    0x50u         /* -40 dB until Windows sets a volume */
#define NWAU_VOL_STEP_16_16     0x8000        /* 0.5 dB in 16.16 fixed point */

/* ---- result codes (also written to the registry as NwAudioStartStatus) --- */
enum {
    NWAU_OK               = 0,
    NWAU_ERES_COUNT       = 0x101,            /* not exactly 5 memory resources */
    NWAU_ERES_BASE        = 0x110,            /* + index: address/size mismatch */
    NWAU_EHDR_MAGIC       = 0x201,
    NWAU_EHDR_VERSION     = 0x202,
    NWAU_EHDR_NOT_READY   = 0x203,
    NWAU_EHDR_RING        = 0x204,
    NWAU_EHDR_IOVA        = 0x205,
    NWAU_EHDR_RATE        = 0x206,
    NWAU_EHDR_FORMAT      = 0x207,
    NWAU_EHDR_CHANNEL     = 0x208,
    NWAU_EHDR_CLUSTER     = 0x209,
    NWAU_EHDR_AMP_ADDR    = 0x20a,
    NWAU_EHDR_VOL_CAP     = 0x20b,
    NWAU_EHDR_SENSE_RING  = 0x20c,            /* v2 sense ring outside the window / overlapping */
    NWAU_EHDR_SENSE_CHAN  = 0x20d,
    NWAU_EHDR_SENSE_FMT   = 0x20e,
    NWAU_EHDR_PROT        = 0x20f,            /* prot_gain not 0 / 0x14, or prot_vol_floor > 0xc9 */
    NWAU_ERING_PARAM      = 0x2f0,
    NWAU_EMAP_BASE        = 0x300,            /* + index: MmMapIoSpaceEx failed */
    NWAU_ENO_MEMORY       = 0x310
};

/* ---- resources ----------------------------------------------------------- */
typedef struct nwau_res_expect {
    nwu64 start;                              /* 0 = any (window) */
    nwu32 length;
} nwau_res_expect;

extern const nwau_res_expect nwau_expected_resources[NWAU_NUM_RESOURCES];

/* index = position in the ACPI _CRS / translated resource list */
int nwau_check_resource(nwu32 index, nwu64 start, nwu64 length);

/* ---- header -------------------------------------------------------------- */
typedef struct nwau_header {
    nwu32 magic;        /* 0x00 */
    nwu32 version;      /* 0x04 */
    nwu64 ring_iova;    /* 0x08 */
    nwu32 ring_bytes;   /* 0x10 */
    nwu32 sample_rate;  /* 0x14 */
    nwu32 format;       /* 0x18 */
    nwu32 admac_chan;   /* 0x1c */
    nwu32 mca_cluster;  /* 0x20 */
    nwu32 amp_addr;     /* 0x24 */
    nwu32 vol_cap;      /* 0x28 */
    nwu32 status;       /* 0x2c */
    /* version 2 */
    nwu64 sense_iova;   /* 0x30: V/I ring IOVA (same DART-sio mapping as the TX ring) */
    nwu32 sense_bytes;  /* 0x38 */
    nwu32 sense_chan;   /* 0x3c: ADMAC RX channel (odd, same cluster group as admac_chan) */
    nwu32 sense_format; /* 0x40: NWAU_SENSE_FORMAT_IV16 */
    nwu32 prot_gain;    /* 0x44: 0 = protection off (sense measured only), 0x14 = may raise gain to 21 dB */
    nwu32 prot_vol_floor; /* 0x48: smallest amp 0x05 value allowed while the gain is 0x14 */
} nwau_header;

void nwau_header_from_words(const nwu32 w[NWAU_HDR_WORDS], nwau_header *h);
int  nwau_header_validate(const nwau_header *h, nwu32 window_bytes);
/* v2 with status bit1: the launcher prepared the sense path */
int  nwau_header_has_sense(const nwau_header *h);
/* byte offset of the sense ring inside the window (only valid after validate + has_sense) */
nwu32 nwau_header_sense_offset(const nwau_header *h);

/* ---- volume -------------------------------------------------------------- */
/* level_16_16: Windows KSPROPERTY_AUDIO_VOLUMELEVEL value (dB * 65536, <= 0).
 * Returns the value for amp reg 0x05.  Never below max(cap, NWAU_VOL_HARD_FLOOR). */
nwu32 nwau_volume_reg(nws32 level_16_16, int muted, nwu32 cap);
/* Same mapping with an explicit base register (no hard floor): base + user
 * attenuation steps, clamped to 0xc9.  Used with gain 0x14, where the floor
 * comes from the thermal model and the gate. */
nwu32 nwau_volume_reg_base(nws32 level_16_16, int muted, nwu32 base);
/* Static mode when the header enables the protection: the same loudness as the
 * protected mapping (gain 0x14, base 0), i.e. 0x14 steps less attenuation at
 * gain 0x00, but never below max(cap, NWAU_VOL_HARD_FLOOR).  A fall-back to
 * static mode then keeps the level unless the static cap binds. */
nwu32 nwau_volume_reg_matched(nws32 level_16_16, int muted, nwu32 cap);

/* ---- format conversion --------------------------------------------------- */
typedef struct nwau_conv {
    nwu8  carry[4];     /* partial stereo frame left over from the previous call */
    nwu32 ncarry;
} nwau_conv;

void  nwau_conv_reset(nwau_conv *c);
/* one stereo s16 frame -> mono, 24-bit value msb aligned in 32 bits */
nwu32 nwau_mono_word(nws16 left, nws16 right);
/* Convert little-endian interleaved s16 stereo bytes.  Writes at most
 * dst_frames 32-bit words with single aligned 32-bit stores (dst may be
 * device/non-cached memory).  *consumed receives the number of src bytes
 * used; bytes of an incomplete trailing frame are kept in the carry.
 * Returns the number of words written. */
nwu32 nwau_conv_run(nwau_conv *c, const nwu8 *src, nwu32 len,
                    volatile nwu32 *dst, nwu32 dst_frames, nwu32 *consumed);
/* Same, and adds the sum of squares of the written mono s16 values to *energy
 * (if not NULL): the digital signal level the sense check compares against. */
nwu32 nwau_conv_run_e(nwau_conv *c, const nwu8 *src, nwu32 len,
                      volatile nwu32 *dst, nwu32 dst_frames, nwu32 *consumed,
                      nwu64 *energy);
/* Discard len bytes while keeping stereo frame alignment. */
void  nwau_conv_skip(nwau_conv *c, nwu32 len);

/* ---- ring / descriptor bookkeeping --------------------------------------- */
/* All positions are byte counts since start; ring offset = pos % ring_bytes.
 * Invariants: done <= sub <= wr, sub and done are chunk multiples,
 * (sub - done) / chunk <= max_inflight, wr - sub <= max_lead. */
typedef struct nwau_ring {
    nwu32 ring_bytes;
    nwu32 chunk_bytes;
    nwu32 max_inflight;
    nwu32 min_inflight;
    nwu32 max_lead;
    nwu64 wr;           /* bytes written into the ring by the driver */
    nwu64 sub;          /* bytes handed to ADMAC as descriptors */
    nwu64 done;         /* bytes ADMAC reported complete */
} nwau_ring;

int   nwau_ring_init(nwau_ring *r, nwu32 ring_bytes, nwu32 chunk_bytes,
                     nwu32 max_inflight, nwu32 min_inflight, nwu32 max_lead);
void  nwau_ring_reset(nwau_ring *r);
nwu32 nwau_ring_inflight(const nwau_ring *r);
nwu32 nwau_ring_write_space(const nwau_ring *r);
/* contiguous writable bytes at the write position; *off = ring offset */
nwu32 nwau_ring_write_span(const nwau_ring *r, nwu32 *off);
void  nwau_ring_commit(nwau_ring *r, nwu32 bytes);

#define NWAU_NEXT_NONE    0
#define NWAU_NEXT_DATA    1   /* a full chunk of written data */
#define NWAU_NEXT_PADDED  2   /* chunk completed with silence: zero pad range first */
/* Decide the next descriptor.  force_pad: complete a partial (or empty) chunk
 * with silence.  On success the ring state is advanced; the caller writes the
 * descriptor for [chunk_off, chunk_off + chunk_bytes).  For NWAU_NEXT_PADDED
 * the caller must zero [pad_off, pad_off + pad_len) before submitting. */
int   nwau_ring_next(nwau_ring *r, int force_pad,
                     nwu32 *chunk_off, nwu32 *pad_off, nwu32 *pad_len);
/* n completed descriptors reported by ADMAC; returns how many were accepted
 * (reports beyond the in-flight count are ignored and should be counted). */
nwu32 nwau_ring_complete(nwau_ring *r, nwu32 n);

/* ---- amplifier write gate -------------------------------------------------
 * Every amp register write goes through nwau_amp_write_allowed().  Loudness
 * rule: full scale output = 11 dB + 0.5 * gain - 0.5 * volume.
 *   gain 0x00: volume >= static_floor (header cap, never below 0x0c): <= +5 dB.
 *   gain 0x14: volume >= prot_floor when the protection is healthy, else
 *              static_floor + 0x14 (the same <= +5 dB full scale).
 *   unknown gain: treated as 0x14 and not healthy.
 *   0x03 = 0x14 when the header configures the protection (prot_gain 0x14)
 *   and the verified volume already satisfies the 0x14 floor; the driver then
 *   keeps 0x14 for the whole time (static mode = volume ceiling 0x20, no
 *   analog gain switching).  0x03 = 0x00 is always allowed (quieter).
 *   Any 0x05 value at or above the verified current volume (quieter or equal)
 *   is allowed: used by the fade-outs around gain changes.
 *   0x03 = 0x14 only when healthy, the header allows it and the verified
 *   volume already keeps the raise at or below the static loudness. */
typedef struct nwau_gate {
    int   serdes_on;    /* MCA TXA EN read back as 1 */
    int   dma_on;
    nwu32 inflight;     /* TX descriptors queued */
    nwu32 static_floor; /* max(header vol_cap, NWAU_VOL_HARD_FLOOR) */
    nwu32 gain_shadow;  /* last verified 0x03 (NWAU_AMP_GAIN_UNKNOWN after a failure) */
    nwu32 vol_shadow;   /* last verified 0x05 (0 = unknown, i.e. loudest) */
    int   prot_ok;      /* thermal protection healthy */
    nwu32 prot_gain;    /* header prot_gain (0 or 0x14) */
    nwu32 prot_floor;   /* max(model floor, header prot_vol_floor) */
} nwau_gate;

nwu32 nwau_gate_vol_floor(const nwau_gate *g);
int   nwau_amp_write_allowed(const nwau_gate *g, nwu32 reg, const nwu8 *data, nwu32 count);

/* ---- speaker thermal protection -------------------------------------------
 * Parameters: Asahi speakersafetyd conf/apple/j274.conf (facts, numbers only).
 * Units: temperatures micro degC (internal) / milli degC (reported), power uW,
 * time us, resistance milliohm, voltage mV, current mA. */
#define NWAU_TS_TR_COIL         40            /* degC/W */
#define NWAU_TS_TR_MAGNET       60            /* degC/W */
#define NWAU_TS_TAU_COIL_US     3700000u      /* 3.7 s */
#define NWAU_TS_TAU_MAGNET_US   250000000u    /* 250 s */
#define NWAU_TS_T_AMBIENT_MC    46000         /* 46 degC */
#define NWAU_TS_T_LIMIT_MC      140000
#define NWAU_TS_T_HEADROOM_MC   40000         /* above t_limit + headroom: fault (amp off) */
#define NWAU_TS_T_WINDOW_MC     20000         /* attenuation ramps over the last 20 degC below t_limit */
#define NWAU_TS_T_HYST_MC       5000
#define NWAU_TS_Z_NOMINAL_MOHM  4600
#define NWAU_TS_A_T_PPM         3700          /* coil resistance tempco 0.0037 / degC */
#define NWAU_TS_IS_SCALE_MA     3750          /* I sense full scale (s16 32768) */
#define NWAU_TS_VS_SCALE_MV     14000         /* V sense full scale */
/* attenuation needed at temp = t_limit for the protected gain (21 dB):
 * 10*log10(((140-46)/(40+60)) / (10^(21/10) / 4.6 * 2)) = -17.65 dB; rounded up.
 * The unit test recomputes it with libm. */
#define NWAU_TS_MIN_ATT_MDB     17652
/* resistance -> temperature (reported only, see PLAN-S256.md): R(T) = Rref (1 + a_t (T - Tref)) */
#define NWAU_TS_RREF_MOHM       NWAU_TS_Z_NOMINAL_MOHM
#define NWAU_TS_TREF_MC         25000
/* health checks */
#define NWAU_TS_SIG_MV2         10000         /* 0.1 V rms: below this no V/I plausibility check / verification */
#define NWAU_TS_Z_MIN_MOHM      2000
#define NWAU_TS_Z_MAX_MOHM      30000
#define NWAU_TS_R_MIN_MOHM      1000          /* resistive part must be clearly positive */
#define NWAU_TS_NEG_UW          (-10000)      /* block average power below -10 mW: bad sign */
#define NWAU_TS_RATIO_LO_PERMILLE 158         /* measured / expected V^2 below -8 dB: sense dead or scaled wrong */
#define NWAU_TS_RATIO_HI_PERMILLE 6310        /* above +8 dB: not verified (no trip, model sees the larger power) */
#define NWAU_TS_VERIFY_LO_PERMILLE 500        /* arming needs measured / expected V^2 within -3..+3 dB: a sense */
#define NWAU_TS_VERIFY_HI_PERMILLE 2000       /* scaled at half (-6 dB, 4x less power) never arms */
#define NWAU_TS_ARM_BLOCKS      6             /* verified loud blocks (256 ms) before the protection is healthy */
#define NWAU_TS_BAD_BLOCKS      3             /* consecutive bad blocks (sign/impedance/saturation) -> trip */
#define NWAU_TS_DEAD_BLOCKS     3             /* consecutive low-ratio blocks -> trip */
#define NWAU_TS_STALE_US        150000u       /* no sense block for 150 ms (guest time) while running -> trip */
#define NWAU_TS_BRIDGE_US       500000u       /* healthy and cool: bridge a sense gap with the blind model up
                                                 to 0.5 s before falling back */
#define NWAU_TS_BRIDGE_MAX_MC   110000        /* ... only while the model is below 110 degC (floor 0 to 120) */
#define NWAU_TS_WARMUP_BLOCKS   1             /* first block of a stream / after an RX restart (capture starts
                                                 just before the amp leaves shutdown): no verdict, model uses
                                                 the sent power */
#define NWAU_TS_BLIND_Z_MOHM    4000          /* blind model (no sense): sent power into 4.0 ohm (P1 measured
                                                 4.35 ohm at 1 kHz), i.e. a little more than the real power */
#define NWAU_TS_SAT_DIV         32            /* more than frames/32 clipped samples = clipped block */
#define NWAU_TS_SAT_EXPLAINED_MV2 12250000    /* (3.5 V)^2: sent signal within 12 dB of the 14 V sense
                                                 full scale, clipping expected (model uses the sent power) */

enum {  /* nwau_ts.state */
    NWAU_TS_OFF = 0,        /* no stream / no sense path: static mode */
    NWAU_TS_ARMING,         /* sense running, model running, not yet verified: static mode */
    NWAU_TS_HEALTHY,        /* verified: gain 0x14 allowed (if the header allows it) */
    NWAU_TS_TRIPPED         /* fail-safe until the next stream start: static mode */
};
enum {  /* nwau_ts.trip_reason / bad block reasons */
    NWAU_TS_R_NONE = 0,
    NWAU_TS_R_STALE,        /* no sense data in time */
    NWAU_TS_R_UNWRITTEN,    /* RX chunk still holds the marker: DMA did not write it */
    NWAU_TS_R_SATURATED,
    NWAU_TS_R_NEG_POWER,
    NWAU_TS_R_IMPEDANCE,
    NWAU_TS_R_DEAD,         /* signal sent, V sense far below what was sent */
    NWAU_TS_R_OVERTEMP,     /* model above t_limit + t_headroom */
    NWAU_TS_R_SETUP,        /* driver could not start the RX path */
    NWAU_TS_R_COUNT
};
enum {  /* nwau_ts_block / nwau_ts_stale events */
    NWAU_TS_EV_NONE = 0,
    NWAU_TS_EV_ARMED,
    NWAU_TS_EV_TRIP,
    NWAU_TS_EV_OVERTEMP,    /* also a trip; the driver must turn the amp off */
    NWAU_TS_EV_BRIDGE       /* sense gap: still healthy, blind model until data returns (or trip) */
};

typedef struct nwau_ts_drive {
    nwu64 tx_energy;    /* sum of mono s16^2 of the TX frames completed since the previous block */
    nwu32 tx_frames;    /* 0 = no TX completion in that time: hold the previous expectation */
    nwu32 gain_reg;     /* amp 0x03 in effect */
    nwu32 vol_reg;      /* amp 0x05 in effect */
} nwau_ts_drive;

typedef struct nwau_ts {
    nws64 t_coil_uc;
    nws64 t_mag_uc;
    nws64 hyst_coil_uc;
    nws64 hyst_mag_uc;
    nws64 max_coil_uc;      /* highest coil estimate since init */
    nwu32 state;
    nwu32 trip_reason;
    nwu32 trips;
    nwu32 verified;         /* verified loud blocks in this arming */
    nwu32 bad_run;          /* consecutive bad (sign / impedance / saturation) blocks */
    nwu32 dead_run;         /* consecutive low ratio blocks */
    nwu32 blocks;           /* processed blocks (all time) */
    nwu32 stream_blocks;    /* blocks in this stream */
    nwu32 bad[NWAU_TS_R_COUNT];  /* bad block counters by reason */
    nws64 exp_inst_mv2;     /* last expected V^2 (held when no TX completion) */
    nws64 exp_s_mv2;        /* smoothed expected V^2 */
    nws64 v2_s_mv2;         /* smoothed measured V^2 */
    /* last block */
    nws32 p_uw;
    nwu32 vrms_mv;
    nwu32 irms_ma;
    nwu32 z_mohm;           /* |V| / |I| (0 = not measured: signal too small) */
    nwu32 r_mohm;           /* sum(v*i) / sum(i*i): resistive part */
    nwu32 r_min_mohm;       /* smallest r since stream start (0 = none) */
    nws32 rtemp_mc;         /* temperature from r_min with Rref / a_t (reported only) */
    nwu32 ratio_permille;   /* smoothed measured / expected V^2 */
    nwu32 sat;              /* clipped samples in the last block */
    nwu64 blind_frames;     /* frames modelled from the sent signal only (TRIPPED or bridging) */
    nwu32 bridging;         /* healthy, in a sense gap, blind model running */
    nwu32 bridges;          /* gaps bridged without a fall-back */
} nwau_ts;

/* exp(-x) with x in Q32 (unsigned), result Q30 (rounded) */
nwu32 nwau_exp_neg_q30(nwu64 x_q32);
/* 10^(db_x2 / 20) power ratio, i.e. 10^(dB/10) for dB = db_x2 / 2, Q30; |db| <= 120 */
nwu64 nwau_pow10_db_x2_q30(nws32 db_x2);
nwu32 nwau_isqrt64(nwu64 v);

/* power-on assumption: coil t_limit - t_window - 1 = 119 degC, magnet on the
 * static line between ambient and the coil (speakersafetyd cold boot rule) */
void  nwau_ts_init(nwau_ts *t);
/* no power for idle_us (amp off): exact decay of the coupled two-stage model */
void  nwau_ts_idle(nwau_ts *t, nwu64 idle_us);
/* restore a saved state that is idle_us old; never below the cold assumption */
void  nwau_ts_restore(nwau_ts *t, nws64 coil_uc, nws64 mag_uc, nwu64 idle_us);
/* stream start: decay over the idle time, raise to at least the cold
 * assumption (also covers time spent in static mode), state ARMING */
void  nwau_ts_stream_start(nwau_ts *t, nwu64 idle_us);
void  nwau_ts_stream_stop(nwau_ts *t);
void  nwau_ts_trip(nwau_ts *t, nwu32 reason);
/* one sense block (frames words of NWAU_SENSE_FORMAT_IV16); returns an NWAU_TS_EV_* */
int   nwau_ts_block(nwau_ts *t, const volatile nwu32 *w, nwu32 frames, const nwau_ts_drive *d);
/* no block for us_since_last while ARMING/HEALTHY: trip after NWAU_TS_STALE_US */
int   nwau_ts_stale(nwau_ts *t, nwu64 us_since_last);
/* while TRIPPED during a stream: run the model on the sent signal only (frames
   of TX data with this energy at gain/vol), so the temperature stays tracked */
int   nwau_ts_blind(nwau_ts *t, nwu64 tx_energy, nwu32 tx_frames, nwu32 gain_reg, nwu32 vol_reg);
/* after the driver restarted a stalled RX path: TRIPPED -> ARMING (warm-up, re-verify) */
void  nwau_ts_rearm(nwau_ts *t);
/* the driver restarted RX inside a bridged gap: next block is a warm-up block */
void  nwau_ts_rx_restarted(nwau_ts *t);
/* attenuation steps (amp 0x05) the model requires at the protected gain */
nwu32 nwau_ts_floor(const nwau_ts *t);
/* full scale output in 0.5 dB units relative to 0 dBV: 22 + gain - vol */
nws32 nwau_full_scale_db_x2(nwu32 gain_reg, nwu32 vol_reg);

#ifdef __cplusplus
}
#endif

#endif /* NWAU_CORE_H */
