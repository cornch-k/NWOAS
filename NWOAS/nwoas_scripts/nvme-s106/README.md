# S106 WinPE SHA256 diagnostic

Run `sh build.sh` on the MacBook with Homebrew LLVM. NWHASH.EXE is a 5 KiB ARM64 PE console program importing kernel32.dll and bcrypt.dll only. It opens input files with GENERIC_READ, hashes SHA256(abc) first as a self-test, checks complete file length, and returns 0 for a matching hash, 1 for mismatch, 2 for an error. Runtime output is authoritative; a successful build is not runtime validation.

The CRLF HASH106.CMD runs only in ARM64 WinPE with the exact NWOAS-S106.TAG value. USB original install.swm is read twice; the NTFS t1.swm copy is read twice, followed by t2.swm. Outputs go to USB NWOAS-SETUP-LOGS/s106-*. Input files are never modified. The script may assign R to the existing Windows test partition identified by byte offset 220856320000. It never formats, applies an image, or runs bcdboot.

Before arming, preserve the previous root autounattend.xml, confirm USB volume UUID 8AA1ED40-57BA-3284-9023-B310B595EC94, confirm S100.TAG is absent, and replace Order 7's installer call with HASH106.CMD. Do not arm S100 alongside this diagnostic. The current real USB has already been staged and moved to the mini; do not restage from the MacBook until it is returned.

## Reading results

- selftest.txt must contain PASS and the expected abc hash.
- usb-first.txt / usb-second.txt must hash all 3987720636 bytes. Compare with 8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c.
- nvme-t1-first.txt / nvme-t1-second.txt are expected to report mismatch against the USB because the existing SSD copy is already different. Compare their actual hash with the host read hash e3734bc93c439dc28a0a695b5cc6f26f7d4f2fc6041ee5298b06f60e9ad880a1.
- nvme-t2.txt must cover 564691107 bytes. Original expected SHA256 is 62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963. A host-side hash of that SSD copy has not yet been taken.
- A FAILED marker alone is insufficient: the already-known t1 mismatch causes it intentionally. Read actual hashes and exit codes.
- Repeated ReadFile calls may use Windows file cache. They are not proof of independent physical reads; use the host NVMe log to establish which reads occurred.

If USB hashes are correct and SSD hashes equal the host, the current read paths agree and prior copy/write or mixed-I/O interaction remains under investigation. If USB hashes differ, investigate USB DMA/buffer handling before changing SSD writes. If SSD hashes differ from the host, investigate guest read/cache behavior. None of these isolated cases alone excludes issues triggered only by concurrent USB-read and SSD-write activity.

S112 run started 2026-09-08 20:36 KST using S103/S102. The first full t1 physical read completed and a second sequential read started, observed in host log nvme-s103-20260908-203600.pL1eaG. Final guest hashes are pending collection.
