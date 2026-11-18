// NWOAS S256: SPK0 = J274 internal speaker path, prepared by the launcher module
// nwoas_scripts/sound-s256/audio_module.py before Windows starts (contract PLAN-S256.md).
// Resource order is fixed by the contract (the driver indexes CmResourceTypeMemory 0..4):
//   0 I2C1                         0x235014000 + 0x4000   ADT /arm-io/i2c1 reg[0]
//   1 ADMAC (admac-sio)            0x238200000 + 0x34000  ADT /arm-io/admac-sio reg[0]
//   2 MCA clusters (mca-switch r0) 0x238400000 + 0x18000  ADT /arm-io/mca-switch reg[0]
//   3 MCA switch1 (mca-switch r1)  0x238300000 + 0x30000  ADT /arm-io/mca-switch reg[1]
//   4 sound window (guest IPA)     0x340000000 + 0x40000  = S256_WIN_IPA in audio_module.py;
//     backed by a proxy-heap buffer outside guest RAM (hv.map_hw), header at +0, ring at +0x1000.
//     fw-audio-s256/build.py refuses to build if this value differs from the module constant.
// 0..3 were checked against the live 13.5 ADT (agx-g1-s246/evidence/g0-adt-20261116-live1) and the
// module re-checks them against the ADT at every boot (header status stays 0 on mismatch).
// No interrupt in stage 1 (the driver polls with a timer).
Device (SPK0)
{
    Name (_HID, "NWAU0001")
    Name (_UID, Zero)
    Name (_STA, 0x0F)
    Name (_CRS, ResourceTemplate ()
    {
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000235014000, 0x0000000235017FFF, 0x0,
            0x0000000000004000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000238200000, 0x0000000238233FFF, 0x0,
            0x0000000000034000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000238400000, 0x0000000238417FFF, 0x0,
            0x0000000000018000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000238300000, 0x000000023832FFFF, 0x0,
            0x0000000000030000, ,, , AddressRangeMemory, TypeStatic)
        QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
            NonCacheable, ReadWrite, 0x0,
            0x0000000340000000, 0x000000034003FFFF, 0x0,
            0x0000000000040000, ,, , AddressRangeMemory, TypeStatic)
    })
}
