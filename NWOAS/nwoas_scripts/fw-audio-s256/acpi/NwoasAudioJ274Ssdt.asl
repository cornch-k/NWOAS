/**
 * NWOAS S256 J274 (Mac mini M1) internal speaker device for the NWAU0001 Windows driver.
 * Contract: research-20261116/sound/PLAN-S256.md. Test payload only (fw-audio-s256/README).
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
 **/
#include <IndustryStandard/Acpi65.h>

DefinitionBlock ("", "SSDT", 0x02, "NWOAS", "J274AUD", 0x00000001)
{
    Scope (\_SB)
    {
        Include ("NwoasAudioJ274.asl.inc")
    }
}
