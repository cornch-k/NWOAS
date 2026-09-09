# S155 FL1100 all-segment event coherence candidate

S154 restored the USB-C xHC and root hub, but the hardware run later reached a
`0x133` single-DPC watchdog after roughly 12 minutes. The bugcheck virtual GIC
snapshot had pending IRQ 900 (NVMe) and IRQ 698 (FL1100), with no USB-C IRQ 857.
The recovered dump is `logs/S154-050722-6296-01.dmp`, SHA-256
`8fde31600639c65456568668c4f77ac19f02859fc75f40071ef471a6c05ece4b`.

S155 fixes a concrete gap in the S151 FL1100 event-coherence path. Windows
programs four event-ring segments, while the old helper invalidated only the
first segment's first 4 KiB. The new helper reads the hardware RTSOFF and
ERSTSZ, accepts 1 through 16 segments, cleans the complete guest-authored ERST,
validates each base and TRB count, then invalidates the complete
controller-owned event segment. It handles stage-2 page boundaries explicitly.
The same helper runs on either USBSTS.EINT or IMAN0.IP pending reads.

USB-C D83 setup, S154 USB-C event coherence, S149 NVMe fast path, eight CPUs,
and the S140 clock setup are unchanged for the first A/B. Success requires both
USB controllers and root hubs at Code 0, sustained USB-A and USB-C input, CPU
and disk validation, and no HSE or watchdog beyond the previous 12-minute
failure point.
