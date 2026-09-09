# S134 Windows inventory

Run `run-as-admin.cmd` once from the Windows desktop. It creates
`C:\NWOAS\S134-Windows-inventory.zip` containing CPU/core and clock metadata,
three one-second processor samples, PnP problem devices and hardware IDs,
network/storage inventory, installed driver bindings, power policy, BCD, and
system information.

On S135/S136 and later boots, the same collector is also present as
`S134.CMD` on the small `NWOASLINK` host-RAM disk, usually `E:`. This route
does not require moving the physical WINARM USB. Open an administrator command
prompt, change to that drive, and run `S134.CMD`.

This collector is read-only apart from its own output directory. It does not
install drivers or change power, boot, network, or device settings. The data
separates these questions before S134 changes hardware exposure:

1. Does Windows report all eight S133 logical processors?
2. Is `0.04 GHz` backed by WMI clock data or only Task Manager presentation?
3. Which USB-C, network, display, and unknown devices currently need drivers?
4. Which device stack owns each visible SSD and USB controller?
