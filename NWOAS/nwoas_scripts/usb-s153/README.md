# S153 USB-C event visibility candidate

S153 preserves the S151 target-side NVMe and FL1100 paths and the S152 D83
USB-C payload alias. It adds two bounded USB-C changes:

- invalidate controller-owned event-ring segments immediately before a real
  pending IRQ 857 is delivered to Windows;
- require USBCMD.INTE as well as IMAN.IE/IP and the guest GIC enable before
  delivery, so reset recovery cannot receive a stale USB-C interrupt.

It also logs DWC3 bus-error and both USB DART error registers once when Windows
enables IRQ 857 and once at the first HSE. These reads do not clear or modify
hardware state.

The first hardware verdict is: eight CPUs, NS1/NS2 I/O, USB-A keyboard, XHC1
root hub OK, direct USB-C Magic Trackpad sustained input, no USBSTS.HSE, and no
bugcheck. Unplug/replug is tested only after the initial direct-device run is
stable.
