/** @file
  NwoasCpuPstate.h - S170 offline UEFI CPU operating-point (P-state) helper.

  Header-only. T8103 (Apple M1) ONLY. This helper requests the *existing*,
  already-supported P12 operating point on the PCPU0 (P-core) cluster after the
  inner m1n1 payload has finished its own initialization (see README, the
  ReadyToBoot integration point). P12 is NOT turbo. The helper does NOT change
  SMC/MCC ownership, does NOT touch thermal-feature (ppt/llc/amx) registers, and
  does NOT alter any voltage table.

  Background (corrected): on hardware the *pre-UEFI* P12 request issued over USB
  RPC was overwritten -- reset back to P7 -- by the inner m1n1 payload's own
  cpufreq initialization (nwoas_scripts/cpufreq-s164/preboot-p12-reverted-p7.json).
  The P12 operating-point gain (~1.53x on the four P-cores, 40/40 workload
  checksums matching) was validated only AFTER Windows had booted, by
  re-applying P12 once the payload init was complete
  (nwoas_scripts/cpufreq-s164/live-p12.json). This helper performs that same
  request at the firmware stage instead, so the setting survives permanently.

  The helper is INERT unless a caller explicitly invokes
  NwoasCpuPstateEnableP12(). Merely including this header does nothing.

  All MMIO and delay operations go through a caller-supplied callback interface
  (NWOAS_CPU_PSTATE_IO) so host tests execute the real code against a fake
  device. Under EDK2 the caller wires read64/write64 to MmioRead64/MmioWrite64
  and delay_us to a MicroSecondDelay wrapper. The header is plain C on top of
  <Base.h> only, so the same file compiles in firmware modules and in the native
  unit test against a stub <Base.h> (see tests/stub/Base.h).

  Copyright (c) 2026, AppleWOA authors. All rights reserved.
  SPDX-License-Identifier: (BSD-2-Clause-Patent OR MIT)
**/

#ifndef NWOAS_CPU_PSTATE_H_
#define NWOAS_CPU_PSTATE_H_

#include <Base.h>

/*
 * Register map. Source: m1n1_windows/src/cpufreq.c
 *   t8103_clusters[] PCPU0 base = 0x211e00000, CLUSTER_PSTATE = 0x20020.
 *   => command register 0x211e20020, status register at +0x30 = 0x211e20050.
 * These literals are T8103-specific and deliberately not derived from ADT here;
 * the caller must confirm chip_id == 0x8103 (enforced below).
 */
#define NWOAS_T8103_CHIP_ID           0x8103u
#define NWOAS_P_CMD_ADDR              0x211e20020ULL
#define NWOAS_P_STATUS_OFFSET         0x30ULL
#define NWOAS_P_STATUS_ADDR           (NWOAS_P_CMD_ADDR + NWOAS_P_STATUS_OFFSET)

/*
 * Command-register fields (T8103). DESIRED1 = GENMASK(4,0), DESIRED2 =
 * GENMASK(15,12); a request programs both to the same P-state, matching
 * set_pstate() and the validated Python PS_FIELDS = 0xf01f.
 */
#define NWOAS_PS_FIELDS               0xf01fULL   /* DESIRED1[4:0] | DESIRED2[15:12] */
#define NWOAS_PSTATE_SET              (1ULL << 25)
#define NWOAS_PSTATE_BUSY             (1ULL << 31)
#define NWOAS_PSTATE_APSC_BUSY        (1ULL << 7)

/*
 * Protected feature bits that must survive a P-state request untouched. These
 * are pre-existing CPU protection/control settings owned by cpufreq_init():
 *   BIT(22) = CLUSTER_PSTATE_M1_APSC_DIS  (APSC disable)
 *   BIT(42) = CLUSTER_PSTATE_FIXED_FREQ_PLL_RECLOCK (fixed-PLL relock)
 * A ReadModifyWrite that only clears NWOAS_PS_FIELDS preserves them; the helper
 * additionally *verifies* they are unchanged and fails loudly if not.
 */
