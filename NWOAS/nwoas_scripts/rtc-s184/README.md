# S184 — read-only T8103 sera PMU RTC helper (portable C, OFFLINE)

Status: **standalone helper plus host tests. Not built into firmware, not run
on hardware, no file outside `nwoas_scripts/rtc-s184/` was touched.** Main
integrates this into the S181 firmware candidate after review and after the
separate SMC `CLKM` comparison.

## What it does

`nwoas_sera_rtc_read()` performs exactly three SPMI `EXT_READL` transactions
through the SoC controller at `0x23d0d9300`, slave id 15:

| # | PMU address | bytes | meaning |
|---|---|---|---|
| 0 | `0xd002` | 6 | RTC counter, 32.16 fixed point (`counter0`) |
| 1 | `0xd100` | 6 | stored offset, 33.15 fixed point (`offset`) |
| 2 | `0xd002` | 6 | RTC counter again (`counter1`) |

and decodes `counter1 + (offset << 1)` modulo 2^49 as a signed 32.16 value into
signed Unix seconds plus a 16-bit fraction (and nanoseconds). The physical
counter `CNTPCT_EL0` is sampled immediately after `counter1` arrives so the
caller can advance the snapshot later.

**Only MMIO writes ever performed:** the three command words
`0xd0028f3d`, `0xd1008f3d`, `0xd0028f3d` to `base+4`. No PMU register write,
no SMC access, no reset or power function exists in this code.

## Files

| Path | Purpose |
|---|---|
| `nwoas_sera_rtc.h` | API, constants, protocol and arithmetic notes |
| `nwoas_sera_rtc.c` | Implementation. C99, no libc, no UEFI headers, no allocation, no globals |
| `tests/test_sera_rtc.c` | Mock SPMI controller behind the callback API plus 429 checks |
| `run_tests.sh` | plain `-O2`, ASan+UBSan, C11 `-Wc++-compat`, C++17, freestanding arm64 object with undefined-symbol audit |
| `build/` | generated |

## Evidence the arithmetic is built on

* OpenBSD `sys/arch/arm64/dev/aplpmu.c` (copy in `rtc-s183/aplpmu.c`):
  `time = counter + (offset << 1)`, `tv_sec = time >> 16`,
  `tv_usec = ((time & 0xffff) * 1000000) >> 16`; settime stores
  `((T - counter) >> 1)` in 6 bytes at `0xd100`.
* `rtc-s178/hardware-s180-evidence.json` (S180 bounded probe, hardware):
  `counter0 = 0x085075720348`, `counter1 = 0x0850757203de`,
  `offset = 0x31289eae70e3`, headers `0x003f0f3d` three times, `cntfrq` 24 MHz.
  With the OpenBSD formula this decodes to **1788981966 = 2026-09-09T19:26:06Z**,
  within 2 s of the probe's own host timestamp 1788981964.2. The earlier
  "raw counter + offset, >> 15" reading (Linux `CLKM` formula applied to the raw
  PMU counter) gave 1928472640 (2031), which is why S180 was marked
  implausible. The helper's hardware-vector test pins both numbers.
* Linux `drivers/spmi/spmi-apple-controller.c`: STATUS `+0` (bit 24 = RX
  empty), CMD `+4`, RSP `+8`; command packing
  `opc | sid<<8 | addr<<16 | (len-1) | 1<<15`; reply = 1 status word then data
  words little endian, bytes beyond `len` discarded.
* The reply header low 16 bits (`0x0f3d`) echo `sid<<8 | opc byte`; the helper
  checks exactly that and tolerates the upper bits (hardware showed `0x3f`,
  plausibly one ack bit per byte, not verified, not enforced).

**Not established here:** whether `0xd002` equals SMC `CLKM`. The 2 s
agreement is strong evidence that `0xd002 + offset` is the wall clock, but the
CLKM comparison remains main's separate measurement.

## Bus discipline

Per transaction:

1. Read STATUS once. Require RX empty (bit 24 set) **and** TX count (bits 7..0)
   zero. Otherwise return `E_FIFO_NOT_IDLE` with nothing written and nothing
   read from RSP; a queued transaction belonging to someone else is preserved.
2. Write the command word once.
3. For each of three response words: poll STATUS until RX not empty, bounded
   by **both** a 100 ms physical-counter deadline (`cntfrq / 10` ticks,
   wrap-safe unsigned subtraction) and a finite poll cap (200000). Deadline
   gives `E_TIMEOUT`; the cap gives `E_POLL_CAP` and guarantees termination
   with a frozen counter.
