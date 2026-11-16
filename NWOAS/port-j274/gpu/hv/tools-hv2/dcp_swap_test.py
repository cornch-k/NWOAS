#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""S250: host-driven iBoot DCP async-swap test on the J274 Mac mini (HDMI), NO hypervisor.

Talks to the g250E m1n1 (m1n1-gpu-hv2 + AFK async command layer + P_NWOAS_DCP_* proxy
commands, see ../README-dcp-s250.md). It refuses to touch anything unless the running m1n1
carries the g250E build tag at the same image offset as the local binary AND answers the
P_NWOAS_DCP_PROBE command with the S250 magic (the stock D4 m1n1 has neither).

  M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1 \\
  PYTHONPATH=$ROOT/m1n1_windows/proxyclient \\
    $ROOT/m1n1_windows/.venv-hv/bin/python3 -u dcp_swap_test.py [--out EVIDENCE.json]

Sequence:
  1. identity gate (local binary sha256, live build tag, probe magic, 13.5 OS firmware)
  2. P_NWOAS_DCP_START(configure): display_configure(NULL) -> HPD, timing modes, current mode
  3. two 1920x1080 BGRA8888 buffers in the proxy heap: A = red|blue halves, B = green|magenta
     halves; both carry a bottom "frame counter" band redrawn before every swap
  4. 120 alternating async swaps (A,B,A,...), each waited for on the device (APPLIED or
     FAILED), device-measured submit->APPLIED microseconds
  5. set_power(off), 5 s, set_power(on), one more swap (A), HPD again
  6. P_NWOAS_DCP_SHUTDOWN: boot framebuffer back (APPLIED + 150 ms), unmap, DCP sleep
