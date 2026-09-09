# S135 SMBIOS speed-reporting candidate

S135 retains the S133 hypervisor and the S131 eight-core, SSD-first UEFI
payload behavior. Its only UEFI delta changes SMBIOS Type 4 `CurrentSpeed`
from the invalid/unknown value 0 to 3228 MHz, matching the existing
`MaxSpeed` value.

This is a reporting correction. It does not add `_CPC`, write Apple PMGR
frequency controls, or claim that Windows can manage M1 P-states. Validate it
only after the S134 inventory establishes what S133 reports through WMI and
Task Manager.

The launcher defaults to the established 115200 baud. After a standalone proxy
baud-ladder run proves a faster value, pass it explicitly, for example:

```sh
M1N1_BAUD=500000 nwoas_scripts/usb-s135-guest-test.sh
```
