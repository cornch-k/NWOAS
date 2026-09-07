#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""NWOAS baud-rate reliability ladder test.

Measures, per candidate baud rate, how reliable the macvdmtool VDM serial
link is while m1n1 sits in stand-alone *proxy* mode (no guest running), and
reports the fastest baud rate with zero observed errors.

Only baud rates where the s5l UART's UBRDIV divider comes out integer-exact
(UART_CLOCK / (16*baud) is an integer, UART_CLOCK=24MHz -- see
m1n1.proxyutils.s5l_actual_baud() / src/uart.c:106-112) are worth testing;
anything else silently aliases to a nearby rate the target never asked for.

Precondition:
    The target must already be sitting at the m1n1 proxy prompt
    ("Running proxy..."), reachable over the serial device below. Do NOT run
    this while run_guest.py / a tethered hv session is active -- they own the
    same serial device and this script will fight them for it.

Usage:
    M1N1DEVICE=/dev/cu.debug-console .venv-hv/bin/python3 \\
        proxyclient/tools/baud_ladder.py
    proxyclient/tools/baud_ladder.py --selftest      # no device required
    proxyclient/tools/baud_ladder.py \\
        --candidates 250000,500000,750000 --bulk-mb 8 --burst 500

Applying the result:
    M1N1_BAUD=<권장 baud> ./run-hv.sh
