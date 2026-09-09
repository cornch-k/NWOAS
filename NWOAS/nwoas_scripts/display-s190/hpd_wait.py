"""display-s190: bounded wait for Tahoe DCP HPD/timing/color counts.

Pure Python. No m1n1, no proxy, no hardware access. Every side effect is an
injected callback so the helper can be driven by fake clocks in tests and by
the real ``service.getModeCount`` / ``time.monotonic`` / ``time.sleep`` in the
pre-guest hook.

Background (S187, host log usb-s187-crcr-20260910-051059.LauU6r):
    Tahoe's DCP emitted "unplug_gated: display HPD removed" while the AP was
    still initialising, and the very first ``service.getModeCount()`` in
    m1n1_windows/tahoe_dcp_guest_hook.py returned an HPD-down triple, so the
    module-level ``assert _boot_hpd and _boot_nt and _boot_nc`` aborted the
    run before Windows ever started. The unplug was transient. This helper
    replaces the single-shot assert with a bounded poll.

Guarantees:
    * Never invents EDID, timings, colour modes or geometry. It only reports
      what ``get_mode_count`` returned.
    * Never *starts* a sleep or a poll once the deadline has passed, and never
      exceeds ``max_attempts``. Either bound alone terminates the loop, so a
      frozen clock cannot spin forever and a stalled sleep cannot hide a stuck
      sink.
    * A sample is accepted as ready only if the clock read *after* the
      callback returned is <= the deadline. A callback that blocks past the
      deadline and then reports ready is reported as ``timeout``.
    * Logs only when the observed (hpd, timings, colors) triple changes.
    * Rejects malformed responses immediately instead of retrying them.

Limit:
    The helper cannot interrupt a blocking ``get_mode_count``. The deadline is
    checked around the callback (before deciding to poll again, and on the
    clock value read immediately after it returns), not inside it. A DCP RPC
    that hangs is bounded only by the transport's own timeout; that stays the
    transport's responsibility.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Callable, List, Optional, Tuple

__all__ = [
    "DEFAULT_MAX_COUNT",
    "MalformedModeCountError",
    "ModeCountSample",
    "WaitResult",
    "validate_mode_count",
    "wait_for_mode_counts",
]

# Upper bound on plausible timing / colour-mode counts. S158 observed
# (True, 33, 10) on a real LG sink; the DCP reply buffers used by the hook are
# 4096 bytes with 24-byte entries, so anything above ~170 could not even fit.
# 256 leaves headroom while still catching garbage such as 0xFFFFFFFF.
DEFAULT_MAX_COUNT = 256

ModeCountSample = Tuple[bool, int, int]


class MalformedModeCountError(ValueError):
    """``get_mode_count`` returned something that is not a valid triple."""


@dataclass
class WaitResult:
    ready: bool
    reason: str                       # "ready" | "timeout" | "attempt-cap"
    attempts: int
    elapsed: float
    # Last validated sample, whatever its readiness. On reason="timeout" this
    # may satisfy the predicate if it arrived after the deadline; ``ready`` is
    # the only field that says whether the wait succeeded.
    last_sample: Optional[ModeCountSample]
    # Every distinct state in observation order as (t_since_start, sample).
    transitions: List[Tuple[float, ModeCountSample]] = field(default_factory=list)

    @property
    def hpd(self) -> bool:
        return bool(self.last_sample[0]) if self.last_sample else False

    @property
    def timing_count(self) -> int:
        return self.last_sample[1] if self.last_sample else 0

    @property
    def color_count(self) -> int:
        return self.last_sample[2] if self.last_sample else 0

    def summary(self) -> str:
        return (f"ready={self.ready} reason={self.reason} attempts={self.attempts} "
                f"elapsed={self.elapsed:.3f}s last={self.last_sample} "
                f"transitions={[s for _, s in self.transitions]}")


def validate_mode_count(raw: object, max_count: int = DEFAULT_MAX_COUNT) -> ModeCountSample:
    """Normalise a ``getModeCount`` reply to ``(bool, int, int)`` or raise.

    Accepts a 3-element tuple/list. HPD may be a bool or an int. The two
    counts must be real ints (``bool`` is rejected so ``(True, True, True)``
    cannot masquerade as counts), non-negative and ``<= max_count``.
    """
    if isinstance(max_count, bool) or not isinstance(max_count, int) or max_count < 0:
        raise ValueError(f"max_count must be a non-negative int, got {max_count!r}")
    if isinstance(raw, (str, bytes, bytearray)) or not isinstance(raw, (tuple, list)):
        raise MalformedModeCountError(f"getModeCount returned {type(raw).__name__}, expected 3-tuple: {raw!r}")
    if len(raw) != 3:
        raise MalformedModeCountError(f"getModeCount returned {len(raw)} fields, expected 3: {raw!r}")
    hpd, nt, nc = raw
    if isinstance(hpd, bool):
        hpd_b = hpd
    elif isinstance(hpd, int):
        if hpd not in (0, 1):
            raise MalformedModeCountError(f"HPD field must be 0/1 or bool, got {hpd!r}")
        hpd_b = bool(hpd)
    else:
        raise MalformedModeCountError(f"HPD field must be bool/int, got {type(hpd).__name__}: {raw!r}")
    counts = []
    for name, value in (("timing", nt), ("color", nc)):
        if isinstance(value, bool) or not isinstance(value, int):
            raise MalformedModeCountError(f"{name} count must be int, got {type(value).__name__}: {raw!r}")
        if value < 0 or value > max_count:
            raise MalformedModeCountError(f"{name} count {value} outside [0, {max_count}]: {raw!r}")
        counts.append(value)
    return (hpd_b, counts[0], counts[1])


def _default_ready(sample: ModeCountSample) -> bool:
    hpd, nt, nc = sample
    return bool(hpd) and nt > 0 and nc > 0


def wait_for_mode_counts(
    get_mode_count: Callable[[], object],
    monotonic: Callable[[], float],
    sleep: Callable[[float], None],
    *,
    timeout_s: float = 8.0,
    max_attempts: int = 200,
    poll_interval_s: float = 0.05,
    max_count: int = DEFAULT_MAX_COUNT,
    ready: Callable[[ModeCountSample], bool] = _default_ready,
    log: Optional[Callable[[str], None]] = None,
    label: str = "display-s190 HPD/counts",
) -> WaitResult:
    """Poll ``get_mode_count`` until HPD, timing and colour counts are nonzero.

    Parameters are all injected so the function has no hardware or wall-clock
    dependency of its own.

    ``timeout_s``      wall-clock budget measured with ``monotonic``. Finite,
                       >= 0, not a bool.
    ``max_attempts``   hard cap on ``get_mode_count`` calls. A real int >= 1
                       (no bool, no float). Protects against a clock that
                       does not advance.
    ``poll_interval_s`` passed to ``sleep`` between attempts. Finite, >= 0.
    ``max_count``      upper bound accepted for timing/colour counts.
    ``ready``          predicate on a validated sample; default is all-nonzero.
    ``log``            called with one string per *changed* state, plus one
                       final summary line. ``None`` disables logging.

    Deadline semantics (``deadline = start + timeout_s``, ``start`` read once
    before the first poll):
        * ``get_mode_count`` is always called at least once, even with
          ``timeout_s=0``.
        * After every call the clock is read again. If that read is
          > ``deadline`` the result is ``timeout`` regardless of the sample,
          because the sample was obtained late (a slow first RPC after start
          is the common case). If it is == ``deadline`` the sample still
          counts; "exactly at the deadline" is inside the budget.
        * The sleep before the next poll is clamped to the remaining budget,
          so a poll is never *started* after the deadline. A sample that
          would only become ready past the deadline is not observed.
        * The helper cannot bound how long ``get_mode_count`` itself blocks.
          The transport behind it must own its own RPC timeout.

    Returns a ``WaitResult``. Never raises on a slow or stuck sink; raises
    ``MalformedModeCountError`` on a structurally invalid reply and
    ``ValueError`` on nonsensical arguments.
    """
    for name, value in (("timeout_s", timeout_s), ("poll_interval_s", poll_interval_s)):
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"{name} must be a real number, got {value!r}")
        if not math.isfinite(value) or value < 0:
            raise ValueError(f"{name} must be finite and >= 0, got {value!r}")
    if isinstance(max_attempts, bool) or not isinstance(max_attempts, int):
        raise ValueError(f"max_attempts must be an int, got {max_attempts!r}")
    if max_attempts < 1:
        raise ValueError(f"max_attempts must be >= 1, got {max_attempts}")
    if isinstance(max_count, bool) or not isinstance(max_count, int) or max_count < 0:
        raise ValueError(f"max_count must be a non-negative int, got {max_count!r}")

    def emit(msg: str) -> None:
        if log is not None:
            log(msg)

    start = monotonic()
    deadline = start + timeout_s
    attempts = 0
    last: Optional[ModeCountSample] = None
    transitions: List[Tuple[float, ModeCountSample]] = []

    def finish(is_ready: bool, reason: str) -> WaitResult:
        elapsed = max(0.0, monotonic() - start)
        result = WaitResult(is_ready, reason, attempts, elapsed, last, transitions)
        emit(f"[FACT] {label} wait {result.summary()}")
        return result

    while True:
        attempts += 1
        sample = validate_mode_count(get_mode_count(), max_count)
        # Clock read immediately after the callback returned. This is the
        # only time value the readiness decision is allowed to use.
        now = monotonic()
        if sample != last:
            transitions.append((max(0.0, now - start), sample))
            emit(f"[FACT] {label} {sample} t=+{max(0.0, now - start):.3f}s attempt={attempts}")
            last = sample
        if now > deadline:
            # The callback itself consumed the budget (slow first RPC, hung
            # transport that eventually returned). Do not accept the sample
            # even if it looks ready; it was obtained outside the window.
            return finish(False, "timeout")
        if ready(sample):
            return finish(True, "ready")
        if attempts >= max_attempts:
            return finish(False, "attempt-cap")
        # Do not sleep or poll again once the budget is spent. The sleep is
        # clamped to the remaining budget so the final poll starts on the
        # deadline, never after it. The caller owns the restart decision.
        remaining = deadline - now
        if remaining <= 0:
            return finish(False, "timeout")
        sleep(min(poll_interval_s, remaining))
        if monotonic() > deadline:
            # Sleep overran (real sleep jitter). Skip the wasted RPC.
            return finish(False, "timeout")