#define NWOAS_M1_APSC_DIS             (1ULL << 22)
#define NWOAS_FIXED_FREQ_PLL_RECLOCK  (1ULL << 42)
#define NWOAS_PROTECTED_BITS          (NWOAS_M1_APSC_DIS | NWOAS_FIXED_FREQ_PLL_RECLOCK)

/* Operating points. P12 is a normal supported OPP; P7 is the APSC/default. */
#define NWOAS_PSTATE_P7               7u
#define NWOAS_PSTATE_P12              12u

/*
 * Bounded idle poll. No RPC transport exists at the firmware stage, so idle is
 * polled directly: NWOAS_IDLE_ATTEMPTS reads, each followed (on a still-BUSY
 * read) by a delay_us(NWOAS_IDLE_DELAY_US). This is an upper attempt bound, not
 * a hardware-latency claim (see README).
 */
#define NWOAS_IDLE_ATTEMPTS           250u
#define NWOAS_IDLE_DELAY_US           1u

/* Encode a P-state into the combined DESIRED1|DESIRED2 field value. */
static inline UINT64
nwoas_cpu_pstate_desired (
  UINT32  Pstate
  )
{
  return ((UINT64)Pstate) | (((UINT64)Pstate) << 12);
}

/* Decode STATUS(+0x30): current = bits[7:4], target = bits[3:0]. */
static inline UINT32
nwoas_cpu_pstate_status_current (
  UINT64  Status
  )
{
  return (UINT32)((Status >> 4) & 0xf);
}

static inline UINT32
nwoas_cpu_pstate_status_target (
  UINT64  Status
  )
{
  return (UINT32)(Status & 0xf);
}

/*
 * Caller-supplied MMIO + delay interface. Every access the helper performs goes
 * through these callbacks; ctx is opaque caller state. chip_id must be the
 * detected SoC id (enforced == 0x8103). read64 and write64 are mandatory;
 * delay_us may be NULL (the poll then simply spins on reads).
 */
typedef struct {
  UINT64 (*read64)(VOID *Ctx, UINT64 Addr);
  VOID   (*write64)(VOID *Ctx, UINT64 Addr, UINT64 Value);
  VOID   (*delay_us)(VOID *Ctx, UINT32 Microseconds);
  VOID   *ctx;
  UINT32  chip_id;
} NWOAS_CPU_PSTATE_IO;

/*
 * Explicit status codes. Every failure path returns one of these; the helper
 * never returns a bare boolean and never claims a rollback happened while the
 * controller is still BUSY.
 */
typedef enum {
  NwoasPstateOk = 0,             /* P12 requested, read back, and STATUS confirms P12/P12 */
  NwoasPstateInvalidIo,          /* io / read64 / write64 callback missing */
  NwoasPstateUnsupportedChip,    /* chip_id != 0x8103 */
  NwoasPstateBaselineBusy,       /* controller BUSY before any request */
  NwoasPstateBaselineNotP7,      /* baseline DESIRED fields are not the P7 baseline */
  NwoasPstateStuckBusy,          /* idle wait exhausted NWOAS_IDLE_ATTEMPTS */
  NwoasPstateReadbackMismatch,   /* idle, but DESIRED readback != requested */
  NwoasPstateProtectedChanged,   /* a protected feature bit changed */
  NwoasPstateStatusMismatch,     /* command accepted but STATUS is not P12/P12 */
  NwoasPstateRolledBack,         /* P12 failed; P7 restored, STATUS 7/7, protected intact */
  NwoasPstateRollbackBusy,       /* P12 failed; controller still BUSY, P7 was NOT written */
  NwoasPstateRollbackUnverified, /* P12 failed; P7 WAS written but idle never returned */
  NwoasPstateRollbackFailed,     /* P12 failed; P7 written and idle, but not verified P7 */
  NwoasPstateStatusMax
} NWOAS_PSTATE_STATUS;

/*
 * Result snapshot. Populated on every return (success or failure) so a caller
 * can log exactly what was observed without re-reading hardware.
 */
