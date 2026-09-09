"""Host tests for S195.

Two layers:
  1. iocore.h pure logic driven through ctypes callbacks: pattern determinism,
     the shared-budget write/read loop, short I/O, API failure, mismatch, and
     the deadline behaviour (first transfer checked, final transfer that
     returns past the deadline cannot PASS, backwards QPC is safe, arithmetic
     is bounded).
  2. wintest.c: the REAL iotest.c Win32 wrapper compiled on the host with
     -DIMP= and mock kernel32 bodies, built plain and with ASan/UBSan, driving
     CREATE_NEW/flags/short-write/flush/reopen/cleanup scenarios.
Also checks the ARM64 PE header and the exact single-separator embedded path.
"""
import ctypes as C, subprocess, tempfile, random, struct, sys
from pathlib import Path
root = Path(__file__).resolve().parent
TOTAL, BLOCK, HZ = 256 << 20, 1 << 20, 10_000_000
DEADLINE = HZ * 300
I64MAX, I64MIN = (1 << 63) - 1, -(1 << 63)

class Result(C.Structure):
    _fields_ = [('code', C.c_int), ('offset', C.c_uint64), ('done', C.c_uint32), ('error', C.c_uint32),
                ('expected', C.c_uint64), ('actual', C.c_uint64), ('bytes', C.c_uint64),
                ('words_verified', C.c_uint64), ('elapsed', C.c_int64)]
XFER = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_uint32, C.POINTER(C.c_uint32), C.POINTER(C.c_uint32))
CLOCK = C.CFUNCTYPE(C.c_int64, C.c_void_p)

