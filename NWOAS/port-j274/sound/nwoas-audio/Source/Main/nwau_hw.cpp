/*
 * NWOAS Mac mini (J274) internal speaker - hardware access layer.
 * Contract: research-20261116/sound/PLAN-S256.md.  See nwau_hw.h.
 *
 * Register layouts come from the MIT-licensed m1n1 proxyclient
 * (m1n1/hw/admac.py, mca.py, i2c.py).  The amplifier sequence is the one that
 * played sound on the hardware in S255 (nwoas_scripts/sound-s255/beep_tone.py)
 * and that the S256 launcher module (nwoas_scripts/sound-s256/audio_module.py)
 * leaves behind.  No GPL code is used.
 *
 * Locking (S259b, 2026-11-18): the spin lock guards the DMA / ring / model
 * state and is held only for short work (TX feed, RX reaping + model, about a
 * hundred us).  All amp I2C runs at PASSIVE_LEVEL under AmpMutex and never with
 * the spin lock held: StreamRun / Pause / PowerDown / Shutdown / Windows volume
 * do it in their own thread, and the DISPATCH paths (render timer, EX_TIMER)
 * only queue the amp worker (gain / volume policy, fault amp-off).  A fault
 * seen at DISPATCH keeps silence flowing (state FAULTING) until the worker has
 * muted the amp.
 *
 * Division of work: the launcher powers the blocks, sets the clocks, resets the
 * ADMAC channel, maps DART-sio stream 2 and the window, configures the amp and
 * leaves it in software shutdown (0x02 = 0x0e).  This file only
 *   - maps the 5 ACPI memory resources (non-cached) and validates the header,
 *   - talks to the amp (I2C1, address from the header, polled),
 *   - feeds ADMAC TX descriptors pointing into the window ring and reads the
 *     completion reports,
 *   - switches the MCA TXA serdes on and off,
 *   - keeps the DMA fed from a periodic timer and stops safely when starved,
 *   - records status and counters under the device's "Device Parameters" key
 *     (start / stop / fault) and, while playing, once per second under the
 *     volatile subkey "Device Parameters\Live" (no disk flush: guest NVMe I/O
 *     traps to the Python hypervisor, which stops every CPU meanwhile).
 *
 * Safety rules enforced here (PLAN-S256.md):
 *   - static mode (gain 0x00): amp 0x05 >= max(header cap, NWAU_VOL_HARD_FLOOR),
 *     full scale <= +5 dB
 *   - protected mode (header v2 sense path, header prot_gain 0x14, thermal
 *     model healthy): gain 0x14 (21 dB) and 0x05 >= max(model floor, header
 *     prot_vol_floor).  The raise is ordered volume first, so the raise itself
 *     never gets louder than static; a trip lowers the gain first.
 *   - amp 0x03 is only ever written 0x00 or 0x14, each verified by read-back
 *   - amp 0x02 = 0x0c / 0x00 only while the serdes is on and descriptors are queued
 *   - stop / starvation / power down / remove / unload / thermal fault:
 *     0x02 = 0x0d, then 0x0e, then gain 0x00
 * All amp rules are checked in one gate (AmpWriteGated -> nwau_amp_write_allowed
 * in nwau_core.c, unit tested); no other code path writes to the amp.
 *
 * V/I sense (header v2, status bit1; the launcher prepared MCA RXB, the ADMAC
 * RX channel and the amp TX slots): RX descriptors into the sense ring of the
 * window, each completed 1024-frame chunk goes through nwau_ts_block() (pure,
 * unit tested).  Missing, stale or implausible sense data trips the
 * protection back to static mode for the rest of the stream.
 */

#include "definitions.h"
#include "nwau_core.h"
#include "nwau_hw.h"

#define NWAU_POOLTAG                'uAwN'

/* ---- I2C (PA Semi style controller, m1n1 hw/i2c.py) ---------------------- */
#define I2C_MTXFIFO                 0x00u
#define I2C_MRXFIFO                 0x04u
#define I2C_SMSTA                   0x14u
#define I2C_CTL                     0x1cu
#define I2C_MTX_READ                (1u << 10)
#define I2C_MTX_STOP                (1u << 9)
#define I2C_MTX_START               (1u << 8)
#define I2C_MRX_EMPTY               (1u << 8)
#define I2C_SMSTA_XIP               (1u << 28)
#define I2C_SMSTA_XEN               (1u << 27)
#define I2C_SMSTA_MTO               (1u << 23)
#define I2C_SMSTA_MTA               (1u << 22)
#define I2C_SMSTA_MTN               (1u << 21)
#define I2C_CTL_ENABLE              (1u << 11)
#define I2C_CTL_MRR                 (1u << 10)
#define I2C_CTL_MTR                 (1u << 9)
#define I2C_CTL_CLK_MASK            0xffu
#define I2C_CTL_CLK_VALUE           0x4u          /* as i2c.py write_reg/read_reg */
#define I2C_TIMEOUT_100NS           (20 * 10000)  /* 20 ms: transfer end / RX byte */
#define I2C_IDLE_TIMEOUT_100NS      (2 * 10000)   /* 2 ms: bus idle after a read */
#define I2C_MAX_POLLS               40000u

/* ---- ADMAC (m1n1 hw/admac.py) -------------------------------------------- */
#define ADMAC_TX_EN                 0x0000u
#define ADMAC_TX_EN_CLR             0x0004u
#define ADMAC_CHAN(ch)              (0x8000u + (ch) * 0x200u)
#define ADMAC_CHAN_CTL(ch)          (ADMAC_CHAN(ch) + 0x00u)
#define ADMAC_CHAN_STATUS0(ch)      (ADMAC_CHAN(ch) + 0x10u)   /* internal line 0 */
#define ADMAC_CHAN_INTMASK0(ch)     (ADMAC_CHAN(ch) + 0x20u)
#define ADMAC_CHAN_BUSWIDTH(ch)     (ADMAC_CHAN(ch) + 0x40u)
#define ADMAC_CHAN_BURSTSIZE(ch)    (ADMAC_CHAN(ch) + 0x54u)
#define ADMAC_CHAN_DESC_RING(ch)    (ADMAC_CHAN(ch) + 0x70u)
#define ADMAC_CHAN_REPORT_RING(ch)  (ADMAC_CHAN(ch) + 0x74u)
#define ADMAC_TX_DESC_WRITE(ch)     (0x10000u + ((ch) / 2u) * 4u)
#define ADMAC_TX_REPORT_READ(ch)    (0x10100u + ((ch) / 2u) * 4u)
#define ADMAC_CTL_RESET_RINGS       (1u << 0)
#define ADMAC_CTL_CLEAR_OF_UF       (1u << 1)
#define ADMAC_RING_EMPTY            (1u << 8)
#define ADMAC_RING_FULL             (1u << 9)
#define ADMAC_RING_ERR              (1u << 10)
#define ADMAC_ST_DESC_DONE          (1u << 0)
#define ADMAC_ST_DESC_RING_EMPTY    (1u << 4)
#define ADMAC_ST_REPORT_RING_FULL   (1u << 5)
#define ADMAC_ST_RING_ERR           (1u << 6)
#define ADMAC_BUSWIDTH_WORD_MASK    0x7u
#define ADMAC_BUSWIDTH_FRAME_MASK   (0x7u << 4)
#define ADMAC_BUSWIDTH_W_32BIT      0x2u
#define ADMAC_FRAME_1_WORD          0x0u
#define ADMAC_BURSTSIZE_DEFAULT     0x00c00060u   /* admac.py reset() */
#define ADMAC_DESC_NOTIFY           (1u << 16)
#define ADMAC_REPORT_FAULT          (1u << 26)    /* admac.py: "memory access fault?" [hypothesis] */
#define ADMAC_MAX_REPORTS_PER_PASS  16u
/* RX side (m1n1 hw/admac.py ADMACRegs: RX_EN 0x8, RX_EN_CLR 0xc, RX_DESC_WRITE 0x14000, RX_REPORT_READ 0x14100) */
#define ADMAC_RX_EN                 0x0008u
#define ADMAC_RX_EN_CLR             0x000cu
#define ADMAC_RX_DESC_WRITE(ch)     (0x14000u + ((ch) / 2u) * 4u)
#define ADMAC_RX_REPORT_READ(ch)    (0x14100u + ((ch) / 2u) * 4u)
#define ADMAC_CHAN_SRAM(ch)         (ADMAC_CHAN(ch) + 0x50u)
#define ADMAC_BUSWIDTH_W_16BIT      0x1u
#define ADMAC_FRAME_2_WORDS         0x1u          /* I and V: two 16-bit words per frame */
#define ADMAC_BURSTSIZE_RX16        0x00600030u   /* limit 0x30 * 2 bytes, threshold 0x18 * 2 bytes */

/* ---- MCA cluster (m1n1 hw/mca.py) ---------------------------------------- */
#define MCA_CLUSTER_STRIDE          0x4000u
#define MCA_MCLK_STATUS             0x000u
#define MCA_SYNCGEN_STATUS          0x100u
#define MCA_TXA_STATUS              0x300u
#define MCA_RXB_STATUS              0x400u        /* m1n1 hw/mca.py MCACluster.rxb (Asahi uses RXB for capture) */
#define MCA_RXB_CONF                0x408u
#define MCA_STATUS_EN               (1u << 0)
#define MCA_STATUS_RST              (1u << 1)
#define MCA_SWITCH1_STRIDE          0x8000u

/* ---- data path ----------------------------------------------------------- */
#define NWAU_PREFILL_CHUNKS         3u            /* silence queued before the amp turns on (128 ms): the queue level
                                                     the stream keeps (the push is paced by the hardware clock) */
#define NWAU_DRY_LIMIT              3u            /* TX ran dry this often in one stream -> stop (watchdog) */
#define NWAU_LEVEL_SETTLE_100NS     (500 * 10000) /* queue-level minimum is recorded after 0.5 s of play */
#define NWAU_TIMER_PERIOD_100NS     (10 * 10000)  /* 10 ms feed / watchdog timer */
/* NwAudioStartStatus value added by this file (others: nwau_core.h) */
#define NWAU_EAMP_NOT_OFF           0x320u        /* amp 0x02 not 0x0e after mute/shutdown at start */
/* NwAudioLastRunStatus values */
#define NWAU_ERUN_CLOCKS            0x401u        /* MCA MCLK/SYNCGEN not running */
#define NWAU_ERUN_AMP_CONFIG        0x402u        /* amp config / volume / gain / state read-back */
#define NWAU_ERUN_DMA               0x403u        /* nothing could be queued */
#define NWAU_ERUN_AMP_ON            0x404u        /* 0x02=0x0c refused by the gate or I2C failed */
#define NWAU_ERUN_AMP_STATE         0x405u        /* 0x02 did not read back 0x0c */
#define NWAU_STARVE_100NS           (300 * 10000) /* no Windows data for 300 ms -> amp off */
/* sense / protection */
#define NWAU_SENSE_INFLIGHT         4u            /* RX descriptor ring depth */
#define NWAU_MAX_TX_CHUNKS          64u
#define NWAU_RX_REARMS_PER_STREAM   5u            /* RX restarts after a stall before staying static */
#define NWAU_RX_RESTART_100NS       (100 * 10000) /* no sense block for 100 ms: restart RX (bridge keeps the ceiling) */
#define NWAU_RX_CARVEOUT_BYTES      0x800u        /* ADMAC RX channel SRAM, Linux apple-admac block size [hypothesis:
                                                     the reset carveout 0 makes RX stop now and then] */
#define ADMAC_RX_SRAM_SIZE          0x0098u
#define ADMAC_CHAN_RESIDUE(ch)      (ADMAC_CHAN(ch) + 0x64u)
#define MCA_INTSTATE                0x700u
#define NWAU_VOL_SLEW_100NS         (10 * 10000)  /* louder by at most 0.5 dB per 10 ms */
#define NWAU_REG_PERIOD_100NS       (1000 * 10000)/* live registry snapshot (volatile key) while playing: 1 s */
#define NWAU_THERM_MAGIC            0x5354574eu   /* 'NWTS' */
/* ADMAC 24 MHz always-running counter (m1n1 hw/admac.py COUNTER, 64 bit, top bit always set):
   real time, also while the hypervisor has stopped the guest (guest time does not see that) */
#define ADMAC_COUNTER_LO            0x0070u
#define ADMAC_COUNTER_HI            0x0074u
#define NWAU_SRC_RENDER             0u            /* NwAuService from the WaveRT render timer (1 ms) */
#define NWAU_SRC_TIMER              1u            /* this file's EX_TIMER (10 ms) */

/* ---- event timeline (Device Parameters[\Live]\NwAudioEvents) ------------- */
#define NWAU_EVT_COUNT              128u
enum {
    NWAU_EV_START = 1,      /* a = 0, b = 0: StreamRun began (time base of the stream) */
    NWAU_EV_STOP,           /* b = stop reason */
    NWAU_EV_PAD,            /* b = silence bytes spliced into a partly written chunk */
    NWAU_EV_DROP,           /* b = Windows bytes dropped (oldest first) */
    NWAU_EV_DRY,            /* TX ran dry; b = dries this stream */
    NWAU_EV_DRYREC,         /* refilled with silence, stream goes on */
    NWAU_EV_GAIN,           /* a = from, b = to (amp 0x03) */
    NWAU_EV_VOL,            /* a = from, b = to | writes << 16 (amp 0x05, coalesced within 100 ms) */
    NWAU_EV_TRIP,           /* b = nwau_ts trip reason */
    NWAU_EV_VERIFIED,       /* protection healthy */
    NWAU_EV_RXREC,          /* RX path restarted; b = restarts this stream */
    NWAU_EV_STALL,          /* services more than 40 ms apart: a = guest ms, b = real ms */
    NWAU_EV_FADE,           /* a = 1 fade-out start / 2 fade-in done, b = volume */
    NWAU_EV_BRIDGE,         /* sense gap bridged by the blind model (ceiling kept); b = gap ms */
    NWAU_EV_RXQUIET         /* RX restarted inside a bridge; a = desc ring >> 16, b = desc ring / report ring */
};
typedef struct _NWAU_EVT {
    ULONG  RealMs;          /* ADMAC clock, ms since the stream's StreamRun */
    ULONG  GuestMs;         /* guest clock, ms since the same moment */
    USHORT Type;
    USHORT A;
    ULONG  B;
} NWAU_EVT;
typedef struct _NWAU_EVTLOG {
    ULONG    Count;         /* events ever recorded; entry i is at [i % NWAU_EVT_COUNT] */
    ULONG    Reserved;
    NWAU_EVT E[NWAU_EVT_COUNT];
} NWAU_EVTLOG;

/* ---- state --------------------------------------------------------------- */
enum {
    NWAU_ST_DOWN = 0,       /* not started / shut down */
    NWAU_ST_IDLE,           /* hardware validated, amp in shutdown */
    NWAU_ST_PLAYING,        /* DMA running, amp active */
    NWAU_ST_FAULT,          /* stopped by a fault, waits for the next StreamRun */
    NWAU_ST_OFF,            /* device in a sleep state (D1..D3) */
    NWAU_ST_STARTING,       /* StreamRun: DMA queued, amp being switched on (PASSIVE, I2C) */
    NWAU_ST_STOPPING,       /* stop: amp being muted (PASSIVE, I2C); the feed queues silence */
    NWAU_ST_FAULTING        /* fault seen at DISPATCH: amp-off queued to the amp worker; silence fed */
};

