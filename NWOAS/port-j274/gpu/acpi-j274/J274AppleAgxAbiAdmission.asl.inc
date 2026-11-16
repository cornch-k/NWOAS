// NWOAS J274 (Mac mini M1) AGX0 device for the windows-on-m1 render-admission KMD
// (ACPI\APPL0002, AppleAgxRenderAdmission.sys). Derived from windows-on-m1
// mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc
// (MIT, Copyright (c) 2026 paulsmir). Only the board values changed.
//
// Every address below was read from the LIVE J274 13.5 ADT
// (nwoas_scripts/agx-g1-s246/evidence/g0-adt-20261116-live1/adt.bin, sha256
// 32e63706ee88..., iBoot-8422.141.2 / V13_5) and equals the saved Tahoe ADT
// (port-j274/gpu/hv/out-hv2/hosttest-adt135.txt, -tahoe.txt):
//   VERIFIED-13.5  sgx reg[0]              0x204000000 + 0x4000000 (covers gfx-asc reg[0]
//                                          0x206400000 + 0x6c000)
//   VERIFIED-13.5  sgx gpu-region          0xBFFFB8000 + 0x4000   (J313: 0x9FFFB8000)
//   VERIFIED-13.5  sgx gfx-handoff         0xBFFF70000 + 0x4000   (J313: 0x9FFF70000)
//   ABI (not ADT)  broker page             0x300000000 + 0x1000   (m1n1-gpu-hv2 hook; outside
//                                          every J274 /arm-io range, checked by the HV at install)
//   ABI (not ADT)  synthetic scanout GSIV  889, Edge              (no 13.5 ADT node uses 889;
//                                          gpu-hv2 never forwards it to AIC; the HV raises it
//                                          only from the scanout service, which is NOT ported,
//                                          so it never fires)
// Not declared here (FACT, KMD backend_platform_windows.c resource check: exactly 5 memory
// resources and exactly 1 interrupt, otherwise STATUS_DEVICE_CONFIGURATION_ERROR):
//   the 9 AGX hardware lines, VERIFIED-13.5: sgx 563 564 565 566 579, gfx-asc 576 575 578 577.
//   gpu-hv2 forwards them identity-mapped (vINTID == AIC hwirq) only if some ACPI device
//   declares and the guest enables them; the render-admission KMD polls the ASC mailbox instead.
// Re-check before every new OS-firmware stub: all VERIFIED-13.5 lines (the HV probe refuses
// on its own if the live ADT differs; this table does not, so a mismatch = KMD bound to stale
// resources).
//
// Requires -DAGX_LOCAL_RESERVE_V2 (local-reserve ABI v3, the only version the KMD accepts) and
// the matching MemoryInitPeiLib change (README.md) so Windows never uses the reserve.
#ifdef AGX_LOCAL_RESERVE_V2
#define AGXLR_BYTES 0x40000000
#define AGXLR_MIN_BYTES 0x08800000
#define AGXLR_MAX 0x3FFFFFFF
#define AGXLR_VERSION 0x03
#else
#error "J274 AGX0 needs AGX_LOCAL_RESERVE_V2 (local-reserve ABI v3)"
#endif
Device (AGX0)
{
    Name (_HID, "APPL0002")
    Name (_UID, Zero)
    Name (_CCA, One)
    Name (_STA, 0x0F)
    // Local-reserve receipt published by the HV broker at +0xD00 (read-only).
    OperationRegion (LREG, SystemMemory, 0x0000000300000D00, 0x28)
    Field (LREG, DWordAcc, NoLock, Preserve)
    { LMAG, 32, LVER, 32, LVAL, 32,
      Offset (0x10), LIPL, 32, LIPH, 32, LHPL, 32, LHPH, 32,
      LBYL, 32, LBYH, 32 }
    Name (BAS0, ResourceTemplate ()
    {
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000204000000, 0x0000000207FFFFFF, 0x0,
            0x0000000004000000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000BFFFB8000, 0x0000000BFFFBBFFF, 0x0,
            0x0000000000004000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000BFFF70000, 0x0000000BFFF73FFF, 0x0,
            0x0000000000004000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000300000000, 0x0000000300000FFF, 0x0,
            0x0000000000001000, ,, , AddressRangeMemory, TypeStatic)
        Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive)
        { 889 }
    })
    Name (LOCR, ResourceTemplate ()
    {
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000204000000, 0x0000000207FFFFFF, 0x0,
            0x0000000004000000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000BFFFB8000, 0x0000000BFFFBBFFF, 0x0,
            0x0000000000004000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000BFFF70000, 0x0000000BFFF73FFF, 0x0,
            0x0000000000004000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000300000000, 0x0000000300000FFF, 0x0,
            0x0000000000001000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000000000000, AGXLR_MAX, 0x0,
            AGXLR_BYTES, ,, LRNG, AddressRangeMemory, TypeStatic)
        Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive)
        { 889 }
    })
    Method (_CRS, 0, Serialized)
    {
        Store (Or (ShiftLeft (LIPH, 0x20), And (LIPL, 0xFFFFFFFF)), Local0)
        Store (Or (ShiftLeft (LHPH, 0x20), And (LHPL, 0xFFFFFFFF)), Local1)
        If ((LMAG == 0x4C584741) && (LVER == AGXLR_VERSION) &&
            (LVAL == One) && (LBYH == Zero) && (Local0 != Zero) &&
            (Local1 == Local0)
            && (LBYL >= AGXLR_MIN_BYTES) && (LBYL <= AGXLR_BYTES) &&
            (And (LBYL, 0xFFFF) == Zero) && (And (Local0, 0xFFFF) == Zero) &&
            (Add (Local0, LBYL) <= 0x10000000000)
            )
        {
            CreateQWordField (LOCR, ^LRNG._MIN, MMIN)
            CreateQWordField (LOCR, ^LRNG._MAX, MMAX)
            CreateQWordField (LOCR, ^LRNG._LEN, MLEN)
            Store (Local0, MMIN)
            Store (Subtract (Add (Local0, LBYL), One), MMAX)
            Store (LBYL, MLEN)
            Return (LOCR)
        }
        // No valid receipt (LOG mode, old HV): 4 memory resources -> the KMD refuses.
        Return (BAS0)
    }
    Name (_DSD, Package ()
    {
        ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
        Package ()
        {
            Package () { "agx-profile", "abi-admission" },
            Package () { "agx-abi-admission-version", 0x01 },
            Package () { "agx-local-reserve-version", 0x03 },
            Package () { "agx-source-g2-sha256", "7c3a7af4e86642b3b72a07132584a352ba475a681828b0be6d3d968c94ebf9a2" }
        }
    })
}
