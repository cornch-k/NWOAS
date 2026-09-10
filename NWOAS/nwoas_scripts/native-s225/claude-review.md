# S225 review: admin_payload.c/.h, test.py, test_bounds.c, README.md

Read-only source review. No files other than this one were written inside the
repository; nothing was run against hardware, USB, the UI or device logs.
Independent checks were compiled and run from /tmp against the local Python
sources with bytecode writing disabled.

## Verdict

No byte-equivalence or bounds defect found. The C builder reproduces the
Identify (CNS 0/1/2) and Get Log Page (LID 1/2/3) bytes and statuses of the
current Python admin path exactly, including the Python quirks. The remaining
findings are contract/documentation gaps and test-coverage gaps, none of which
change emitted bytes for the currently deployed configuration.

## What was compared

Oracle path actually executed by the live stack (loader-s223 chain):

- `nvme-s139/controller.py` (`NWOAS_CONTROLLER_DIR`) subclasses
  `nvme-s124/controller.py` and does not override `admin()`. `nvme-s143`
  likewise inherits `admin()` unchanged.
- `nvme-s160/guest_module.py:185-186` builds `WindowWritableNamespace(61279344, ...)`
  with `mdts = MAX_TRANSFER.bit_length()-1-12`, i.e. 8 for
  `NWOAS_MAX_TRANSFER=1048576` (S223) and 4 for the 65536 default.
- `memory-s180/link_module.py:48-58` wraps that in `CachedPair(NamespacePair)`
  with a `LinkNamespace` (capacity 8448). `CachedPair` overrides only `io`,
  `flush` and cache accessors; `admin()` is the S123 `NamespacePair.admin`.
  `LinkNamespace` does not override `ReadOnlyNamespace.admin`.
- `TransportReadOnly` (byte 99 forced to 1) is defined in both
  `transport-s123/guest_module.py` and `memory-s180/link_module.py` but is
  **not applied** in the memory-s180 chain (`CachedPair(_controller.ns, _link)`
  passes the unwrapped namespace). So the live policy is
  `paired=True, primary_readonly=False, volatile_cache=True, mdts=8 or 4`.

test.py's `Policy(61279344, 8448, 8, paired, False, True)` therefore matches
the S223 live configuration. Using `ReadOnlyNamespace(CAPACITY)` instead of
`LinkNamespace` as the link is admin-equivalent (no `admin` override, same
capacity); the substitution avoids the session.json side effect and is fine.

## Byte-equivalence walk-through (C vs Python)

Verified by reading both sides and by execution:

- Flags byte and MPTR (`cmd[1]`, bytes 16..23) rejected with status 2 before
  dispatch: `admin_payload.c:20` vs `controller.py:147` and
  `readonly_namespace.py:40`. Same order relative to CNS/NSID checks, so the
  same status wins when several fields are bad.
- Identify: `dw10>>8` or any of dw11..dw15 nonzero gives status 2 before the
  CNS switch, matching `transport.py:133` and `readonly_namespace.py:50`.
- CNS 0: NSID 1 gives primary capacity in NSZE/NCAP/NUSE, LBADS 12 at byte 130,
  NSATTR byte 99 from policy (Python: ReadOnly sets 1, WindowWritable clears
  it). NSID 2 when paired gives link capacity with byte 99 forced 0
  (`transport.py:143`). Any other NSID gives 0x0b. Matches.
- CNS 1: NSID must be 0 or 0xffffffff, else 0x0b. VID/SSVID 0x1234, SN
  20 bytes, MN 40 bytes space-padded, FR "S92", MDTS from policy, CNTLID 1,
  VER 0x10300, SQES/CQES 0x66/0x44, NN 1 or 2 (`transport.py:147`), VWC byte
  525 from policy (`writable_namespace.py:74`). Matches.
- CNS 2: list contains 1 if NSID < 1 and 2 if paired and NSID < 2; no NSID
  validation and no error for 0xffffffff, exactly as `transport.py:136` and
  `readonly_namespace.py:73`. Matches.
- CNS >= 3: status 2. Matches.
- Get Log Page: LID must be 1/2/3, NUMD from dw10[31:16] | dw11[15:0],
  length `(NUMD+1)*4 <= 4096`, dw12/dw13 must be 0. NSID, dw10 bits 8..15
  (LSP/RAE), dw11 upper half (LSI), dw14 and dw15 are ignored on both sides.
  LID 2 writes 300 at bytes 1..2 and 100 at byte 3; the C code omits Python's
  `length>=4` guard, which is dead on both sides because the minimum length is
  4. LID 3 writes byte 0 = 1 and "S93" padded at 8..15 only when length >= 16.
  Matches.
- Error paths return before any write to `out`; success paths write exactly
  `bytes` bytes and leave the tail untouched.

### Independent execution

Rebuilt the dylib from `admin_payload.c` into /tmp and compared against
`nvme-s139.Controller` + nvme-s124 namespaces + s123 `NamespacePair`:

| Configuration axis | Values covered |
|---|---|
| primary namespace | WindowWritableNamespace, ReadOnlyNamespace, TransportReadOnly-wrapped |
| paired | False, True |
| mdts | 0, 4, 8 |
| Identify | all 256 CNS values x NSID {0,1,2,3,0xffffffff} |
| Get Log Page | all 256 LIDs x NUMD {0..4, 1022, 1023, 1024, 0xffff, 0x10000, 0xffffffff} |
| random | 4000 fully random 64-byte commands per configuration (half biased to valid) |

Result: 145,728 commands, 0 mismatches in return code, status, length,
payload bytes, or untouched tail. This covers the `primary_readonly=True` and
`volatile_cache=False` policy paths that test.py never exercises.