typedef struct _NWAU_STATS {
    ULONG Starts;
    ULONG Stops;
    ULONG LastStopReason;
    ULONG DescSubmitted;
    ULONG DescCompleted;
    ULONG StrayReports;
    ULONG ReportFaults;
    ULONG RingErrors;
    ULONG PadChunks;
    ULONG PadBytes;
    ULONG PushBytes;
    ULONG DropBytes;
    ULONG DmaRanDry;
    ULONG OsStarved;
    LONG  I2cErrors;
    ULONG AmpVerifyFails;
    ULONG AmpState;         /* last read of amp 0x02 (0xffffffff = read failed) */
    ULONG VolReg;           /* last value written to amp 0x05 */
    ULONG MaxServiceGapUs;  /* guest time between two services (any source) */
    ULONG OfUf;             /* ADMAC desc ring OF_UF counter (bits 31:16) at the last service */
    ULONG Switch1;          /* mca-switch1[cluster] read at start */
    ULONG StartFails;
    ULONG LastRunStatus;
    /* sense / protection */
    ULONG RxDescSubmitted;
    ULONG RxDescCompleted;
    ULONG RxStrayReports;
    ULONG RxReportFaults;
    ULONG RxRingErrors;
    ULONG RxOfUf;
    ULONG RxSetupFails;
    ULONG RxRecoveries;     /* RX path restarted after a stall */
    ULONG SenseCfgFails;
    ULONG ProtRaises;       /* gain 0x00 -> 0x14 */
    ULONG ProtDrops;        /* gain 0x14 -> 0x00 (trip or stream end) */
    ULONG GainReg;          /* gain shadow (verified 0x03, 0xff unknown) */
    ULONG ProtFloor;        /* last model floor (0x05 steps at gain 0x14) */
    ULONG TxCarveout;       /* ADMAC SRAM carveout of the TX / RX channel at start */
    ULONG RxCarveout;
    ULONG RxConf;           /* MCA RXB CONF at start (launcher value) */
    ULONG AmpTdm4;          /* amp 0x0e..0x10 read back after the sense config (0x0e | 0x0f << 8 | 0x10 << 16) */
    ULONG ThermRestored;    /* 1 = the saved model state of a previous driver instance was used */
    /* timing (us; "real" = ADMAC 24 MHz counter, includes time the hypervisor stopped the guest) */
    ULONG MaxRealGapUs;     /* real time between two services */
    ULONG MaxStolenUs;      /* real gap - guest gap: guest stopped by the hypervisor */
    ULONG RealGaps40ms;     /* services more than 40 / 80 ms (real) apart */
    ULONG RealGaps80ms;
    ULONG MaxGapRenderUs;   /* guest gap per service source */
    ULONG MaxGapTimerUs;
    ULONG MaxServiceUs;     /* real time inside one service (lock held) */
    ULONG MaxSenseUs;       /* of which RX reaping + model */
    ULONG I2cOps;
    ULONG I2cMaxUs;         /* one I2C transaction (never under the spin lock) */
    ULONG I2cTotalMs;
    ULONG AmpWorkRuns;
    ULONG MaxAmpWorkUs;
    ULONG VolSets;          /* volume / mute property writes from Windows */
    LONG  LastVolLevel;     /* last level (16.16 dB) and channel */
    ULONG LastVolChannel;
    ULONG GateRefusals;
    ULONG BlindMs;          /* time modelled from the sent signal only (sense tripped) */
    ULONG StreamRealMs;     /* last stream: real / guest duration */
    ULONG StreamGuestMs;
    ULONG MinQueueUs;       /* lowest queued audio (in flight + written ahead) after 0.5 s, this stream */
    ULONG MinInflight;      /* lowest TX descriptors in flight after 0.5 s, this stream */
    ULONG DryRecoveries;    /* TX ran dry and was refilled with silence (stream kept going) */
    ULONG RxSramSize;       /* ADMAC RX SRAM size (0x98) and the RX channel carveout written */
    ULONG RxCarveoutSet;
    ULONG RxQuietRestarts;  /* RX restarted inside a bridged gap (protection kept) */
    ULONG RxStallDesc;      /* RX registers when the last stall was seen */
    ULONG RxStallReport;
    ULONG RxStallStatus;
    ULONG RxStallResidue;
    ULONG RxStallSerdes;
    ULONG RxStallMcaInt;
    ULONG RxStallInflight;
} NWAU_STATS;

typedef struct _NWAU_DEV {
    BOOLEAN             Started;
    LONG                State;
    KSPIN_LOCK          Lock;
    PDEVICE_OBJECT      Fdo;
    PDEVICE_OBJECT      Pdo;

    volatile UCHAR     *Va[NWAU_NUM_RESOURCES];
    SIZE_T              Len[NWAU_NUM_RESOURCES];
    PHYSICAL_ADDRESS    Pa[NWAU_NUM_RESOURCES];
    volatile UCHAR     *McaCl;           /* MCA cluster base (resource 2 + 0x4000 * cluster) */
    volatile nwu32     *Ring;            /* window + 0x1000 */

    ULONG               HdrWords[NWAU_HDR_WORDS];
    nwau_header         Hdr;
    ULONG               StartStatus;

    nwau_ring           RingState;
    nwau_conv           Conv;
    ULONG               DescId;
    BOOLEAN             DmaOn;
    BOOLEAN             SerdesOn;

    LONG                VolumeLevel;     /* 16.16 dB: louder of the two channel levels from Windows */
    LONG                VolCh[2];
    BOOLEAN             Muted;

    /* amp: all I2C at PASSIVE under AmpMutex, never under the spin lock */
    FAST_MUTEX          AmpMutex;
    PIO_WORKITEM        AmpWorkItem;
    LONG                AmpWorkQueued;
    LONG                AmpWorkAgain;
    KEVENT              AmpWorkIdle;
    BOOLEAN             AmpWorkStop;
    ULONG               FaultReason;
    /* amp shadows (verified by read-back; written only with AmpMutex held, under the lock) */
    ULONG               GainShadow;      /* 0x00, 0x14 or NWAU_AMP_GAIN_UNKNOWN */
    ULONG               VolShadow;       /* 0 = unknown (treated as loudest) */
    ULONGLONG           LastVolStep;

    /* V/I sense + thermal model */
    BOOLEAN             Sense;           /* header v2 with the sense path prepared */
    BOOLEAN             SenseCfgOk;      /* amp TX slots written and verified this run */
    BOOLEAN             RxDmaOn;
    BOOLEAN             RxSerdesOn;
    volatile nwu32     *SenseRing;
    ULONG               SenseChunks;
    ULONG               RxSub;           /* chunks handed to ADMAC RX (since the last RX start) */
    ULONG               RxDone;          /* chunks completed and processed */
    ULONG               RxDescId;
    ULONG               RxRearms;        /* RX restarts in this stream */
    ULONGLONG           LastRxRestart;
    ULONGLONG           LastSenseBlock;
    ULONGLONG           TsStopTime;      /* model idle since (KeQueryInterruptTime) */
    nwau_ts             Ts;
    BOOLEAN             TsValid;         /* Ts initialised (only then is it saved) */
    ULONGLONG           TxChunkE[NWAU_MAX_TX_CHUNKS];   /* sum of mono s16^2 per TX ring chunk */
    ULONGLONG           TxEAcc;          /* energy / frames of TX chunks completed since the last sense block */
    ULONG               TxFAcc;
    ULONGLONG           LastRegWrite;

    ULONGLONG           LastPush;        /* KeQueryInterruptTime */
    ULONGLONG           LastService;
    ULONGLONG           LastServiceBySrc[2];
    ULONGLONG           LastReal;        /* real us at the last service */
    ULONGLONG           StreamStartGuest;
    ULONGLONG           StreamStartReal;
    ULONG               StreamDries;

    PEX_TIMER           Timer;
    PIO_WORKITEM        WorkItem;
    LONG                WorkQueued;
    LONG                RegWantStable;
    KEVENT              WorkIdle;

    NWAU_EVTLOG         Evt;             /* lock held for every access */
    ULONGLONG           EvtBaseReal;     /* us */
    ULONGLONG           EvtBaseGuest;    /* 100 ns */

    NWAU_STATS          Stats;
} NWAU_DEV;

static NWAU_DEV g_Nw;

/* ---- MMIO helpers -------------------------------------------------------- */
static __forceinline ULONG Rd(_In_ volatile UCHAR *Base, _In_ ULONG Off)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Base + Off));
}

static __forceinline VOID Wr(_In_ volatile UCHAR *Base, _In_ ULONG Off, _In_ ULONG Val)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Base + Off), Val);
}

static __forceinline VOID FullBarrier(VOID)
{
#if defined(_M_ARM64)
    /* ring writes (window mapping) must reach memory before the descriptor
       write (ADMAC mapping) makes the device read them */
    __dsb(_ARM64_BARRIER_SY);
#else
    KeMemoryBarrier();
#endif
}

#define I2C_BASE    (g_Nw.Va[NWAU_RES_I2C])
#define ADMAC_BASE  (g_Nw.Va[NWAU_RES_ADMAC])

/* ---- I2C, polled (sequence of m1n1 hw/i2c.py write_reg / read_reg) -------- */
static BOOLEAN I2cPoll(_In_ ULONG Off, _In_ ULONG Mask, _In_ BOOLEAN WantSet, _In_ ULONGLONG Timeout100ns,
                       _Out_opt_ PULONG Last)
{
    ULONGLONG t0 = KeQueryInterruptTime();
    ULONG v = 0;
    ULONG n;

    for (n = 0; n < I2C_MAX_POLLS; n++) {
        v = Rd(I2C_BASE, Off);
        if (((v & Mask) != 0) == (WantSet != FALSE)) {
            if (Last) {
                *Last = v;
            }
            return TRUE;
        }
        if (KeQueryInterruptTime() - t0 > Timeout100ns) {
            break;
        }
        KeStallExecutionProcessor(1);
    }
    if (Last) {
        *Last = v;
    }
    return FALSE;
}

static VOID I2cBegin(VOID)
{
    ULONG ctl;

    /* clear_fifos (read-modify-write, as i2c.py), clear_status */
    ctl = Rd(I2C_BASE, I2C_CTL);
    Wr(I2C_BASE, I2C_CTL, ctl | I2C_CTL_MTR | I2C_CTL_MRR);
    Wr(I2C_BASE, I2C_SMSTA, 0xffffffffu);
    /* CTL.set(ENABLE=1, CLK=4) */
    ctl = Rd(I2C_BASE, I2C_CTL);
    ctl = (ctl & ~I2C_CTL_CLK_MASK) | I2C_CTL_ENABLE | I2C_CTL_CLK_VALUE;
    Wr(I2C_BASE, I2C_CTL, ctl);
}

static VOID I2cEnd(VOID)
{
    /* CTL.set(ENABLE=0, CLK=4) */
    ULONG ctl = Rd(I2C_BASE, I2C_CTL);
    ctl = (ctl & ~(I2C_CTL_CLK_MASK | I2C_CTL_ENABLE)) | I2C_CTL_CLK_VALUE;
    Wr(I2C_BASE, I2C_CTL, ctl);
}

/* raw register write, only called through AmpWriteGated */
static BOOLEAN I2cWriteRaw(_In_ UCHAR Addr, _In_ UCHAR Reg, _In_reads_(Count) const UCHAR *Data, _In_ ULONG Count)
{
    ULONG st = 0;
    ULONG i;
    BOOLEAN ok;

    I2cBegin();
    Wr(I2C_BASE, I2C_MTXFIFO, I2C_MTX_START | ((ULONG)Addr << 1));
    Wr(I2C_BASE, I2C_MTXFIFO, (ULONG)Reg | (Count == 0 ? I2C_MTX_STOP : 0u));
    for (i = 0; i < Count; i++) {
        Wr(I2C_BASE, I2C_MTXFIFO, (ULONG)Data[i] | (i + 1 == Count ? I2C_MTX_STOP : 0u));
    }
    ok = I2cPoll(I2C_SMSTA, I2C_SMSTA_XEN, TRUE, I2C_TIMEOUT_100NS, &st);
    /* [hypothesis] MTN/MTO/MTA = NACK / timeout / arbitration lost (i2c.py names).
       The caller verifies the important registers by reading them back anyway. */
    if (st & (I2C_SMSTA_MTN | I2C_SMSTA_MTO | I2C_SMSTA_MTA)) {
        ok = FALSE;
    }
    I2cEnd();
    if (!ok) {
        InterlockedIncrement(&g_Nw.Stats.I2cErrors);
    }
    return ok;
}

static BOOLEAN I2cRead(_In_ UCHAR Addr, _In_ UCHAR Reg, _Out_writes_(Count) UCHAR *Data, _In_ ULONG Count)
{
    ULONG i;
    ULONG v = 0;
    BOOLEAN ok = TRUE;

    for (i = 0; i < Count; i++) {
        Data[i] = 0;
    }
    I2cBegin();
    Wr(I2C_BASE, I2C_MTXFIFO, I2C_MTX_START | ((ULONG)Addr << 1));
    Wr(I2C_BASE, I2C_MTXFIFO, (ULONG)Reg);
    Wr(I2C_BASE, I2C_MTXFIFO, I2C_MTX_START | ((ULONG)Addr << 1) | 1u);
    Wr(I2C_BASE, I2C_MTXFIFO, I2C_MTX_READ | I2C_MTX_STOP | (Count & 0xffu));
    for (i = 0; i < Count; i++) {
        if (!I2cPoll(I2C_MRXFIFO, I2C_MRX_EMPTY, FALSE, I2C_TIMEOUT_100NS, &v)) {
            ok = FALSE;
            break;
        }
        Data[i] = (UCHAR)(v & 0xffu);
    }
    (VOID)I2cPoll(I2C_SMSTA, I2C_SMSTA_XIP, FALSE, I2C_IDLE_TIMEOUT_100NS, NULL);   /* bus idle (m1n1 src/i2c.c) */
    I2cEnd();
    if (!ok) {
        InterlockedIncrement(&g_Nw.Stats.I2cErrors);
    }
    return ok;
}

/* ---- real time (ADMAC 24 MHz counter) ------------------------------------- */
static ULONGLONG RealNowUs(VOID)
{
    ULONG hi, lo, hi2, n;

    if (ADMAC_BASE == NULL) {
        return 0;
    }
    for (n = 0; n < 4; n++) {
        hi = Rd(ADMAC_BASE, ADMAC_COUNTER_HI);
        lo = Rd(ADMAC_BASE, ADMAC_COUNTER_LO);
        hi2 = Rd(ADMAC_BASE, ADMAC_COUNTER_HI);
        if (hi == hi2) {
            break;
        }
    }
    return ((((ULONGLONG)(hi2 & 0x7fffffffu)) << 32) | lo) / 24u;
}

static VOID MaxU(_Inout_ ULONG *Max, _In_ ULONGLONG V)
{
    ULONG v = V > 0xffffffffu ? 0xffffffffu : (ULONG)V;
    if (v > *Max) {
        *Max = v;
    }
}

/* lock held */
static VOID EvtLocked(_In_ ULONG Type, _In_ ULONG A, _In_ ULONG B)
{
    ULONG rms = (ULONG)((RealNowUs() - g_Nw.EvtBaseReal) / 1000u);
    ULONG gms = (ULONG)((KeQueryInterruptTime() - g_Nw.EvtBaseGuest) / 10000u);
    NWAU_EVT *e;

    if (Type == NWAU_EV_VOL && g_Nw.Evt.Count > 0) {
        NWAU_EVT *l = &g_Nw.Evt.E[(g_Nw.Evt.Count - 1) % NWAU_EVT_COUNT];
        if (l->Type == NWAU_EV_VOL && rms - l->RealMs < 100u) {
            ULONG n = (l->B >> 16) + 1u;
            l->B = (B & 0xffffu) | ((n > 0xffffu ? 0xffffu : n) << 16);
            return;
        }
        B = (B & 0xffffu) | (1u << 16);
    }
    e = &g_Nw.Evt.E[g_Nw.Evt.Count % NWAU_EVT_COUNT];
    e->RealMs = rms;
    e->GuestMs = gms;
    e->Type = (USHORT)Type;
    e->A = (USHORT)(A > 0xffffu ? 0xffffu : A);
    e->B = B;
    g_Nw.Evt.Count++;
}

