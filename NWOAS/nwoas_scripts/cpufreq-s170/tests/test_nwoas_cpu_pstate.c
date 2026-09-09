/*
 * Host tests for NwoasCpuPstate.h. Each case runs the *actual* helper against
 * the fake T8103 P-cluster (NwoasCpuPstateHostStub.h). Build success here is
 * evidence the logic executes and branches as intended; it is NOT proof of a
 * good boot on hardware (see README).
 *
 * Compiled against the stub <Base.h> (-Itests/stub) via the same
 * <Library/NwoasCpuPstate.h> path the firmware uses (-IInclude).
 */

#include <stdio.h>

#include "NwoasCpuPstateHostStub.h"

static int  g_failures = 0;
static int  g_checks   = 0;

#define CHECK(cond, msg)                                                        \
  do {                                                                          \
    g_checks++;                                                                 \
    if (!(cond)) {                                                              \
      g_failures++;                                                             \
      printf ("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);               \
    }                                                                           \
  } while (0)

/* Validated P7 baseline: P7 DESIRED fields + protected bits + an unrelated bit. */
#define BASE_P7_FIELDS  0x7007ULL                    /* 7 | (7 << 12) */
#define UNRELATED_BIT   (1ULL << 20)                 /* CLUSTER_PSTATE_UNK_M1 */
#define BASELINE_CMD                                                            \
  (BASE_P7_FIELDS | NWOAS_M1_APSC_DIS | NWOAS_FIXED_FREQ_PLL_RECLOCK | UNRELATED_BIT)
#define BASELINE_STATUS 0x77ULL

/* 1. Successful P7 -> P12 transition. */
static void
test_success (void)
{
  NWOAS_FAKE_DEV  d;
  NWOAS_CPU_PSTATE_IO  io;
  NWOAS_CPU_PSTATE_RESULT  r;
  NWOAS_PSTATE_STATUS  st;

  printf ("test_success\n");
  nwoas_fake_init (&d, BASELINE_CMD, BASELINE_STATUS);
  d.busy_on_write = 3; /* clears after a few polls */
  io = nwoas_fake_io (&d, NWOAS_T8103_CHIP_ID);

  st = nwoas_cpu_pstate_enable_p12 (&io, &r);

  CHECK (st == NwoasPstateOk, "overall status Ok");
  CHECK (r.initial_status == NwoasPstateOk, "no P12 failure recorded");
  CHECK (!r.rollback_attempted, "no rollback attempted on success");
  CHECK ((r.final_cmd & NWOAS_PS_FIELDS) == nwoas_cpu_pstate_desired (NWOAS_PSTATE_P12),
         "command DESIRED reads back P12");
  CHECK (r.final_current == NWOAS_PSTATE_P12, "STATUS current is P12");
  CHECK (r.final_target == NWOAS_PSTATE_P12, "STATUS target is P12");
  CHECK (r.protected_preserved, "protected bits preserved");
  CHECK (d.cmd_writes == 1, "exactly one command write");
}

/* 2. Controller already BUSY before any request: no write, explicit status. */
static void
test_initially_busy (void)
{
  NWOAS_FAKE_DEV  d;
  NWOAS_CPU_PSTATE_IO  io;
  NWOAS_CPU_PSTATE_RESULT  r;
  NWOAS_PSTATE_STATUS  st;

  printf ("test_initially_busy\n");
  nwoas_fake_init (&d, BASELINE_CMD, BASELINE_STATUS);
  d.busy_remaining = 1; /* first (baseline) read reports BUSY */
  io = nwoas_fake_io (&d, NWOAS_T8103_CHIP_ID);

  st = nwoas_cpu_pstate_enable_p12 (&io, &r);

  CHECK (st == NwoasPstateBaselineBusy, "overall status BaselineBusy");
  CHECK (d.cmd_writes == 0, "no command written when initially busy");
  CHECK ((r.baseline_cmd & NWOAS_PSTATE_BUSY) != 0, "snapshot shows BUSY baseline");
  CHECK (!r.rollback_attempted, "no rollback for a busy baseline");
}

/* 3. Request accepted then controller sticks BUSY forever: the rollback's
 *    pre-write idle wait times out, so P7 is NOT written and we report
 *    RollbackBusy -- never claiming restoration while busy. */
static void
test_request_stuck_busy_skips_rollback_write (void)
{
  NWOAS_FAKE_DEV  d;
  NWOAS_CPU_PSTATE_IO  io;
  NWOAS_CPU_PSTATE_RESULT  r;
  NWOAS_PSTATE_STATUS  st;

  printf ("test_request_stuck_busy_skips_rollback_write\n");
  nwoas_fake_init (&d, BASELINE_CMD, BASELINE_STATUS);
  d.stuck_busy = TRUE; /* every write pins BUSY beyond 250 polls */
  io = nwoas_fake_io (&d, NWOAS_T8103_CHIP_ID);

  st = nwoas_cpu_pstate_enable_p12 (&io, &r);

  CHECK (st == NwoasPstateRollbackBusy, "overall status RollbackBusy");
  CHECK (r.initial_status == NwoasPstateStuckBusy, "P12 failed StuckBusy");
  CHECK (r.rollback_status == NwoasPstateStuckBusy, "rollback saw StuckBusy");
  CHECK (r.rollback_attempted, "rollback was attempted");
  CHECK (!r.rollback_write_issued, "P7 was NOT written (skipped while busy)");
  CHECK (d.cmd_writes == 1, "only the P12 trigger write");
  CHECK (d.delay_calls >= (int)NWOAS_IDLE_ATTEMPTS, "idle poll was bounded and exhausted");
  CHECK (st != NwoasPstateRolledBack, "never reports RolledBack while busy");
}

/* 1b (defect 1). P12 fails idle (readback mismatch); the P7 rollback IS written
 *     but then sticks BUSY, so the post-write wait times out: the write was
 *     issued but cannot be verified -> RollbackUnverified (distinct from the
 *     skipped-busy case above). */
static void
test_rollback_write_issued_then_busy (void)
{
  NWOAS_FAKE_DEV  d;
  NWOAS_CPU_PSTATE_IO  io;
  NWOAS_CPU_PSTATE_RESULT  r;
  NWOAS_PSTATE_STATUS  st;

  printf ("test_rollback_write_issued_then_busy\n");
  nwoas_fake_init (&d, BASELINE_CMD, BASELINE_STATUS);
  d.busy_on_write  = 2;
  d.reject_pstate  = (int)NWOAS_PSTATE_P12; /* P12 ignored -> idle readback mismatch */
  d.sticky_pstate  = (int)NWOAS_PSTATE_P7;  /* the accepted P7 write pins BUSY */
  io = nwoas_fake_io (&d, NWOAS_T8103_CHIP_ID);

  st = nwoas_cpu_pstate_enable_p12 (&io, &r);

  CHECK (st == NwoasPstateRollbackUnverified, "overall status RollbackUnverified");
  CHECK (r.initial_status == NwoasPstateReadbackMismatch, "P12 failed ReadbackMismatch");
  CHECK (r.rollback_status == NwoasPstateStuckBusy, "rollback timed out after write");
  CHECK (r.rollback_write_issued, "P7 WAS written");
  CHECK (d.cmd_writes == 2, "P12 attempt then P7 write");
  CHECK (st != NwoasPstateRolledBack, "never claims RolledBack when unverified");
}

/* 4 (defect 2). Idle controller but wrong readback; the bounded P7 rollback is
 *    accepted and STATUS returns to 7/7 -> RolledBack. */
static void
test_idle_wrong_readback_rollback (void)
{
  NWOAS_FAKE_DEV  d;
  NWOAS_CPU_PSTATE_IO  io;
  NWOAS_CPU_PSTATE_RESULT  r;
  NWOAS_PSTATE_STATUS  st;

  printf ("test_idle_wrong_readback_rollback\n");
  nwoas_fake_init (&d, BASELINE_CMD, BASELINE_STATUS);
  d.busy_on_write = 2;
  d.reject_pstate = (int)NWOAS_PSTATE_P12; /* P12 write ignored -> stays P7 */
  io = nwoas_fake_io (&d, NWOAS_T8103_CHIP_ID);

  st = nwoas_cpu_pstate_enable_p12 (&io, &r);

  CHECK (st == NwoasPstateRolledBack, "overall status RolledBack");
  CHECK (r.initial_status == NwoasPstateReadbackMismatch, "P12 failed ReadbackMismatch");
  CHECK (r.rollback_status == NwoasPstateOk, "P7 command restore reported Ok");
  CHECK (r.final_current == NWOAS_PSTATE_P7, "STATUS current back at P7");
  CHECK (r.final_target == NWOAS_PSTATE_P7, "STATUS target back at P7");
  CHECK ((r.final_cmd & NWOAS_PS_FIELDS) == nwoas_cpu_pstate_desired (NWOAS_PSTATE_P7),
         "command back at P7 fields");
  CHECK (r.protected_preserved, "protected bits preserved through rollback");
  CHECK (d.cmd_writes == 2, "P12 attempt then P7 restore");
}

/* 4b (defect 2). Rollback command is accepted (fields match P7) but STATUS never
 *     reaches 7/7: must NOT be reported as RolledBack -> RollbackFailed. */
static void
test_rollback_command_ok_but_status_not_p7 (void)
{
  NWOAS_FAKE_DEV  d;
  NWOAS_CPU_PSTATE_IO  io;
  NWOAS_CPU_PSTATE_RESULT  r;
  NWOAS_PSTATE_STATUS  st;

  printf ("test_rollback_command_ok_but_status_not_p7\n");
  /* STATUS is frozen at 0x99 (current 9): the P7 command settles but the point
   * never actually returns to 7. */
  nwoas_fake_init (&d, BASELINE_CMD, 0x99ULL);
  d.busy_on_write  = 1;
  d.reject_pstate  = (int)NWOAS_PSTATE_P12; /* P12 fails idle readback mismatch */
  d.freeze_status  = TRUE;                  /* STATUS never updates */
  io = nwoas_fake_io (&d, NWOAS_T8103_CHIP_ID);

  st = nwoas_cpu_pstate_enable_p12 (&io, &r);

  CHECK (st == NwoasPstateRollbackFailed, "overall status RollbackFailed");
  CHECK (r.initial_status == NwoasPstateReadbackMismatch, "P12 failed ReadbackMismatch");
  CHECK (r.rollback_status == NwoasPstateOk, "P7 command matched, but...");
  CHECK (r.final_current != NWOAS_PSTATE_P7, "STATUS current is not P7");
  CHECK (st != NwoasPstateRolledBack, "not claimed as RolledBack when STATUS wrong");
}

/* 5. Unrelated feature/control bits outside DESIRED are preserved verbatim. */
static void
test_preserves_unrelated_bits (void)
{
  NWOAS_FAKE_DEV  d;
  NWOAS_CPU_PSTATE_IO  io;
  NWOAS_CPU_PSTATE_RESULT  r;
  NWOAS_PSTATE_STATUS  st;
  UINT64  preserved_mask;

  printf ("test_preserves_unrelated_bits\n");
  nwoas_fake_init (&d, BASELINE_CMD, BASELINE_STATUS);
  d.busy_on_write = 1;
  io = nwoas_fake_io (&d, NWOAS_T8103_CHIP_ID);

  st = nwoas_cpu_pstate_enable_p12 (&io, &r);

  CHECK (st == NwoasPstateOk, "transition succeeded");
  /* Everything except the DESIRED fields and the SET trigger bit is identical
   * to the baseline, proving no unrelated bit was disturbed. */
  preserved_mask = ~(NWOAS_PS_FIELDS | NWOAS_PSTATE_SET);
  CHECK ((r.final_cmd & preserved_mask) == (BASELINE_CMD & preserved_mask),
         "all non-DESIRED/non-SET bits preserved");
  CHECK ((r.final_cmd & NWOAS_M1_APSC_DIS) != 0, "APSC-disable bit still set");
  CHECK ((r.final_cmd & NWOAS_FIXED_FREQ_PLL_RECLOCK) != 0, "fixed-PLL relock bit still set");
  CHECK ((r.final_cmd & UNRELATED_BIT) != 0, "unrelated UNK_M1 bit still set");
}

/* 6. Failure status reporting: a non-P7 baseline yields an explicit status and a
 *    populated snapshot, with no writes; plus non-T8103 and NULL/incomplete Io. */
static void
test_failure_status_reporting (void)
{
  NWOAS_FAKE_DEV  d, d2;
  NWOAS_CPU_PSTATE_IO  io, io2;
  NWOAS_CPU_PSTATE_RESULT  r, r2;
  NWOAS_PSTATE_STATUS  st, st2;
  UINT64  p5_baseline;

  printf ("test_failure_status_reporting\n");
  /* Baseline sitting at P5 rather than the required P7. */
  p5_baseline = (5ULL | (5ULL << 12)) | NWOAS_M1_APSC_DIS | NWOAS_FIXED_FREQ_PLL_RECLOCK;
  nwoas_fake_init (&d, p5_baseline, 0x55ULL);
  io = nwoas_fake_io (&d, NWOAS_T8103_CHIP_ID);

  st = nwoas_cpu_pstate_enable_p12 (&io, &r);

  CHECK (st == NwoasPstateBaselineNotP7, "explicit BaselineNotP7 status");
  CHECK (d.cmd_writes == 0, "no command written on baseline failure");
  CHECK (r.baseline_cmd == p5_baseline, "snapshot carries the observed baseline");
  CHECK (r.final_status == 0x55ULL, "snapshot carries the observed STATUS");
  CHECK (!r.rollback_attempted, "no rollback for a bad baseline");

  /* Non-T8103 chip is rejected outright and inertly (no MMIO). */
  nwoas_fake_init (&d2, BASELINE_CMD, BASELINE_STATUS);
  io2 = nwoas_fake_io (&d2, 0x6000u /* T6000, not T8103 */);
  st2 = nwoas_cpu_pstate_enable_p12 (&io2, &r2);
  CHECK (st2 == NwoasPstateUnsupportedChip, "non-T8103 rejected");
  CHECK ((d2.cmd_reads == 0) && (d2.cmd_writes == 0), "no MMIO on unsupported chip");
}

/* 4 (defect 4). NULL / incomplete Io is rejected before any dereference. */
static void
test_invalid_io (void)
{
  NWOAS_CPU_PSTATE_IO  io_no_read;
  NWOAS_CPU_PSTATE_IO  io_no_write;
  NWOAS_CPU_PSTATE_RESULT  r;
  NWOAS_PSTATE_STATUS  st;

  printf ("test_invalid_io\n");

  /* NULL interface pointer. */
  st = nwoas_cpu_pstate_enable_p12 (NULL, &r);
  CHECK (st == NwoasPstateInvalidIo, "NULL io rejected");
  CHECK (r.status == NwoasPstateInvalidIo, "snapshot records InvalidIo");

  /* Missing read64. */
  io_no_read.read64   = NULL;
  io_no_read.write64  = nwoas_fake_write64;
  io_no_read.delay_us = nwoas_fake_delay_us;
  io_no_read.ctx      = NULL;
  io_no_read.chip_id  = NWOAS_T8103_CHIP_ID;
  st = nwoas_cpu_pstate_enable_p12 (&io_no_read, &r);
  CHECK (st == NwoasPstateInvalidIo, "missing read64 rejected");

  /* Missing write64. */
  io_no_write.read64   = nwoas_fake_read64;
  io_no_write.write64  = NULL;
  io_no_write.delay_us = nwoas_fake_delay_us;
  io_no_write.ctx      = NULL;
  io_no_write.chip_id  = NWOAS_T8103_CHIP_ID;
  st = nwoas_cpu_pstate_enable_p12 (&io_no_write, &r);
  CHECK (st == NwoasPstateInvalidIo, "missing write64 rejected");

  /* A NULL Result pointer must be tolerated (no crash, status still returned). */
  st = nwoas_cpu_pstate_enable_p12 (NULL, NULL);
  CHECK (st == NwoasPstateInvalidIo, "NULL result tolerated");
}

/* 3 (defect 3). STATUS changes between successive reads: the helper must
 *    validate exactly the single value it snapshots. The device would return
 *    P12 then P7; a single-capture success must report P12 consistently and
 *    read STATUS exactly once. */
static void
test_status_single_capture (void)
{
  NWOAS_FAKE_DEV  d;
  NWOAS_CPU_PSTATE_IO  io;
  NWOAS_CPU_PSTATE_RESULT  r;
  NWOAS_PSTATE_STATUS  st;

  printf ("test_status_single_capture\n");
  nwoas_fake_init (&d, BASELINE_CMD, BASELINE_STATUS);
  d.busy_on_write   = 1;
  /* First STATUS read returns P12/P12; any second read would return P7/P7. */
  d.status_seq[0]   = 0xccULL;
  d.status_seq[1]   = 0x77ULL;
  d.status_seq_len  = 2;
  io = nwoas_fake_io (&d, NWOAS_T8103_CHIP_ID);

  st = nwoas_cpu_pstate_enable_p12 (&io, &r);

  CHECK (st == NwoasPstateOk, "success from the captured P12 STATUS");
  CHECK (d.status_reads == 1, "STATUS read exactly once (single capture)");
  CHECK (r.final_status == 0xccULL, "snapshot holds the validated STATUS value");
  CHECK (r.final_current == NWOAS_PSTATE_P12, "snapshot current matches validation");
  CHECK (r.final_target == NWOAS_PSTATE_P12, "snapshot target matches validation");
}

/* Bonus: command latched but STATUS never reaches P12 (frozen at P7) ->
 * StatusMismatch then a successful P7 rollback (STATUS is 7/7). */
static void
test_status_mismatch_rollback (void)
{
  NWOAS_FAKE_DEV  d;
  NWOAS_CPU_PSTATE_IO  io;
  NWOAS_CPU_PSTATE_RESULT  r;
  NWOAS_PSTATE_STATUS  st;

  printf ("test_status_mismatch_rollback\n");
  nwoas_fake_init (&d, BASELINE_CMD, BASELINE_STATUS);
  d.busy_on_write = 1;
  d.freeze_status = TRUE; /* fields accept P12 but STATUS stays 0x77 */
  io = nwoas_fake_io (&d, NWOAS_T8103_CHIP_ID);

  st = nwoas_cpu_pstate_enable_p12 (&io, &r);

  CHECK (st == NwoasPstateRolledBack, "status mismatch rolls back to P7");
  CHECK (r.initial_status == NwoasPstateStatusMismatch, "P12 failed StatusMismatch");
  CHECK ((r.final_cmd & NWOAS_PS_FIELDS) == nwoas_cpu_pstate_desired (NWOAS_PSTATE_P7),
         "restored to P7 fields");
  CHECK (r.final_current == NWOAS_PSTATE_P7, "STATUS current 7 after rollback");
}

int
main (
  void
  )
{
  test_success ();
  test_initially_busy ();
  test_request_stuck_busy_skips_rollback_write ();
  test_rollback_write_issued_then_busy ();
  test_idle_wrong_readback_rollback ();
  test_rollback_command_ok_but_status_not_p7 ();
  test_preserves_unrelated_bits ();
  test_failure_status_reporting ();
  test_invalid_io ();
  test_status_single_capture ();
  test_status_mismatch_rollback ();

  printf ("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