Struct layouts: C `sizeof`/`offsetof` for the policy (24 bytes; offsets 8, 16,
17, 18, 19) and result (8 bytes; `bytes` at 4) match the ctypes definitions in
test.py.

## Bounds and API contract

- Every write is inside `out[0..4095]`; `capacity >= 4096` is enforced and
  `bytes <= 4096` is checked before `zero(out, bytes)`. NUMD arithmetic is done
  in `uint64_t` and cannot overflow. test_bounds.c canaries and ASan/UBSan
  agree.
- All reads of `cmd` happen before the first write to `out`, so the code is in
  fact overlap-safe even though the header demands disjoint buffers. Not a
  defect; the stricter documented contract is fine.
- `-1` is returned for a NULL `result`, so the "result set" promise cannot hold
  in that case; expected.

## Findings

### F1. Policy constraints are enforced but undocumented (contract)

`admin_payload.c:17-18` rejects `primary_lbas == 0`, `primary_lbas >= 2^40`,
`mdts > 8`, and (when paired) the same range check on `link_lbas`. The header
and README only say "invalid API arguments/policy". The `mdts > 8` cap is
stricter than Python (`ReadOnlyNamespace.mdts` is any byte). It cannot bite
today because nvme-s160 restricts `NWOAS_MAX_TRANSFER` to 65536 or 1048576
(mdts 4 or 8), but a future caller will not learn the limits from the header.
Document the four constraints in admin_payload.h. Severity: low.

### F2. `result` is written on NOT_HANDLED and on -1 (contract)

`admin_payload.c:15` sets `status=2, bytes=0` before validating anything, so a
return of 0 (belongs to another handler) leaves `status=2` behind. The header
does not say what `result` holds on return 0/-1. A caller that publishes
`result.status` without checking the return code would complete a Create CQ or
Get Features command with Invalid Field. Either document "result is
unspecified unless the return is 1" or leave it untouched on 0/-1.
Severity: low, but this is the one place the contract can mislead an
integrator.

### F3. "No libc import" is a build-flag property, not a source property (claim)

README: "There is no allocation, libc import ...". True for the freestanding
ARM64 object: `llvm-nm out/payload-arm64.o` shows only `T nwoas_admin_payload`
and no undefined symbols. But the hosted dylib built by the same source for
test.py imports `_bzero` (clang turned the `zero()` loop into a call). The
property therefore depends on `-ffreestanding -fno-builtin`; if the eventual
integration builds with different flags it silently acquires a memset/bzero
dependency. test.sh does enforce the empty undefined-symbol list, which is
good. Recommend stating the required flags in the README and header.
Severity: low.

### F4. test.py coverage gaps (test)

test.py only exercises one policy point (`mdts=8, primary_readonly=False,
volatile_cache=True`). The `primary_readonly` and `volatile_cache` branches
in the C code and the mdts field are compared only at that one point. My run
covers them (0 mismatches), but the shipped test should too: add
`ReadOnlyNamespace` as primary and mdts 4 (the default-transfer live value).
The fuzz also never randomises bytes 2..3, 8..15 or 24..39; harmless because
neither side reads them, but full-random commands are cheap. Severity: low.

### F5. test_bounds.c "large policy values" is one case (claim)

README: "canary tests cover ... large policy values". test_bounds.c tests only
`primary_lbas = UINT64_MAX`. The paired `link_lbas` range check and the
`mdts > 8` rejection are not exercised anywhere. Severity: low; add
`link_lbas = 1<<40` with `paired=true` and `mdts = 9` expecting -1.

### F6. README oracle wording (claim)

README says the builder matches "the current S139/S124 controller and S123
NamespacePair" and validation is "against the actual Python stack". The live
chain is nvme-s139 controller + nvme-s160 guest_module namespace +
memory-s180 `CachedPair`. All of those inherit the admin path unchanged, so
the equivalence claim holds, but the README should say the oracle is the
inherited admin path, and that the memory-s180 chain does not apply
`TransportReadOnly` (so NSATTR byte 99 is 0 for NS1 in production). The
12,182 count is correct: 2 x (35 + 56 + 6000).

### F7. Reproduced Python leniencies (note, not a defect)

The C code faithfully reproduces behaviours that are not spec-conformant:
Get Log Page ignores NSID, LSP/RAE, LSI, dw14/dw15; Identify CNS 2 with
NSID 0xffffffff succeeds with an empty list; CNS 0 for NS2 reports NSATTR 0
regardless of policy. README already states quirks are preserved for byte
compatibility. Any future "fix" must change both sides together or the
differential test breaks by design.

## Things confirmed as correct

- `text()` pads with spaces after the terminator and never reads past a short
  string; matches `ljust`. Serial string is exactly 20 bytes.
- `put(out+4*i++, ...)` sequencing is well defined (single modification per
  full expression).
- Status values are the Python unshifted convention (0, 2, 0x0b); the caller's
  `(status<<1)|phase` packing in `controller.py:217` still applies.
- Sources (09:36) predate out/ and differential-result.json (09:38), so the
  recorded 12,182-command pass corresponds to the reviewed sources.
- No DMA, PRP, queue, allocation, file or device access in admin_payload.c;
  nothing in the reviewed files installs or wires the builder.

## Suggested follow-ups (no code changed)

1. Add the policy constraints, the result-on-0/-1 rule and the required
   freestanding build flags to admin_payload.h and README.
2. Extend test.py with a ReadOnlyNamespace primary and mdts 4; extend
   test_bounds.c with `link_lbas` and `mdts` rejection cases.
3. Reword README's oracle description per F6.
