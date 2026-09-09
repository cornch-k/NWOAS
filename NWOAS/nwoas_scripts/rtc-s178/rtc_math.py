#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Pure conversion helpers for the Apple Silicon (T8103) RTC value, mirroring
Linux drivers/rtc/rtc-macsmc.c (macsmc_rtc_get_time):

    now_seconds = sign_extend64(ctr + off, 47) >> 15

  ctr : 48-bit counter at 32768 Hz   (Linux: SMC key "CLKM", 6 bytes)
  off : 48-bit offset                (Linux: PMU nvmem "rtc_offset" @0xd100, 6 bytes)

Both are little-endian 6-byte values. No hardware access here; offline only.
Run `python3 rtc_math.py` for the self-test.
"""
import datetime
import struct

RTC_BYTES = 6
RTC_BITS = 8 * RTC_BYTES
RTC_SEC_SHIFT = 15  # 32768 Hz
MASK48 = (1 << RTC_BITS) - 1


def le48(b):
    """6 little-endian bytes -> unsigned int."""
    if len(b) < RTC_BYTES:
        raise ValueError("need %d bytes, got %d" % (RTC_BYTES, len(b)))
    return int.from_bytes(bytes(b[:RTC_BYTES]), "little")


def sign_extend48(v):
    v &= MASK48
    return v - (1 << RTC_BITS) if v & (1 << (RTC_BITS - 1)) else v


def rtc_to_epoch(ctr, off):
    """Linux formula. ctr/off are unsigned 48-bit ints. Returns Unix seconds (int)."""
    return sign_extend48((ctr + off) & MASK48) >> RTC_SEC_SHIFT


def rtc_to_epoch_ticks(ctr, off):
    """Same but returns (seconds, remainder_ticks_of_32768)."""
    total = sign_extend48((ctr + off) & MASK48)
    return total >> RTC_SEC_SHIFT, total & ((1 << RTC_SEC_SHIFT) - 1)


def epoch_to_utc_str(sec):
    return datetime.datetime.fromtimestamp(sec, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")


# --- proposed seed blob carried in ADT /chosen "nwoas,rtc-snapshot" (little endian) ---
SNAP_MAGIC = 0x4E525443  # 'CTRN' little-endian bytes "CTRN"? (stored as u32 0x4E525443)
SNAP_VERSION = 1
SNAP_FLAG_VALID = 1 << 0
SNAP_SRC_SPMI = 1 << 1   # counter read directly from PMU over SPMI
SNAP_SRC_SMC = 1 << 2    # counter read from SMC key CLKM
SNAP_FMT = "<IIQQII"     # magic, version, epoch_seconds, cntpct_at_snapshot, cntfrq_hz, flags
SNAP_SIZE = struct.calcsize(SNAP_FMT)  # 32 bytes


def pack_snapshot(epoch_seconds, cntpct, cntfrq, flags):
    return struct.pack(SNAP_FMT, SNAP_MAGIC, SNAP_VERSION, epoch_seconds, cntpct, cntfrq, flags)


def unpack_snapshot(blob):
    magic, ver, epoch, cntpct, cntfrq, flags = struct.unpack(SNAP_FMT, bytes(blob[:SNAP_SIZE]))
    if magic != SNAP_MAGIC or ver != SNAP_VERSION:
        raise ValueError("bad snapshot header")
    return dict(epoch=epoch, cntpct=cntpct, cntfrq=cntfrq, flags=flags)


def uefi_now(snapshot, cntpct_now):
    """What the proposed UEFI LibGetTime would compute (integer seconds + ns)."""
    delta = (cntpct_now - snapshot["cntpct"]) & ((1 << 64) - 1)
    sec = snapshot["epoch"] + delta // snapshot["cntfrq"]
    ns = (delta % snapshot["cntfrq"]) * 1_000_000_000 // snapshot["cntfrq"]
    return sec, ns


def plausible(epoch, lo=datetime.datetime(2024, 1, 1, tzinfo=datetime.timezone.utc),
              hi=datetime.datetime(2036, 1, 1, tzinfo=datetime.timezone.utc)):
    return int(lo.timestamp()) <= epoch < int(hi.timestamp())


def _selftest():
    # 1. Known-answer: 2026-09-10 00:00:00 UTC encoded with a zero offset
    t = int(datetime.datetime(2026, 9, 10, tzinfo=datetime.timezone.utc).timestamp())
    ctr = (t << RTC_SEC_SHIFT) & MASK48
    assert rtc_to_epoch(ctr, 0) == t
    # 2. Linux set_time semantic: off = (t << 15) - ctr  (mod 2^48) restores t for any ctr
    for ctr in (0, 1, 0x1234_5678_9ABC, MASK48, (1 << 47) + 5):
        off = ((t << RTC_SEC_SHIFT) - ctr) & MASK48
        assert rtc_to_epoch(ctr, off) == t, hex(ctr)
    # 3. Negative wrap: counter small, offset "negative" in 48-bit two's complement
    off_neg = (-(1 << RTC_SEC_SHIFT)) & MASK48  # -1 second
    assert rtc_to_epoch(0, off_neg) == -1
    # 4. byte helpers
    assert le48(bytes.fromhex("bc9a78563412")) == 0x123456789ABC
    # 5. snapshot pack/unpack and UEFI arithmetic at 24 MHz
    blob = pack_snapshot(t, 1_000_000_000, 24_000_000, SNAP_FLAG_VALID | SNAP_SRC_SPMI)
    assert len(blob) == SNAP_SIZE == 32
    s = unpack_snapshot(blob)
    sec, ns = uefi_now(s, 1_000_000_000 + 24_000_000 * 3600 + 12_000_000)
    assert sec == t + 3600 and ns == 500_000_000, (sec, ns)
    # 6. plausibility gate
    assert plausible(t) and not plausible(0) and not plausible(int(datetime.datetime(2022, 5, 7, tzinfo=datetime.timezone.utc).timestamp()))
    print("rtc_math selftest OK;", epoch_to_utc_str(t), "ctr=%#x" % ctr)


if __name__ == "__main__":
    _selftest()