Logs alone are NOT a pass: the person at the monitor must see A/B alternate, the monitor
go dark during the 5 s and come back (README-dcp-s250.md "판정").
"""

import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import sys
import time

ROOT = Path("/Users/angigyeom/Desktop/DEV/NWOAS")
EXPECTED_TAG = "v1.0.2-1474-gbddf7f06-g250E"
DEFAULT_BIN = ROOT / "port-j274/gpu/hv/out-hv2/m1n1-g250E-agx-hv2-dcpasync.bin"
EXPECTED_BIN_SHA256 = "9693cc6ac4a50c8428e58a1ddfd21011188e026d8ec4cd3dab6216dbeec0c0ee"
OS_FW_135 = b"iBoot-8422.141.2"
MAGIC = int.from_bytes(b"NWDCP250", "little")

# m1n1-gpu-hv2/src/proxy.h (raw opcodes so any proxyclient checkout works)
P_NWOAS_DCP_PROBE = 0x1180
P_NWOAS_DCP_START = 0x1181
P_NWOAS_DCP_POWER = 0x1182
P_NWOAS_DCP_SWAP = 0x1183
P_NWOAS_DCP_POLL = 0x1184
P_NWOAS_DCP_SHUTDOWN = 0x1185

START_CONFIGURE = 1
FMT_BGRA8888 = 0
POLL_STATUS = {0: "PENDING", 1: "APPLIED", 2: "FAILED", 3: "NO_SWAP"}
STAGES = {0: "IDLE", 1: "BEGIN", 2: "SET_LAYER", 3: "END", 4: "COMPLETE", 5: "ERROR"}
DCP_SLEEP_IF_EXTERNAL = 1
SHUT_RESTORED, SHUT_UNMAPPED, SHUT_DONE = 1, 2, 4

INFO_FMT = "<QIiiiiiIIQQIIIIII"     # struct nwoas_dcp_info (80 bytes)
MODE_FMT = "<IIII"                  # struct nwoas_dcp_mode_rec
POLL_FMT = "<IIiiQQQII"             # struct nwoas_dcp_poll_out (48 bytes)
assert struct.calcsize(INFO_FMT) == 80 and struct.calcsize(POLL_FMT) == 48

W, H = 1920, 1080
STRIDE = W * 4
BUF_SIZE = (STRIDE * H + 0x3fff) & ~0x3fff
BAND_Y, BAND_H = H - 136, 128      # counter band rows (full-width => contiguous memory)

# BGRA8888 little-endian u32 = 0xAARRGGBB
RED, BLUE, GREEN, MAGENTA = 0xffff0000, 0xff0000ff, 0xff00ff00, 0xffff00ff
WHITE, BLACK = 0xffffffff, 0xff000000


class Refused(Exception):
    pass


class DeadlineExceeded(Exception):
    pass


@contextmanager
def deadline(seconds, step):
    def fire(signum, frame):
        raise DeadlineExceeded(f"hard deadline exceeded: {step} ({seconds}s)")
    old = signal.signal(signal.SIGALRM, fire)
    signal.setitimer(signal.ITIMER_REAL, seconds)
    try:
        yield
    finally:
        signal.setitimer(signal.ITIMER_REAL, 0)
        signal.signal(signal.SIGALRM, old)


def s64(v):
    v = int(v) & ((1 << 64) - 1)
    return v - (1 << 64) if v >> 63 else v


def local_identity(path):
    data = path.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    if sha != EXPECTED_BIN_SHA256:
        raise Refused(f"local binary {path} sha256 {sha} != expected {EXPECTED_BIN_SHA256}")
    marker = b"##m1n1_ver##" + EXPECTED_TAG.encode() + b"\0"
    off = data.find(marker)
    if off < 0:
        raise Refused(f"local binary has no build tag {EXPECTED_TAG}")
    return off, marker


def connect():
    if not os.environ.get("M1N1DEVICE"):
        raise Refused("M1N1DEVICE is required")
    os.environ["M1N1_KEEP_BAUD"] = "1"
    from m1n1.proxy import UartInterface, M1N1Proxy
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    iface = UartInterface()
    p = M1N1Proxy(iface, debug=False)
    bootstrap_port(iface, p)
    return iface, p, ProxyUtils(p)


def live_gate(u, p, off, marker):
    got = u.iface.readmem(int(u.base) + off, len(marker))
    if got != marker:
        raise Refused(f"running m1n1 is not g250E (tag bytes at base+{off:#x}: {got!r})")
    from m1n1.proxy import ProxyCommandError
    try:
        magic = int(p.request(P_NWOAS_DCP_PROBE))
    except ProxyCommandError as exc:
        raise Refused(f"P_NWOAS_DCP_PROBE rejected ({exc}); not the S250 m1n1") from exc
    if magic != MAGIC:
        raise Refused(f"probe magic {magic:#x} != {MAGIC:#x}")
    fw = u.adt["/chosen"].firmware_version
    fw = (fw.encode() if isinstance(fw, str) else bytes(fw)).split(b"\0")[0]
    if not fw.startswith(OS_FW_135):
        raise Refused(f"OS firmware {fw!r} is not {OS_FW_135!r}")
    return {"m1n1_base": hex(int(u.base)), "tag": EXPECTED_TAG, "probe_magic": hex(magic),
            "os_firmware": fw.decode(errors="replace")}


def read_info(u, info_addr, info_size):
    raw = u.iface.readmem(info_addr, info_size)
    names = ("magic version start_ret hpd timing_cnt color_cnt modes_ret is_external n_modes "
             "fb_base fb_dva cur_width cur_height cur_stride cur_depth os_fw_version "
             "iboot_ready").split()
    info = dict(zip(names, struct.unpack_from(INFO_FMT, raw, 0)))
    modes = []
    for i in range(info["n_modes"]):
        v, w, h, fps = struct.unpack_from(MODE_FMT, raw, 80 + 16 * i)
        modes.append({"valid": v, "width": w, "height": h,
                      "hz": round(fps / 65536.0, 2)})
    info["modes"] = modes
    return info


def print_info(label, info):
    print(f"[S250] {label}: start_ret={info['start_ret']} hpd={info['hpd']} "
          f"timing_cnt={info['timing_cnt']} color_cnt={info['color_cnt']} "
          f"external={info['is_external']} iboot_ready={info['iboot_ready']}", flush=True)
    print(f"[S250] {label}: current mode {info['cur_width']}x{info['cur_height']} "
          f"stride={info['cur_stride']} depth={info['cur_depth']:#x} "
          f"fb_base={info['fb_base']:#x} fb_dva={info['fb_dva']:#x}", flush=True)
    if info["modes"]:
        print(f"[S250] {label}: | # | valid | width | height | Hz |", flush=True)
        for i, m in enumerate(info["modes"]):
            print(f"[S250] {label}: | {i} | {m['valid']} | {m['width']} | {m['height']} | "
                  f"{m['hz']:.2f} |", flush=True)


def fill_buffer(u, addr, left, right):
    row = struct.pack("<I", left) * (W // 2) + struct.pack("<I", right) * (W - W // 2)
    row += b"\0" * (STRIDE - W * 4)
    img = row * H + b"\0" * (BUF_SIZE - STRIDE * H)
    u.compressed_writemem(addr, img)
    # spot check: first pixel of the left and right halves on the middle row
    mid = addr + (H // 2) * STRIDE
    l = struct.unpack("<I", u.iface.readmem(mid, 4))[0]
    r = struct.unpack("<I", u.iface.readmem(mid + (W - 1) * 4, 4))[0]
    if (l, r) != (left, right):
        raise RuntimeError(f"buffer {addr:#x} readback {l:#x}/{r:#x} != {left:#x}/{right:#x}")


def draw_counter(p, addr, n):
    """Bottom band: the top (n % 64) * 2 rows white, the rest black (two memset32 calls)."""
    lit = (n % 64) * 2
    band = addr + BAND_Y * STRIDE
    if lit:
        p.memset32(band, WHITE, lit * STRIDE)
    if BAND_H - lit:
        p.memset32(band + lit * STRIDE, BLACK, (BAND_H - lit) * STRIDE)


def do_swap(u, p, buf, out_addr, max_wait_us):
    t = time.monotonic()
    cookie = s64(p.request(P_NWOAS_DCP_SWAP, buf, W, H, STRIDE, FMT_BGRA8888, 0))
    if cookie <= 0:
        return {"submit_ret": cookie, "status": "SUBMIT_FAILED"}
    packed = int(p.request(P_NWOAS_DCP_POLL, cookie, max_wait_us, out_addr))
    host_ms = (time.monotonic() - t) * 1000.0
    st, stage, swap_id, cmd_result, elapsed, ck, clean_us, idle, _ = struct.unpack(
        POLL_FMT, u.iface.readmem(out_addr, 48))
    return {"cookie": cookie, "status": POLL_STATUS.get(st, str(st)),
            "stage": STAGES.get(stage, str(stage)), "swap_id": swap_id,
            "cmd_result": cmd_result, "applied_us": elapsed, "clean_us": clean_us,
            "endpoint_idle": idle, "host_ms": round(host_ms, 3), "packed": hex(packed)}


def stats(vals):
    return (min(vals), sum(vals) / len(vals), max(vals)) if vals else (0, 0, 0)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", type=Path, default=DEFAULT_BIN, help="local g250E m1n1 binary")
    ap.add_argument("--out", type=Path, help="evidence JSON (must not exist)")
    ap.add_argument("--swaps", type=int, default=120)
    ap.add_argument("--hold-ms", type=int, default=250,
                    help="pause after each APPLIED swap so a person can see the alternation")
    ap.add_argument("--off-seconds", type=float, default=5.0)
    ap.add_argument("--max-wait-us", type=int, default=2000000,
                    help="device-side wait per swap (3 AFK stages x 500 ms timeout)")
    ap.add_argument("--reconfigure-after-power", action="store_true",
                    help="after set_power(on) run display_configure again before the last swap")
    ap.add_argument("--gate-only", action="store_true",
                    help="identity gate only; no DCP command is sent")
    args = ap.parse_args(argv)
    if args.out and args.out.exists():
        print(f"STOP: {args.out} exists", file=sys.stderr)
        return 2

    ev = {"tool": "dcp_swap_test.py", "started_at": time.time(), "args": vars(args).copy()}
    ev["args"] = {k: str(v) for k, v in ev["args"].items()}
    try:
        off, marker = local_identity(args.bin)
        with deadline(60, "connect"):
            iface, p, u = connect()
        with deadline(60, "identity gate"):
            ev["identity"] = live_gate(u, p, off, marker)
    except (Refused, DeadlineExceeded) as exc:
        print(f"[S250] REFUSED: {exc}", file=sys.stderr, flush=True)
        return 2
    print(f"[S250] identity OK {json.dumps(ev['identity'])}", flush=True)
    if args.gate_only:
        return 0

    rc = 1
    started = False
    bufs = []
    info_addr = u.memalign(0x4000, 0x4000)
    out_addr = u.memalign(0x40, 0x40)
    swaps = []
    try:
        with deadline(60, "start"):
            started = True
            ret = s64(p.request(P_NWOAS_DCP_START, START_CONFIGURE, info_addr, 0x4000))
            info = read_info(u, info_addr, 0x4000)
        ev["start_ret"] = ret
        ev["info_start"] = info
        print_info("start", info)
        if ret < 0 or info["hpd"] <= 0:
            raise RuntimeError(f"DCP start failed or no HPD (ret={ret}, hpd={info['hpd']})")
        if info["cur_width"] < W or info["cur_height"] < H:
            raise RuntimeError(f"current mode {info['cur_width']}x{info['cur_height']} "
                               f"smaller than {W}x{H}")

        with deadline(120, "buffers"):
            for left, right in ((RED, BLUE), (GREEN, MAGENTA)):
                a = u.memalign(0x4000, BUF_SIZE)
                bufs.append(a)
                fill_buffer(u, a, left, right)
        ev["buffers"] = [hex(b) for b in bufs]
        print(f"[S250] buffers A(red|blue)={bufs[0]:#x} B(green|magenta)={bufs[1]:#x} "
              f"size={BUF_SIZE:#x}", flush=True)

        for i in range(args.swaps):
            buf = bufs[i % 2]
            with deadline(10, f"swap {i}"):
                draw_counter(p, buf, i)
                r = do_swap(u, p, buf, out_addr, args.max_wait_us)
            r["index"], r["buffer"] = i, "AB"[i % 2]
            swaps.append(r)
            print(f"[S250] swap {i:3d} {r['buffer']} {r['status']} swap_id={r.get('swap_id')} "
                  f"applied_us={r.get('applied_us')} clean_us={r.get('clean_us')} "
                  f"host_ms={r.get('host_ms')}", flush=True)
            if r["status"] != "APPLIED":
                raise RuntimeError(f"swap {i} not APPLIED: {r}")
            time.sleep(args.hold_ms / 1000.0)

        with deadline(30, "power off"):
            ev["power_off_ret"] = s64(p.request(P_NWOAS_DCP_POWER, 0))
        print(f"[S250] set_power(off) ret={ev['power_off_ret']} -- monitor should go dark now "
              f"for {args.off_seconds:g} s", flush=True)
        time.sleep(args.off_seconds)
        with deadline(30, "power on"):
            ev["power_on_ret"] = s64(p.request(P_NWOAS_DCP_POWER, 1))
        print(f"[S250] set_power(on) ret={ev['power_on_ret']}", flush=True)
        if args.reconfigure_after_power:
            with deadline(60, "reconfigure"):
                ev["reconfigure_ret"] = s64(p.request(P_NWOAS_DCP_START, START_CONFIGURE,
                                                      info_addr, 0x4000))
            print(f"[S250] reconfigure ret={ev['reconfigure_ret']}", flush=True)

        with deadline(10, "final swap"):
            draw_counter(p, bufs[0], args.swaps)
            final = do_swap(u, p, bufs[0], out_addr, args.max_wait_us)
        ev["final_swap"] = final
        print(f"[S250] final swap A {final['status']} swap_id={final.get('swap_id')} "
              f"applied_us={final.get('applied_us')}", flush=True)
        time.sleep(2.0)

        with deadline(60, "hpd again"):
            ret = s64(p.request(P_NWOAS_DCP_START, 0, info_addr, 0x4000))
            info2 = read_info(u, info_addr, 0x4000)
        ev["info_end"] = info2
        print_info("end", info2)
        rc = 0 if final["status"] == "APPLIED" and info2["hpd"] > 0 else 1
    except Exception as exc:
        ev["error"] = f"{type(exc).__name__}: {exc}"
        print(f"[S250] ERROR {ev['error']}", flush=True)
    finally:
        if started:
            try:
                with deadline(30, "shutdown"):
                    res = int(p.request(P_NWOAS_DCP_SHUTDOWN, DCP_SLEEP_IF_EXTERNAL))
                ev["shutdown_result"] = res
                print(f"[S250] shutdown result={res:#x} restored={bool(res & SHUT_RESTORED)} "
                      f"unmapped={bool(res & SHUT_UNMAPPED)} done={bool(res & SHUT_DONE)}",
                      flush=True)
                if res & SHUT_UNMAPPED:
                    for b in bufs:
                        u.free(b)
                else:
                    print("[S250] buffers left allocated: DCP may still reference them",
                          flush=True)
            except Exception as exc:
                ev["shutdown_error"] = f"{type(exc).__name__}: {exc}"
                print(f"[S250] SHUTDOWN ERROR {ev['shutdown_error']} -- reboot the Mac mini; "
                      f"if the monitor stays black after reboot, remove power for 10 s",
                      flush=True)
                rc = 1

    applied = [s["applied_us"] for s in swaps if s.get("status") == "APPLIED"]
    clean = [s["clean_us"] for s in swaps if s.get("status") == "APPLIED"]
    host = [s["host_ms"] for s in swaps if s.get("status") == "APPLIED"]
    print("[S250] | metric | count | min | avg | max |", flush=True)
    for name, vals in (("submit->APPLIED (us, device)", applied),
                       ("cache clean before submit (us, device)", clean),
                       ("swap+poll round trip (ms, host)", host)):
        lo, avg, hi = stats(vals)
        print(f"[S250] | {name} | {len(vals)} | {lo:.1f} | {avg:.1f} | {hi:.1f} |", flush=True)
    ev["swaps"] = swaps
    ev["summary"] = {"applied_us": stats(applied), "clean_us": stats(clean),
                     "host_ms": stats(host), "count": len(applied)}
    ev["rc"] = rc
    print("[S250] logs are not a pass: the person at the monitor decides (README-dcp-s250.md)",
          flush=True)
    if args.out:
        tmp = args.out.with_name(args.out.name + ".tmp")
        tmp.write_text(json.dumps(ev, indent=2, default=str) + "\n")
        tmp.replace(args.out)
    return rc


if __name__ == "__main__":
    sys.exit(main())
