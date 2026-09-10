# Main resolution of S224 source review

The review's two prerequisite assumptions were inspected in the actual source
before running the candidate's validation jobs:

1. `hv_exc.c:hv_exc_sync` calls `hv_exc_entry` before `hv_handle_dabort`.
   `hv_exc_entry` acquires `bhl`; `hv_exc_exit` releases it. The MMIO mirror is
   called in that data-abort path. `_hv_exc_proxy` runs `uartproxy_run` inside
   the same locked exception path, with `hv_rendezvous` when time stealing is
   enabled. `hv_exc_proxy` reacquires `bhl` after any CPU-switch wait before
   calling `_hv_exc_proxy`. Action 11 therefore cannot race a different CPU's
   MMIO mirror read. Guest CPUs may run between calls, but their read traps
   cannot pass the held lock. A separate enabled-flag acquire/release protocol
   is neither required nor a substitute for this single-owner serialization.
   All initialization publication happens before guest start.
2. `HV.add_tracer` assigns `self.mmio_maps[zone, ident] = (...)`.
   `DictRangeMap.__setitem__` updates the dictionary entry for that same key;
   it does not append callbacks. S224 uses identical ranges and identifiers.
   The actual S160 callback / S139 controller / S224 overlay integration test
   separately confirms each wrapper calls its old owner once before publishing.
3. `M1N1Proxy.nwoas_nvme_fastpath` explicitly defaults all five trailing
   arguments to zero and forwards all six words. The disable call sets flags=0.
4. The PCI pending snapshot follows Python CQ state exactly. Target-owned I/O
   queue completions never mutate Python's CQ state, either before or after
   this change; Python-owned admin CQ changes happen within wrapped writes.
   Therefore this does not add staleness relative to existing Python reads.
   It deliberately does not repair the pre-existing incomplete fast-CQ pending
   representation; live INTx behavior and CQ phase publication stay unchanged.
5. S160 `local_mask_reads` no longer counts reads intercepted by S224. Use
   S224 register-read counter (action 14); no numerical equivalence is claimed
   between these different diagnostic counters.

No mirror source correction was required by these confirmations. They do not
establish hardware stability; live evidence remains in separate result files.
