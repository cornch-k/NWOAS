# S162 interpretation checkpoint

Opus report in claude-s156/opus-s162-intmc-result.md has useful source pointers but overstates evidence:
- Snapshot SQempty/noIOactive/CQpending2 does NOT prove stale completion, nor "SQ idle >10min" during the S160a crash. The 10min idle belongs to a separate S160b run. New completions can have arrived from seven other cores while CPU0 completes a DPC.
- It does not rule out live reassert, timing starvation, or timer interaction.
- The driver uses CQ status phase != context+0xac to indicate availability; context may hold last-consumed phase. Do not label phase equality as availability.
- Repeated PC at storport+d8fc is ADD SP immediately after emulated INTMC store, not a polling loop.

Main exact-image finding:
NVMeMaskInterrupt +1d404 reads adapter+0x19; +1d408 CBNZ skips INTMS write if the flag is already set. First mask writes INTMS at+1d438 then sets flag at+1d43c.
NVMeCompletionDpcRoutine writes INTMC via call+18920 and only clears flag at+18924, after helper return.
Thus an interrupt injected synchronously on unmask before the guest retires ADDSP/RET/STRB can invoke an ISR that skips re-masking because flag is still1. If CQ entries are present, repeated level reassertion can starve the in-progress DPC. This mechanism is plausible with live CQ entries; no stale phase premise required.
Need bounded event/IRQ observations or an isolated delayed-delivery comparison to establish causality. Do not patch the Windows driver, disable watchdog, or permanently suppress level IRQ.

Fable follow-up report also overclaimed a missing INTMS gate: the actual
nwoas_nvme_fastpath_update_irq includes !(mask & 1), and Controller.update_irq
does likewise. local_mask_allowed controls ownership of register emulation,
not IRQ masking. No such missing-gate fix is warranted. The vulnerable window
is after INTMC has legitimately cleared mask and before guest byte19 is cleared.
Repeated samples do not mean the DPC repeatedly executes that epilogue; it can
remain preempted at the same PC.

Measured S162: PAR stage1 and combined ff000009de580b80, CQ IPA9de580000;
Normal WB, inner-shareable. No NC/WB mismatch evidence. One intentional short
shell query, then immediate resume; no uninterrupted-soak claim for this run.
