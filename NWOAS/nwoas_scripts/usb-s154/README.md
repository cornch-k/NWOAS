# S154 USB-C reset-poll coherence candidate

S154 keeps S153's IRQ-time event-ring invalidation and reset-safe USBCMD.INTE
gate. It additionally invalidates controller-owned event-ring segments when the
Windows kernel reads USBSTS.EINT or IMAN.IP as pending. This covers USBHUB3 root
hub reset sequences that poll completions while global interrupts are disabled.

The read hook is bounded to 32-bit reads in the non-debug USB-C xHC MMIO range,
only after a canonical Windows kernel PC is observed, and only when the returned
status contains a real pending bit. Event-ring memory is invalidate-only.

Success requires both XHC1 and its USB Root Hub to report ConfigManagerErrorCode
0, sustained direct Magic Trackpad input, no USBSTS.HSE, and preservation of the
S153 eight-CPU and NS1/NS2 I/O passes.

## Hardware result

The clean S154 run is `logs/usb-s154-20260909-215003.tNx1gP`. Windows reached
the desktop and NWOS connected without manual launch. Both xHCI controllers and
both USB 3 root hubs report `Status=OK`, `ConfigManagerErrorCode=0`. The Apple
`VID_05AC&PID_0324` composite device is present below the USB-C root hub; its
touch-pad and mouse HID interfaces report `CM_PROB_NONE`.

The same run reports 8 cores and 8 logical processors. The CPU test completed
in 149,776 us and the 64 MiB disk test in 193,252 us with zero failures. Both
pointer movement and USB-C device enumeration worked. The run had no fatal
marker through the 720-second EL2 alive point, but Windows subsequently raised
bugcheck 0x133. S154 therefore proves USB-C root-hub recovery, not long-term
stability. The recovered dump is `logs/S154-050722-6296-01.dmp`.
