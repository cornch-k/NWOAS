# S144 USB CDC transport measurement

`benchmark_cdc.py` compares effective m1n1 proxy latency with CDC ACM line
coding set to 115200 and 1500000.  On the direct Mac mini USB connection these
numbers are descriptors for a USB bulk transport, rather than a physical UART
clock.  The benchmark issues NOP requests only.  A preliminary large `MEMREAD`
reset the current DWC3 device inside `usb_dwc3_command`, so bulk stress is kept
out of this diagnostic and tracked as a separate transport defect.

The experiment answers whether changing the displayed baud value can speed up
the hypervisor proxy.  If rates are equivalent, useful optimization must reduce
the number of synchronous proxy round trips or separate diagnostics from the
proxy CDC endpoint.

`usb-s144-guest-test.sh` implements the latter: NY1 carries only UART proxy
frames and NY3 carries console and guest UART diagnostics.  The companion
`capture_vuart.py` reconnects to NY3 after USB re-enumeration and appends raw
diagnostics to the run's `.vuart` file.

S144 proved the split works, but forwarding the debug UEFI UART stream produced
784 KiB in seconds and coincided with a failed second segment of the Windows
bootloader read.  `usb-s145-guest-test.sh` keeps the split while forwarding only
`HVLOG:` guest lines; the complete guest UART tail remains in the target's memory
ring for reset diagnostics.

The 1 MiB S141 transfer experiment still fails on the Windows bootloader's
1.625 MiB file read.  `usb-s146-guest-test.sh` therefore combines filtered,
split CDC with the S139/S140 64 KiB MDTS that previously reached the desktop.

S147 fixes the rejected S141 experiment by building target C with
`NWOAS_NVME_MAX_BLOCKS=256`, matching the Python backend and advertised Windows
MDTS. The default target build remains at 16 blocks.
