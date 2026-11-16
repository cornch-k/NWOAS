/**
 * NWOAS J274 AGX ABI-admission SSDT (test payload only, see README.md).
 * Derived from windows-on-m1 J313AppleAgxAbiAdmissionSsdt.asl (MIT, Copyright (c) 2026 paulsmir).
 * The only interrupt visible to Windows is the HV-owned synthetic scanout edge.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
 **/
#include <IndustryStandard/Acpi65.h>

DefinitionBlock ("", "SSDT", 0x02, "Apple", "J274ABI", 0x00000001)
{
    Scope (\_SB)
    {
        Include ("J274AppleAgxAbiAdmission.asl.inc")
    }
}
