// NWOAS S259: built-in Wi-Fi.  Contract research-20261118/wifi/CONTRACT-WIN.md (values must match
// nwoas_scripts/wifi-s259/nwoas_wifi_s259.py; fw-wifi-s259/build.py and test_wifi_hv.py check them).
//
// NWC0 = ACPI0004 container, the same shape as DSDT SCB0 (USB-A XHC0): it PRODUCES the MMIO its child
// consumes (Windows resource arbiter rule, DSDT SCB0 comment) and carries _DMA (device address == CPU
// physical address, _TRA 0, 48 GiB) and _CCA 0 (non-coherent, as SCB0).  IORT has a Named Component
// node "\_SB_.NWC0.WLN0" like the one for "\_SB_.SCB0.XHC0".
//
// WLN0 resources (CmResourceTypeMemory in this order; the driver should match them by length):
//   0  BAR0 chip registers    CPU 0x6E0800000 + 0x10000   (PCI bus 0xE0800000)
//   1  BAR2 chip memory (TCM) CPU 0x6E0000000 + 0x800000  (PCI bus 0xE0000000)
//   2  NWOAS control page     guest IPA 0x348000000 + 0x4000 (hypervisor HOOK, wifi_module.py)
//   Interrupt 695 = apcie port-0 INTx aggregate (level, active high), vGIC SPI 695 == AIC 695.
//
// Power: _INI and _PS0 write CMD 1 (power up + link + BARs, done by the hypervisor inside that write),
// _PS3 writes CMD 2 (bus master off, PERST#, power off).  _STA hides the device when the first power up
// (from _INI) failed: the BARs do not decode then and touching them would fault.
Device (NWC0)
{
    Name (_HID, "ACPI0004")
    Name (_UID, 0x11)
    Name (_CCA, Zero)
    Method (_STA) { Return (0x0F) }
    Name (_CRS, ResourceTemplate ()
    {
        QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x00000006E0000000, 0x00000006E08FFFFF, 0x0,
            0x0000000000900000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000348000000, 0x0000000348003FFF, 0x0,
            0x0000000000004000, ,, , AddressRangeMemory, TypeStatic)
    })
    Name (_DMA, ResourceTemplate ()
    {
        QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000000000000, 0x0000000BFFFFFFFF, 0x0,
            0x0000000C00000000, ,, , AddressRangeMemory, TypeStatic)
    })

    Device (WLN0)
    {
        Name (_HID, "NWOA4378")
        Name (_UID, Zero)
        Name (_CCA, Zero)
        Name (_DDN, "NWOAS BCM4378 Wi-Fi (apcie port 0, hypervisor powered)")
        OperationRegion (WCTL, SystemMemory, 0x0000000348000000, 0x20)
        Field (WCTL, DWordAcc, NoLock, Preserve)
        {
            MAGC, 32,       // 0x00 'NWWF' 0x4657574E
            VERS, 32,       // 0x04
            STAT, 32,       // 0x08 0 off, 1 up, 2 off by request, 3 failed
            ERRC, 32,       // 0x0C
            CMDR, 32,       // 0x10 1 power up, 2 power down
            SEQN, 32,       // 0x14
            AVLB, 32        // 0x18 1 = first power up (from _INI) succeeded
        }
        Name (INIR, Zero)
        Method (_INI, 0, Serialized)
        {
            If (LEqual (MAGC, 0x4657574E)) { Store (One, CMDR) }
            Store (One, INIR)
        }
        Method (_STA, 0, Serialized)
        {
            If (LEqual (INIR, Zero)) { Return (0x0F) }
            If (LNotEqual (MAGC, 0x4657574E)) { Return (Zero) }
            If (LEqual (AVLB, One)) { Return (0x0F) }
            Return (Zero)
        }
        Method (_PS0, 0, Serialized)
        {
            If (LEqual (MAGC, 0x4657574E)) { Store (One, CMDR) }
        }
        Method (_PS3, 0, Serialized)
        {
            If (LEqual (MAGC, 0x4657574E)) { Store (0x02, CMDR) }
        }
        Name (_CRS, ResourceTemplate ()
        {
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                NonCacheable, ReadWrite, 0x0,
                0x00000006E0800000, 0x00000006E080FFFF, 0x0,
                0x0000000000010000, ,, , AddressRangeMemory, TypeStatic)
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                NonCacheable, ReadWrite, 0x0,
                0x00000006E0000000, 0x00000006E07FFFFF, 0x0,
                0x0000000000800000, ,, , AddressRangeMemory, TypeStatic)
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                NonCacheable, ReadWrite, 0x0,
                0x0000000348000000, 0x0000000348003FFF, 0x0,
                0x0000000000004000, ,, , AddressRangeMemory, TypeStatic)
            Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 695 }
        })
    }
}
