#ifndef S195_IOCORE_H
#define S195_IOCORE_H
/* Pure, OS-free write/read integrity loop for S195. Included by the ARM64
 * Windows executable and by the host tests through a cc-built shared library.
 * All I/O and clock access goes through callbacks so tests can inject short
 * transfers, API failures, deadline expiry and corrupted data.
 *
 * Time model: the caller captures one clock reading (start) before the write
 * loop and passes that same start, plus one budget in ticks, to every loop.
 * That is the single 5-minute wall budget shared across write, flush, reopen
 * and read. The budget is checked before AND after each transfer, so a call
 * that returned only after the budget expired (including the very last one)
 * cannot produce a false PASS. It does not and cannot interrupt a transfer
 * that is already inside the kernel. */
#include <stdint.h>

#define S195_TOTAL_BYTES 268435456ULL   /* exactly 256 MiB */
#define S195_BLOCK_BYTES 1048576ULL     /* 1 MiB per WriteFile/ReadFile */
#define S195_DEADLINE_SECONDS 300ULL    /* 5 min wall bound, checked around I/O calls */

enum {
 S195_OK = 0,
 S195_API_FAIL = 1,   /* callback returned false; result.error holds GetLastError */
 S195_SHORT_IO = 2,   /* callback succeeded but transferred fewer bytes than asked */
 S195_TIMEOUT = 3,    /* shared budget exceeded before or after an I/O call */
 S195_MISMATCH = 4,   /* read-back word differs from expected pattern */
 S195_BAD_ARGS = 5
};

typedef int (*s195_transfer)(void *ctx, void *buffer, uint32_t length, uint32_t *done, uint32_t *error);
typedef int64_t (*s195_clock)(void *ctx);

typedef struct {
 int code;
 uint64_t offset;         /* byte offset of the failing block or first mismatching word */
 uint32_t done;           /* bytes reported for the failing transfer */
 uint32_t error;          /* GetLastError value on S195_API_FAIL */
 uint64_t expected;       /* pattern word at offset on S195_MISMATCH */
 uint64_t actual;
 uint64_t bytes;          /* bytes fully transferred (and verified in read mode) */
 uint64_t words_verified; /* read mode only */
 int64_t elapsed;         /* clock ticks from this loop's entry to its exit */
} s195_result;

/* Deterministic offset-dependent 64-bit word (splitmix64 finaliser over the
 * byte offset). Distinct constant from the S171 memory pattern. */
static inline uint64_t s195_pattern(uint64_t byte_offset) {
 uint64_t x = byte_offset ^ 0xd1b54a32d192ed03ULL;
 x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
 x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
 return x ^ (x >> 31);
}

static inline void s195_fill(uint64_t *buffer, uint64_t byte_offset, uint64_t block) {
 for (uint64_t w = 0; w < block / 8; w++) buffer[w] = s195_pattern(byte_offset + w * 8);
}

/* A clock that moved backwards or became negative cannot establish a
 * bounded successful run. Return an out-of-budget sentinel, without signed UB. */
static inline uint64_t s195_elapsed(int64_t now, int64_t start) {
 if (now < 0 || start < 0 || now < start) return UINT64_MAX;
 return (uint64_t)now - (uint64_t)start;
}

/* hz * seconds saturated to INT64_MAX to avoid signed overflow on an absurd
 * or hostile frequency. Returns 0 for a non-positive hz. */
static inline int64_t s195_budget(int64_t hz, uint64_t seconds) {
 if (hz <= 0 || seconds == 0) return 0;
 uint64_t h = (uint64_t)hz;
 if (h > (uint64_t)INT64_MAX / seconds) return INT64_MAX;
 return (int64_t)(h * seconds);
}

/* ticks -> microseconds without overflowing ticks * 1000000 for long hangs. */
static inline uint64_t s195_ticks_to_us(int64_t ticks, int64_t hz) {
 if (hz <= 0 || ticks <= 0) return 0;
 uint64_t t = (uint64_t)ticks, h = (uint64_t)hz;
 uint64_t q = t / h, r = t % h;
 if (q > UINT64_MAX / 1000000ULL || r > UINT64_MAX / 1000000ULL) return UINT64_MAX;
 uint64_t whole = q * 1000000ULL, fraction = r * 1000000ULL / h;
 return whole > UINT64_MAX - fraction ? UINT64_MAX : whole + fraction;
}

/* verify=0: fill buffer with pattern and call transfer (write side).
 * verify=1: call transfer into buffer then check every word (read side).
 * start/deadline_ticks describe the shared budget: the deadline is
 * start + deadline_ticks, checked before every transfer (including the
 * first) and again after every transfer returns. */
static inline void s195_run(s195_result *r, int verify, uint64_t *buffer, uint64_t total,
                            uint64_t block, s195_transfer transfer, s195_clock clock,
                            void *ctx, int64_t start, int64_t deadline_ticks) {
 r->code = S195_OK; r->offset = 0; r->done = 0; r->error = 0; r->expected = 0; r->actual = 0;
 r->bytes = 0; r->words_verified = 0; r->elapsed = 0;
 if (!buffer || !total || !block || (block & 7) || total % block || block > 0xffffffffULL ||
     !transfer || !clock || deadline_ticks <= 0) { r->code = S195_BAD_ARGS; return; }
 uint64_t budget = (uint64_t)deadline_ticks;
 int64_t run_start = clock(ctx);
 for (uint64_t at = 0; at < total; at += block) {
  /* before every transfer, first one included */
  if (s195_elapsed(clock(ctx), start) > budget) {
   r->code = S195_TIMEOUT; r->offset = at; r->elapsed = (int64_t)s195_elapsed(clock(ctx), run_start); return;
  }
  if (!verify) s195_fill(buffer, at, block);
  uint32_t done = 0, error = 0;
  int ok = transfer(ctx, buffer, (uint32_t)block, &done, &error);
  r->offset = at; r->done = done;
  if (!ok) { r->code = S195_API_FAIL; r->error = error; r->elapsed = (int64_t)s195_elapsed(clock(ctx), run_start); return; }
  if (done != block) { r->code = S195_SHORT_IO; r->elapsed = (int64_t)s195_elapsed(clock(ctx), run_start); return; }
  /* after the transfer returns: a call that ran past the budget (including the
   * final block) fails here instead of counting toward a PASS */
  if (s195_elapsed(clock(ctx), start) > budget) {
   r->code = S195_TIMEOUT; r->offset = at + block; r->elapsed = (int64_t)s195_elapsed(clock(ctx), run_start); return;
  }
  if (verify) {
   for (uint64_t w = 0; w < block / 8; w++) {
    uint64_t want = s195_pattern(at + w * 8), got = buffer[w];
    if (got != want) {
     r->code = S195_MISMATCH; r->offset = at + w * 8; r->expected = want; r->actual = got;
     r->elapsed = (int64_t)s195_elapsed(clock(ctx), run_start); return;
    }
    r->words_verified++;
   }
  }
  r->bytes += block;
 }
 r->offset = total; r->done = 0;
 r->elapsed = (int64_t)s195_elapsed(clock(ctx), run_start);
}

/* Full PASS requires a clean read loop that verified every word of total. A
 * TIMEOUT (code != S195_OK) can never pass, so a final read that returned
 * after the budget expired is rejected here too. */
static inline int s195_pass(const s195_result *read, uint64_t total) {
 return read->code == S195_OK && read->bytes == total && read->words_verified == total / 8;
}
#endif
