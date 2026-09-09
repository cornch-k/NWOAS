"""Fake-time tests for display-s190/hpd_wait.py.

Run from anywhere:
    python3 -m unittest /Volumes/X31/NWOAS/nwoas_scripts/display-s190/test_hpd_wait.py -v

No hardware, no real sleeping. The clock only advances when the helper calls
``sleep`` (or when a test explicitly advances it).
"""

from __future__ import annotations

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from hpd_wait import (  # noqa: E402
    DEFAULT_MAX_COUNT,
    MalformedModeCountError,
    validate_mode_count,
    wait_for_mode_counts,
)

READY = (True, 33, 10)          # S158 observed triple on an LG sink
UNPLUGGED = (False, 0, 0)       # S187 first read after "display HPD removed"


class FakeClock:
    def __init__(self, frozen: bool = False):
        self.t = 100.0            # nonzero start catches "assumes start==0" bugs
        self.frozen = frozen
        self.sleeps = []

    def monotonic(self) -> float:
        return self.t

    def sleep(self, dt: float) -> None:
        self.sleeps.append(dt)
        if not self.frozen:
            self.t += dt


class TimedService:
    """Returns the sample whose start time is <= now, from a (t_offset, sample) list."""

    def __init__(self, clock: FakeClock, schedule):
        self.clock = clock
        self.t0 = clock.t
        self.schedule = sorted(schedule)
        self.calls = 0

    def getModeCount(self):
        self.calls += 1
        rel = self.clock.t - self.t0
        current = self.schedule[0][1]
        for t_off, sample in self.schedule:
            if rel >= t_off:
                current = sample
        return current


class SequenceService:
    """Returns samples by attempt index; repeats the last one forever."""

    def __init__(self, samples):
        self.samples = list(samples)
        self.calls = 0

    def getModeCount(self):
        self.calls += 1
        idx = min(self.calls - 1, len(self.samples) - 1)
        return self.samples[idx]


class BlockingService:
    """Each call advances the clock by ``cost`` seconds *inside* the callback
    before returning its sample. Models a getModeCount RPC that blocks; the
    helper cannot interrupt it and must judge the sample by the clock value
    after the call returns. Entries are (cost_s, sample); the last repeats."""

    def __init__(self, clock: FakeClock, entries):
        self.clock = clock
        self.entries = list(entries)
        self.calls = 0

    def getModeCount(self):
        self.calls += 1
        cost, sample = self.entries[min(self.calls - 1, len(self.entries) - 1)]
        self.clock.t += cost
        return sample


def run(clock, service, **kw):
    logs = []
    kw.setdefault("timeout_s", 8.0)
    kw.setdefault("max_attempts", 200)
    kw.setdefault("poll_interval_s", 0.05)
    result = wait_for_mode_counts(
        service.getModeCount, clock.monotonic, clock.sleep, log=logs.append, **kw)
    return result, logs


def state_logs(logs):
    return [l for l in logs if " wait " not in l]


