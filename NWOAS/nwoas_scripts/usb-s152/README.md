# S152 USB-C restoration candidate

S152 keeps the S151 target-owned NVMe path and deterministic FL1100 real-event
visibility. It changes the guest environment in two bounded ways:

- use the existing S135 eight-CPU, SSD-first payload, which leaves XHC1 visible
  and reports SMBIOS CurrentSpeed as 3228 MHz;
- replace the USB-A-only map module with the hardware-proven D83 USB-C module.

The D83 path retains USB1 DART bypass and translates only slot 1 Normal/Data
TRB low IPAs to their stage-2-backed physical addresses before an endpoint
doorbell. It follows Link TRBs and remembers the live segment after Windows
retires the original segment. EventData and immediate payloads are not changed.

First hardware validation is a single HID device directly attached to the
non-debug USB-C port. Success requires a Windows desktop, eight processors,
USBSTS without HSE, and sustained physical input including unplug/replug.
USB-C hubs and multiple devices remain outside the first test.
