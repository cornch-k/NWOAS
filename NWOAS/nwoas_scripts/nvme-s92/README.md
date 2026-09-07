# S92 read-only NVMe command layer

Implemented: Identify controller/namespace/active namespace list; bounded4KiB
NVM reads up to64KiB; no write API; reject all non-read I/O opcodes. Read errors
return no partial payload. PRP resolver accepts one data page, direct second
page or one page-aligned list, and validates every address against a caller
provided guest-RAM policy before transport use. Chained/SGL/metadata unsupported.

Validation:14 host tests, including all255 non-read I/O opcodes never reaching
the backend, LBA bounds/overflow, short reads, bad PRP/MMIO descriptor addresses.
Hardware test on idle S90/J274: Identify reports61279344 sectors; Read commands
fetch LBAs0,1,61279343 via real ANS2. Primary GPT CRC and previously measured disk
GUID match. Mutating commands rejected before backend calls. Clean shutdown.
Log: ../logs/nvme-s92-hardware.log. Result hardware-result-*.json.

NOT implemented: PCI/ACPI presentation, NVMe MMIO registers, SQ/CQ processing,
interrupt delivery, cache-coherent guest DMA integration, Windows enumeration,
write support, disk formatting, Windows installation. This is not a loadable
Windows .sys driver and has not run under Windows. No changes to target data.

Next integration must use an isolated synthetic PCI root and reserved software
IRQ, rather than exposing Apple's unmanaged physical PCI tree. Current platform
PCI0 is intentionally hidden; USB controllers are separate ACPI devices. Add
and validate PCI enumeration independently, then queues and interrupts, then
connect this read-only backend. Guard all guest memory against MMIO and host
heap/firmware ranges; never treat a guest pointer as an unchecked host PA.

References (layout/status constants):
https://github.com/qemu/qemu/blob/master/include/block/nvme.h
https://github.com/torvalds/linux/blob/master/include/linux/nvme.h