typedef struct {
  NWOAS_PSTATE_STATUS  status;          /* overall outcome */
  NWOAS_PSTATE_STATUS  initial_status;  /* why the P12 request failed (Ok if it succeeded) */
  NWOAS_PSTATE_STATUS  rollback_status; /* result of the P7 restore attempt, if any */
  UINT64               baseline_cmd;    /* command register before any request */
  UINT64               requested_cmd;   /* command register after the P12 request settled */
  UINT64               final_cmd;       /* last command register value read */
  UINT64               final_status;    /* STATUS(+0x30) at return */
  UINT32               final_current;   /* STATUS current[7:4] */
  UINT32               final_target;    /* STATUS target[3:0] */
  BOOLEAN              apsc_busy;        /* command BIT(7) at return */
  BOOLEAN              protected_preserved; /* protected bits unchanged vs baseline */
  BOOLEAN              rollback_attempted;  /* a P7 restore was attempted */
  BOOLEAN              rollback_write_issued; /* the rollback actually wrote P7 */
} NWOAS_CPU_PSTATE_RESULT;

/* Short ASCII name for a status, for DEBUG/log lines. Never returns NULL. */
static inline CONST CHAR8 *
nwoas_cpu_pstate_status_name (
  NWOAS_PSTATE_STATUS  Status
  )
{
  switch (Status) {
    case NwoasPstateOk:                 return "ok";
    case NwoasPstateInvalidIo:          return "invalid-io";
    case NwoasPstateUnsupportedChip:    return "unsupported-chip";
    case NwoasPstateBaselineBusy:       return "baseline-busy";
    case NwoasPstateBaselineNotP7:      return "baseline-not-p7";
    case NwoasPstateStuckBusy:          return "stuck-busy";
    case NwoasPstateReadbackMismatch:   return "readback-mismatch";
    case NwoasPstateProtectedChanged:   return "protected-changed";
    case NwoasPstateStatusMismatch:     return "status-mismatch";
    case NwoasPstateRolledBack:         return "rolled-back";
    case NwoasPstateRollbackBusy:       return "rollback-busy-skipped";
    case NwoasPstateRollbackUnverified: return "rollback-unverified";
    case NwoasPstateRollbackFailed:     return "rollback-failed";
    default:                            return "unknown";
  }
}

/*
 * Poll the command register until BUSY clears, bounded by NWOAS_IDLE_ATTEMPTS.
 * Returns TRUE and stores the idle value in *Out on success; returns FALSE and
 * stores the last (still-BUSY) value on timeout. Delays only between reads.
 * The caller has already validated Io and its read64 callback.
 */
static inline BOOLEAN
nwoas_cpu_pstate_wait_idle (
  CONST NWOAS_CPU_PSTATE_IO  *Io,
  UINT64                     *Out
  )
{
  UINT64  Value = 0;
  UINT32  Index;

  for (Index = 0; Index < NWOAS_IDLE_ATTEMPTS; Index++) {
    Value = Io->read64 (Io->ctx, NWOAS_P_CMD_ADDR);
    if ((Value & NWOAS_PSTATE_BUSY) == 0) {
      if (Out != NULL) {
        *Out = Value;
      }
      return TRUE;
    }
    if (Io->delay_us != NULL) {
      Io->delay_us (Io->ctx, NWOAS_IDLE_DELAY_US);
    }
  }
  if (Out != NULL) {
    *Out = Value;
  }
  return FALSE;
}

/*
 * Request a single P-state via ReadModifyWrite, preserving every bit outside
 * NWOAS_PS_FIELDS (including the protected feature bits). Mirrors the validated
 * Python request_state():
 *   - wait for idle first (also applies to rollback callers) so we never write
 *     a new request into a BUSY transition;
 *   - clear only DESIRED, set SET + the desired P-state;
 *   - wait for idle again and verify the DESIRED readback and protected bits.
 *
 * On StuckBusy, *WriteIssued distinguishes the two timeouts: FALSE means the
 * controller was still BUSY *before* any write (nothing was written), TRUE means
 * the write was issued but the post-write idle wait timed out (result unknown).
 * On success *Out holds the settled command value.
 */
