# S227 — bounded local admin submission/completion engine

Uninstalled freestanding C foundation composing S226 commands and S225 payloads.
It uses caller-provided fake-memory callbacks in tests; no hardware, physical
ANS driver, guest mapping or transport change is installed by this component.

The engine fetches a copied 64-byte command from an admin SQ, leaves one CQ
slot unused, handles held AER without a completion, validates the complete
one/two-span PRP payload before any payload write, and publishes a 14-byte CQE
body followed by a caller barrier and final 16-bit status/phase store. A write,
fetch or descriptor reconciliation failure is fatal. Invalid PRP is command
Invalid Field. It compares completed bytes, CQ wrap/phase and state against
the actual S139 Python Controller; splitting its one 16-byte CQ write into
ordered body/status writes is an intentional publication improvement.

CQ head acknowledgement only retires entries and updates pending. It never
fetches another command. SQ-tail writes service at most 256 commands. A separate
poll API is available for an explicitly scheduled owner, not a completion DPC.
Reset invalidates the ring and held AER; CC-disable preserves features, full
reset clears them. Configure is transactional and rejects replacing an active
ring. There is no asynchronous work inside this component. The enclosing owner
must cancel/drain old physical work, reconcile I/O descriptors and aggregate
admin/I/O interrupt state under the existing lock. The lifecycle counter alone
does not implement external stale-completion protection.

All entry points are serialized; callbacks cannot reenter. Mapping validity and
callback objects must remain stable during one call. Memory callbacks transfer
all bytes or report failure; they must only expose validated RAM. All software
objects/buffers are disjoint. Queue/PRP addresses are integers, never dereferenced
by this code. The range checker conservatively rejects spans that would require
an unrepresentable uint64 end address. A failed status write is a controller
fault; no rollback or successful-completion claim is made.

Run `bash nwoas_scripts/native-s227/test.sh`:
- 11,830 comparisons, 7,273 submitted commands, 21 configurations; actual S139
  full fake-RAM bytes, ring/admin state, cache callbacks and interrupt level.
- 3,176 CQ acknowledgements perform no fetch; 2,259 full-CQ submissions and
  411 observed phase wraps exercise backlog and wrap behavior.
- Directed ASan/UBSan cases inject read, each payload/CQE write, invalid ranges,
  reconcile and cache failures; phase-store callback ordering is asserted.
- 50,000 additional malformed commands through valid rings under sanitizers.
- Combined S227/S226/S225 freestanding ARM64 object has no undefined imports.

Control register ownership, physical backend/IRQ integration and NS2 support
remain future work. Passing these tests does not establish hardware compatibility.

Final review corrections add 65 actual-oracle comparisons and 10,000
dispatch-biased sanitized commands after the 50,000 malformed-header cases.
The biased pass asserts 60,000 command reads, 59,999 completions and one held
AER cumulatively; see test_faults.c and claude-resolution.md.