/* ---- amplifier (TAS5770L, TAS2770 register layout) ------------------------
 * Every function from AmpWriteGated down runs at PASSIVE_LEVEL with AmpMutex
 * held and the spin lock NOT held.  The spin lock is only taken for short
 * snapshots / commits of the shared state, so the TX feed (render timer DPC)
 * never waits for an I2C transaction.  Functions marked "lock held" are the
 * small helpers used inside those snapshots. */
static ULONG VolumeFloor(VOID)
{
    ULONG f = g_Nw.Hdr.vol_cap;
    if (f < NWAU_VOL_HARD_FLOOR) {
        f = NWAU_VOL_HARD_FLOOR;
    }
    return f;
}

/* protection healthy and allowed by the header (lock held) */
static BOOLEAN ProtOk(VOID)
{
    return g_Nw.Sense && g_Nw.State == NWAU_ST_PLAYING && g_Nw.Ts.state == NWAU_TS_HEALTHY &&
           g_Nw.Hdr.version == NWAU_HDR_VERSION_SENSE && g_Nw.Hdr.prot_gain == NWAU_PROT_GAIN;
}

/* lock held */
static ULONG ProtFloor(VOID)
{
    ULONG f = nwau_ts_floor(&g_Nw.Ts);
    ULONG h = g_Nw.Hdr.version == NWAU_HDR_VERSION_SENSE ? g_Nw.Hdr.prot_vol_floor : NWAU_VOL_REG_MAX;
    return f > h ? f : h;
}

/* lock held */
static VOID GateFill(_Out_ nwau_gate *g)
{
    g->serdes_on = (g_Nw.SerdesOn && g_Nw.McaCl != NULL &&
                    (Rd(g_Nw.McaCl, MCA_TXA_STATUS) & MCA_STATUS_EN) != 0) ? 1 : 0;
    g->dma_on = g_Nw.DmaOn ? 1 : 0;
    g->inflight = nwau_ring_inflight(&g_Nw.RingState);
    g->static_floor = VolumeFloor();
    g->gain_shadow = g_Nw.GainShadow;
    g->vol_shadow = g_Nw.VolShadow;
    g->prot_ok = ProtOk() ? 1 : 0;
    g->prot_gain = g_Nw.Hdr.version == NWAU_HDR_VERSION_SENSE ? g_Nw.Hdr.prot_gain : 0;
    g->prot_floor = ProtFloor();
}

/* the volume the current mode wants, before slew (lock held) */
static ULONG CurrentVolumeRegLocked(VOID)
{
    nwau_gate g;
    ULONG reg, floor;

    GateFill(&g);
    floor = nwau_gate_vol_floor(&g);
    if (g_Nw.GainShadow == NWAU_AMP_GAIN_STATIC) {
        if (g_Nw.Sense && g_Nw.Hdr.version == NWAU_HDR_VERSION_SENSE && g_Nw.Hdr.prot_gain == NWAU_PROT_GAIN) {
            /* protection configured: static mode at the protected loudness, capped,
               so falling back or arming does not change the level below the cap */
            reg = nwau_volume_reg_matched(g_Nw.VolumeLevel, g_Nw.Muted ? 1 : 0, g_Nw.Hdr.vol_cap);
        } else {
            reg = nwau_volume_reg(g_Nw.VolumeLevel, g_Nw.Muted ? 1 : 0, g_Nw.Hdr.vol_cap);
        }
    } else {
        /* gain 0x14: Windows 0 dB = amp 0 dB, limited by the model / gate floor */
        reg = nwau_volume_reg_base(g_Nw.VolumeLevel, g_Nw.Muted ? 1 : 0, 0);
    }
    if (reg < floor) {
        reg = floor;                                    /* defence in depth */
    }
    return reg;
}

static VOID I2cAccount(_In_ ULONGLONG T0)
{
    ULONGLONG dt = RealNowUs() - T0;
    KIRQL irql;

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    g_Nw.Stats.I2cOps++;
    MaxU(&g_Nw.Stats.I2cMaxUs, dt);
    g_Nw.Stats.I2cTotalMs += (ULONG)(dt / 1000u);
    KeReleaseSpinLock(&g_Nw.Lock, irql);
}

/* The only path that writes to the amplifier.  PASSIVE, AmpMutex held. */
static BOOLEAN AmpWriteGated(_In_ UCHAR Reg, _In_reads_(Count) const UCHAR *Data, _In_ ULONG Count)
{
    nwau_gate g;
    KIRQL irql;
    ULONGLONG t0;
    BOOLEAN ok;

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    GateFill(&g);
    if (!nwau_amp_write_allowed(&g, Reg, Data, Count)) {
        g_Nw.Stats.GateRefusals++;
        KeReleaseSpinLock(&g_Nw.Lock, irql);
        return FALSE;
    }
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    t0 = RealNowUs();
    ok = I2cWriteRaw((UCHAR)g_Nw.Hdr.amp_addr, Reg, Data, Count);
    I2cAccount(t0);
    return ok;
}

static BOOLEAN AmpWrite1(_In_ UCHAR Reg, _In_ UCHAR Val)
{
    return AmpWriteGated(Reg, &Val, 1);
}

static ULONG AmpRead1(_In_ UCHAR Reg)
{
    UCHAR v = 0;
    ULONGLONG t0 = RealNowUs();
    BOOLEAN ok = I2cRead((UCHAR)g_Nw.Hdr.amp_addr, Reg, &v, 1);

    I2cAccount(t0);
    return ok ? v : 0xffffffffu;
}

/* mute (0x0d) -> software shutdown (0x0e).
   WasActive = FALSE is only used when the amp may already be in shutdown
   (start, power down from idle): if 0x02 then reads back exactly 0x0e the mute
   step is skipped, so an amp that is verified off is not briefly brought out of
   shutdown into mute.  Any other read-back (including a failed read) gets the
   full 0x0d -> 0x0e sequence. */
static VOID AmpOff(_In_ BOOLEAN WasActive)
{
    if (WasActive || AmpRead1(NWAU_AMP_REG_PWR_CTRL) != NWAU_AMP_PWR_SHUTDOWN) {
        (VOID)AmpWrite1(NWAU_AMP_REG_PWR_CTRL, NWAU_AMP_PWR_MUTE);
        (VOID)AmpWrite1(NWAU_AMP_REG_PWR_CTRL, NWAU_AMP_PWR_SHUTDOWN);
    }
    g_Nw.Stats.AmpState = AmpRead1(NWAU_AMP_REG_PWR_CTRL);
}

static VOID CommitGain(_In_ ULONG V)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    if (g_Nw.GainShadow != V) {
        EvtLocked(NWAU_EV_GAIN, g_Nw.GainShadow, V);
    }
    g_Nw.GainShadow = V;
    g_Nw.Stats.GainReg = V;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
}

static VOID CommitVol(_In_ ULONG V, _In_ BOOLEAN Known)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    if (g_Nw.VolShadow != (Known ? V : 0)) {
        EvtLocked(NWAU_EV_VOL, g_Nw.VolShadow, Known ? V : 0xffffu);
    }
    g_Nw.VolShadow = Known ? V : 0;                     /* unknown: loudest */
    g_Nw.Stats.VolReg = Known ? V : 0xffffffffu;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
}

/* write 0x03 and verify; the shadow becomes "unknown" (worst case) on any failure */
static BOOLEAN AmpGainSet(_In_ ULONG Val)
{
    BOOLEAN ok = AmpWrite1(NWAU_AMP_REG_GAIN, (UCHAR)Val);
    ULONG back = AmpRead1(NWAU_AMP_REG_GAIN);

    if (ok && back == Val) {
        CommitGain(Val);
        return TRUE;
    }
    CommitGain((back == NWAU_AMP_GAIN_STATIC && Val == NWAU_AMP_GAIN_STATIC) ?
               NWAU_AMP_GAIN_STATIC : NWAU_AMP_GAIN_UNKNOWN);
    return FALSE;
}

/* write 0x05 = Reg and read it back */
static BOOLEAN AmpSetVolumeReg(_In_ ULONG Reg)
{
    if (!AmpWrite1(NWAU_AMP_REG_VOLUME, (UCHAR)Reg)) {
        return FALSE;
    }
    if (AmpRead1(NWAU_AMP_REG_VOLUME) != Reg) {
        CommitVol(0, FALSE);
        return FALSE;
    }
    CommitVol(Reg, TRUE);
    return TRUE;
}

static BOOLEAN AmpSetVolume(VOID)
{
    KIRQL irql;
    ULONG reg;

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    reg = CurrentVolumeRegLocked();
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    return AmpSetVolumeReg(reg);
}

/* Move 0x05 to Target in steps of at most Step (0.5 dB units), StallUs apart,
   the last write read back.  Every step passes the gate.  Big level jumps in
   one write click; a few ms of steps do not. */
static BOOLEAN AmpVolumeRamp(_In_ ULONG Target, _In_ ULONG Step, _In_ ULONG StallUs)
{
    ULONG cur = g_Nw.VolShadow;                         /* written only by AmpMutex holders */
    ULONG n = 0;

    if (cur == 0 || Step == 0) {
        return AmpSetVolumeReg(Target);                 /* unknown start: one verified write */
    }
    while (cur != Target && n < 64) {
        ULONG next = cur < Target ? (Target - cur > Step ? cur + Step : Target)
                                  : (cur - Target > Step ? cur - Step : Target);
        if (next == Target) {
            break;
        }
        if (!AmpWrite1(NWAU_AMP_REG_VOLUME, (UCHAR)next)) {
            return FALSE;
        }
        if (next > cur) {
            CommitVol(next, TRUE);                      /* quieter: the shadow may follow at once */
        }
        cur = next;
        n++;
        KeStallExecutionProcessor(StallUs);
    }
    return AmpSetVolumeReg(Target);
}

/* protection configured by the header (constant after start) */
static BOOLEAN ProtConfigured(VOID)
{
    return g_Nw.Sense && g_Nw.Hdr.version == NWAU_HDR_VERSION_SENSE && g_Nw.Hdr.prot_gain == NWAU_PROT_GAIN;
}

static ULONG CurrentVolumeReg(VOID)
{
    KIRQL irql;
    ULONG reg;

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    reg = CurrentVolumeRegLocked();
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    return reg;
}

/* same config bytes as audio_module.py B9 / beep_tone.py; amp stays in shutdown.
   With the sense path: also the TX slot config (I slot 0, V slot 2, zero fill). */
static BOOLEAN AmpConfigure(VOID)
{
    static const UCHAR c08[] = { 0x40 };
    static const UCHAR c0a[] = { 0x06, 0x00, 0x1a };
    static const UCHAR c1b[] = { 0x01, 0x82, 0x06 };
    static const UCHAR c16[] = { 0x50, 0x04 };
    static const UCHAR c0d[] = { 0x00 };
    static const UCHAR c0e[] = { NWAU_AMP_SNS_TDM4, NWAU_AMP_SNS_TDM5, NWAU_AMP_SNS_TDM6 };
    BOOLEAN ok = TRUE;

    AmpOff(FALSE);                                      /* 0x0d -> 0x0e unless already 0x0e */
    ok = AmpGainSet(NWAU_AMP_GAIN_STATIC) && ok;        /* gain first: never louder than static below */
    ok = AmpSetVolume() && ok;                          /* volume, as in S255 */
    ok = AmpWriteGated(0x08, c08, sizeof(c08)) && ok;
    ok = AmpWriteGated(0x0a, c0a, sizeof(c0a)) && ok;
    ok = AmpWriteGated(0x1b, c1b, sizeof(c1b)) && ok;
    ok = AmpWriteGated(0x16, c16, sizeof(c16)) && ok;
    ok = AmpWriteGated(0x0d, c0d, sizeof(c0d)) && ok;
    if (ProtConfigured()) {
        /* protection configured: the analog gain is 21 dB for the whole stream
           (no gain switching = no click); static mode is the volume ceiling
           0x0c + 0x14.  Order: volume to the 0x14 floor (verified), then gain. */
        ULONG v = VolumeFloor() + NWAU_PROT_GAIN;
        ULONG t = CurrentVolumeReg();
        if (t > v) {
            v = t;
        }
        ok = AmpSetVolumeReg(v) && ok;
        ok = AmpGainSet(NWAU_PROT_GAIN) && ok;
    } else {
        ok = AmpGainSet(NWAU_AMP_GAIN_STATIC) && ok;
    }
    ok = AmpSetVolume() && ok;
    g_Nw.SenseCfgOk = FALSE;
    if (g_Nw.Sense) {
        ULONG r0, r1, r2;
        BOOLEAN sok = AmpWriteGated(NWAU_AMP_REG_TDM4, c0e, sizeof(c0e));
        r0 = AmpRead1(NWAU_AMP_REG_TDM4);
        r1 = AmpRead1(NWAU_AMP_REG_TDM4 + 1);
        r2 = AmpRead1(NWAU_AMP_REG_TDM4 + 2);
        g_Nw.Stats.AmpTdm4 = (r0 & 0xffu) | ((r1 & 0xffu) << 8) | ((r2 & 0xffu) << 16);
        if (sok && r0 == NWAU_AMP_SNS_TDM4 && r1 == NWAU_AMP_SNS_TDM5 && r2 == NWAU_AMP_SNS_TDM6) {
            g_Nw.SenseCfgOk = TRUE;
        } else {
            g_Nw.Stats.SenseCfgFails++;                 /* playback goes on in static mode */
        }
    }
    g_Nw.Stats.AmpState = AmpRead1(NWAU_AMP_REG_PWR_CTRL);
    if (g_Nw.Stats.AmpState != NWAU_AMP_PWR_SHUTDOWN ||
        g_Nw.GainShadow != (ProtConfigured() ? NWAU_PROT_GAIN : NWAU_AMP_GAIN_STATIC)) {
        ok = FALSE;
    }
    if (!ok) {
        g_Nw.Stats.AmpVerifyFails++;
    }
    return ok;
}

/* ---- MCA / ADMAC ---------------------------------------------------------- */
static VOID SerdesSet(_In_ BOOLEAN On)
{
    ULONG v = Rd(g_Nw.McaCl, MCA_TXA_STATUS);
    v = On ? (v | MCA_STATUS_EN) : (v & ~MCA_STATUS_EN);
    Wr(g_Nw.McaCl, MCA_TXA_STATUS, v);
    g_Nw.SerdesOn = On;
}

static BOOLEAN ClocksRunning(VOID)
{
    return (Rd(g_Nw.McaCl, MCA_MCLK_STATUS) & MCA_STATUS_EN) != 0 &&
           (Rd(g_Nw.McaCl, MCA_SYNCGEN_STATUS) & MCA_STATUS_EN) != 0;
}

static VOID DmaDisable(VOID)
{
    Wr(ADMAC_BASE, ADMAC_TX_EN_CLR, 1u << (g_Nw.Hdr.admac_chan / 2u));
    g_Nw.DmaOn = FALSE;
}

static ULONG DmaReapReports(VOID)
{
    ULONG ch = g_Nw.Hdr.admac_chan;
    ULONG n = 0;
    ULONG w[4];
    ULONG k;

    while (n < ADMAC_MAX_REPORTS_PER_PASS &&
           (Rd(ADMAC_BASE, ADMAC_CHAN_REPORT_RING(ch)) & ADMAC_RING_EMPTY) == 0) {
        for (k = 0; k < 4; k++) {
            w[k] = Rd(ADMAC_BASE, ADMAC_TX_REPORT_READ(ch));
        }
        if (w[3] & ADMAC_REPORT_FAULT) {
            g_Nw.Stats.ReportFaults++;
        }
        n++;
    }
    return n;
}