static inline NWOAS_PSTATE_STATUS
nwoas_cpu_pstate_request (
  CONST NWOAS_CPU_PSTATE_IO  *Io,
  UINT32                     Pstate,
  UINT64                     Reference,
  UINT64                     *Out,
  BOOLEAN                    *WriteIssued
  )
{
  UINT64  Value;
  UINT64  Desired;
  UINT64  Request;

  if (WriteIssued != NULL) {
    *WriteIssued = FALSE;
  }

  /* Bounded idle BEFORE the write. If still busy, do not write a request. */
  if (!nwoas_cpu_pstate_wait_idle (Io, &Value)) {
    if (Out != NULL) {
      *Out = Value;
    }
    return NwoasPstateStuckBusy; /* nothing written (WriteIssued stays FALSE) */
  }

  Desired = nwoas_cpu_pstate_desired (Pstate);
  Request = (Value & ~NWOAS_PS_FIELDS) | NWOAS_PSTATE_SET | Desired;
  Io->write64 (Io->ctx, NWOAS_P_CMD_ADDR, Request);
  if (WriteIssued != NULL) {
    *WriteIssued = TRUE;
  }

  /* Bounded idle AFTER the write; the transition must complete. */
  if (!nwoas_cpu_pstate_wait_idle (Io, &Value)) {
    if (Out != NULL) {
      *Out = Value;
    }
    return NwoasPstateStuckBusy; /* write issued, outcome unverifiable */
  }
  if (Out != NULL) {
    *Out = Value;
  }

  if ((Value & NWOAS_PS_FIELDS) != Desired) {
    return NwoasPstateReadbackMismatch;
  }
  if (((Value ^ Reference) & NWOAS_PROTECTED_BITS) != 0) {
    return NwoasPstateProtectedChanged;
  }
  return NwoasPstateOk;
}

/*
 * Fill the snapshot tail from an already-captured command value and an
 * already-captured STATUS value. Pure: performs no MMIO, so the value a caller
 * validates is exactly the value it records (see the single-capture rule in
 * NwoasCpuPstateEnableP12).
 */
static inline VOID
nwoas_cpu_pstate_fill (
  NWOAS_CPU_PSTATE_RESULT  *Result,
  UINT64                   Cmd,
  UINT64                   Status
  )
{
  Result->final_cmd     = Cmd;
  Result->final_status  = Status;
  Result->final_current = nwoas_cpu_pstate_status_current (Status);
  Result->final_target  = nwoas_cpu_pstate_status_target (Status);
  Result->apsc_busy     = (BOOLEAN)((Cmd & NWOAS_PSTATE_APSC_BUSY) != 0);
  Result->protected_preserved =
    (BOOLEAN)(((Cmd ^ Result->baseline_cmd) & NWOAS_PROTECTED_BITS) == 0);
}

/*
 * Top-level explicit entry. Requests the existing P12 operating point on the
 * PCPU0 cluster starting from the validated P7 baseline.
 *
 * Success requires, from a SINGLE captured STATUS read, that current == target
 * == P12 and that the protected bits are intact. On any P12 failure it attempts
 * a bounded P7 restoration; a successful rollback likewise requires STATUS 7/7
 * and protected bits intact, else NwoasPstateRollbackFailed. If the controller
 * is still BUSY it never claims restoration:
 *   - BUSY before the P7 write  -> NwoasPstateRollbackBusy (P7 not written)
 *   - P7 written, idle timed out -> NwoasPstateRollbackUnverified
 *
 * Returns the overall status and, if Result != NULL, a full snapshot. A NULL or
 * incomplete Io is rejected with NwoasPstateInvalidIo before any dereference.
 */