class WaitTests(unittest.TestCase):

    def test_ready_immediately(self):
        clock = FakeClock()
        svc = SequenceService([READY])
        result, logs = run(clock, svc)
        self.assertTrue(result.ready)
        self.assertEqual(result.reason, "ready")
        self.assertEqual(result.attempts, 1)
        self.assertEqual(svc.calls, 1)
        self.assertEqual(clock.sleeps, [])
        self.assertEqual(result.last_sample, READY)
        self.assertEqual(result.elapsed, 0.0)
        self.assertEqual([s for _, s in result.transitions], [READY])
        self.assertEqual(len(state_logs(logs)), 1)

    def test_delayed_ready_after_transient_unplug(self):
        # S187 shape: unplugged for the first 1.2 s, then the sink comes back.
        clock = FakeClock()
        svc = TimedService(clock, [(0.0, UNPLUGGED), (1.2, READY)])
        result, logs = run(clock, svc)
        self.assertTrue(result.ready)
        self.assertEqual(result.reason, "ready")
        self.assertEqual(result.last_sample, READY)
        self.assertGreaterEqual(result.elapsed, 1.2)
        self.assertLess(result.elapsed, 1.3)
        self.assertEqual([s for _, s in result.transitions], [UNPLUGGED, READY])
        # Dozens of identical UNPLUGGED polls must produce exactly two state lines.
        self.assertGreater(svc.calls, 20)
        self.assertEqual(len(state_logs(logs)), 2)
        self.assertIn(str(UNPLUGGED), state_logs(logs)[0])
        self.assertIn(str(READY), state_logs(logs)[1])

    def test_true_false_true_cycle_logs_three_states(self):
        clock = FakeClock()
        # Change-only logging: five polls, three distinct states, three lines.
        svc = SequenceService([UNPLUGGED, UNPLUGGED, (True, 0, 0), (True, 0, 0), READY])
        result, logs = run(clock, svc)
        self.assertTrue(result.ready)
        self.assertEqual([s for _, s in result.transitions], [UNPLUGGED, (True, 0, 0), READY])
        self.assertEqual(len(state_logs(logs)), 3)

    def test_stuck_hpd_times_out(self):
        clock = FakeClock()
        svc = TimedService(clock, [(0.0, (False, 33, 10))])
        result, logs = run(clock, svc, timeout_s=2.0)
        self.assertFalse(result.ready)
        self.assertEqual(result.reason, "timeout")
        self.assertEqual(result.last_sample, (False, 33, 10))
        self.assertAlmostEqual(result.elapsed, 2.0, places=6)
        self.assertLessEqual(clock.t - 100.0, 2.0)            # never slept past deadline
        self.assertEqual(len(state_logs(logs)), 1)             # one state, logged once
        self.assertEqual(len(result.transitions), 1)
        self.assertLess(result.attempts, 200)

    def test_nonzero_hpd_but_zero_counts_is_not_ready(self):
        clock = FakeClock()
        svc = SequenceService([(True, 0, 10)])
        result, _ = run(clock, svc, timeout_s=1.0)
        self.assertFalse(result.ready)
        self.assertEqual(result.reason, "timeout")

    def test_late_ready_past_deadline_is_not_observed(self):
        clock = FakeClock()
        svc = TimedService(clock, [(0.0, UNPLUGGED), (3.0, READY)])
        result, logs = run(clock, svc, timeout_s=2.0)
        self.assertFalse(result.ready)
        self.assertEqual(result.reason, "timeout")
        self.assertEqual(result.last_sample, UNPLUGGED)
        self.assertEqual([s for _, s in result.transitions], [UNPLUGGED])
        self.assertLessEqual(clock.t - 100.0, 2.0)
        self.assertNotIn(str(READY), "\n".join(logs))

    def test_ready_exactly_at_deadline_still_counts(self):
        clock = FakeClock()
        svc = TimedService(clock, [(0.0, UNPLUGGED), (1.0, READY)])
        result, _ = run(clock, svc, timeout_s=1.0, poll_interval_s=0.25)
        # The poll at t=+1.0 happens (deadline reached, not exceeded before sleep).
        self.assertTrue(result.ready)
        self.assertAlmostEqual(result.elapsed, 1.0, places=6)

    # --- callback consumes time (the gap Main found) ---------------------

    def test_callback_blocking_past_deadline_then_ready_is_timeout(self):
        # Poll 1 returns UNPLUGGED quickly. Poll 2 blocks 3 s inside the RPC
        # and then returns READY, with only 2 s budget. Must NOT be accepted.
        clock = FakeClock()
        svc = BlockingService(clock, [(0.1, UNPLUGGED), (3.0, READY)])
        result, logs = run(clock, svc, timeout_s=2.0)
        self.assertFalse(result.ready)
        self.assertEqual(result.reason, "timeout")
        self.assertEqual(result.attempts, 2)
        self.assertEqual(svc.calls, 2)
        self.assertEqual(len(clock.sleeps), 1)
        self.assertGreater(result.elapsed, 2.0)
        # The late sample is still recorded as fact; only readiness is denied.
        self.assertEqual(result.last_sample, READY)
        self.assertEqual([s for _, s in result.transitions], [UNPLUGGED, READY])
        self.assertIn("reason=timeout", logs[-1])

    def test_callback_landing_exactly_on_deadline_is_ready(self):
        # 0.5 s poll + 0.5 s sleep + 1.0 s poll == 2.0 s budget exactly.
        clock = FakeClock()
        svc = BlockingService(clock, [(0.5, UNPLUGGED), (1.0, READY)])
        result, _ = run(clock, svc, timeout_s=2.0, poll_interval_s=0.5)
        self.assertTrue(result.ready)
        self.assertEqual(result.reason, "ready")
        self.assertEqual(result.attempts, 2)
        self.assertAlmostEqual(result.elapsed, 2.0, places=6)

    def test_delayed_first_poll_past_deadline_is_timeout(self):
        # The very first RPC after start blocks longer than the whole budget
        # and comes back ready. One attempt, no sleep, reason timeout.
        clock = FakeClock()
        svc = BlockingService(clock, [(2.5, READY)])
        result, _ = run(clock, svc, timeout_s=2.0)
        self.assertFalse(result.ready)
        self.assertEqual(result.reason, "timeout")
        self.assertEqual(result.attempts, 1)
        self.assertEqual(svc.calls, 1)
        self.assertEqual(clock.sleeps, [])
        self.assertEqual(result.last_sample, READY)
        self.assertAlmostEqual(result.elapsed, 2.5, places=6)

    def test_delayed_first_poll_not_ready_does_not_poll_again(self):
        clock = FakeClock()
        svc = BlockingService(clock, [(2.5, UNPLUGGED), (0.0, READY)])
        result, _ = run(clock, svc, timeout_s=2.0)
        self.assertFalse(result.ready)
        self.assertEqual(result.reason, "timeout")
        self.assertEqual(svc.calls, 1)
        self.assertEqual(clock.sleeps, [])

    def test_delayed_first_poll_exactly_at_deadline_is_ready(self):
        clock = FakeClock()
        svc = BlockingService(clock, [(2.0, READY)])
        result, _ = run(clock, svc, timeout_s=2.0)
        self.assertTrue(result.ready)
        self.assertEqual(result.attempts, 1)
        self.assertAlmostEqual(result.elapsed, 2.0, places=6)

    def test_zero_timeout_is_single_instant_poll(self):
        clock = FakeClock()
        result, _ = run(clock, SequenceService([READY]), timeout_s=0)
        self.assertTrue(result.ready)
        self.assertEqual(result.attempts, 1)
        clock = FakeClock()
        svc = SequenceService([UNPLUGGED, READY])
        result, _ = run(clock, svc, timeout_s=0)
        self.assertFalse(result.ready)
        self.assertEqual(result.reason, "timeout")
        self.assertEqual(svc.calls, 1)
        self.assertEqual(clock.sleeps, [])

    def test_sleep_overrun_past_deadline_skips_extra_rpc(self):
        # Real time.sleep can overshoot. If it wakes past the deadline the
        # helper must not spend another RPC.
        clock = FakeClock()
        clock.sleep = lambda dt: setattr(clock, "t", clock.t + dt + 0.5)
        svc = SequenceService([UNPLUGGED, READY])
        result, _ = run(clock, svc, timeout_s=0.3, poll_interval_s=0.1)
        self.assertFalse(result.ready)
        self.assertEqual(result.reason, "timeout")
        self.assertEqual(svc.calls, 1)

    def test_malformed_response_raises_without_retry(self):
        cases = [
            None,
            (True, 33),
            (True, 33, 10, 7),
            [True, "33", 10],
            (True, 33.0, 10),
            (True, True, True),
            (2, 33, 10),
            ("yes", 33, 10),
            (True, -1, 10),
            (True, 33, DEFAULT_MAX_COUNT + 1),
            (True, 0xFFFFFFFF, 10),
            "True,33,10",
            b"\x01\x21\x0a",
        ]
        for bad in cases:
            with self.subTest(bad=bad):
                clock = FakeClock()
                svc = SequenceService([bad, READY])
                with self.assertRaises(MalformedModeCountError):
                    run(clock, svc)
                self.assertEqual(svc.calls, 1)      # no retry on garbage
                self.assertEqual(clock.sleeps, [])

    def test_malformed_after_good_samples_still_raises(self):
        clock = FakeClock()
        svc = SequenceService([UNPLUGGED, UNPLUGGED, (True, 33)])
        with self.assertRaises(MalformedModeCountError):
            run(clock, svc)
        self.assertEqual(svc.calls, 3)

    def test_validate_normalises_int_hpd_and_list(self):
        self.assertEqual(validate_mode_count([1, 33, 10]), (True, 33, 10))
        self.assertEqual(validate_mode_count((0, 0, 0)), (False, 0, 0))
        self.assertEqual(validate_mode_count((True, 33, 10), max_count=40), (True, 33, 10))
        with self.assertRaises(MalformedModeCountError):
            validate_mode_count((True, 41, 10), max_count=40)
        for bad_max in (True, -1, 2.5, "40"):
            with self.subTest(max_count=bad_max):
                with self.assertRaises(ValueError):
                    validate_mode_count((True, 33, 10), max_count=bad_max)

    def test_frozen_clock_hits_attempt_cap(self):
        clock = FakeClock(frozen=True)
        svc = TimedService(clock, [(0.0, UNPLUGGED)])
        result, logs = run(clock, svc, timeout_s=8.0, max_attempts=25)
        self.assertFalse(result.ready)
        self.assertEqual(result.reason, "attempt-cap")
        self.assertEqual(result.attempts, 25)
        self.assertEqual(svc.calls, 25)
        self.assertEqual(len(clock.sleeps), 24)                # no sleep after last poll
        self.assertEqual(result.elapsed, 0.0)
        self.assertEqual(len(state_logs(logs)), 1)

    def test_frozen_clock_but_ready_returns_ready(self):
        clock = FakeClock(frozen=True)
        svc = SequenceService([UNPLUGGED, UNPLUGGED, READY])
        result, _ = run(clock, svc, max_attempts=10)
        self.assertTrue(result.ready)
        self.assertEqual(result.attempts, 3)

    def test_attempt_cap_of_one_is_single_shot(self):
        clock = FakeClock()
        svc = SequenceService([UNPLUGGED, READY])
        result, _ = run(clock, svc, max_attempts=1)
        self.assertFalse(result.ready)
        self.assertEqual(result.reason, "attempt-cap")
        self.assertEqual(svc.calls, 1)

    def test_custom_ready_predicate(self):
        clock = FakeClock()
        svc = SequenceService([(True, 33, 0)])
        result, _ = run(clock, svc, ready=lambda s: s[0] and s[1] > 0)
        self.assertTrue(result.ready)

    def test_no_log_callback_is_silent_and_fine(self):
        clock = FakeClock()
        result = wait_for_mode_counts(
            SequenceService([UNPLUGGED, READY]).getModeCount,
            clock.monotonic, clock.sleep, timeout_s=1.0)
        self.assertTrue(result.ready)

    def test_bad_arguments_rejected(self):
        clock = FakeClock()
        svc = SequenceService([READY])
        nan, inf = float("nan"), float("inf")
        cases = [
            {"timeout_s": -1}, {"timeout_s": nan}, {"timeout_s": inf}, {"timeout_s": -inf},
            {"timeout_s": True}, {"timeout_s": "8"}, {"timeout_s": None},
            {"poll_interval_s": -0.1}, {"poll_interval_s": nan}, {"poll_interval_s": inf},
            {"poll_interval_s": False},
            {"max_attempts": 0}, {"max_attempts": -5}, {"max_attempts": 5.0},
            {"max_attempts": True}, {"max_attempts": "5"}, {"max_attempts": None},
            {"max_count": -1}, {"max_count": 3.5}, {"max_count": True},
        ]
        for kw in cases:
            with self.subTest(kw=kw):
                with self.assertRaises(ValueError):
                    run(clock, svc, **kw)
        self.assertEqual(svc.calls, 0)          # rejected before any RPC
        self.assertEqual(clock.sleeps, [])

    def test_int_budgets_are_accepted(self):
        clock = FakeClock()
        result, _ = run(clock, SequenceService([UNPLUGGED, READY]),
                        timeout_s=2, poll_interval_s=1, max_attempts=3, max_count=40)
        self.assertTrue(result.ready)
        self.assertEqual(result.attempts, 2)

    def test_result_accessors(self):
        clock = FakeClock()
        result, _ = run(clock, SequenceService([READY]))
        self.assertTrue(result.hpd)
        self.assertEqual(result.timing_count, 33)
        self.assertEqual(result.color_count, 10)
        self.assertIn("reason=ready", result.summary())


if __name__ == "__main__":
    unittest.main()