/* admac.py ADMACChannel.disable() + reset() + buswidth/framesize */
static VOID DmaReset(VOID)
{
    ULONG ch = g_Nw.Hdr.admac_chan;
    ULONG v;

    DmaDisable();
    v = Rd(ADMAC_BASE, ADMAC_CHAN_CTL(ch));
    Wr(ADMAC_BASE, ADMAC_CHAN_CTL(ch), v | ADMAC_CTL_RESET_RINGS | ADMAC_CTL_CLEAR_OF_UF);
    v = Rd(ADMAC_BASE, ADMAC_CHAN_CTL(ch));
    Wr(ADMAC_BASE, ADMAC_CHAN_CTL(ch), v & ~(ADMAC_CTL_RESET_RINGS | ADMAC_CTL_CLEAR_OF_UF));
    Wr(ADMAC_BASE, ADMAC_CHAN_BURSTSIZE(ch), ADMAC_BURSTSIZE_DEFAULT);
    v = Rd(ADMAC_BASE, ADMAC_CHAN_BUSWIDTH(ch));
    v = (v & ~ADMAC_BUSWIDTH_WORD_MASK) | ADMAC_BUSWIDTH_W_32BIT;
    Wr(ADMAC_BASE, ADMAC_CHAN_BUSWIDTH(ch), v);
    v = Rd(ADMAC_BASE, ADMAC_CHAN_BUSWIDTH(ch));
    v = (v & ~ADMAC_BUSWIDTH_FRAME_MASK) | (ADMAC_FRAME_1_WORD << 4);
    Wr(ADMAC_BASE, ADMAC_CHAN_BUSWIDTH(ch), v);
    (VOID)DmaReapReports();                             /* read_reports(): drain */
    Wr(ADMAC_BASE, ADMAC_CHAN_STATUS0(ch), ADMAC_ST_DESC_DONE);
}

static VOID DmaEnable(VOID)
{
    ULONG ch = g_Nw.Hdr.admac_chan;
    /* admac.py enable(): line 0 mask, then TX_EN bit.  The ADMAC interrupt is
       not in the ACPI resources and Windows never enables its SPI, so the line
       stays masked at the interrupt controller (m1n1-gpu-hv2/src/hv_vgic.c
       unmasks an AIC line only when the guest enables it in GICD; read from
       the source, not tested with the ADMAC line). */
    Wr(ADMAC_BASE, ADMAC_CHAN_INTMASK0(ch),
       ADMAC_ST_DESC_DONE | ADMAC_ST_DESC_RING_EMPTY | ADMAC_ST_REPORT_RING_FULL | ADMAC_ST_RING_ERR);
    Wr(ADMAC_BASE, ADMAC_TX_EN, 1u << (ch / 2u));
    g_Nw.DmaOn = TRUE;
}

static BOOLEAN DmaCanSubmit(VOID)
{
    return (Rd(ADMAC_BASE, ADMAC_CHAN_DESC_RING(g_Nw.Hdr.admac_chan)) & ADMAC_RING_FULL) == 0;
}

static VOID DmaSubmit(_In_ ULONG RingOff)
{
    ULONG ch = g_Nw.Hdr.admac_chan;
    ULONGLONG iova = g_Nw.Hdr.ring_iova + RingOff;

    FullBarrier();
    Wr(ADMAC_BASE, ADMAC_TX_DESC_WRITE(ch), (ULONG)(iova & 0xffffffffu));
    Wr(ADMAC_BASE, ADMAC_TX_DESC_WRITE(ch), (ULONG)(iova >> 32));
    Wr(ADMAC_BASE, ADMAC_TX_DESC_WRITE(ch), g_Nw.RingState.chunk_bytes);
    Wr(ADMAC_BASE, ADMAC_TX_DESC_WRITE(ch), ADMAC_DESC_NOTIFY | (g_Nw.DescId & 0xffu));
    g_Nw.DescId = (g_Nw.DescId + 1) & 0xffu;
    g_Nw.Stats.DescSubmitted++;
}

static VOID RingZero(_In_ ULONG Off, _In_ ULONG Len)
{
    ULONG i;
    for (i = 0; i < Len / 4u; i++) {
        g_Nw.Ring[Off / 4u + i] = 0;                     /* single aligned 32-bit stores */
    }
}

/* queue as many descriptors as allowed; pad with silence when fewer than
   min_inflight are queued (ForceAll: pad every one) */
static VOID DmaFill(_In_ BOOLEAN ForceAll)
{
    nwu32 off = 0, padOff = 0, padLen = 0;
    int r;

    for (;;) {
        BOOLEAN force = ForceAll ||
                        nwau_ring_inflight(&g_Nw.RingState) < g_Nw.RingState.min_inflight;
        if (!DmaCanSubmit()) {
            break;
        }
        r = nwau_ring_next(&g_Nw.RingState, force ? 1 : 0, &off, &padOff, &padLen);
        if (r == NWAU_NEXT_NONE) {
            break;
        }
        if (r == NWAU_NEXT_PADDED) {
            RingZero(padOff, padLen);
            if ((padOff % NWAU_CHUNK_BYTES) == 0) {
                g_Nw.TxChunkE[(padOff / NWAU_CHUNK_BYTES) % NWAU_MAX_TX_CHUNKS] = 0;   /* all silence */
            }
            g_Nw.Stats.PadChunks++;
            if (g_Nw.State == NWAU_ST_PLAYING) {
                EvtLocked(NWAU_EV_PAD, 0, padLen);
            }
            g_Nw.Stats.PadBytes += padLen;
        }
        DmaSubmit(off);
    }
}

/* ---- V/I sense capture (MCA cluster RXB -> ADMAC RX channel -> sense ring) - */
static VOID RxDisable(VOID)
{
    if (g_Nw.Sense) {
        Wr(ADMAC_BASE, ADMAC_RX_EN_CLR, 1u << (g_Nw.Hdr.sense_chan / 2u));
    }
    g_Nw.RxDmaOn = FALSE;
}

static VOID RxSerdesOff(VOID)
{
    if (g_Nw.Sense && g_Nw.McaCl != NULL) {
        ULONG v = Rd(g_Nw.McaCl, MCA_RXB_STATUS);
        Wr(g_Nw.McaCl, MCA_RXB_STATUS, v & ~(MCA_STATUS_EN | MCA_STATUS_RST));
    }
    g_Nw.RxSerdesOn = FALSE;
}

static VOID RxStop(VOID)
{
    RxSerdesOff();
    RxDisable();
}

static ULONG RxReapReports(VOID)
{
    ULONG ch = g_Nw.Hdr.sense_chan;
    ULONG n = 0;
    ULONG w[4];
    ULONG k;

    while (n < ADMAC_MAX_REPORTS_PER_PASS &&
           (Rd(ADMAC_BASE, ADMAC_CHAN_REPORT_RING(ch)) & ADMAC_RING_EMPTY) == 0) {
        for (k = 0; k < 4; k++) {
            w[k] = Rd(ADMAC_BASE, ADMAC_RX_REPORT_READ(ch));
        }
        if (w[3] & ADMAC_REPORT_FAULT) {
            g_Nw.Stats.RxReportFaults++;
        }
        n++;
    }
    return n;
}

/* same reset as the TX channel, 16-bit words, 2-word (I, V) frames */
static VOID RxReset(VOID)
{
    ULONG ch = g_Nw.Hdr.sense_chan;
    ULONG v;

    RxDisable();
    v = Rd(ADMAC_BASE, ADMAC_CHAN_CTL(ch));
    Wr(ADMAC_BASE, ADMAC_CHAN_CTL(ch), v | ADMAC_CTL_RESET_RINGS | ADMAC_CTL_CLEAR_OF_UF);
    v = Rd(ADMAC_BASE, ADMAC_CHAN_CTL(ch));
    Wr(ADMAC_BASE, ADMAC_CHAN_CTL(ch), v & ~(ADMAC_CTL_RESET_RINGS | ADMAC_CTL_CLEAR_OF_UF));
    Wr(ADMAC_BASE, ADMAC_CHAN_BURSTSIZE(ch), ADMAC_BURSTSIZE_RX16);
    /* own SRAM for the RX channel (the RX pool is separate from the TX pool) */
    g_Nw.Stats.RxSramSize = Rd(ADMAC_BASE, ADMAC_RX_SRAM_SIZE);
    if (g_Nw.Stats.RxSramSize >= NWAU_RX_CARVEOUT_BYTES && g_Nw.Stats.RxSramSize < 0x10000u) {
        Wr(ADMAC_BASE, ADMAC_CHAN_SRAM(ch), (NWAU_RX_CARVEOUT_BYTES << 16) | 0u);
    }
    g_Nw.Stats.RxCarveoutSet = Rd(ADMAC_BASE, ADMAC_CHAN_SRAM(ch));
    v = Rd(ADMAC_BASE, ADMAC_CHAN_BUSWIDTH(ch));
    v = (v & ~(ADMAC_BUSWIDTH_WORD_MASK | ADMAC_BUSWIDTH_FRAME_MASK)) |
        ADMAC_BUSWIDTH_W_16BIT | (ADMAC_FRAME_2_WORDS << 4);
    Wr(ADMAC_BASE, ADMAC_CHAN_BUSWIDTH(ch), v);
    (VOID)RxReapReports();
    Wr(ADMAC_BASE, ADMAC_CHAN_STATUS0(ch), ADMAC_ST_DESC_DONE);
}

/* fill the chunk with the marker (so an unwritten chunk is detected), then queue it */
static BOOLEAN RxSubmitNext(VOID)
{
    ULONG ch = g_Nw.Hdr.sense_chan;
    ULONG chunk = g_Nw.RxSub % g_Nw.SenseChunks;
    ULONG off = chunk * NWAU_SENSE_CHUNK_BYTES;
    ULONGLONG iova = g_Nw.Hdr.sense_iova + off;
    ULONG i;

    if ((Rd(ADMAC_BASE, ADMAC_CHAN_DESC_RING(ch)) & ADMAC_RING_FULL) != 0) {
        return FALSE;
    }
    for (i = 0; i < NWAU_SENSE_CHUNK_FRAMES; i++) {
        g_Nw.SenseRing[off / 4u + i] = NWAU_SENSE_MARKER;
    }
    FullBarrier();
    Wr(ADMAC_BASE, ADMAC_RX_DESC_WRITE(ch), (ULONG)(iova & 0xffffffffu));
    Wr(ADMAC_BASE, ADMAC_RX_DESC_WRITE(ch), (ULONG)(iova >> 32));
    Wr(ADMAC_BASE, ADMAC_RX_DESC_WRITE(ch), NWAU_SENSE_CHUNK_BYTES);
    Wr(ADMAC_BASE, ADMAC_RX_DESC_WRITE(ch), ADMAC_DESC_NOTIFY | (g_Nw.RxDescId & 0xffu));
    g_Nw.RxDescId = (g_Nw.RxDescId + 1) & 0xffu;
    g_Nw.RxSub++;
    g_Nw.Stats.RxDescSubmitted++;
    return TRUE;
}

static VOID RxFill(VOID)
{
    while (g_Nw.RxSub - g_Nw.RxDone < NWAU_SENSE_INFLIGHT) {
        if (!RxSubmitNext()) {
            break;
        }
    }
}

/* (re)start the capture: channel reset, 4 marker-filled descriptors, RX on,
   RXB reset pulse then enable.  Lock held.  FALSE: the path did not start. */
static BOOLEAN RxArm(VOID)
{
    ULONG ch = g_Nw.Hdr.sense_chan;
    ULONG v;
    ULONG i;

    RxSerdesOff();
    RxReset();
    g_Nw.RxSub = g_Nw.RxDone = 0;
    g_Nw.RxDescId = 0;
    for (i = 0; i < NWAU_SENSE_INFLIGHT; i++) {
        if (!RxSubmitNext()) {
            break;
        }
    }
    if (g_Nw.RxSub == 0) {
        RxStop();
        return FALSE;
    }
    Wr(ADMAC_BASE, ADMAC_CHAN_INTMASK0(ch),
       ADMAC_ST_DESC_DONE | ADMAC_ST_DESC_RING_EMPTY | ADMAC_ST_REPORT_RING_FULL | ADMAC_ST_RING_ERR);
    Wr(ADMAC_BASE, ADMAC_RX_EN, 1u << (ch / 2u));
    g_Nw.RxDmaOn = TRUE;
    /* serdes: reset pulse, settle, enable (Asahi mca.c start order: reset, ~50 us, DMA, enable) */
    v = Rd(g_Nw.McaCl, MCA_RXB_STATUS);
    Wr(g_Nw.McaCl, MCA_RXB_STATUS, (v & ~MCA_STATUS_EN) | MCA_STATUS_RST);
    KeStallExecutionProcessor(50);
    v = Rd(g_Nw.McaCl, MCA_RXB_STATUS);
    Wr(g_Nw.McaCl, MCA_RXB_STATUS, (v & ~MCA_STATUS_RST) | MCA_STATUS_EN);
    g_Nw.RxSerdesOn = TRUE;
    if ((Rd(g_Nw.McaCl, MCA_RXB_STATUS) & MCA_STATUS_EN) == 0) {
        RxStop();
        return FALSE;
    }
    g_Nw.LastRxRestart = KeQueryInterruptTime();
    return TRUE;
}

/* Start capture for a new stream.  Never refuses playback: any failure trips
   the protection (static mode, blind model).  Lock held. */
static VOID RxStart(VOID)
{
    g_Nw.TxEAcc = 0;
    g_Nw.TxFAcc = 0;
    g_Nw.RxRearms = 0;
    nwau_ts_stream_start(&g_Nw.Ts, (KeQueryInterruptTime() - g_Nw.TsStopTime) / 10u);
    if (!g_Nw.SenseCfgOk || !RxArm()) {
        g_Nw.Stats.RxSetupFails++;
        nwau_ts_trip(&g_Nw.Ts, NWAU_TS_R_SETUP);
    }
    g_Nw.LastSenseBlock = KeQueryInterruptTime();
}

/* snapshot of the RX path when it stopped delivering (lock held) */
static VOID RxStallSnapshot(VOID)
{
    ULONG ch = g_Nw.Hdr.sense_chan;

    g_Nw.Stats.RxStallDesc = Rd(ADMAC_BASE, ADMAC_CHAN_DESC_RING(ch));
    g_Nw.Stats.RxStallReport = Rd(ADMAC_BASE, ADMAC_CHAN_REPORT_RING(ch));
    g_Nw.Stats.RxStallStatus = Rd(ADMAC_BASE, ADMAC_CHAN_STATUS0(ch));
    g_Nw.Stats.RxStallResidue = Rd(ADMAC_BASE, ADMAC_CHAN_RESIDUE(ch));
    g_Nw.Stats.RxStallSerdes = Rd(g_Nw.McaCl, MCA_RXB_STATUS);
    g_Nw.Stats.RxStallMcaInt = Rd(g_Nw.McaCl, MCA_INTSTATE);
    g_Nw.Stats.RxStallInflight = g_Nw.RxSub - g_Nw.RxDone;
}

/* RX gave nothing for NWAU_RX_RESTART_100NS while the protection bridges the
   gap: restart the capture without touching the protection state (lock held) */
static VOID RxRestartQuiet(VOID)
{
    RxStallSnapshot();
    EvtLocked(NWAU_EV_RXQUIET, g_Nw.Stats.RxStallDesc >> 16,
              (g_Nw.Stats.RxStallDesc & 0xffffu) | (g_Nw.Stats.RxStallReport << 16));
    g_Nw.Stats.RxQuietRestarts++;
    if (RxArm()) {
        nwau_ts_rx_restarted(&g_Nw.Ts);
    }
}

/* RX stalled (e.g. its 4 descriptors ran out while the hypervisor had stopped
   the guest; the channel does not resume by itself): restart it and re-verify
   the sense data from scratch.  The protection stays tripped (static mode,
   blind model) until the re-armed path verifies again.  Lock held. */
