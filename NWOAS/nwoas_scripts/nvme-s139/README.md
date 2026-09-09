# S139: DPC-safe NVMe completion acknowledgement

The S137 minidump identifies `stornvme!NVMeCompletionDpcRoutine` followed by
`storport!StorPortWriteRegisterUlong` and `storport!RaidpAdapterDpcRoutine`.
The register write is the completion-queue head doorbell.  The earlier virtual
controller performed synchronous physical SSD I/O while handling that write,
refilled the queue, and reasserted the interrupt before Windows could leave the
DPC.  Eight-core S137 consequently stopped with bugcheck `0x133`.

S139 makes the completion-queue head doorbell an acknowledgement-only path.
Commands execute when Windows writes an SQ-tail doorbell.  The first hardware
boot showed that StorNVMe otherwise creates four 256-entry SQs sharing one CQ,
so S139 advertises one I/O queue pair.  Its equal-depth SQ/CQ can hold every
legal outstanding command while the controller retains its one-slot guard.
