# display-s190: bounded HPD/mode-count wait for the Tahoe DCP pre-guest hook

## Problem (fact, from host log `nwoas_scripts/logs/usb-s187-crcr-20260910-051059.LauU6r`)

During DCP bring-up, before `hv.init()` and before Windows started, Tahoe logged
a hotplug cycle (`oInterfaceIOAV.cpp:1332 unplug_gated: display HPD removed`,
`Time Elapsed between Hotplugs: 7016`, `IOMFB: power on -> off`). The hook's
first read then hit an HPD-down triple and the module-level line

```
m1n1_windows/tahoe_dcp_guest_hook.py:146  _boot_hpd, _boot_nt, _boot_nc = service.getModeCount()
m1n1_windows/tahoe_dcp_guest_hook.py:147  assert _boot_hpd and _boot_nt and _boot_nc
```

raised `AssertionError` and the run never reached the guest. The unplug was
transient; a single-shot read is the weak point. The later HDMI settle loop at
lines 210-224 already polls, but it only runs after the first commit.

## What this directory contains

| File | Purpose |
|------|---------|
| `hpd_wait.py` | Pure-Python `wait_for_mode_counts()` with injected `get_mode_count`, `monotonic`, `sleep`, `log`. No m1n1 / proxy / hardware imports. |
| `test_hpd_wait.py` | Fake-clock unittest suite (ready, delayed ready, stuck HPD, late ready past deadline, callback that blocks past the deadline, delayed first poll, malformed response, frozen clock attempt cap, argument validation). |

Original hook, m1n1 sources and launchers are untouched.

Behaviour of the helper:

- Polls until `(hpd, n_timings, n_colors)` are all nonzero (predicate is injectable).
- Terminates on either the `monotonic` deadline or the `max_attempts` cap, whichever
  first. A sink that only becomes ready after the budget is reported as
  `reason="timeout"`, not silently accepted.
- Logs one line per *changed* state plus a final summary. Identical polls are silent.
- Validates every reply: 3-element tuple/list, HPD bool or 0/1, counts real non-bool
  ints within `[0, max_count]` (default 256). Anything else raises
  `MalformedModeCountError` immediately, with no retry.
- Validates its own arguments before the first RPC: `timeout_s` and `poll_interval_s`
  must be finite real numbers `>= 0` (NaN, ±inf, bools and strings raise
  `ValueError`); `max_attempts` must be a real int `>= 1` (no bool, no float);
  `max_count` a non-negative real int.
- Returns a `WaitResult` (`ready`, `reason`, `attempts`, `elapsed`, `last_sample`,
  `transitions`). It never fabricates EDID, timings, colour modes or geometry; the
  caller still fetches those from the DCP after `ready` is true.

### Deadline semantics (exact)

`deadline = start + timeout_s`, with `start` read once before the first poll.

- `get_mode_count` is always called at least once, even with `timeout_s=0`.
- The deadline is checked **around** the callback, not inside it. Immediately after
  each call returns, the clock is read again:
  - read `> deadline`: result is `timeout`, **even if the sample is ready**. The
    sample was obtained outside the window. This covers a slow first RPC after
    `start` and an RPC that hung and eventually returned.
  - read `== deadline`: the sample still counts. "Exactly at the deadline" is inside
    the budget.
- Before sleeping, the sleep is clamped to the remaining budget, so a poll is never
  *started* after the deadline. If the sleep overruns anyway (real `time.sleep`
  jitter), the helper returns `timeout` without spending another RPC.
- On `timeout`, `last_sample` still reports what was actually observed, including a
  late ready triple. Only `result.ready` says whether the wait succeeded. Callers
  must branch on `ready`, never on `last_sample` alone.

### What the helper cannot do

It cannot interrupt or bound a `get_mode_count` call that blocks. If the DCP RPC
hangs, the helper hangs with it and only notices once the call returns. The
transport behind `service.getModeCount` must own its own RPC timeout; this helper
does not replace that. An earlier version of this README claimed the helper "never
polls past the deadline"; that was too strong and is corrected above.

## Tests

```
python3 -m unittest /Volumes/X31/NWOAS/nwoas_scripts/display-s190/test_hpd_wait.py -v
```

All tests use a fake clock; no real sleeping, no hardware. Callback cost is modelled
by a service that advances the fake clock *inside* `getModeCount` before returning.

### Results (2026-09-10, follow-up)

| Run | Outcome |
|-----|---------|
| Current helper, full suite | 25 tests, all pass |
| Same suite against a temp copy with the pre-fix ordering (ready checked before the post-callback clock) | 2 fail: `test_callback_blocking_past_deadline_then_ready_is_timeout`, `test_delayed_first_poll_past_deadline_is_timeout` |

The gap Main found was real: the original loop sampled `now` after the callback but
accepted `ready` before comparing `now` to the deadline, so a callback that blocked
past the budget and then returned a ready triple was accepted. The original
"late ready" test only covered readiness that appeared *between* polls (never
observed), not a callback that itself consumed the budget. Both cases are now
tested, along with exactly-at-deadline (still ready) for both the sleep path and the
blocking-callback path, delayed first poll (ready and not ready), `timeout_s=0`,
sleep overrun, and the argument rejections listed above.

## Integration suggestion (not applied)

Replace the single-shot read at `tahoe_dcp_guest_hook.py:146-147` with the bounded
wait, keeping the same failure semantics (still abort the run if the sink never
becomes ready, so no false "success"):

```python
import sys, time
sys.path.insert(0, "/Volumes/X31/NWOAS/nwoas_scripts/display-s190")
from hpd_wait import wait_for_mode_counts

_boot_wait = wait_for_mode_counts(
    service.getModeCount, time.monotonic, time.sleep,
    timeout_s=8.0, max_attempts=200, poll_interval_s=0.05,
    log=print, label="pre-guest boot HPD/counts")
assert _boot_wait.ready, _boot_wait.summary()
_boot_hpd, _boot_nt, _boot_nc = _boot_wait.last_sample
```

Notes for whoever integrates:

- `timeout_s=8.0` mirrors the existing settle window at line 210. The S187 log
  shows `Time Elapsed between Hotplugs: 7016` (ms), so 8 s is a plausible but
  unverified budget; tune from real logs, not from this README.
- The same call can replace the `assert hpd` at line 159 inside
  `configure_tahoe_display()`; the settle loop at 210-224 could also use it with a
  custom `ready` predicate, but that loop has different semantics (it wants to see
  the True/False/True cycle), so leave it unless there is a reason to change it.
- `send_cmd(4)` / `send_cmd(5)` and `choose_timing()` must still run after the wait
  returns ready. The helper reports counts only; it never supplies mode data.
- On `ready=False` the correct action is the existing controller restart path that
  Main owns. Do not retry the guest with stale geometry. The snippet above asserts
  on `ready` before touching `last_sample`; keep that order.
- `timeout_s` bounds the polling loop only. If `service.getModeCount` can block
  indefinitely on the proxy, that has to be bounded by the proxy/transport itself;
  the helper will not return until the call does.

## Status

Helper and tests are pure software and pass locally. Nothing here has been run
against the M1 hardware, and no claim is made that the wait alone recovers the
S187 boot; that needs an actual run with the hook modified as above.
