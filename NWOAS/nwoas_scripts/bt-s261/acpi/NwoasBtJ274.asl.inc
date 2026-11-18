// NWOAS S261: built-in Bluetooth (BCM4378 B1 PCIe function 1).  Contract research-20261118/bt/CONTRACT-BT.md
// (values must match nwoas_scripts/bt-s261/nwoas_bt_s261.py; fw-bt-s261/build.py and test_bt_hv.py check them).
//
// NWC1 = second ACPI0004 container, same shape as the S259 NWC0 (which stays byte-identical): it PRODUCES the
// fn1 BAR range and the BT control page, carries _DMA (device address == CPU physical address, _TRA 0) and
// _CCA 0.  The Bluetooth DMA is 32-bit only (the chip's own limit), the driver allocates below 0xBFF00000.
//
// BTH0 resources (CmResourceTypeMemory; the driver matches them by length):
//   0  fn1 BAR0 registers     CPU 0x6E0900000 + 0x8000    (PCI bus 0xE0900000)
//   1  fn1 BAR2 chip memory   CPU 0x6E0C00000 + 0x400000  (PCI bus 0xE0C00000)
//   2  NWOAS BT control page  guest IPA 0x348004000 + 0x4000 (hypervisor HOOK, bt_module.py)
//   No interrupt: the chip's INTx is masked by the command register (fn1 has no usable INTx status
//   register and MSI has no ITS here); the driver polls.
//
// Power: the chip is powered by Wi-Fi (\_SB.NWC0.WLN0).  BTH0 _INI and _PS0 write CMD 1 ("chip and fn1 up":
// the hypervisor powers the chip only when Wi-Fi left it off, never when Wi-Fi failed), _PS3 writes CMD 2
// (fn1 bus master off; chip power unchanged).  _STA hides BTH0 when fn1 never came up.
Device (NWC1)
{
    Name (_HID, "ACPI0004")
    Name (_UID, 0x12)
    Name (_CCA, Zero)
    Method (_STA) { Return (0x0F) }
    Name (_CRS, ResourceTemplate ()
    {
        QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x00000006E0900000, 0x00000006E0FFFFFF, 0x0,
            0x0000000000700000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000348004000, 0x0000000348007FFF, 0x0,
            0x0000000000004000, ,, , AddressRangeMemory, TypeStatic)
    })
    Name (_DMA, ResourceTemplate ()
    {
        QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000000000000, 0x00000000FFFFFFFF, 0x0,
            0x0000000100000000, ,, , AddressRangeMemory, TypeStatic)
    })

    Device (BTH0)
    {
        Name (_HID, "NWOA5F69")
        Name (_UID, Zero)
        Name (_CCA, Zero)
        Name (_DDN, "NWOAS BCM4378 Bluetooth (apcie port 0 fn1, hypervisor powered)")
        OperationRegion (BCTL, SystemMemory, 0x0000000348004000, 0x20)
        Field (BCTL, DWordAcc, NoLock, Preserve)
        {
            MAGC, 32,       // 0x00 'NWBT' 0x5442574E
            VERS, 32,       // 0x04
            STAT, 32,       // 0x08 0 off, 1 up, 2 quiet, 3 failed
            ERRC, 32,       // 0x0C
            CMDR, 32,       // 0x10 1 chip+fn1 up, 2 fn1 quiet
            SEQN, 32,       // 0x14
            AVLB, 32        // 0x18 1 = fn1 came up at least once
        }
        Name (INIR, Zero)
        Method (_INI, 0, Serialized)
        {
            If (LEqual (MAGC, 0x5442574E)) { Store (One, CMDR) }
            Store (One, INIR)
        }
        Method (_STA, 0, Serialized)
        {
            If (LEqual (INIR, Zero)) { Return (0x0F) }
            If (LNotEqual (MAGC, 0x5442574E)) { Return (Zero) }
            If (LEqual (AVLB, One)) { Return (0x0F) }
            Return (Zero)
        }
        Method (_PS0, 0, Serialized)
        {
            If (LEqual (MAGC, 0x5442574E)) { Store (One, CMDR) }
        }
        Method (_PS3, 0, Serialized)
        {
            If (LEqual (MAGC, 0x5442574E)) { Store (0x02, CMDR) }
        }
        Name (_CRS, ResourceTemplate ()
        {
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                NonCacheable, ReadWrite, 0x0,
                0x00000006E0900000, 0x00000006E0907FFF, 0x0,
                0x0000000000008000, ,, , AddressRangeMemory, TypeStatic)
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                NonCacheable, ReadWrite, 0x0,
                0x00000006E0C00000, 0x00000006E0FFFFFF, 0x0,
                0x0000000000400000, ,, , AddressRangeMemory, TypeStatic)
            QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                NonCacheable, ReadWrite, 0x0,
                0x0000000348004000, 0x0000000348007FFF, 0x0,
                0x0000000000004000, ,, , AddressRangeMemory, TypeStatic)
        })
    }
}
