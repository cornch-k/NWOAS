# S230 process-harness review corrections

The review describes the initial 11-fixture harness. The final harness has
28 directed cases and was rerun under ASan/UBSan with nonrecovering failures.

1. NVME_GUEST constants are now extracted verbatim from the production nvme.h
   rather than redefined. REFUSED is 2. The header hash is recorded. Extracting
   these declarations avoids importing target-only typedefs into the host ABI.
2. Real nwoas_nvme_link_execute is now extracted with the production request
   structure/constants. Only hv_exc_proxy is mocked: valid response, changed
   lifecycle epoch, bad response, wrong sequence, oversized status and separate
   read/write error translation are checked, including link_busy clearing.
   This remains an injected response test, not a serial physical failure test.
3. Real nwoas_nvme_fastpath_poll is extracted and invoked for CQ backpressure
   and two-command submission. The tests check one-command processing budget.
4. Real guest_ptr is extracted, including the 16KiB boundary rule; IPA lookup
   and DRAM limits remain fake RAM callbacks. NULL handling and boundary are
   checked separately. This is not an MMU hardware conformance test.
5. The initial CQ phase, cache-disabled write flush, post-write flush failure,
   preserved SQ head/no CQE on fatal, full CQ built through 15 real commands,
   SQ/CQ wrap and execute validation boundaries are covered.

No target binary/source change was required by these harness corrections.
The separate S229 production F1/F2 corrections were already in the S230 image.

The first soak assessment used the default THIRTY MINUTE completion marker
against a TEN MINUTE command, and therefore rejected otherwise valid evidence.
qualify.py now supplies the exact marker. Re-assessment kept the original
11 samples, checksums, exit0 and 600-second minimum; their span was 602.505147s.
No hardware rerun or relaxed pass threshold was used for this correction.
