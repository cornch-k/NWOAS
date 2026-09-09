/*
 * NwoasCpuPstateHostStub.h - host-only fake T8103 P-cluster for exercising the
 * real NwoasCpuPstate.h code paths. Not for firmware use.
 *
 * The fake models just enough of the command/status registers to reproduce the
 * transition behaviour the helper depends on:
 *   - reads of the command register return BUSY for a configurable number of
 *     polls after a write, then clear (or never clear, for the stuck cases);
 *   - a write updates the DESIRED fields unless the written P-state matches
 *     reject_pstate (simulating the inner-payload reset that keeps the old
 *     operating point), and preserves whatever unrelated bits the helper wrote;
 *   - a write of sticky_pstate pins BUSY (so the post-write idle wait times out
 *     even though the write was accepted -- the "rollback unverified" case);
 *   - STATUS(+0x30) tracks the settled operating point unless freeze_status is
 *     set, and can be driven through an explicit sequence (status_seq) to prove
 *     the helper validates exactly the STATUS value it snapshots.
 *
 * The header is included via the same <Library/...> path the firmware uses, so
 * -IInclude resolves it and -Itests/stub resolves its <Base.h>.
 */

#ifndef NWOAS_CPU_PSTATE_HOST_STUB_H_
#define NWOAS_CPU_PSTATE_HOST_STUB_H_

#include <Library/NwoasCpuPstate.h>

typedef struct {
  UINT64  cmd;              /* command register backing store (settled value) */
  UINT64  status;           /* STATUS(+0x30) backing store (when no sequence) */
  int     busy_remaining;   /* reads of cmd that still report BUSY */
  int     busy_on_write;    /* busy_remaining loaded on each accepted write */
  BOOLEAN stuck_busy;       /* if set, every write pins BUSY far beyond 250 polls */
  int     reject_pstate;    /* a request for this P-state keeps the old fields (-1 = none) */
  int     sticky_pstate;    /* an accepted write of this P-state pins BUSY (-1 = none) */
  BOOLEAN freeze_status;    /* if set, STATUS is not updated on a settle */

  UINT64  status_seq[4];    /* optional explicit STATUS read sequence */
  int     status_seq_len;   /* 0 = use `status`; else return successive seq entries */
  int     status_idx;       /* next sequence entry (clamps at the last) */

  /* Instrumentation. */
  int     cmd_reads;
  int     cmd_writes;
  int     status_reads;
  int     delay_calls;
  UINT64  last_write;
} NWOAS_FAKE_DEV;

static inline VOID
nwoas_fake_init (
  NWOAS_FAKE_DEV  *Dev,
  UINT64          Cmd,
  UINT64          Status
  )
{
  NWOAS_FAKE_DEV  Zero = {0};

  *Dev              = Zero;
  Dev->cmd          = Cmd;
  Dev->status       = Status;
  Dev->reject_pstate = -1;
  Dev->sticky_pstate = -1;
}

static inline UINT64
nwoas_fake_read64 (
  VOID    *Ctx,
  UINT64  Addr
  )
{
  NWOAS_FAKE_DEV  *Dev = (NWOAS_FAKE_DEV *)Ctx;

  if (Addr == NWOAS_P_CMD_ADDR) {
    Dev->cmd_reads++;
    if (Dev->busy_remaining > 0) {
      Dev->busy_remaining--;
      return Dev->cmd | NWOAS_PSTATE_BUSY;
    }
    return Dev->cmd; /* BUSY clear */
  }
  if (Addr == NWOAS_P_STATUS_ADDR) {
    Dev->status_reads++;
    if (Dev->status_seq_len > 0) {
      int  Pick = (Dev->status_idx < Dev->status_seq_len) ? Dev->status_idx
                                                          : (Dev->status_seq_len - 1);
      Dev->status_idx++;
      return Dev->status_seq[Pick];
    }
    return Dev->status;
  }
  return 0;
}

static inline VOID
nwoas_fake_write64 (
  VOID    *Ctx,
  UINT64  Addr,
  UINT64  Value
  )
{
  NWOAS_FAKE_DEV  *Dev = (NWOAS_FAKE_DEV *)Ctx;
  UINT32          Want;
  BOOLEAN         Accept;

  if (Addr != NWOAS_P_CMD_ADDR) {
    return;
  }
  Dev->cmd_writes++;
  Dev->last_write = Value;

  Want   = (UINT32)(Value & 0x1f); /* DESIRED1 */
  Accept = (BOOLEAN)((Dev->reject_pstate < 0) || ((int)Want != Dev->reject_pstate));
  if (Accept) {
    /* Store verbatim: preserves every unrelated/protected bit the helper kept,
     * exactly as real MMIO would. */
    Dev->cmd = Value;
    if (!Dev->freeze_status) {
      Dev->status = ((UINT64)Want << 4) | (UINT64)Want;
    }
  }
  /* Rejected writes leave Dev->cmd (old fields) and Dev->status unchanged. */

  if (Dev->stuck_busy) {
    Dev->busy_remaining = 1000000;
  } else if (Accept && (Dev->sticky_pstate >= 0) && ((int)Want == Dev->sticky_pstate)) {
    Dev->busy_remaining = 1000000;
  } else {
    Dev->busy_remaining = Dev->busy_on_write;
  }
}

static inline VOID
nwoas_fake_delay_us (
  VOID    *Ctx,
  UINT32  Microseconds
  )
{
  NWOAS_FAKE_DEV  *Dev = (NWOAS_FAKE_DEV *)Ctx;

  (VOID)Microseconds;
  Dev->delay_calls++;
}

/* Wire a fake device into an IO interface with the given chip id. */
static inline NWOAS_CPU_PSTATE_IO
nwoas_fake_io (
  NWOAS_FAKE_DEV  *Dev,
  UINT32          ChipId
  )
{
  NWOAS_CPU_PSTATE_IO  Io;

  Io.read64   = nwoas_fake_read64;
  Io.write64  = nwoas_fake_write64;
  Io.delay_us = nwoas_fake_delay_us;
  Io.ctx      = Dev;
  Io.chip_id  = ChipId;
  return Io;
}

#endif /* NWOAS_CPU_PSTATE_HOST_STUB_H_ */
