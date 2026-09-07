# S93: isolated NVMe transport (read-only until S96; S96 adds writes limited to WINTEST LBA 53839104-59968511)

Experimental, Windows command-level internal SSD reads verified; physical Setup disk list not yet verified. Windows requested256 admin entries; correcting the advertised/accepted limit enabled Identify and real reads. Snapshot:388 I/O reads, zero I/O errors; see hardware-evidence.json.

- Keeps D83 USB behavior and S90 ANS2 fixes. Adds opt-in synthetic IRQ900 gate at EL2 and proxy opcode0xc30.
- Isolated ACPI PCI segment1, ECAM0x700000000, BAR0x700100000/16KiB. Physical PCI0 stays hidden. INTx only.
- NVMe1.3 Identify, GetLogPage, queue creation/deletion, basic volatile feature settings, one pending AER, bounded Read via S90 namespace1 backend. Since S96, Write/Flush are accepted only inside the WINTEST window (writable_namespace.py, guest_module.py backend_write, and m1n1 nvme_write all enforce it); every other mutation stays rejected. Windows Setup cannot complete an install on this layout because GPT/ESP/MSR writes are outside the window and are refused by design.
- Guest RAM DMA validates low-window/high-range addresses and excludes TZ carveouts; host scratch copy and cache maintenance. Windows completed real GPT/partition reads through this copy path; broader coherence/stress coverage is pending.
- Four I/O queue pairs, max256 entries, 64KiB transfers, contiguous queues, bounded PRP lists; CQ backpressure, wrap phase and IRQ masking tested.
- Host queue tests: 9 PASS. Prior S92 namespace/PRP tests:14 PASS, source retained. Build succeeds; Windows Identify/Read proven; no installation or write-success claim.
- Existing source backups in `before/`; exact binary/payload hashes in `artifacts.json`. Known-good D83/S90 artifacts preserved.
- Harness `../nvme-s93-guest-test.sh`: one chainload from ready proxy, no host reboot, no kmutil, fixed115200. First hardware log `../logs/nvme-s93-20260907-124920.RlFxMC`.
- Source layout reference: https://github.com/qemu/qemu/blob/master/include/block/nvme.h . This prototype does not copy QEMU implementation.

No erasure/partition changes. User-authorized future Mac mini reinitialization still requires a viable dual-boot/recovery sequence. MacBook and X31 preserved.
