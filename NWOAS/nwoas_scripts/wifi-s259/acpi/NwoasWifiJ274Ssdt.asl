/**
 * NWOAS S259 J274 (Mac mini M1) built-in Wi-Fi (BCM4378 B1, PCIe 1:0.0 behind apcie port 0) as the
 * ACPI platform device \_SB.NWC0.WLN0 (_HID "NWOA4378") for the NWOAS Windows Wi-Fi driver.
 * Contract: research-20261118/wifi/CONTRACT-WIN.md.  Test payload only (nwoas_scripts/fw-wifi-s259).
 * Needs the S259 launcher (nwoas_scripts/wifi-s259/wifi_module.py): the control page read below is a
 * hypervisor HOOK page and is unmapped without that module.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
 **/
#include <IndustryStandard/Acpi65.h>

DefinitionBlock ("", "SSDT", 0x02, "NWOAS", "J274WIFI", 0x00000001)
{
    Scope (\_SB)
    {
        Include ("NwoasWifiJ274.asl.inc")
    }
}
