# Main assessment of Opus4.8 integration review

Raw review: ../claude-s156/opus-s170-integration-review-result.json. Direct MMIO range and AppleDT API checks are useful; source review does not establish hardware persistence.

Corrections to review prose:
- The selected S170 control baseline is the already tested S139 8-core USB-A payload, not an arbitrary S131 original. Eight cores, low-memory cap and hiddenXHC1 therefore match that control. S170 should not be compared as a P12-only experiment against the expanded S172 memory payload.
- Main executed memory and S170 builders sequentially; there is no concurrently writing memory agent. Both restore checks passed, and git diff lists only pre-existing submodule dirt. Concurrent-writer handling remains a general tooling limitation, not an observed conflict.
- The review confusingly names outer initialization while concluding after inner initialization. The observed prebootP12 reverted after the host helper ran, and inner `src/payload.c` calls `cpufreq_init` before loading UEFI. That is the leading source explanation; exact hardware instruction not traced. ReadyToBoot is later.
- An enabled tracer does not inherently break reachability: behavior depends on the tracer/hook. The tested harness leaves the CPU register hardware mapping intact.

Hardware trial remains required. APSC stays enabled, no claim of permanently fixed MHz.
