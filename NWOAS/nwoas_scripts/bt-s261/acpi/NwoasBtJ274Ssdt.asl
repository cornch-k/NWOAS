/**
 * NWOAS S261 J274 (Mac mini M1) built-in Bluetooth (BCM4378 B1, PCIe 1:0.1 behind apcie port 0) as the
 * ACPI platform device \_SB.NWC1.BTH0 (_HID "NWOA5F69") for the NWOAS Bluetooth transport driver.
 * Contract: research-20261118/bt/CONTRACT-BT.md.  Test payload only (nwoas_scripts/fw-bt-s261).
 * Needs the S261 launcher (nwoas_scripts/bt-s261/bt_module.py): the control page read below is a
 * hypervisor HOOK page and is unmapped without that module.  The S259 Wi-Fi SSDT is unchanged.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
 **/
#include <IndustryStandard/Acpi65.h>

DefinitionBlock ("", "SSDT", 0x02, "NWOAS", "J274BTH", 0x00000001)
{
    Scope (\_SB)
    {
        Include ("NwoasBtJ274.asl.inc")
    }
}
