# S117: unbuffered USB-to-NVMe copy verification

HARDWARE-UNVERIFIED until the report is recovered. Uses S103/S102, unchanged.
S115 NWREAD direct reads matched the original twice; buffered reads failed.
COPY117 uses Windows xcopy /J to copy the two SWMs to a new S117SRC directory.
Existing files, partitions and boot configuration remain intact. Refuses an existing destination.
Before writing, requires one candidate t1.swm of the exact known length outside the USB,
then verifies its full known S106/S112 SHA256 as a volume fingerprint.
No successful fingerprint means no SSD write. This is not a general-purpose disk selector.
Both resulting files must match original SHA256 in direct and buffered reads for PASS.
Full report assembled in RAM, copied once to USB, then WinPE shuts down.
Copy failure (including insufficient space) preserves partial files and records failure.
This test does not apply Windows or prove an installed Windows boot.
