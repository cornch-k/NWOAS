# S143: break the NVMe completion-DPC refill loop

The S141 dump `050722-3015-01.dmp` is bugcheck `0x133` parameter 1 zero and
again contains `stornvme.sys` and `storport.sys` completion-path frames.  S139
made CQ-head acknowledgement side-effect free, but an I/O SQ-tail doorbell
issued by the same completion DPC could still synchronously execute physical
I/O and publish another CQE.

StorPort masks legacy INTx during this path.  S143 records I/O SQ tails while
masked and publishes at most one deferred completion when StorPort writes
INTMC.  This bounds each completion DPC and preserves forward progress with one
I/O queue pair.