with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p / 'test.c').write_text('#include "iocore.h"\n'
        'uint64_t pattern(uint64_t o){return s195_pattern(o);}\n'
        'void run(s195_result*r,int v,uint64_t*b,uint64_t t,uint64_t k,s195_transfer x,s195_clock c,void*ctx,int64_t st,int64_t dl){s195_run(r,v,b,t,k,x,c,ctx,st,dl);}\n'
        'int pass(const s195_result*r,uint64_t t){return s195_pass(r,t);}\n'
        'uint64_t elapsed(int64_t n,int64_t s){return s195_elapsed(n,s);}\n'
        'int64_t budget(int64_t hz,uint64_t sec){return s195_budget(hz,sec);}\n'
        'uint64_t ticksus(int64_t t,int64_t hz){return s195_ticks_to_us(t,hz);}\n')
    # build the shim with sanitizers too: any signed-overflow/UB in the header trips UBSan
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-shared', '-fPIC', '-I' + str(root),
                    str(p / 'test.c'), '-o', str(p / 'test.dylib')], check=True)
    lib = C.CDLL(str(p / 'test.dylib'))
    lib.pattern.argtypes = [C.c_uint64]; lib.pattern.restype = C.c_uint64
    lib.run.argtypes = [C.POINTER(Result), C.c_int, C.c_void_p, C.c_uint64, C.c_uint64, XFER, CLOCK, C.c_void_p, C.c_int64, C.c_int64]
    lib.pass_ = lib['pass']; lib.pass_.argtypes = [C.POINTER(Result), C.c_uint64]; lib.pass_.restype = C.c_int
    lib.elapsed.argtypes = [C.c_int64, C.c_int64]; lib.elapsed.restype = C.c_uint64
    lib.budget.argtypes = [C.c_int64, C.c_uint64]; lib.budget.restype = C.c_int64
    lib.ticksus.argtypes = [C.c_int64, C.c_int64]; lib.ticksus.restype = C.c_uint64

    # 1. pattern: independent Python splitmix64 reference, deterministic, offset dependent
    M = (1 << 64) - 1
    def py(o):
        x = o ^ 0xd1b54a32d192ed03; x = ((x ^ (x >> 30)) * 0xbf58476d1ce4e5b9) & M
        x = ((x ^ (x >> 27)) * 0x94d049bb133111eb) & M; return x ^ (x >> 31)
    rng = random.Random(195)
    for _ in range(2000):
        o = rng.randrange(0, TOTAL // 8) * 8; assert lib.pattern(o) == py(o)
    assert lib.pattern(0) != 0 and lib.pattern(8) != lib.pattern(0) and lib.pattern(TOTAL - 8) == py(TOTAL - 8)

    buf = C.create_string_buffer(BLOCK)

    class Fake:
        """Backing store plus fault injection. The clock reads a wall value that
        only transfers advance (by `tick`), so elapsed depends on transfers, not
        on how many times the loop reads the clock. `wall` persists across runs
        on the same Fake, modelling one shared wall clock; `base` seeds it."""
        def __init__(self, short_at=None, short_len=None, fail_at=None, fail_err=0, tick=1, base=0):
            self.data = bytearray(TOTAL); self.pos = 0; self.calls = 0; self.wall = base; self.tick = tick
            self.short_at, self.short_len, self.fail_at, self.fail_err = short_at, short_len, fail_at, fail_err
            self.xfer_w = XFER(self.write); self.xfer_r = XFER(self.read); self.clk = CLOCK(self.clock)
        def clock(self, ctx): return self.wall
        def write(self, ctx, b, n, done, err):
            self.calls += 1
            if self.pos == self.fail_at: err[0] = self.fail_err; return 0
            m = self.short_len if self.pos == self.short_at else n
            self.data[self.pos:self.pos + m] = C.string_at(b, m); self.pos += m; done[0] = m
            self.wall += self.tick; return 1
        def read(self, ctx, b, n, done, err):
            self.calls += 1
            if self.pos == self.fail_at: err[0] = self.fail_err; return 0
            m = self.short_len if self.pos == self.short_at else n
            C.memmove(b, bytes(self.data[self.pos:self.pos + m]), m); self.pos += m; done[0] = m
            self.wall += self.tick; return 1
        def run(self, verify, deadline=DEADLINE, total=TOTAL, start=0):
            r = Result(); self.pos = 0
            lib.run(C.byref(r), verify, C.cast(buf, C.c_void_p), total, BLOCK,
                    self.xfer_r if verify else self.xfer_w, self.clk, None, start, deadline)
            return r

    # 2. full success: write emits the pattern for every byte, read verifies every word
    f = Fake(); w = f.run(0)
    assert w.code == 0 and w.bytes == TOTAL and w.offset == TOTAL and f.calls == TOTAL // BLOCK, (w.code, w.bytes)
    assert f.data[:8] == py(0).to_bytes(8, 'little') and f.data[-8:] == py(TOTAL - 8).to_bytes(8, 'little')
    assert f.data[BLOCK * 7 + 8 * 3:BLOCK * 7 + 8 * 4] == py(BLOCK * 7 + 24).to_bytes(8, 'little')
    assert w.elapsed > 0 and not lib.pass_(C.byref(w), TOTAL)   # a write result alone never passes
    r = Fake(); r.data = f.data; rr = r.run(1)
    assert rr.code == 0 and rr.bytes == TOTAL and rr.words_verified == TOTAL // 8 and lib.pass_(C.byref(rr), TOTAL)
    assert rr.expected == 0 and rr.actual == 0 and rr.error == 0

    # 3. partial write: short transfer on block 5 -> S195_SHORT_IO with offset/done
    f = Fake(short_at=5 * BLOCK, short_len=4096); w = f.run(0)
    assert (w.code, w.offset, w.done, w.bytes) == (2, 5 * BLOCK, 4096, 5 * BLOCK), (w.code, w.offset, w.done)
    assert f.calls == 6 and not lib.pass_(C.byref(w), TOTAL)
    good = Fake(); good.run(0); pr = Fake(short_at=0, short_len=8); pr.data = good.data; r = pr.run(1)
    assert r.code == 2 and r.words_verified == 0 and r.bytes == 0

    # 4. API failure: GetLastError propagated (112 = ERROR_DISK_FULL), loop stops at once
    f = Fake(fail_at=17 * BLOCK, fail_err=112); w = f.run(0)
    assert (w.code, w.offset, w.error, w.bytes) == (1, 17 * BLOCK, 112, 17 * BLOCK) and f.calls == 18
    f = Fake(fail_at=0, fail_err=5); r = f.run(1)
    assert (r.code, r.error, r.words_verified) == (1, 5, 0)

    # 5. deadline is checked before AND after each transfer, equal elapsed still passes.
    #    tick = DEADLINE/4: after-checks see D/4, 2D/4, 3D/4, 4D/4 (=D, in bound) for blocks 0..3;
    #    block 4's after-check sees 5D/4 -> TIMEOUT at offset 5*BLOCK, 4 blocks committed.
    f = Fake(tick=DEADLINE // 4); w = f.run(0)
    assert w.code == 3 and w.offset == 5 * BLOCK and w.bytes == 4 * BLOCK and f.calls == 5, (w.code, w.offset, w.bytes, f.calls)
    assert w.elapsed > DEADLINE

    # 5b. the FIRST transfer is now checked against the SHARED budget. A write that
    #     used the whole budget leaves the read's very first transfer over deadline:
    #     it times out at offset 0 with nothing verified, so it cannot PASS.
    g = Fake(tick=1); g.run(0)                       # fills data, wall advances to 256
    r = Fake(); r.data = g.data; r.wall = DEADLINE + 1  # shared wall already past the budget
    rr = r.run(1, deadline=DEADLINE, start=0)
    assert rr.code == 3 and rr.offset == 0 and rr.words_verified == 0 and rr.bytes == 0
    assert r.calls == 0 and not lib.pass_(C.byref(rr), TOTAL)

    # 5c. final-transfer-past-deadline: every block transfers, but the LAST one returns
    #     after the budget. The post-transfer check rejects it, so no false PASS even
    #     though the data was correct. tick in (D/256, D/255]: only block 255 trips it.
    tick = DEADLINE // 256 + 1
    assert 255 * tick <= DEADLINE < 256 * tick
    g = Fake(); g.run(0)
    r = Fake(tick=tick); r.data = g.data; rr = r.run(1, deadline=DEADLINE, start=0)
    assert rr.code == 3 and rr.offset == TOTAL and rr.bytes == 255 * BLOCK
    assert rr.words_verified == 255 * BLOCK // 8 and not lib.pass_(C.byref(rr), TOTAL)

    # 5d. backwards clock cannot prove bounded completion: fail closed without UB.
    f = Fake(tick=-100, base=1_000_000); w = f.run(0, start=1_000_000)
    assert w.code == 3 and w.bytes == 0 and f.calls == 1

    # 6. mismatch: flip one bit in word 3 of block 200 -> first mismatch offset, expected/actual
    f = Fake(); f.run(0); off = 200 * BLOCK + 3 * 8
    f.data[off] ^= 0x40; r = Fake(); r.data = f.data; rr = r.run(1)
    assert rr.code == 4 and rr.offset == off and rr.expected == py(off) and rr.actual == py(off) ^ 0x40
    assert rr.words_verified == off // 8 and rr.bytes == 200 * BLOCK and not lib.pass_(C.byref(rr), TOTAL)
    f.data[off + 8 * 100] ^= 1; f.data[BLOCK * 10] ^= 1; r = Fake(); r.data = f.data; rr = r.run(1); assert rr.offset == BLOCK * 10
    f = Fake(); f.run(0); f.data[BLOCK * 255:] = bytes(BLOCK); r = Fake(); r.data = f.data; rr = r.run(1)
    assert rr.code == 4 and rr.offset == BLOCK * 255

    # 7. argument guards, including non-positive deadline
    r = Result(); f = Fake()
    lib.run(C.byref(r), 0, C.cast(buf, C.c_void_p), TOTAL + 1, BLOCK, f.xfer_w, f.clk, None, 0, DEADLINE); assert r.code == 5
    lib.run(C.byref(r), 0, None, TOTAL, BLOCK, f.xfer_w, f.clk, None, 0, DEADLINE); assert r.code == 5
    lib.run(C.byref(r), 0, C.cast(buf, C.c_void_p), TOTAL, BLOCK, f.xfer_w, f.clk, None, 0, 0); assert r.code == 5

    # 8. bounded arithmetic and safe clock math (also exercised under UBSan above)
    assert lib.elapsed(10, 5) == 5 and lib.elapsed(5, 10) == M            # backwards -> expired
    assert lib.elapsed(I64MIN, I64MAX) == M                               # unsigned, no overflow UB
    assert lib.budget(HZ, 300) == DEADLINE and lib.budget(0, 300) == 0 and lib.budget(-5, 300) == 0
    assert lib.budget(I64MAX, 300) == I64MAX                              # saturates, no overflow
    assert lib.ticksus(I64MAX, 1) == M
    assert lib.ticksus(I64MAX-1, I64MAX) == M  # conservative saturation for unrepresentable intermediate
    assert lib.ticksus(HZ, HZ) == 1_000_000 and lib.ticksus(0, HZ) == 0
    assert lib.ticksus(I64MAX, HZ) == (I64MAX // HZ) * 1_000_000 + (I64MAX % HZ) * 1_000_000 // HZ

# 9. the actual Win32 wrapper: compile iotest.c via wintest.c, plain and ASan/UBSan
for extra, label in (([], 'plain'), (['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'], 'ASan/UBSan')):
    with tempfile.TemporaryDirectory() as d:
        exe = Path(d) / 'wintest'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-I' + str(root)]
                       + extra + [str(root / 'wintest.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        print('wrapper mock (%s): 7 scenarios, fixed path/flags/cleanup asserted' % label)

# 10. built ARM64 PE and exact single-separator embedded path
exe = (root / 'IOTEST.EXE').read_bytes(); pe = struct.unpack_from('<I', exe, 0x3c)[0]
assert exe[pe:pe + 4] == b'PE\0\0' and struct.unpack_from('<H', exe, pe + 4)[0] == 0xaa64
wide = 'C:\\ProgramData\\NWOAS\\IO195.DAT'.encode('utf-16le')
assert wide in exe and b'KERNEL32.dll' in exe
sep1 = '\\'.encode('utf-16le')      # one wide backslash
assert wide.count(sep1) == 3        # exactly three separators, none doubled
assert (sep1 + sep1) not in exe     # no doubled UTF-16 separator anywhere
assert b'C:\\\\ProgramData' not in exe and b'C:\\ProgramData\\NWOAS\\IO195.DAT' in exe  # ASCII path single-sep

print('PASS: 2000 pattern vectors; 256 MiB write+all-word verify; short I/O; API failure;'
      ' shared-budget deadline (first+final transfer checked, no false PASS); backwards QPC;'
      ' bounded arithmetic; arg guards; Win32 wrapper mock (plain+ASan/UBSan); ARM64 PE; single-separator path')