static VOID RxRecover(VOID)
{
    if (g_Nw.RxRearms >= NWAU_RX_REARMS_PER_STREAM) {
        RxStop();
        return;
    }
    RxStallSnapshot();
    g_Nw.RxRearms++;
    g_Nw.Stats.RxRecoveries++;
    EvtLocked(NWAU_EV_RXREC, 0, g_Nw.RxRearms);
    if (RxArm()) {
        nwau_ts_rearm(&g_Nw.Ts);
    }
    g_Nw.LastSenseBlock = KeQueryInterruptTime();
}

/* reap completed RX chunks and run the model on each.  Lock held.
   Returns the last non-NONE nwau_ts event. */
static int RxService(VOID)
{
    ULONG ch = g_Nw.Hdr.sense_chan;
    ULONG n, k, st;
    int ev = NWAU_TS_EV_NONE;

    if (!g_Nw.RxDmaOn) {
        return NWAU_TS_EV_NONE;
    }
    n = RxReapReports();
    st = Rd(ADMAC_BASE, ADMAC_CHAN_STATUS0(ch));
    if (st & ADMAC_ST_DESC_DONE) {
        Wr(ADMAC_BASE, ADMAC_CHAN_STATUS0(ch), ADMAC_ST_DESC_DONE);
    }
    if (st & ADMAC_ST_RING_ERR) {
        ULONG v;
        g_Nw.Stats.RxRingErrors++;
        v = Rd(ADMAC_BASE, ADMAC_CHAN_DESC_RING(ch));
        Wr(ADMAC_BASE, ADMAC_CHAN_DESC_RING(ch), v | ADMAC_RING_ERR);
        v = Rd(ADMAC_BASE, ADMAC_CHAN_REPORT_RING(ch));
        Wr(ADMAC_BASE, ADMAC_CHAN_REPORT_RING(ch), v | ADMAC_RING_ERR);
    }
    g_Nw.Stats.RxOfUf = Rd(ADMAC_BASE, ADMAC_CHAN_DESC_RING(ch)) >> 16;
    for (k = 0; k < n; k++) {
        nwau_ts_drive d;
        ULONG off;
        int e;

        if (g_Nw.RxDone == g_Nw.RxSub) {
            g_Nw.Stats.RxStrayReports++;
            continue;
        }
        off = (g_Nw.RxDone % g_Nw.SenseChunks) * NWAU_SENSE_CHUNK_BYTES;
        d.tx_energy = g_Nw.TxEAcc;
        d.tx_frames = g_Nw.TxFAcc;
        d.gain_reg = g_Nw.GainShadow == NWAU_AMP_GAIN_STATIC ? NWAU_AMP_GAIN_STATIC : NWAU_PROT_GAIN;
        d.vol_reg = g_Nw.VolShadow;
        g_Nw.TxEAcc = 0;
        g_Nw.TxFAcc = 0;
        e = nwau_ts_block(&g_Nw.Ts, g_Nw.SenseRing + off / 4u, NWAU_SENSE_CHUNK_FRAMES, &d);
        if (e == NWAU_TS_EV_ARMED) {
            EvtLocked(NWAU_EV_VERIFIED, 0, g_Nw.Ts.verified);
        }
        g_Nw.RxDone++;
        g_Nw.Stats.RxDescCompleted++;
        g_Nw.LastSenseBlock = KeQueryInterruptTime();
        if (e != NWAU_TS_EV_NONE && ev != NWAU_TS_EV_OVERTEMP) {
            ev = e;
        }
    }
    RxFill();
    return ev;
}

/* ---- registry ------------------------------------------------------------- */
static VOID RegDword(_In_ HANDLE Key, _In_ PCWSTR Name, _In_ ULONG Value)
{
    UNICODE_STRING n;
    RtlInitUnicodeString(&n, Name);
    (VOID)ZwSetValueKey(Key, &n, 0, REG_DWORD, &Value, sizeof(Value));
}

/* saved thermal model state (Device Parameters\NwAudioThermSave) */
typedef struct _NWAU_THERM_SAVE {
    ULONG     Magic;
    ULONG     Version;
    LONGLONG  CoilUc;
    LONGLONG  MagUc;
    ULONGLONG Time;         /* KeQueryInterruptTime of the save */
} NWAU_THERM_SAVE;

static LONG ClampL(_In_ LONGLONG v)
{
    return v > 0x7fffffffll ? 0x7fffffff : (v < -0x7fffffffll ? -0x7fffffff : (LONG)v);
}

typedef struct _NWAU_REG_SNAP {
    NWAU_STATS      s;
    nwau_ts         ts;
    NWAU_THERM_SAVE save;
    ULONG           hdr[NWAU_HDR_WORDS];
    ULONG           status, state, sense;
    BOOLEAN         tsValid;
    ULONGLONG       winPa;
    NWAU_EVTLOG     evt;
} NWAU_REG_SNAP;

_IRQL_requires_(PASSIVE_LEVEL)
static VOID RegWriteValues(_In_ HANDLE key, _In_ const NWAU_REG_SNAP *r, _In_ BOOLEAN Stable)
{
    const NWAU_STATS &s = r->s;
    const nwau_ts &ts = r->ts;
    UNICODE_STRING n;

    RegDword(key, L"NwAudioStartStatus", r->status);
    RegDword(key, L"NwAudioState", r->state);
    RtlInitUnicodeString(&n, L"NwAudioHeader");
    (VOID)ZwSetValueKey(key, &n, 0, REG_BINARY, (PVOID)r->hdr, sizeof(r->hdr));
    RtlInitUnicodeString(&n, L"NwAudioWindowIpa");
    (VOID)ZwSetValueKey(key, &n, 0, REG_QWORD, (PVOID)&r->winPa, sizeof(r->winPa));
    RegDword(key, L"NwAudioStarts", s.Starts);
    RegDword(key, L"NwAudioStops", s.Stops);
    RegDword(key, L"NwAudioLastStopReason", s.LastStopReason);
    RegDword(key, L"NwAudioLastRunStatus", s.LastRunStatus);
    RegDword(key, L"NwAudioStartFails", s.StartFails);
    RegDword(key, L"NwAudioDescSubmitted", s.DescSubmitted);
    RegDword(key, L"NwAudioDescCompleted", s.DescCompleted);
    RegDword(key, L"NwAudioStrayReports", s.StrayReports);
    RegDword(key, L"NwAudioReportFaults", s.ReportFaults);
    RegDword(key, L"NwAudioRingErrors", s.RingErrors);
    RegDword(key, L"NwAudioPadChunks", s.PadChunks);
    RegDword(key, L"NwAudioPadBytes", s.PadBytes);
    RegDword(key, L"NwAudioPushBytes", s.PushBytes);
    RegDword(key, L"NwAudioDropBytes", s.DropBytes);
    RegDword(key, L"NwAudioDmaRanDry", s.DmaRanDry);
    RegDword(key, L"NwAudioOsStarved", s.OsStarved);
    RegDword(key, L"NwAudioI2cErrors", (ULONG)s.I2cErrors);
    RegDword(key, L"NwAudioAmpVerifyFails", s.AmpVerifyFails);
    RegDword(key, L"NwAudioAmpState", s.AmpState);
    RegDword(key, L"NwAudioVolReg", s.VolReg);
    RegDword(key, L"NwAudioMaxServiceGapUs", s.MaxServiceGapUs);
    RegDword(key, L"NwAudioOfUf", s.OfUf);
    RegDword(key, L"NwAudioSwitch1", s.Switch1);
    /* V/I sense and thermal protection */
    RegDword(key, L"NwAudioSense", r->sense);
    RegDword(key, L"NwAudioProtState", ts.state);
    RegDword(key, L"NwAudioProtTripReason", ts.trip_reason);
    RegDword(key, L"NwAudioProtTrips", ts.trips & 0x7fffffffu);
    RegDword(key, L"NwAudioProtVerified", ts.verified);
    RegDword(key, L"NwAudioProtRaises", s.ProtRaises);
    RegDword(key, L"NwAudioProtDrops", s.ProtDrops);
    RegDword(key, L"NwAudioProtFloor", s.ProtFloor);
    RegDword(key, L"NwAudioGainReg", s.GainReg);
    RegDword(key, L"NwAudioCoilTempMilliC", (ULONG)ClampL(ts.t_coil_uc / 1000));
    RegDword(key, L"NwAudioMagnetTempMilliC", (ULONG)ClampL(ts.t_mag_uc / 1000));
    RegDword(key, L"NwAudioMaxCoilTempMilliC", (ULONG)ClampL(ts.max_coil_uc / 1000));
    RegDword(key, L"NwAudioSensePowerUw", (ULONG)ts.p_uw);
    RegDword(key, L"NwAudioSenseVrmsMv", ts.vrms_mv);
    RegDword(key, L"NwAudioSenseIrmsMa", ts.irms_ma);
    RegDword(key, L"NwAudioSenseZMilliohm", ts.z_mohm);
    RegDword(key, L"NwAudioSenseRMilliohm", ts.r_mohm);
    RegDword(key, L"NwAudioSenseRMinMilliohm", ts.r_min_mohm);
    RegDword(key, L"NwAudioSenseRTempMilliC", (ULONG)ts.rtemp_mc);
    RegDword(key, L"NwAudioSenseRatioPermille", ts.ratio_permille);
    RegDword(key, L"NwAudioSenseExpMv2", (ULONG)ClampL(ts.exp_s_mv2));
    RegDword(key, L"NwAudioSenseSat", ts.sat);
    RegDword(key, L"NwAudioSenseBlocks", ts.blocks);
    RegDword(key, L"NwAudioSenseBadStale", ts.bad[NWAU_TS_R_STALE]);
    RegDword(key, L"NwAudioSenseBadUnwritten", ts.bad[NWAU_TS_R_UNWRITTEN]);
    RegDword(key, L"NwAudioSenseBadSaturated", ts.bad[NWAU_TS_R_SATURATED]);
    RegDword(key, L"NwAudioSenseBadNegPower", ts.bad[NWAU_TS_R_NEG_POWER]);
    RegDword(key, L"NwAudioSenseBadImpedance", ts.bad[NWAU_TS_R_IMPEDANCE]);
    RegDword(key, L"NwAudioSenseBadDead", ts.bad[NWAU_TS_R_DEAD]);
    RegDword(key, L"NwAudioSenseBadOvertemp", ts.bad[NWAU_TS_R_OVERTEMP]);
    RegDword(key, L"NwAudioRxDescSubmitted", s.RxDescSubmitted);
    RegDword(key, L"NwAudioRxDescCompleted", s.RxDescCompleted);
    RegDword(key, L"NwAudioRxStrayReports", s.RxStrayReports);
    RegDword(key, L"NwAudioRxReportFaults", s.RxReportFaults);
    RegDword(key, L"NwAudioRxRingErrors", s.RxRingErrors);
    RegDword(key, L"NwAudioRxOfUf", s.RxOfUf);
    RegDword(key, L"NwAudioRxSetupFails", s.RxSetupFails);
    RegDword(key, L"NwAudioSenseCfgFails", s.SenseCfgFails);
    RegDword(key, L"NwAudioAmpTdm4", s.AmpTdm4);
    RegDword(key, L"NwAudioTxCarveout", s.TxCarveout);
    RegDword(key, L"NwAudioRxCarveout", s.RxCarveout);
    RegDword(key, L"NwAudioRxConf", s.RxConf);
    RegDword(key, L"NwAudioThermRestored", s.ThermRestored);
    RegDword(key, L"NwAudioRxRecoveries", s.RxRecoveries);
    RegDword(key, L"NwAudioMaxRealGapUs", s.MaxRealGapUs);
    RegDword(key, L"NwAudioMaxStolenUs", s.MaxStolenUs);
    RegDword(key, L"NwAudioRealGaps40ms", s.RealGaps40ms);
    RegDword(key, L"NwAudioRealGaps80ms", s.RealGaps80ms);
    RegDword(key, L"NwAudioMaxGapRenderUs", s.MaxGapRenderUs);
    RegDword(key, L"NwAudioMaxGapTimerUs", s.MaxGapTimerUs);
    RegDword(key, L"NwAudioMaxServiceUs", s.MaxServiceUs);
    RegDword(key, L"NwAudioMaxSenseUs", s.MaxSenseUs);
    RegDword(key, L"NwAudioI2cOps", s.I2cOps);
    RegDword(key, L"NwAudioI2cMaxUs", s.I2cMaxUs);
    RegDword(key, L"NwAudioI2cTotalMs", s.I2cTotalMs);
    RegDword(key, L"NwAudioAmpWorkRuns", s.AmpWorkRuns);
    RegDword(key, L"NwAudioMaxAmpWorkUs", s.MaxAmpWorkUs);
    RegDword(key, L"NwAudioVolSets", s.VolSets);
    RegDword(key, L"NwAudioLastVolLevel", (ULONG)s.LastVolLevel);
    RegDword(key, L"NwAudioLastVolChannel", s.LastVolChannel);
    RegDword(key, L"NwAudioGateRefusals", s.GateRefusals);
    RegDword(key, L"NwAudioBlindMs", (ULONG)(ts.blind_frames / 48u));
    RegDword(key, L"NwAudioStreamRealMs", s.StreamRealMs);
    RegDword(key, L"NwAudioStreamGuestMs", s.StreamGuestMs);
    RegDword(key, L"NwAudioMinQueueUs", s.MinQueueUs);
    RegDword(key, L"NwAudioMinInflight", s.MinInflight);
    RegDword(key, L"NwAudioDryRecoveries", s.DryRecoveries);
    RegDword(key, L"NwAudioRxSramSize", s.RxSramSize);
    RegDword(key, L"NwAudioRxCarveoutSet", s.RxCarveoutSet);
    RegDword(key, L"NwAudioRxQuietRestarts", s.RxQuietRestarts);
    RegDword(key, L"NwAudioRxStallDesc", s.RxStallDesc);
    RegDword(key, L"NwAudioRxStallReport", s.RxStallReport);
    RegDword(key, L"NwAudioRxStallStatus", s.RxStallStatus);
    RegDword(key, L"NwAudioRxStallResidue", s.RxStallResidue);
    RegDword(key, L"NwAudioRxStallSerdes", s.RxStallSerdes);
    RegDword(key, L"NwAudioRxStallMcaInt", s.RxStallMcaInt);
    RegDword(key, L"NwAudioRxStallInflight", s.RxStallInflight);
    RegDword(key, L"NwAudioProtBridges", ts.bridges);
    RtlInitUnicodeString(&n, L"NwAudioEvents");
    (VOID)ZwSetValueKey(key, &n, 0, REG_BINARY, (PVOID)&r->evt, sizeof(r->evt));
    if (Stable && r->tsValid) {
        RtlInitUnicodeString(&n, L"NwAudioThermSave");
        (VOID)ZwSetValueKey(key, &n, 0, REG_BINARY, (PVOID)&r->save, sizeof(r->save));
    }
}

/* Stable = FALSE: only the volatile subkey "Device Parameters\Live" (never
   flushed to disk).  Disk writes matter here: the guest's NVMe is emulated by
   the Python hypervisor and every proxy trap stops all CPUs (time stealing), so
   a registry hive flush can freeze Windows for longer than the queued audio.
   Stable = TRUE (start / stop / fault / shutdown only): also "Device Parameters"
   itself and the thermal save. */