4. Verify the header echo, then read STATUS once more. If RX is not empty an
   extra reply is present: return `E_EXTRA_RESPONSE`, leave the word in the
   FIFO, do not drain, do not retry.

Across the whole call: at most three CMD writes, never more after an error.
`cntfrq` must be within 1 MHz..4 GHz (T8103: 24 MHz) or nothing is touched.

## Validation of the clock

* `counter1 < counter0` → `E_COUNTER_BACKWARD`.
* `counter1 - counter0 > 32768` (0.5 s in 32.16 units; hardware showed 150
  units over 6.8 ms) → `E_COUNTER_SPAN`.
* 49-bit signed sum negative → `E_NEGATIVE_TIME`.
* seconds outside 2000-01-01..2099-12-31 (EFI_TIME range) → `E_RANGE`.

Every error is explicit; `utc_seconds` is only meaningful on `NWOAS_SERA_OK`.
Raw fields collected so far stay in the result for logging.

## Integration advice (for the S181 candidate)

* Call `nwoas_sera_rtc_read()` **once**, from `LibRtcInitialize` in DXE
  before `ExitBootServices`, with callbacks that do `MmioRead32/MmioWrite32`
  on the physical SPMI window and read `CNTPCT_EL0`/`CNTFRQ_EL0` through the
  physical-counter timer library (m1n1 only offsets the virtual counter).
* Keep only the integers from the result in module globals:
  `utc_seconds`, `nanoseconds`, `cntpct`, `cntfrq`. This is the same shape as
  the S181 seed (`epoch`, `cntpct`, `cntfrq`), so the S181 advance path
  (`nwoas_rtc_advance`) can consume it unchanged with the `SPMI` source flag.
* `LibGetTime` at runtime advances that snapshot from `CNTPCT_EL0` only.
  **Do not retain an MMIO pointer, do not call this helper after
  ExitBootServices.** The SPMI window is not in the runtime memory map and
  `SetVirtualAddressMap` will not translate it.
* On any non-OK status log the status string and the raw fields, then fall
  back to the existing seed or `EFI_NOT_READY`. Never retry in a loop at
  boot; one bounded attempt is the contract.
* The SPMI controller may be shared with other DXE drivers (PMU nvmem, power).
  `E_FIFO_NOT_IDLE` at init means someone else is mid-transaction; treat as
  "no clock" rather than draining.
* The fraction is one 1/65536 unit low when the raw counter is odd (settime
  discards bit 0). Seconds are exact; sub-second is consistent, not absolute.

## Tests

```
./run_tests.sh
```

Covers: constant/packing cross-check against the Linux formula and the S180
header value; the S180 hardware vector end to end (writes, headers, seconds,
fraction, cntpct pairing, exact RSP read count); settime round trip for
positive and negative offsets incl. odd counters; the 49-bit wrap versus the
unreduced 64-bit sum; fixed-point fraction to ns (0, 1/2, 1/4, LSB, max,
masked input, agreement with OpenBSD `tv_usec`); overflow at all-ones,
2^48 wrap, most-negative value, zero clock, window edges ±1 s both ways, a
3000+ sample sweep over counters × wall times; full read with a negative
offset; counter derived from CNTPCT with the physical counter near
`UINT64_MAX` (wrap-safe deadline); timeout with advancing counter (elapsed
ticks equal the deadline), frozen counter (poll cap, exact poll count), slow
reply under frozen counter, reply loss after transaction 1 and 2, header-only
reply; non-idle FIFO via RX word, TX count 1 and TX bit 7 (no CMD write, FIFO
preserved); malformed header (opcode, sid, byte order), tolerated upper bits,
extra reply word left in FIFO, garbage in bytes 6..7 ignored; counter
backward, span limit ±1, identical counters, 1999 clock, zero offset,
negative clock; NULL ops/callbacks/result, cntfrq 0/min-1/max+1/all-ones
without any MMIO, cntfrq at max; and the exact write log (three words to
`base+4`, opcode byte `0x3d`, never `EXT_WRITEL`) on every path.

Last run (2026-09-10, Apple clang, arm64 host): plain and ASan+UBSan
429 checks, 0 failures, no sanitizer findings; C11/C++ hygiene OK;
freestanding `arm64-none-elf` object has no undefined symbols.

Main review: changed deadline from100ms perword to100ms sharedpertransaction. Checkelapsedbeforeacceptingreadydata, so a late reply cannot skiptimeout. Finitepollcapstillapplies. Runtime snapshotmustalsoverifyphysicalcounterwindow; integrationaddsitsowncounterspancheck.