static inline NWOAS_PSTATE_STATUS
nwoas_cpu_pstate_enable_p12 (
  CONST NWOAS_CPU_PSTATE_IO  *Io,
  NWOAS_CPU_PSTATE_RESULT    *Result
  )
{
  NWOAS_CPU_PSTATE_RESULT  R = {0};
  UINT64                   Baseline;
  UINT64                   Cmd = 0;
  UINT64                   StatusReg;
  NWOAS_PSTATE_STATUS      St;
  BOOLEAN                  WriteIssued = FALSE;

  R.initial_status  = NwoasPstateOk;
  R.rollback_status = NwoasPstateOk;

  /* Validate the callback interface before touching it. */
  if ((Io == NULL) || (Io->read64 == NULL) || (Io->write64 == NULL)) {
    R.status = NwoasPstateInvalidIo;
    if (Result != NULL) {
      *Result = R;
    }
    return R.status;
  }

  if (Io->chip_id != NWOAS_T8103_CHIP_ID) {
    R.status = NwoasPstateUnsupportedChip;
    if (Result != NULL) {
      *Result = R;
    }
    return R.status;
  }

  Baseline         = Io->read64 (Io->ctx, NWOAS_P_CMD_ADDR);
  R.baseline_cmd   = Baseline;
  R.requested_cmd  = Baseline;

  if ((Baseline & NWOAS_PSTATE_BUSY) != 0) {
    R.status  = NwoasPstateBaselineBusy;
    StatusReg = Io->read64 (Io->ctx, NWOAS_P_STATUS_ADDR);
    nwoas_cpu_pstate_fill (&R, Baseline, StatusReg);
    if (Result != NULL) {
      *Result = R;
    }
    return R.status;
  }
  if ((Baseline & NWOAS_PS_FIELDS) != nwoas_cpu_pstate_desired (NWOAS_PSTATE_P7)) {
    R.status  = NwoasPstateBaselineNotP7;
    StatusReg = Io->read64 (Io->ctx, NWOAS_P_STATUS_ADDR);
    nwoas_cpu_pstate_fill (&R, Baseline, StatusReg);
    if (Result != NULL) {
      *Result = R;
    }
    return R.status;
  }

  /* Request the P12 operating point. */
  St               = nwoas_cpu_pstate_request (Io, NWOAS_PSTATE_P12, Baseline, &Cmd, &WriteIssued);
  R.requested_cmd  = Cmd;
  R.initial_status = St;

  if (St == NwoasPstateOk) {
    /* Single STATUS capture: validate exactly the value we record. */
    StatusReg = Io->read64 (Io->ctx, NWOAS_P_STATUS_ADDR);
    nwoas_cpu_pstate_fill (&R, Cmd, StatusReg);
    if ((R.final_current == NWOAS_PSTATE_P12) &&
        (R.final_target == NWOAS_PSTATE_P12) &&
        R.protected_preserved) {
      R.status = NwoasPstateOk;
      if (Result != NULL) {
        *Result = R;
      }
      return R.status;
    }
    St               = NwoasPstateStatusMismatch;
    R.initial_status = St;
  }

  /* Failure path: bounded P7 restoration, only if the controller is idle. */
  R.rollback_attempted = TRUE;
  {
    UINT64               RollbackCmd  = Cmd;
    BOOLEAN              RollbackWrite = FALSE;
    NWOAS_PSTATE_STATUS  RollbackSt;

    RollbackSt              = nwoas_cpu_pstate_request (Io, NWOAS_PSTATE_P7, Baseline,
                                                        &RollbackCmd, &RollbackWrite);
    R.rollback_status       = RollbackSt;
    R.rollback_write_issued = RollbackWrite;
    StatusReg               = Io->read64 (Io->ctx, NWOAS_P_STATUS_ADDR);
    nwoas_cpu_pstate_fill (&R, RollbackCmd, StatusReg);

    if (RollbackSt == NwoasPstateStuckBusy) {
      /* Distinguish skipped-because-busy from written-but-unverifiable. */
      R.status = RollbackWrite ? NwoasPstateRollbackUnverified : NwoasPstateRollbackBusy;
    } else if (RollbackSt == NwoasPstateOk) {
      /* Require the controller to actually be back at P7 (STATUS 7/7) and the
       * protected bits intact; a matching command field alone is not enough. */
      if ((R.final_current == NWOAS_PSTATE_P7) &&
          (R.final_target == NWOAS_PSTATE_P7) &&
          R.protected_preserved) {
        R.status = NwoasPstateRolledBack;
      } else {
        R.status = NwoasPstateRollbackFailed;
      }
    } else {
      R.status = NwoasPstateRollbackFailed;
    }
  }

  if (Result != NULL) {
    *Result = R;
  }
  return R.status;
}

#endif /* NWOAS_CPU_PSTATE_H_ */