_IRQL_requires_(PASSIVE_LEVEL)
static VOID NwAuWriteRegistry(_In_ BOOLEAN Stable)
{
    NWAU_REG_SNAP *r;                   /* about 3 KiB: from pool, not the stack */
    HANDLE key = NULL, live = NULL;
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING n;
    KIRQL irql;
    NTSTATUS st;

    if (g_Nw.Pdo == NULL) {
        return;
    }
    r = (NWAU_REG_SNAP *)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*r), NWAU_POOLTAG);
    if (r == NULL) {
        return;
    }
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    r->evt = g_Nw.Evt;
    r->s = g_Nw.Stats;
    r->ts = g_Nw.Ts;
    RtlCopyMemory(r->hdr, g_Nw.HdrWords, sizeof(r->hdr));
    r->status = g_Nw.StartStatus;
    r->state = (ULONG)g_Nw.State;
    r->sense = g_Nw.Sense ? 1u : 0u;
    r->tsValid = g_Nw.TsValid;
    r->winPa = (ULONGLONG)g_Nw.Pa[NWAU_RES_WINDOW].QuadPart;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    r->save.Magic = NWAU_THERM_MAGIC;
    r->save.Version = 1;
    r->save.CoilUc = r->ts.t_coil_uc;
    r->save.MagUc = r->ts.t_mag_uc;
    r->save.Time = KeQueryInterruptTime();

    st = IoOpenDeviceRegistryKey(g_Nw.Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE | KEY_CREATE_SUB_KEY, &key);
    if (!NT_SUCCESS(st)) {
        ExFreePoolWithTag(r, NWAU_POOLTAG);
        return;
    }
    if (Stable) {
        RegWriteValues(key, r, TRUE);
    }
    RtlInitUnicodeString(&n, L"Live");
    InitializeObjectAttributes(&oa, &n, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, key, NULL);
    if (NT_SUCCESS(ZwCreateKey(&live, KEY_SET_VALUE, &oa, 0, NULL, REG_OPTION_VOLATILE, NULL))) {
        RegWriteValues(live, r, FALSE);
        ZwClose(live);
    }
    ZwClose(key);
    ExFreePoolWithTag(r, NWAU_POOLTAG);
}

/* previous driver instance's model state (same boot only; never below the cold assumption) */
_IRQL_requires_(PASSIVE_LEVEL)
static VOID NwAuRestoreThermal(VOID)
{
    HANDLE key = NULL;
    UNICODE_STRING n;
    UCHAR buf[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(NWAU_THERM_SAVE)];
    PKEY_VALUE_PARTIAL_INFORMATION kv = (PKEY_VALUE_PARTIAL_INFORMATION)buf;
    ULONG got = 0;
    NWAU_THERM_SAVE save;
    ULONGLONG now = KeQueryInterruptTime();

    nwau_ts_init(&g_Nw.Ts);
    g_Nw.TsStopTime = now;
    g_Nw.TsValid = TRUE;
    if (g_Nw.Pdo == NULL ||
        !NT_SUCCESS(IoOpenDeviceRegistryKey(g_Nw.Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_READ, &key))) {
        return;
    }
    RtlInitUnicodeString(&n, L"NwAudioThermSave");
    RtlZeroMemory(buf, sizeof(buf));
    if (NT_SUCCESS(ZwQueryValueKey(key, &n, KeyValuePartialInformation, kv, sizeof(buf), &got)) &&
        kv->Type == REG_BINARY && kv->DataLength == sizeof(save)) {
        RtlCopyMemory(&save, kv->Data, sizeof(save));
        /* a save from this boot has Time <= now; one from an earlier boot with a
           smaller uptime also passes, which only under-estimates the cooling */
        if (save.Magic == NWAU_THERM_MAGIC && save.Version == 1 && save.Time <= now) {
            nwau_ts_restore(&g_Nw.Ts, save.CoilUc, save.MagUc, (now - save.Time) / 10u);
            g_Nw.Stats.ThermRestored = 1;
        }
    }
    ZwClose(key);
}

static IO_WORKITEM_ROUTINE NwAuWorkRoutine;

static VOID NwAuWorkRoutine(_In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);
    NwAuWriteRegistry(InterlockedExchange(&g_Nw.RegWantStable, 0) != 0);
    InterlockedExchange(&g_Nw.WorkQueued, 0);
    KeSetEvent(&g_Nw.WorkIdle, IO_NO_INCREMENT, FALSE);
}

/* <= DISPATCH_LEVEL, lock may be held */
static VOID QueueRegistryWrite(_In_ BOOLEAN Stable)
{
    if (g_Nw.WorkItem == NULL) {
        return;
    }
    if (Stable) {
        InterlockedExchange(&g_Nw.RegWantStable, 1);
    }
    if (InterlockedCompareExchange(&g_Nw.WorkQueued, 1, 0) == 0) {
        KeClearEvent(&g_Nw.WorkIdle);
        IoQueueWorkItem(g_Nw.WorkItem, NwAuWorkRoutine, DelayedWorkQueue, NULL);
    }
}

/* ---- amp worker (PASSIVE) -------------------------------------------------- */
static IO_WORKITEM_ROUTINE NwAuAmpWorkRoutine;

/* <= DISPATCH_LEVEL, lock may be held */
static VOID QueueAmpWork(VOID)
{
    if (g_Nw.AmpWorkItem == NULL || g_Nw.AmpWorkStop) {
        return;
    }
    InterlockedExchange(&g_Nw.AmpWorkAgain, 1);
    if (InterlockedCompareExchange(&g_Nw.AmpWorkQueued, 1, 0) == 0) {
        KeClearEvent(&g_Nw.AmpWorkIdle);
        IoQueueWorkItem(g_Nw.AmpWorkItem, NwAuAmpWorkRoutine, CriticalWorkQueue, NULL);
    }
}

/* A fault seen with the spin lock held (DISPATCH): the amp cannot be switched
   off here (I2C).  The feed keeps queueing silence and the amp worker mutes and
   shuts the amp down, then stops the data path.  Lock held. */
static VOID FaultBeginLocked(_In_ ULONG Reason)
{
    if (g_Nw.State == NWAU_ST_PLAYING || g_Nw.State == NWAU_ST_STARTING) {
        g_Nw.State = NWAU_ST_FAULTING;
        g_Nw.FaultReason = Reason;
        QueueAmpWork();
    }
}

/* data path off (lock held) */
static VOID DataPathOffLocked(_In_ ULONG Reason, _In_ LONG NewState)
{
    ULONGLONG now = KeQueryInterruptTime();
    ULONGLONG real = RealNowUs();

    DmaDisable();
    SerdesSet(FALSE);
    RxStop();
    if (g_Nw.Ts.state != NWAU_TS_OFF) {
        nwau_ts_stream_stop(&g_Nw.Ts);
        g_Nw.TsStopTime = now;
    }
    if (g_Nw.StreamStartGuest != 0) {
        g_Nw.Stats.StreamGuestMs = (ULONG)((now - g_Nw.StreamStartGuest) / 10000u);
        g_Nw.Stats.StreamRealMs = (ULONG)((real - g_Nw.StreamStartReal) / 1000u);
        g_Nw.StreamStartGuest = 0;
    }
    g_Nw.Stats.Stops++;
    g_Nw.Stats.LastStopReason = Reason;
    EvtLocked(NWAU_EV_STOP, 0, Reason);
    g_Nw.State = NewState;
}

/* Stop with the amp muted first (order of beep_tone.py's finally block): amp
   0x0d -> 0x0e and gain 0x00 while the feed still queues silence, then DMA,
   serdes and RX off.  PASSIVE, AmpMutex held, lock NOT held. */
static VOID SafeOffPassive(_In_ ULONG Reason, _In_ LONG NewState)
{
    KIRQL irql;
    BOOLEAN wasActive;

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    wasActive = g_Nw.State == NWAU_ST_PLAYING || g_Nw.State == NWAU_ST_STARTING ||
                g_Nw.State == NWAU_ST_STOPPING || g_Nw.State == NWAU_ST_FAULTING ||
                g_Nw.DmaOn || g_Nw.SerdesOn;
    if (g_Nw.State == NWAU_ST_PLAYING || g_Nw.State == NWAU_ST_STARTING) {
        g_Nw.State = NWAU_ST_STOPPING;
    }
    KeReleaseSpinLock(&g_Nw.Lock, irql);

    if (wasActive && g_Nw.VolShadow != 0) {
        (VOID)AmpVolumeRamp(NWAU_VOL_REG_MAX, 22, 600);  /* ~5 ms fade to -100 dB, then mute */
    }
    AmpOff(wasActive);
    if (g_Nw.GainShadow != NWAU_AMP_GAIN_STATIC && (NewState == NWAU_ST_DOWN || NewState == NWAU_ST_OFF)) {
        (VOID)AmpGainSet(NWAU_AMP_GAIN_STATIC);         /* leave the amp at 11 dB when we let go of it */
    }

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    DataPathOffLocked(Reason, NewState);
    KeReleaseSpinLock(&g_Nw.Lock, irql);
}

/* lock held: does the gain / volume policy want an amp write? */
static BOOLEAN PolicyDirtyLocked(VOID)
{
    return CurrentVolumeRegLocked() != g_Nw.VolShadow;
}

enum { NWAU_ACT_NONE = 0, NWAU_ACT_VOL };

/* Gain / volume policy, one verified I2C action per step.  PASSIVE, AmpMutex
   held, lock NOT held.  Quieter at once, louder with gain 0x14 by one 0.5 dB
   step per NWAU_VOL_SLEW_100NS (the service re-queues while not done). */
static VOID AmpPolicy(VOID)
{
    ULONG iter;

    for (iter = 0; iter < 32; iter++) {
        KIRQL irql;
        ULONG act = NWAU_ACT_NONE, v = 0;
        BOOLEAN playing, ok = TRUE;
        ULONGLONG now = KeQueryInterruptTime();

        KeAcquireSpinLock(&g_Nw.Lock, &irql);
        if (!g_Nw.Started || (g_Nw.State != NWAU_ST_PLAYING && g_Nw.State != NWAU_ST_IDLE &&
                              g_Nw.State != NWAU_ST_FAULT)) {
            KeReleaseSpinLock(&g_Nw.Lock, irql);
            return;                                     /* start / stop / fault paths own the amp */
        }
        playing = g_Nw.State == NWAU_ST_PLAYING;
        g_Nw.Stats.ProtFloor = nwau_ts_floor(&g_Nw.Ts);
        {
            /* the gain never changes here: the protection state only moves the
               volume floor (gain 0x14: healthy = model floor, else 0x20) */
            ULONG target = CurrentVolumeRegLocked();
            if (target != g_Nw.VolShadow) {
                if (g_Nw.GainShadow != NWAU_AMP_GAIN_STATIC && target < g_Nw.VolShadow) {
                    if (now - g_Nw.LastVolStep < NWAU_VOL_SLEW_100NS) {
                        KeReleaseSpinLock(&g_Nw.Lock, irql);
                        return;
                    }
                    g_Nw.LastVolStep = now;
                    target = g_Nw.VolShadow - 1;
                }
                act = NWAU_ACT_VOL;
                v = target;
            }
        }
        KeReleaseSpinLock(&g_Nw.Lock, irql);

        switch (act) {
        case NWAU_ACT_NONE:
            return;
        default:
            ok = AmpVolumeRamp(v, 4, 500);              /* 2 dB per 0.5 ms: no zipper click */
            break;
        }
        if (!ok) {
            g_Nw.Stats.AmpVerifyFails++;
            KeAcquireSpinLock(&g_Nw.Lock, &irql);
            if (playing && g_Nw.GainShadow != NWAU_AMP_GAIN_STATIC) {
                FaultBeginLocked(NWAU_STOP_AMP_VERIFY);
            }
            KeReleaseSpinLock(&g_Nw.Lock, irql);
            return;
        }
    }
}

static VOID NwAuAmpWorkRoutine(_In_ PDEVICE_OBJECT DeviceObject, _In_opt_ PVOID Context)
{
    BOOLEAN stable = FALSE;
    ULONGLONG t0;
    KIRQL irql;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);
    do {
        LONG st;
        ULONG reason;

        InterlockedExchange(&g_Nw.AmpWorkAgain, 0);
        if (g_Nw.AmpWorkStop) {
            break;
        }
        ExAcquireFastMutex(&g_Nw.AmpMutex);
        t0 = RealNowUs();
        KeAcquireSpinLock(&g_Nw.Lock, &irql);
        st = g_Nw.State;
        reason = g_Nw.FaultReason;
        KeReleaseSpinLock(&g_Nw.Lock, irql);
        if (st == NWAU_ST_FAULTING) {
            SafeOffPassive(reason, NWAU_ST_FAULT);
            stable = TRUE;
        } else {
            AmpPolicy();
            KeAcquireSpinLock(&g_Nw.Lock, &irql);
            if (g_Nw.State == NWAU_ST_FAULTING) {
                InterlockedExchange(&g_Nw.AmpWorkAgain, 1);   /* the policy itself started a fault */
            }
            KeReleaseSpinLock(&g_Nw.Lock, irql);
        }
        KeAcquireSpinLock(&g_Nw.Lock, &irql);
        g_Nw.Stats.AmpWorkRuns++;
        MaxU(&g_Nw.Stats.MaxAmpWorkUs, RealNowUs() - t0);
        KeReleaseSpinLock(&g_Nw.Lock, irql);
        ExReleaseFastMutex(&g_Nw.AmpMutex);
    } while (g_Nw.AmpWorkAgain != 0);

    if (stable) {
        NwAuWriteRegistry(TRUE);                        /* PASSIVE, no mutex held */
    }
    InterlockedExchange(&g_Nw.AmpWorkQueued, 0);
    if (g_Nw.AmpWorkAgain != 0 && !g_Nw.AmpWorkStop &&
        InterlockedCompareExchange(&g_Nw.AmpWorkQueued, 1, 0) == 0) {
        IoQueueWorkItem(g_Nw.AmpWorkItem, NwAuAmpWorkRoutine, CriticalWorkQueue, NULL);
        return;                                         /* still busy: AmpWorkIdle stays clear */
    }
    KeSetEvent(&g_Nw.AmpWorkIdle, IO_NO_INCREMENT, FALSE);
}

/* ---- service (lock held, <= DISPATCH) --------------------------------------- */
static VOID ReapTxLocked(VOID)
{
    ULONG ch = g_Nw.Hdr.admac_chan;
    ULONG n, acc, st, k, nchunks;
    nwu64 doneChunk;

    n = DmaReapReports();
    st = Rd(ADMAC_BASE, ADMAC_CHAN_STATUS0(ch));
    if (st & ADMAC_ST_DESC_DONE) {
        Wr(ADMAC_BASE, ADMAC_CHAN_STATUS0(ch), ADMAC_ST_DESC_DONE);
    }
    if (st & ADMAC_ST_RING_ERR) {
        ULONG v;
        g_Nw.Stats.RingErrors++;
        v = Rd(ADMAC_BASE, ADMAC_CHAN_DESC_RING(ch));
        Wr(ADMAC_BASE, ADMAC_CHAN_DESC_RING(ch), v | ADMAC_RING_ERR);
        v = Rd(ADMAC_BASE, ADMAC_CHAN_REPORT_RING(ch));
        Wr(ADMAC_BASE, ADMAC_CHAN_REPORT_RING(ch), v | ADMAC_RING_ERR);
    }
    /* digital energy of the TX chunks that just completed (= being played now) */
    doneChunk = g_Nw.RingState.done / NWAU_CHUNK_BYTES;
    nchunks = g_Nw.RingState.ring_bytes / NWAU_CHUNK_BYTES;
    acc = nwau_ring_complete(&g_Nw.RingState, n);
    for (k = 0; k < acc; k++) {
        g_Nw.TxEAcc += g_Nw.TxChunkE[(ULONG)((doneChunk + k) % nchunks) % NWAU_MAX_TX_CHUNKS];
        g_Nw.TxFAcc += NWAU_CHUNK_BYTES / 4u;
    }
    g_Nw.Stats.DescCompleted += acc;
    g_Nw.Stats.StrayReports += n - acc;
    g_Nw.Stats.OfUf = Rd(ADMAC_BASE, ADMAC_CHAN_DESC_RING(ch)) >> 16;
}