"""
import sys, pathlib
sys.path.append(str(pathlib.Path(__file__).resolve().parents[1]))

import argparse
import os
import time

# NWOAS: force a known baseline connection regardless of the caller's ambient
# environment, so this tool's own baud experiments aren't pre-empted by an
# M1N1_BAUD/M1N1_KEEP_BAUD already exported in the shell. Must be set before
# m1n1.proxyutils.bootstrap_port() runs (see main()).
os.environ["M1N1_KEEP_BAUD"] = "1"

BASELINE_BAUD = 115200
# NWOAS: the s5l UART only hits an integer-exact bit rate when
# 24000000/(16*baud) is an integer (see s5l_actual_baud() below); of the
# rates that satisfy that in a useful range, these 7 are the full, exact set
# -- 150000/187500/250000/300000/375000/500000/750000. Do NOT add 1000000,
# 460800, 921600, 230400 here: they all alias (checked at runtime by
# validate_candidates(), and visible as "alias, avoid" in the UBRDIV table).
DEFAULT_CANDIDATES = [150000, 187500, 250000, 300000, 375000, 500000, 750000]
# NWOAS: 1500000 (UBRDIV=0) IS integer-exact by the UBRDIV math, so it will
# NOT show up as an "alias" in the UBRDIV table -- but it is excluded from
# DEFAULT_CANDIDATES and actively rejected by validate_candidates() below
# because the macvdmtool VDM serial channel itself (not the UART divider) is
# known to drop bytes at 1.5 Mbaud, per proxyclient/m1n1/proxyutils.py
# bootstrap_port()'s M1N1_KEEP_BAUD comment (~line 587-589). This is a
# link-layer finding, not a math one, so it can't be derived from
# s5l_actual_baud() alone and must be listed explicitly. Single source of
# truth is m1n1.proxyutils.LINK_UNRELIABLE_BAUDS (also consulted by the
# M1N1_BAUD gate in bootstrap_port()) -- imported lazily in
# validate_candidates() below, matching this file's existing lazy-import
# convention for the m1n1 package.
BULK_SIZES = [4 * 1024, 32 * 1024, 128 * 1024, 1024 * 1024]
BURST_SIZE = 368  # sizeof(ExcInfo) -- the exception-context struct read on
                   # every guest trap; this is the real-world "handshake" read.


def parse_args():
    ap = argparse.ArgumentParser(
        description="NWOAS baud-rate ladder test (m1n1 proxy stand-alone mode)")
    ap.add_argument("--candidates",
                     default=",".join(str(c) for c in DEFAULT_CANDIDATES),
                     help="Comma-separated candidate baud rates to test")
    ap.add_argument("--bulk-mb", type=float, default=4.0,
                     help="Minimum total MB of bulk writemem/readmem traffic per candidate")
    ap.add_argument("--burst", type=int, default=2000,
                     help="Number of small round-trip reads per candidate (trap-handshake sim)")
    ap.add_argument("--selftest", action="store_true",
                     help="Print the UBRDIV table and exit; no device required")
    return ap.parse_args()


def print_ubrdiv_table(bauds):
    from m1n1.proxyutils import s5l_actual_baud
    print(f"{'requested':>10} {'UBRDIV':>8} {'actual':>14} {'error%':>8}")
    for b in bauds:
        ubrdiv, actual, error = s5l_actual_baud(b)
        flag = "" if error <= 0.02 else "  <-- alias, avoid"
        print(f"{b:>10} {ubrdiv:>8} {actual:>14.1f} {error * 100:>7.2f}{flag}")


def validate_candidates(candidates):
    """Drop any candidate this project has established is unsafe to actually
    send to the target, whether it came from DEFAULT_CANDIDATES or a
    caller-supplied --candidates list. Two independent reasons a candidate
    gets dropped here:

      1. UBRDIV aliasing (error > 2%, mirrors the M1N1_BAUD gate in
         proxyclient/m1n1/proxyutils.py bootstrap_port()): the target ends up
         at a *different* baud than the host thinks it switched to, e.g.
         requesting 230400 actually yields 250000 on the wire (see
         s5l_actual_baud()) -- host and target then disagree on bit rate and
         every subsequent byte is garbage.
      2. LINK_UNRELIABLE_BAUDS (currently just 1500000): integer-exact by the
         UBRDIV math, but empirically known to drop bytes on the macvdmtool
         VDM serial channel itself.

    Never lets switch_to()/p.set_baud() see a rejected candidate -- refusing
    up front is safer than corrupting the host's own baudrate mid-run and
    only noticing from a wall of bulk/burst errors."""
    from m1n1.proxyutils import s5l_actual_baud, LINK_UNRELIABLE_BAUDS

    valid = []
    for c in candidates:
        if c in LINK_UNRELIABLE_BAUDS:
            print(f"  [{c}] 제외: UBRDIV은 정수-정확하지만 macvdmtool VDM 링크가 1.5Mbaud에서 "
                  f"바이트를 드롭하는 것으로 알려짐 (proxyclient/m1n1/proxyutils.py "
                  f"bootstrap_port() 주석 참고) - 사다리에서 건너뜀")
            continue
        try:
            _, actual, error = s5l_actual_baud(c)
        except ValueError as e:
            print(f"  [{c}] 제외: {e}")
            continue
        if error > 0.02:
            print(f"  [{c}] 제외: UBRDIV 정수-비정확 (실제 baud {actual:.1f}, "
                  f"오차 {error * 100:.2f}%) - 타깃이 요청과 다른 baud로 앨리어싱되어 "
                  f"호스트/타깃 baud 불일치를 유발함")
            continue
        valid.append(c)
    return valid


# ---------------------------------------------------------------------------
# Link-level helpers
# ---------------------------------------------------------------------------

def sync_at(iface, host_baud, retries=4):
    """Set the host's own baudrate and confirm the link is alive with a few
    nop() retries. Returns True/False; never raises."""
    iface.dev.baudrate = host_baud
    for _ in range(retries):
        try:
            iface.nop()
            return True
        except Exception:
            time.sleep(0.05)
    return False


def switch_to(iface, p, candidate, retries=4):
    """Ask the target to switch to `candidate` baud, then confirm sync.
    Returns True/False; never raises."""
    try:
        p.set_baud(candidate)
    except Exception:
        pass
    # NWOAS: regardless of whether the reply above was received cleanly, the
    # target has *already* reprogrammed its UBRDIV divider by this point --
    # P_SET_BAUD calls uart_setbaud() before it transmits the reply
    # (src/proxy.c:61-72) -- so make sure our own baudrate matches before
    # probing with nop().
    return sync_at(iface, candidate, retries=retries)


def ensure_candidate_sync(iface, p, candidate):
    """Full recovery-aware sync sequence for one candidate. Returns True if
    the link is confirmed alive at `candidate` baud, False if the candidate
    should be skipped (link recovered at 115200), or exits the process if
    the link could not be recovered at all."""
    if switch_to(iface, p, candidate):
        return True
    print(f"  [{candidate}] 동기화 실패 (candidate baud) - 115200 복귀 시도")
    if sync_at(iface, BASELINE_BAUD):
        print(f"  [{candidate}] 115200 복귀 성공 - 이 후보는 건너뜀")
        return False
    print("치명적: 링크 동기화 실패 (candidate baud도, 115200 복귀도 실패).")
    print("macvdmtool reboot serial로 타깃 재부팅이 필요합니다.")
    sys.exit(2)


def restore_baseline(iface, p):
    try:
        p.set_baud(BASELINE_BAUD)
    except Exception:
        pass
    if not sync_at(iface, BASELINE_BAUD):
        print("  경고: 115200 복귀 확인 실패 - 이후 후보 테스트가 불안정할 수 있음")


def _write_read_once(iface, addr, size, data, max_attempts=3):
    """Write `data` to addr, read it back, compare. Uses the raw (non
    chunk-retrying) primitives deliberately, so link instability shows up as
    counted retries/errors here instead of being silently absorbed by
    UartInterface.readmem()'s own internal resync loop.
    Returns (mismatch_or_failed_bytes, retries, ok)."""
    from m1n1.proxy import UartTimeout, UartChecksumError

    retries = 0
    for _ in range(max_attempts):
        try:
            iface.writemem(addr, data)
            break
        except (UartTimeout, UartChecksumError):
            retries += 1
            iface._resync_link()
    else:
        return size, retries, False

    for _ in range(max_attempts):
        try:
            readback = iface._readmem_chunk(addr, size)
            break
        except (UartTimeout, UartChecksumError):
            retries += 1
            iface._resync_link()
    else:
        return size, retries, False

    mismatches = sum(1 for a, b in zip(data, readback) if a != b)
    return mismatches, retries, True


def bulk_test(iface, u, bulk_mb):
    target_bytes = int(bulk_mb * 1024 * 1024)
    per_size_target = max(target_bytes // len(BULK_SIZES), BULK_SIZES[0])

    total_errors = 0
    total_retries = 0
    total_bytes = 0

    buf_addr = u.malloc(max(BULK_SIZES))
    try:
        for size in BULK_SIZES:
            reps = max(1, -(-per_size_target // size))  # ceil div
            for _ in range(reps):
                data = os.urandom(size)
                mism, retries, ok = _write_read_once(iface, buf_addr, size, data)
                total_bytes += size
                total_retries += retries
                total_errors += size if not ok else mism
    finally:
        u.free(buf_addr)

    print(f"    bulk: {total_bytes / 1e6:.1f} MB 전송, 오류(바이트) {total_errors}, "
          f"재시도 {total_retries}")
    return total_errors, total_retries


def burst_test(iface, u, count):
    from m1n1.proxy import UartTimeout, UartChecksumError

    pattern = os.urandom(BURST_SIZE)
    buf_addr = u.malloc(BURST_SIZE)
    try:
        iface.writemem(buf_addr, pattern)

        errors = 0
        retries = 0
        max_latency = 0.0
        for _ in range(count):
            t0 = time.monotonic()
            ok = False
            data = None
            for _attempt in range(3):
                try:
                    data = iface._readmem_chunk(buf_addr, BURST_SIZE)
                    ok = True
                    break
                except (UartTimeout, UartChecksumError):
                    retries += 1
                    iface._resync_link()
            dt = time.monotonic() - t0
            max_latency = max(max_latency, dt)
            if not ok or data != pattern:
                errors += 1
    finally:
        u.free(buf_addr)

    print(f"    burst: {count}회 왕복, 오류 {errors}, 재시도 {retries}, "
          f"최대지연 {max_latency * 1000:.1f}ms")
    return errors, retries, max_latency


def run_candidate(iface, p, u, candidate, args, is_baseline):
    iface.dev.timeout = 0.5

    if is_baseline:
        if not sync_at(iface, BASELINE_BAUD):
            print("  기준선(115200) 동기화 실패 - 장치/케이블을 확인하세요")
            return {"skipped": True}
        print(f"  동기화 OK @ {candidate} baud (기준선)")
    else:
        if not ensure_candidate_sync(iface, p, candidate):
            return {"skipped": True}
        print(f"  동기화 OK @ {candidate} baud")

    bulk_errors, bulk_retries = bulk_test(iface, u, args.bulk_mb)
    burst_errors, burst_retries, max_latency = burst_test(iface, u, args.burst)

    if not is_baseline:
        restore_baseline(iface, p)

    return {
        "skipped": False,
        "bulk_errors": bulk_errors,
        "bulk_retries": bulk_retries,
        "burst_errors": burst_errors,
        "burst_retries": burst_retries,
        "max_latency": max_latency,
    }


def print_report(results):
    print()
    print(f"{'baud':>10} {'bulk_err':>9} {'bulk_retry':>11} {'burst_err':>10} "
          f"{'burst_retry':>12} {'max_lat(ms)':>12}")
    for baud, r in results.items():
        if r.get("skipped"):
            print(f"{baud:>10} {'(동기화 실패, 건너뜀)':>9}")
            continue
        print(f"{baud:>10} {r['bulk_errors']:>9} {r['bulk_retries']:>11} "
              f"{r['burst_errors']:>10} {r['burst_retries']:>12} "
              f"{r['max_latency'] * 1000:>12.1f}")


def pick_recommended(results):
    clean = [
        baud for baud, r in results.items()
        if not r.get("skipped")
        and r["bulk_errors"] == 0 and r["bulk_retries"] == 0
        and r["burst_errors"] == 0 and r["burst_retries"] == 0
    ]
    if not clean:
        return BASELINE_BAUD
    return max(clean)


def main():
    args = parse_args()
    try:
        candidates = [int(c, 0) for c in args.candidates.split(",") if c.strip()]
    except ValueError as e:
        # Robustness: a malformed --candidates token must fail cleanly (before any
        # device access), not with a raw traceback. No HW is touched on this path.
        print(f"--candidates 파싱 실패: {e!r} (쉼표로 구분한 정수만 허용, 예: 250000,500000)")
        return 2

    if args.selftest:
        print_ubrdiv_table([BASELINE_BAUD] + candidates)
        return 0

    print("=== UBRDIV table (baseline + candidates) ===")
    print_ubrdiv_table([BASELINE_BAUD] + candidates)
    print()

    # NWOAS: never let an aliasing or link-unreliable candidate reach the
    # target -- filter here, after the (unfiltered, informational) table
    # above, and before anything touches the device.
    safe_candidates = validate_candidates(candidates)
    dropped = [c for c in candidates if c not in safe_candidates]
    if dropped:
        print(f"제외된 후보 (사다리에서 건너뜀): {dropped}")
    candidates = safe_candidates
    if not candidates:
        print("유효한 후보가 없습니다 (모두 제외됨). 기준선(115200)만 확인하고 종료합니다.")

    from m1n1.proxy import UartInterface, M1N1Proxy
    from m1n1.proxyutils import ProxyUtils, bootstrap_port

    iface = UartInterface()
    p = M1N1Proxy(iface, debug=False)
    print(f"Bootstrapping at {BASELINE_BAUD} baud (M1N1_KEEP_BAUD forced)...")
    bootstrap_port(iface, p)
    u = ProxyUtils(p, heap_size=64 * 1024 * 1024)
    print(f"m1n1 base: 0x{u.base:x}")

    results = {}
    try:
        print()
        print(f"--- baseline {BASELINE_BAUD} ---")
        results[BASELINE_BAUD] = run_candidate(iface, p, u, BASELINE_BAUD, args, is_baseline=True)

        for cand in candidates:
            print()
            print(f"--- candidate {cand} ---")
            results[cand] = run_candidate(iface, p, u, cand, args, is_baseline=False)
    finally:
        # NWOAS: always try to leave the link at 115200, whatever happened above.
        try:
            restore_baseline(iface, p)
        except Exception:
            pass

    print_report(results)
    recommended = pick_recommended(results)
    print(f"\n권장 baud: {recommended}")
    if recommended != BASELINE_BAUD:
        print(f"적용 예: M1N1_BAUD={recommended} ./run-hv.sh")
    else:
        print("(모든 후보에서 오류/재시도가 관측되어 115200 유지를 권장합니다)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