static VOID ServiceLocked(_In_ ULONG Source)
{
    ULONGLONG now, real, gap, rgap;
    int ev;

    if (!g_Nw.Started) {
        return;
    }
    if (g_Nw.State == NWAU_ST_STARTING || g_Nw.State == NWAU_ST_STOPPING ||
        g_Nw.State == NWAU_ST_FAULTING) {
        /* the amp is being switched by the PASSIVE side: keep silence flowing */
        if (g_Nw.DmaOn) {
            ReapTxLocked();
            DmaFill(TRUE);
        }
        return;
    }
    if (g_Nw.State != NWAU_ST_PLAYING) {
        return;
    }
    now = KeQueryInterruptTime();
    real = RealNowUs();
    gap = (now - g_Nw.LastService) / 10u;               /* us, guest time */
    rgap = real - g_Nw.LastReal;                        /* us, real time */
    MaxU(&g_Nw.Stats.MaxServiceGapUs, gap);
    MaxU(&g_Nw.Stats.MaxRealGapUs, rgap);
    if (rgap > gap) {
        MaxU(&g_Nw.Stats.MaxStolenUs, rgap - gap);
    }
    if (rgap > 40000u) {
        g_Nw.Stats.RealGaps40ms++;
        EvtLocked(NWAU_EV_STALL, (ULONG)(gap / 1000u), (ULONG)(rgap / 1000u));
    }
    if (rgap > 80000u) {
        g_Nw.Stats.RealGaps80ms++;
    }
    if (Source < 2) {
        ULONGLONG sg = (now - g_Nw.LastServiceBySrc[Source]) / 10u;
        if (g_Nw.LastServiceBySrc[Source] != 0) {
            MaxU(Source == NWAU_SRC_RENDER ? &g_Nw.Stats.MaxGapRenderUs : &g_Nw.Stats.MaxGapTimerUs, sg);
        }
        g_Nw.LastServiceBySrc[Source] = now;
    }
    g_Nw.LastService = now;
    g_Nw.LastReal = real;

    ReapTxLocked();
    if (nwau_ring_inflight(&g_Nw.RingState) == 0) {
        /* every queued chunk was played (the timers came late): refill with
           silence and go on; stop only when it keeps happening */
        g_Nw.Stats.DmaRanDry++;
        g_Nw.StreamDries++;
        EvtLocked(NWAU_EV_DRY, 0, g_Nw.StreamDries);
        if (g_Nw.StreamDries >= NWAU_DRY_LIMIT) {
            FaultBeginLocked(NWAU_STOP_WATCHDOG);
            DmaFill(TRUE);
            return;
        }
        g_Nw.Stats.DryRecoveries++;
        EvtLocked(NWAU_EV_DRYREC, 0, g_Nw.StreamDries);
        DmaFill(TRUE);
    }
    if (now - g_Nw.StreamStartGuest > NWAU_LEVEL_SETTLE_100NS) {
        ULONG inf = nwau_ring_inflight(&g_Nw.RingState);
        ULONGLONG q = (ULONGLONG)inf * NWAU_CHUNK_BYTES + (g_Nw.RingState.wr - g_Nw.RingState.sub);
        ULONG qus = (ULONG)(q * 1000000u / (NWAU_SAMPLE_RATE * 4u));
        if (qus < g_Nw.Stats.MinQueueUs) {
            g_Nw.Stats.MinQueueUs = qus;
        }
        if (inf < g_Nw.Stats.MinInflight) {
            g_Nw.Stats.MinInflight = inf;
        }
    }
    if (now - g_Nw.LastPush > NWAU_STARVE_100NS) {
        g_Nw.Stats.OsStarved++;
        FaultBeginLocked(NWAU_STOP_OS_STARVED);
        DmaFill(TRUE);
        return;
    }

    /* V/I sense -> thermal model (no I2C here; amp writes go to the worker) */
    if (g_Nw.Sense) {
        ULONGLONG t0 = RealNowUs();
        int evb;

        ev = RxService();
        if (ev == NWAU_TS_EV_NONE &&
            (g_Nw.Ts.state == NWAU_TS_ARMING || g_Nw.Ts.state == NWAU_TS_HEALTHY)) {
            ULONGLONG since = now - g_Nw.LastSenseBlock;
            /* RX has stopped with buffers queued (seen on hardware, not a DPC delay):
               restart it early; a healthy, cool protection bridges the gap */
            if (since > NWAU_RX_RESTART_100NS && now - g_Nw.LastRxRestart > NWAU_RX_RESTART_100NS &&
                g_Nw.Ts.state == NWAU_TS_HEALTHY) {
                RxRestartQuiet();
            }
            ev = nwau_ts_stale(&g_Nw.Ts, since / 10u);
            if (ev == NWAU_TS_EV_BRIDGE) {
                EvtLocked(NWAU_EV_BRIDGE, 0, (ULONG)(since / 10000u));
                ev = NWAU_TS_EV_NONE;
            }
        }
        if (g_Nw.Ts.state == NWAU_TS_TRIPPED || g_Nw.Ts.bridging) {
            /* no trusted sense: keep the model running on the sent signal */
            evb = nwau_ts_blind(&g_Nw.Ts, g_Nw.TxEAcc, g_Nw.TxFAcc,
                                g_Nw.GainShadow == NWAU_AMP_GAIN_STATIC ? NWAU_AMP_GAIN_STATIC : NWAU_PROT_GAIN,
                                g_Nw.VolShadow);
            g_Nw.TxEAcc = 0;
            g_Nw.TxFAcc = 0;
            if (evb == NWAU_TS_EV_OVERTEMP) {
                ev = evb;
            }
        }
        if (ev == NWAU_TS_EV_TRIP || ev == NWAU_TS_EV_OVERTEMP) {
            EvtLocked(NWAU_EV_TRIP, 0, g_Nw.Ts.trip_reason);
        }
        if (ev == NWAU_TS_EV_OVERTEMP) {
            FaultBeginLocked(NWAU_STOP_THERMAL);
            DmaFill(TRUE);
            return;
        }
        if (ev == NWAU_TS_EV_TRIP) {
            if (g_Nw.Ts.trip_reason == NWAU_TS_R_STALE || g_Nw.Ts.trip_reason == NWAU_TS_R_UNWRITTEN) {
                RxRecover();                            /* DMA problem: restart and re-verify */
            } else {
                RxStop();                               /* implausible data: static for this stream */
            }
        }
        MaxU(&g_Nw.Stats.MaxSenseUs, RealNowUs() - t0);
        if (PolicyDirtyLocked()) {
            QueueAmpWork();
        }
        if (now - g_Nw.LastRegWrite > NWAU_REG_PERIOD_100NS) {
            g_Nw.LastRegWrite = now;
            QueueRegistryWrite(FALSE);                  /* volatile key only */
        }
    }
    DmaFill(FALSE);
    MaxU(&g_Nw.Stats.MaxServiceUs, RealNowUs() - real);
}

/* ---- timer ---------------------------------------------------------------- */
static EXT_CALLBACK NwAuTimerCallback;

static VOID NwAuTimerCallback(_In_ PEX_TIMER Timer, _In_opt_ PVOID Context)
{
    KIRQL irql;
    UNREFERENCED_PARAMETER(Timer);
    UNREFERENCED_PARAMETER(Context);

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    ServiceLocked(NWAU_SRC_TIMER);
    KeReleaseSpinLock(&g_Nw.Lock, irql);
}

/* ---- start / shutdown ---------------------------------------------------- */
static VOID UnmapAll(VOID)
{
    ULONG i;
    for (i = 0; i < NWAU_NUM_RESOURCES; i++) {
        if (g_Nw.Va[i] != NULL) {
            MmUnmapIoSpace((PVOID)g_Nw.Va[i], g_Nw.Len[i]);
            g_Nw.Va[i] = NULL;
        }
    }
    g_Nw.McaCl = NULL;
    g_Nw.Ring = NULL;
}

static VOID FreeWorkItems(VOID)
{
    if (g_Nw.WorkItem != NULL) {
        IoFreeWorkItem(g_Nw.WorkItem);
        g_Nw.WorkItem = NULL;
    }
    if (g_Nw.AmpWorkItem != NULL) {
        IoFreeWorkItem(g_Nw.AmpWorkItem);
        g_Nw.AmpWorkItem = NULL;
    }
}

/* Not pageable: these functions take the spin lock (DISPATCH_LEVEL). */
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS NwAuStart(_In_ PDEVICE_OBJECT Fdo, _In_ PRESOURCELIST ResourceList)
{
    ULONG i, count, rc = NWAU_OK;
    NTSTATUS status = STATUS_SUCCESS;
    KIRQL irql;

    RtlZeroMemory(&g_Nw, sizeof(g_Nw));
    KeInitializeSpinLock(&g_Nw.Lock);
    KeInitializeEvent(&g_Nw.WorkIdle, NotificationEvent, TRUE);
    KeInitializeEvent(&g_Nw.AmpWorkIdle, NotificationEvent, TRUE);
    ExInitializeFastMutex(&g_Nw.AmpMutex);
    g_Nw.Fdo = Fdo;
    g_Nw.VolumeLevel = NWAU_DEFAULT_LEVEL_16_16;
    g_Nw.VolCh[0] = g_Nw.VolCh[1] = NWAU_DEFAULT_LEVEL_16_16;
    g_Nw.Stats.AmpState = 0xffffffffu;
    if (!NT_SUCCESS(PcGetPhysicalDeviceObject(Fdo, &g_Nw.Pdo))) {
        g_Nw.Pdo = NULL;
    }
    g_Nw.WorkItem = IoAllocateWorkItem(Fdo);
    g_Nw.AmpWorkItem = IoAllocateWorkItem(Fdo);
    if (g_Nw.AmpWorkItem == NULL) {
        rc = NWAU_ENO_MEMORY;
        goto Fail;
    }

    /* 1. resources: exactly 5 memory ranges, in contract order */
    count = ResourceList ? ResourceList->NumberOfEntriesOfType(CmResourceTypeMemory) : 0;
    if (count != NWAU_NUM_RESOURCES) {
        rc = NWAU_ERES_COUNT;
        goto Fail;
    }
    for (i = 0; i < NWAU_NUM_RESOURCES; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d = ResourceList->FindTranslatedEntry(CmResourceTypeMemory, i);
        PHYSICAL_ADDRESS start;
        ULONGLONG len;
        if (d == NULL) {
            rc = NWAU_ERES_COUNT;
            goto Fail;
        }
        start.QuadPart = 0;
        len = RtlCmDecodeMemIoResource(d, (PULONGLONG)&start.QuadPart);
        rc = (ULONG)nwau_check_resource(i, (nwu64)start.QuadPart, (nwu64)len);
        if (rc != NWAU_OK) {
            goto Fail;
        }
        g_Nw.Pa[i] = start;
        g_Nw.Len[i] = (SIZE_T)len;
    }
    /* 2. map everything non-cached (device memory); the window is only ever
       accessed with single aligned 32-bit loads/stores */
    for (i = 0; i < NWAU_NUM_RESOURCES; i++) {
        g_Nw.Va[i] = (volatile UCHAR *)MmMapIoSpaceEx(g_Nw.Pa[i], g_Nw.Len[i], PAGE_READWRITE | PAGE_NOCACHE);
        if (g_Nw.Va[i] == NULL) {
            rc = NWAU_EMAP_BASE + i;
            goto Fail;
        }
    }
    /* 3. header (written by the launcher, cleaned to memory with dc civac) */
    for (i = 0; i < NWAU_HDR_WORDS; i++) {
        g_Nw.HdrWords[i] = Rd(g_Nw.Va[NWAU_RES_WINDOW], i * 4u);
    }
    nwau_header_from_words((const nwu32 *)g_Nw.HdrWords, &g_Nw.Hdr);
    rc = (ULONG)nwau_header_validate(&g_Nw.Hdr, (nwu32)g_Nw.Len[NWAU_RES_WINDOW]);
    if (rc != NWAU_OK) {
        goto Fail;          /* no hardware register has been touched */
    }
    rc = (ULONG)nwau_ring_init(&g_Nw.RingState, g_Nw.Hdr.ring_bytes, NWAU_CHUNK_BYTES,
                               NWAU_MAX_INFLIGHT, NWAU_MIN_INFLIGHT, NWAU_MAX_LEAD_BYTES);
    if (rc != NWAU_OK) {
        goto Fail;
    }
    g_Nw.McaCl = g_Nw.Va[NWAU_RES_MCA_CLUSTER] + MCA_CLUSTER_STRIDE * g_Nw.Hdr.mca_cluster;
    g_Nw.Ring = (volatile nwu32 *)(g_Nw.Va[NWAU_RES_WINDOW] + NWAU_RING_OFFSET);
    if ((g_Nw.Hdr.ring_bytes / NWAU_CHUNK_BYTES) > NWAU_MAX_TX_CHUNKS) {
        rc = NWAU_EHDR_RING;
        goto Fail;
    }
    /* header v2 + status bit1: V/I sense path prepared by the launcher */
    g_Nw.Sense = nwau_header_has_sense(&g_Nw.Hdr) ? TRUE : FALSE;
    if (g_Nw.Sense) {
        g_Nw.SenseRing = (volatile nwu32 *)(g_Nw.Va[NWAU_RES_WINDOW] + nwau_header_sense_offset(&g_Nw.Hdr));
        g_Nw.SenseChunks = g_Nw.Hdr.sense_bytes / NWAU_SENSE_CHUNK_BYTES;
    }
    NwAuRestoreThermal();

    g_Nw.Timer = ExAllocateTimer(NwAuTimerCallback, NULL, EX_TIMER_HIGH_RESOLUTION);
    if (g_Nw.Timer == NULL) {
        rc = NWAU_ENO_MEMORY;
        goto Fail;
    }

    /* 4. hardware: the header says the launcher powered and configured it.
       Make sure nothing plays, then set the volume (I2C without the spin lock). */
    ExAcquireFastMutex(&g_Nw.AmpMutex);
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    g_Nw.Started = TRUE;
    g_Nw.Stats.Switch1 = Rd(g_Nw.Va[NWAU_RES_MCA_SWITCH1], MCA_SWITCH1_STRIDE * g_Nw.Hdr.mca_cluster);
    g_Nw.Stats.TxCarveout = Rd(ADMAC_BASE, ADMAC_CHAN_SRAM(g_Nw.Hdr.admac_chan));
    if (g_Nw.Sense) {
        g_Nw.Stats.RxCarveout = Rd(ADMAC_BASE, ADMAC_CHAN_SRAM(g_Nw.Hdr.sense_chan));
        g_Nw.Stats.RxConf = Rd(g_Nw.McaCl, MCA_RXB_CONF);
    }
    g_Nw.GainShadow = NWAU_AMP_GAIN_UNKNOWN;           /* until read back below */
    g_Nw.VolShadow = 0;
    DmaDisable();
    SerdesSet(FALSE);
    RxStop();
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    AmpOff(FALSE);
    /* amp unreachable or not in shutdown: do not start (no further hardware access);
       the launcher left the gain at 0x00: make sure (allowed in any state) */
    if (g_Nw.Stats.AmpState != NWAU_AMP_PWR_SHUTDOWN || !AmpGainSet(NWAU_AMP_GAIN_STATIC)) {
        KeAcquireSpinLock(&g_Nw.Lock, &irql);
        g_Nw.Started = FALSE;
        KeReleaseSpinLock(&g_Nw.Lock, irql);
        ExReleaseFastMutex(&g_Nw.AmpMutex);
        rc = NWAU_EAMP_NOT_OFF;
        goto Fail;
    }
    (VOID)AmpSetVolume();
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    g_Nw.State = NWAU_ST_IDLE;
    g_Nw.StartStatus = NWAU_OK;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    ExReleaseFastMutex(&g_Nw.AmpMutex);
    NwAuWriteRegistry(TRUE);
    return STATUS_SUCCESS;

Fail:
    g_Nw.StartStatus = rc;
    g_Nw.State = NWAU_ST_DOWN;
    g_Nw.AmpWorkStop = TRUE;
    NwAuWriteRegistry(TRUE);
    if (g_Nw.Timer != NULL) {
        ExDeleteTimer(g_Nw.Timer, TRUE, TRUE, NULL);
        g_Nw.Timer = NULL;
    }
    FreeWorkItems();
    UnmapAll();
    status = (rc == NWAU_EHDR_NOT_READY) ? STATUS_DEVICE_NOT_READY
           : (rc == NWAU_ENO_MEMORY)     ? STATUS_INSUFFICIENT_RESOURCES
           : STATUS_DEVICE_CONFIGURATION_ERROR;
    return status;
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuShutdown(VOID)
{
    KIRQL irql;
    BOOLEAN was;

    if (g_Nw.Timer == NULL && !g_Nw.Started) {
        return;
    }
    if (g_Nw.Timer != NULL) {
        ExDeleteTimer(g_Nw.Timer, TRUE, TRUE, NULL);    /* waits for a running callback */
        g_Nw.Timer = NULL;
    }
    /* no new amp jobs; a running one finishes (a pending fault is handled below) */
    g_Nw.AmpWorkStop = TRUE;
    (VOID)KeWaitForSingleObject(&g_Nw.AmpWorkIdle, Executive, KernelMode, FALSE, NULL);
    ExAcquireFastMutex(&g_Nw.AmpMutex);
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    was = g_Nw.Started;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    if (was) {
        SafeOffPassive(NWAU_STOP_SHUTDOWN, NWAU_ST_DOWN);
    }
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    g_Nw.Started = FALSE;
    g_Nw.State = NWAU_ST_DOWN;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    ExReleaseFastMutex(&g_Nw.AmpMutex);
    (VOID)KeWaitForSingleObject(&g_Nw.AmpWorkIdle, Executive, KernelMode, FALSE, NULL);
    (VOID)KeWaitForSingleObject(&g_Nw.WorkIdle, Executive, KernelMode, FALSE, NULL);
    if (was) {
        NwAuWriteRegistry(TRUE);
    }
    FreeWorkItems();
    UnmapAll();
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuPowerDown(VOID)
{
    KIRQL irql;
    BOOLEAN started;

    if (g_Nw.Timer != NULL) {
        (VOID)ExCancelTimer(g_Nw.Timer, NULL);
    }
    ExAcquireFastMutex(&g_Nw.AmpMutex);
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    started = g_Nw.Started;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    if (!started) {
        ExReleaseFastMutex(&g_Nw.AmpMutex);
        return;
    }
    /* always: mute -> shutdown, gain 0x00, DMA off, serdes off (also when already idle) */
    SafeOffPassive(NWAU_STOP_POWER, NWAU_ST_OFF);
    ExReleaseFastMutex(&g_Nw.AmpMutex);
    NwAuWriteRegistry(TRUE);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuPowerUp(VOID)
{
    KIRQL irql;

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    if (g_Nw.Started && g_Nw.State == NWAU_ST_OFF) {
        g_Nw.State = NWAU_ST_IDLE;     /* hardware is re-checked and re-configured by StreamRun */
    }
    KeReleaseSpinLock(&g_Nw.Lock, irql);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuStreamRun(VOID)
{
    KIRQL irql;
    ULONG i;
    ULONG active, target;
    ULONG rc = NWAU_OK;
    BOOLEAN sns;

    ExAcquireFastMutex(&g_Nw.AmpMutex);
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    if (!g_Nw.Started || (g_Nw.State != NWAU_ST_IDLE && g_Nw.State != NWAU_ST_FAULT)) {
        KeReleaseSpinLock(&g_Nw.Lock, irql);
        ExReleaseFastMutex(&g_Nw.AmpMutex);
        return;
    }
    g_Nw.EvtBaseReal = RealNowUs();
    g_Nw.EvtBaseGuest = KeQueryInterruptTime();
    EvtLocked(NWAU_EV_START, 0, 0);
    /* 0. the launcher left the clocks running; if they are gone (power lost), refuse */
    if (!ClocksRunning()) {
        KeReleaseSpinLock(&g_Nw.Lock, irql);
        rc = NWAU_ERUN_CLOCKS;
        goto Refuse;
    }
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    /* 1. amp: configure + volume + gain while it is still in software shutdown, verify */
    if (!AmpConfigure()) {
        rc = NWAU_ERUN_AMP_CONFIG;
        goto Refuse;
    }
    /* 2. DMA: reset channel, queue silence, enable (beep_tone order: submit, enable, submit more) */
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    DmaReset();
    nwau_ring_reset(&g_Nw.RingState);
    nwau_conv_reset(&g_Nw.Conv);
    RtlZeroMemory(g_Nw.TxChunkE, sizeof(g_Nw.TxChunkE));
    g_Nw.DescId = 0;
    for (i = 0; i < NWAU_PREFILL_CHUNKS; i++) {
        nwu32 off = 0, padOff = 0, padLen = 0;
        if (!DmaCanSubmit() ||
            nwau_ring_next(&g_Nw.RingState, 1, &off, &padOff, &padLen) == NWAU_NEXT_NONE) {
            break;
        }
        RingZero(padOff, padLen);
        DmaSubmit(off);
        if (i == 0) {
            DmaEnable();
        }
    }
    if (!g_Nw.DmaOn || nwau_ring_inflight(&g_Nw.RingState) == 0) {
        KeReleaseSpinLock(&g_Nw.Lock, irql);
        rc = NWAU_ERUN_DMA;
        goto Refuse;
    }
    /* 2b. V/I sense capture (never refuses playback; a failure means static mode) */
    if (g_Nw.Sense) {
        RxStart();
    }
    /* 3. serdes on; from here the service keeps the queue filled with silence */
    SerdesSet(TRUE);
    g_Nw.State = NWAU_ST_STARTING;
    sns = g_Nw.Sense && g_Nw.SenseCfgOk;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    /* 4. amp active (the gate re-checks serdes EN, DMA on and queued data).
       With the sense path the sense bits are cleared while still in shutdown
       (0x0e -> 0x02), then active with sense on (0x00). */
    /* click-free power-up: digital volume at -100 dB, silence queued, then the
       amp leaves shutdown; the volume is ramped up afterwards (still silence) */
    target = CurrentVolumeReg();
    if (!AmpSetVolumeReg(NWAU_VOL_REG_MAX)) {
        rc = NWAU_ERUN_AMP_CONFIG;
        goto Refuse;
    }
    active = NWAU_AMP_PWR_ACTIVE;
    if (sns) {
        if (!AmpWrite1(NWAU_AMP_REG_PWR_CTRL, NWAU_AMP_PWR_SHDN_SNS)) {
            rc = NWAU_ERUN_AMP_ON;
            goto Refuse;
        }
        active = NWAU_AMP_PWR_ACTIVE_SNS;
    }
    if (!AmpWrite1(NWAU_AMP_REG_PWR_CTRL, (UCHAR)active)) {
        rc = NWAU_ERUN_AMP_ON;
        goto Refuse;
    }
    g_Nw.Stats.AmpState = AmpRead1(NWAU_AMP_REG_PWR_CTRL);
    if (g_Nw.Stats.AmpState != active) {
        g_Nw.Stats.AmpVerifyFails++;
        rc = NWAU_ERUN_AMP_STATE;
        goto Refuse;
    }
    KeStallExecutionProcessor(2000);                    /* let the output stage settle on silence */
    if (!AmpVolumeRamp(target, 22, 600)) {
        rc = NWAU_ERUN_AMP_CONFIG;
        goto Refuse;
    }
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    if (g_Nw.State != NWAU_ST_STARTING) {
        /* the feed saw a fault meanwhile (DISPATCH): the worker turns the amp off */
        KeReleaseSpinLock(&g_Nw.Lock, irql);
        ExReleaseFastMutex(&g_Nw.AmpMutex);
        return;
    }
    g_Nw.LastPush = g_Nw.LastService = KeQueryInterruptTime();
    g_Nw.LastSenseBlock = g_Nw.LastRegWrite = g_Nw.LastService;
    g_Nw.LastServiceBySrc[0] = g_Nw.LastServiceBySrc[1] = 0;
    g_Nw.LastReal = RealNowUs();
    g_Nw.StreamStartGuest = g_Nw.LastService;
    g_Nw.StreamStartReal = g_Nw.LastReal;
    g_Nw.StreamDries = 0;
    g_Nw.Stats.MinQueueUs = 0xffffffffu;
    g_Nw.Stats.MinInflight = 0xffffffffu;
    g_Nw.State = NWAU_ST_PLAYING;
    g_Nw.Stats.Starts++;
    g_Nw.Stats.LastRunStatus = NWAU_OK;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    ExReleaseFastMutex(&g_Nw.AmpMutex);
    if (g_Nw.Timer != NULL) {
        (VOID)ExSetTimer(g_Nw.Timer, -NWAU_TIMER_PERIOD_100NS, NWAU_TIMER_PERIOD_100NS, NULL);
    }
    /* volatile key only: a stable write would make the hive flush a few seconds
       later, i.e. in the middle of this stream (disk I/O stops the guest) */
    NwAuWriteRegistry(FALSE);
    return;

Refuse:
    SafeOffPassive(NWAU_STOP_AMP_VERIFY, NWAU_ST_FAULT);
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    g_Nw.Stats.StartFails++;
    g_Nw.Stats.LastRunStatus = rc;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    ExReleaseFastMutex(&g_Nw.AmpMutex);
    NwAuWriteRegistry(TRUE);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuStreamPause(VOID)
{
    KIRQL irql;
    LONG st;
    BOOLEAN stopped = FALSE;

    if (g_Nw.Timer != NULL) {
        (VOID)ExCancelTimer(g_Nw.Timer, NULL);
    }
    ExAcquireFastMutex(&g_Nw.AmpMutex);
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    st = g_Nw.Started ? g_Nw.State : NWAU_ST_DOWN;
    if (st == NWAU_ST_FAULT) {
        g_Nw.State = NWAU_ST_IDLE;
        stopped = TRUE;
    }
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    if (st == NWAU_ST_PLAYING || st == NWAU_ST_FAULTING) {
        SafeOffPassive(st == NWAU_ST_FAULTING ? g_Nw.FaultReason : NWAU_STOP_STREAM, NWAU_ST_IDLE);
        stopped = TRUE;
    }
    ExReleaseFastMutex(&g_Nw.AmpMutex);
    if (stopped) {
        NwAuWriteRegistry(TRUE);
    }
}

/* Windows volume / mute: PASSIVE, I2C under AmpMutex, never under the spin lock */
static VOID VolumeChanged(VOID)
{
    ExAcquireFastMutex(&g_Nw.AmpMutex);
    AmpPolicy();
    ExReleaseFastMutex(&g_Nw.AmpMutex);
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuSetVolumeLevel(_In_ ULONG Channel, _In_ LONG Level16_16)
{
    KIRQL irql;
    BOOLEAN started;

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    if (Channel < 2) {
        g_Nw.VolCh[Channel] = Level16_16;
    } else {
        g_Nw.VolCh[0] = g_Nw.VolCh[1] = Level16_16;
    }
    /* the amp is mono: the louder channel (a balance setting does not mute it) */
    g_Nw.VolumeLevel = g_Nw.VolCh[0] > g_Nw.VolCh[1] ? g_Nw.VolCh[0] : g_Nw.VolCh[1];
    g_Nw.Stats.VolSets++;
    g_Nw.Stats.LastVolLevel = Level16_16;
    g_Nw.Stats.LastVolChannel = Channel;
    started = g_Nw.Started;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    if (started) {
        VolumeChanged();
    }
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID NwAuSetMute(_In_ ULONG Channel, _In_ BOOL Mute)
{
    KIRQL irql;
    BOOLEAN started;

    UNREFERENCED_PARAMETER(Channel);
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    g_Nw.Muted = Mute ? TRUE : FALSE;
    g_Nw.Stats.VolSets++;
    started = g_Nw.Started;
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    if (started) {
        VolumeChanged();
    }
}

/* ---- data path (<= DISPATCH_LEVEL) ---------------------------------------- */
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID NwAuRenderPush(_In_reads_bytes_(Bytes) const BYTE *Pcm, _In_ ULONG Bytes)
{
    KIRQL irql;
    ULONG used = 0;

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    if (!g_Nw.Started || g_Nw.State != NWAU_ST_PLAYING) {
        KeReleaseSpinLock(&g_Nw.Lock, irql);
        return;
    }
    g_Nw.LastPush = KeQueryInterruptTime();
    g_Nw.Stats.PushBytes += Bytes;
    {
        /* More than the ring can take happens only after the hardware ran dry
           (the position follows the hardware clock): keep the newest audio,
           drop the oldest, once, instead of cutting the tail of every push. */
        nwu32 room = nwau_ring_write_space(&g_Nw.RingState) & ~3u;
        if (Bytes > room) {
            ULONG excess = (Bytes - room) & ~3u;
            nwau_conv_skip(&g_Nw.Conv, excess);
            used = excess;
            g_Nw.Stats.DropBytes += excess;
            EvtLocked(NWAU_EV_DROP, 0, excess);
        }
    }
    while (used < Bytes) {
        nwu32 off = 0;
        nwu32 span = nwau_ring_write_span(&g_Nw.RingState, &off);
        nwu32 consumed = 0;
        nwu32 words;

        if (span == 0) {
            /* ring lead is full (OS clock ahead of the audio clock): drop */
            nwau_conv_skip(&g_Nw.Conv, Bytes - used);
            g_Nw.Stats.DropBytes += Bytes - used;
            EvtLocked(NWAU_EV_DROP, 1, Bytes - used);
            break;
        }
        {
            /* keep each conversion inside one ring chunk so its energy is per chunk */
            nwu32 inChunk = off % NWAU_CHUNK_BYTES;
            nwu32 idx = (off / NWAU_CHUNK_BYTES) % NWAU_MAX_TX_CHUNKS;
            nwu64 e = 0;
            if (span > NWAU_CHUNK_BYTES - inChunk) {
                span = NWAU_CHUNK_BYTES - inChunk;
            }
            if (inChunk == 0) {
                g_Nw.TxChunkE[idx] = 0;                 /* new lap of this chunk */
            }
            words = nwau_conv_run_e(&g_Nw.Conv, Pcm + used, Bytes - used,
                                    g_Nw.Ring + off / 4u, span / 4u, &consumed, &e);
            g_Nw.TxChunkE[idx] += e;
        }
        nwau_ring_commit(&g_Nw.RingState, words * 4u);
        used += consumed;
        if (consumed == 0 && words == 0) {
            break;      /* defensive: no progress */
        }
    }
    KeReleaseSpinLock(&g_Nw.Lock, irql);
}


_IRQL_requires_max_(DISPATCH_LEVEL)
VOID NwAuService(VOID)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    ServiceLocked(NWAU_SRC_RENDER);
    KeReleaseSpinLock(&g_Nw.Lock, irql);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
ULONGLONG NwAuRealTime100ns(VOID)
{
    KIRQL irql;
    ULONGLONG t = 0;

    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    if (g_Nw.Started && ADMAC_BASE != NULL) {
        t = RealNowUs() * 10u;
    }
    KeReleaseSpinLock(&g_Nw.Lock, irql);
    return t;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID NwAuStreamStarved(VOID)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_Nw.Lock, &irql);
    if (g_Nw.Started && g_Nw.State == NWAU_ST_PLAYING) {
        g_Nw.Stats.OsStarved++;
        FaultBeginLocked(NWAU_STOP_OS_STARVED);
        DmaFill(TRUE);
    }
    KeReleaseSpinLock(&g_Nw.Lock, irql);
}
