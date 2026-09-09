/**
 * Copyright (c) 2025 AppleWOA authors.
 * 
 * Module Name:
 *     DSDT.asl
 * 
 * Abstract:
 *     Differentiated System Description Table. This source file implements the DSDT table
 *     for the Mac Mini (2020) platform.
 * 
 * Environment:
 *     UEFI firmware/runtime services.
 * 
 * License:
 *     SPDX-License-Identifier: BSD-2-Clause-Patent OR MIT
 * 
 **/
#include <IndustryStandard/Acpi65.h>

 DefinitionBlock("DSDT.aml", "DSDT", 0x02, "Apple", "J274", 0x8103) {
    Scope(\_SB) {
        //
        // Cluster low power states. On T8101/T8103, there are only 2 clusters, the P and E core clusters,
        // so should be easier to track
        //
        Name (CLPI, Package() {
            0, // Version
            0, // Level Index
            1, // Count
            Package() { // Power Gating state for Cluster
            1, // Min residency (uS)
            1, // Wake latency (uS)
            1, // Flags
            1, // Arch Context Flags
            0, //Residency Counter Frequency
            0, // No Parent State
            0x00000000, // Integer Entry method (currently NULL, TODO actually add an entry method)
            ResourceTemplate() { // Null Residency Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            ResourceTemplate() { // Null Usage Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            "ClusterRetention"
            },
        })

        //
        // Per processor low power states.
        //
        Name(PLPI, Package() {
            0, // Version
            0, // Level Index
            2, // Count
            Package() { // WFI for CPU
            1, // Min residency (uS)
            1, // Wake latency (uS)
            1, // Flags
            0, // Arch Context Flags
            0, //Residency Counter Frequency
            0, // No parent state
            ResourceTemplate () {
                // Register Entry method
                Register (SystemMemory,
                0x00,               // Bit Width
                0x00,               // Bit Offset
                0x00,         // Address
                0x00,               // Access Size
                )
            },
            ResourceTemplate() { // Null Residency Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            ResourceTemplate() { // Null Usage Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            "WFI",
            },
            Package() { // Power Gating state for CPU
            1, // Min residency (uS)
            1, // Wake latency (uS)
            1, // Flags
            1, // Arch Context Flags
            0, //Residency Counter Frequency
            1, // Parent node can be in any state
            ResourceTemplate () {
                // Register Entry method
                Register (SystemMemory,
                0x00,               // Bit Width
                0x00,               // Bit Offset
                0x00000000,         // Address
                0x00,               // Access Size
                )
            },
            ResourceTemplate() { // Null Residency Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            ResourceTemplate() { // Null Usage Counter
                Register (SystemMemory, 0, 0, 0, 0)
            },
            "CorePwrDn"
            },
        })

        Device (XHC1) {
            Name (_HID, "PNP0D15")
            Name (_UID, One)
            Name (_CCA, One)

            Method (_CRS, 0, Serialized) {
                Name (RBUF, ResourceTemplate () {
                    QWordMemory (
                        ResourceConsumer,     // ResourceUsage
                        PosDecode,            // Decode
                        MinFixed,             // IsMinFixed
                        MaxFixed,             // IsMaxFixed
                        NonCacheable,         // Cacheable
                        ReadWrite,            // ReadAndWrite
                        0,                    // AddressGranularity - GRA
                        0x502280000,          // AddressMinimum - MIN
                        0x50237FFFF,          // AddressMaximum - MAX
                        0,                    // AddressTranslation - TRA
                        0x100000              // RangeLength - LEN
                        )
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) {
                        857
                    }
                })
                Return (RBUF)
            }

            Method (_STA) {
                Return (0xF)
            }
        }

        //
        // NWOAS develop-ahead (overnight, hardware-UNVERIFIED): Apple Silicon display
        // PnP stub. On ARM SoC there is no PCIe GPU for Windows PnP to enumerate, so the
        // inbox BasicDisplay (BDD) WDDM driver never loads and there is no desktop. BDD
        // inherits the linear framebuffer from the UEFI GOP (via winload, NOT from ACPI
        // _CRS), so this node needs NO memory resource -- it exists only to give PnP a
        // device to match. ACTION STILL NEEDED on hardware: ship a companion stub INF
        // binding ACPI\APPL0100 -> basicdisplay.sys; without it Windows marks this an
        // "unknown device" (harmless) and BDD does not load. (Sources: MS BasicDisplay
        // docs, UEFI-requirements-for-WoA. iasl-parsed; BDD load NOT verified.)
        //
        Device (DISP) {
            Name (_HID, "APPL0100")
            Name (_UID, Zero)

            Method (_STA) {
                Return (0xF)
            }
        }

        Device(COM0) {
            Name(_HID, "APPL8900") // naming it APPL8900 since the Samsung based UART was used since the 8900
            Name(_UID, Zero)
            Name (_CRS, ResourceTemplate () {
                QWordMemory (
                ResourceProducer,     // ResourceUsage
                PosDecode,            // Decode
                MinFixed,             // IsMinFixed
                MaxFixed,             // IsMaxFixed
                NonCacheable,         // Cacheable
                ReadWrite,            // ReadAndWrite
                0x0000000000000000,   // AddressGranularity - GRA
                // FixedPcdGet64(PcdAppleUartBase),   // AddressMinimum - MIN
                // (FixedPcdGet64(PcdAppleUartBase) + 0xFFFF),   // AddressMaximum - MAX
                0x235200000,   // AddressMinimum - MIN
                0x235200fff,   // AddressMaximum - MAX
                0x0000000000000000,   // AddressTranslation - TRA
                0x0000000000001000    // RangeLength - LEN
                )
                Interrupt(ResourceConsumer, Level, ActiveHigh, Exclusive) { 1097 }            
            })
            Method (_STA) {
                Return (0xF)
            }
        }

        //
        // NWOAS Stage 1: Apple PCIe (apcie) root bridge. Exposes the root complex so ARM64
        // Windows enumerates the PCIe devices (FL1100 USB-A @ bus2/dev0, Ethernet, Wi-Fi).
        // Requires the new MCFG.aslc (ECAM @ 0x690000000). IORT is intentionally omitted so
        // Windows falls back to line interrupts (INTx via _PRT), not MSI/ITS (Apple has no ITS).
        // NOTE: this Stage-1 change deliberately does NOT include the port INTx unmask (that is
        // a separate AppleSiliconPciPlatformDxe.c change staged AFTER an interrupt-storm fix in
        // m1n1). Stage 1 tests ONLY enumeration + ECAM-probe safety, with no interrupt armed.
        // Topology/values verified against apple-j274.dtb pcie@690000000.
        //
        //
        // NWOAS Stage 3: PCI0 is now HIDDEN (_STA=0). Stage-2 diagnosis proved Windows
        // enumerates this root fine (bridges CMD=0x0407, leaf BARs+INTx assigned, USBXHCI
        // loaded) but never STARTS leaf devices (CMD stays 0), systematically -- and even if
        // it did, Windows has no Apple DART driver and the PCIe DARTs have no bypass + only
        // 32-bit IOVA, so pci.sys-owned DMA could never work. The xHCI is instead exposed as
        // an ACPI platform device (SCB0/XHC0 PNP0D10 below, RPi4-proven path) with a _DMA
        // window the UEFI DART driver installs at ExitBootServices. Keep the full PCI0
        // definition for reference / easy revert.
        //
        Device (PCI0) {
            Name (_HID, EISAID ("PNP0A08"))   // PCI Express Root Bridge
            Name (_CID, EISAID ("PNP0A03"))   // Compatible PCI Root Bridge
            Name (_SEG, Zero)                 // Segment 0 (matches MCFG PciSegmentGroupNumber)
            Name (_BBN, Zero)                 // Root bus number 0 (matches MCFG StartBusNumber)
            Name (_UID, Zero)
            Name (_CCA, One)                  // ARM64: cache-coherent DMA (DTB dma-coherent).
            Method (_STA) { Return (Zero) }   // NWOAS Stage 3: hidden (was 0x0F in Stage 1)

            // ---- Root-bus apertures. ECAM/config is delivered via MCFG, not here. ----
            Name (_CRS, ResourceTemplate () {
                // Bus numbers 0..3 (DTB bus-range <0 3>)
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    0x0000,               // Granularity
                    0x0000,               // Min (bus 0)
                    0x0003,               // Max (bus 3)
                    0x0000,               // Translation
                    0x0004)               // Length (4 buses)

                // 32-bit non-prefetchable: PCI 0xC0000000 -> CPU 0x6C0000000 (TRA 0x600000000), 1GB.
                // QWordMemory is mandatory (translation is 64-bit).
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x0000000000000000,   // Granularity
                    0x00000000C0000000,   // Min (PCI side)
                    0x00000000FFFFFFFF,   // Max (PCI side)
                    0x0000000600000000,   // Translation (CpuAddress - PciAddress)
                    0x0000000040000000)   // Length (1 GB)

                // 64-bit prefetchable: PCI 0x6A0000000 -> CPU 0x6A0000000 (TRA 0), 512MB.
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    Prefetchable, ReadWrite,
                    0x0000000000000000,   // Granularity
                    0x00000006A0000000,   // Min (PCI == CPU)
                    0x00000006BFFFFFFF,   // Max
                    0x0000000000000000,   // Translation (identity)
                    0x0000000020000000)   // Length (512 MB)
                // ARM: no PCI I/O space (DTB ranges has no I/O entry).
            })

            // ---- _OSC: grant NO native control. m1n1/UEFI does not service AER/PME/hotplug,
            //      and we want INTx-only (no MSI). Prevents Windows enabling unsupported features. ----
            Method (_OSC, 4) {
                CreateDWordField (Arg3, 0, CDW1)
                If (LEqual (Arg0, ToUUID ("33DB4D5B-1FF7-401C-9657-7441C03DD766"))) {
                    CreateDWordField (Arg3, 8, CDW3)
                    Store (Zero, CDW3)                        // grant no native control
                    If (LNotEqual (Arg1, One)) { Or (CDW1, 0x08, CDW1) }  // unknown revision
                    Or (CDW1, 0x10, CDW1)                     // capabilities were masked
                    Return (Arg3)
                } Else {
                    Or (CDW1, 0x04, CDW1)                     // unrecognized UUID
                    Return (Arg3)
                }
            }

            // ---- INTx routing. Each Apple port has a DEDICATED aggregate AIC line (not a PCI
            //      swizzle), so per-bridge _PRT is required. GSIV = raw AIC hwirq (m1n1 vGIC maps
            //      GIC SPI N <-> AIC N 1:1). port0=695, port1(FL1100)=698, port2=701 (DTB). ----
            Name (_PRT, Package () {
                Package () { 0x0000FFFF, 0, 0, 695 },   // bus0 dev0 = port0 bridge
                Package () { 0x0001FFFF, 0, 0, 698 },   // bus0 dev1 = port1 bridge (FL1100 path)
                Package () { 0x0002FFFF, 0, 0, 701 },   // bus0 dev2 = port2 bridge
            })

            // port0 bridge (bus0/dev0) -> secondary bus1 (Wi-Fi/BT), AIC 695
            Device (P2P0) {
                Name (_ADR, 0x00000000)
                Name (_PRT, Package () {
                    Package () { 0x0000FFFF, 0, 0, 695 },
                    Package () { 0x0000FFFF, 1, 0, 695 },
                    Package () { 0x0000FFFF, 2, 0, 695 },
                    Package () { 0x0000FFFF, 3, 0, 695 },
                })
            }
            // port1 bridge (bus0/dev1) -> secondary bus2 (FL1100 USB-A, VID 0x1b73), AIC 698
            Device (P2P1) {
                Name (_ADR, 0x00010000)
                Name (_PRT, Package () {
                    Package () { 0x0000FFFF, 0, 0, 698 },   // FL1100 INTA -> AIC 698
                    Package () { 0x0000FFFF, 1, 0, 698 },
                    Package () { 0x0000FFFF, 2, 0, 698 },
                    Package () { 0x0000FFFF, 3, 0, 698 },
                })
            }
            // port2 bridge (bus0/dev2) -> secondary bus3 (Ethernet BCM57762), AIC 701
            Device (P2P2) {
                Name (_ADR, 0x00020000)
                Name (_PRT, Package () {
                    Package () { 0x0000FFFF, 0, 0, 701 },
                    Package () { 0x0000FFFF, 1, 0, 701 },
                    Package () { 0x0000FFFF, 2, 0, 701 },
                    Package () { 0x0000FFFF, 3, 0, 701 },
                })
            }
        }

        //
        // NWOAS Stage 3: FL1100 xHCI as an ACPI PLATFORM device (RPi4-proven path,
        // bypasses pci.sys entirely -- see PCI0 comment above for why).
        //  - UEFI leaves the FL1100 BAR at PCI 0xC0000000 = CPU 0x6C0000000 and the
        //    DART EBS hook forces bridge+leaf COMMAND on at ExitBootServices, so the
        //    controller is live at a FIXED address when Windows takes over.
        //  - Interrupt 698 = the port1 INTx aggregate AIC line (level-high), routed by
        //    m1n1's vGIC as SPI 698; the port INTx unmask (Stage 2 UEFI change) is live.
        //  - _DMA (on the ACPI0004 container, as on RPi4): device DMA addresses
        //    [0, 0xF0000000) translate to CPU [0x8_0000_0000, 0x8_F000_0000) -- the
        //    exact window the DART EBS hook installs. Windows bounce-buffers all DMA
        //    into this range; the DART translates it back. (PCIe DARTs: no bypass,
        //    32-bit IOVA -> a declared window is the only workable shape.)
        //
        Device (SCB0) {
            Name (_HID, "ACPI0004")           // generic container (RPi4 pattern)
            Name (_UID, 0x10)
            Name (_CCA, Zero)                 // NWOAS §3.2: Zero (non-coherent) -- CORRECT/truthful. The apcie
                                              // DART path is proven NON-coherent (AppleDartIoMmuDxe.c:828-834
                                              // winload checksum-mismatch, :896-906 polled event-ring timeout ->
                                              // UEFI allocates rings WC). _CCA=1 was TESTED 2026-07-11 and gave
                                              // DCBAAP=0 SAME as _CCA=0 -> cacheability is NOT the DCBAAP=0 gate
                                              // (rules out the cache-type hypothesis; the wall is the DMA
                                              // adapter/enrollment path or window PFN adoption, not _CCA).

            Method (_STA) { Return (0x0F) }
            //
            // NWOAS Stage 3 FIX (root cause): an ACPI0004 container that carries _DMA MUST also
            // produce, via a ResourceProducer _CRS, the MMIO window its children consume (ACPI 6.x
            // module-device rule; RPi4's SCB0 does exactly this). WITHOUT this, Windows' ACPI
            // resource arbiter cannot assign XHC0's ResourceConsumer BAR -> USBXHCI never receives
            // a CmResourceTypeMemory descriptor -> never MmMapIoSpace / EvtDevicePrepareHardware
            // (so USBCMD.RS stays 0, xHCI regs untouched) -- while the GIC-arbitrated interrupt
            // still connects (SPI 698). That was the exact observed symptom (stage3-late.log).
            //
            Name (_CRS, ResourceTemplate () {
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x0000000000000000,       // Granularity
                    0x00000006C0000000,       // Min  (== XHC0 BAR base)
                    0x00000006C000FFFF,       // Max  (+64KB)
                    0x0000000000000000,       // Translation (no MMIO translation)
                    0x0000000000010000,       // Length 64KB
                    , , , AddressRangeMemory, TypeStatic)
            })
            // NWOAS Stage 3 DMA FIX: the whole 32-bit device window must land inside real guest RAM
            // (common buffers are NOT bounce-buffered; they must be allocatable at a valid device
            // address). Guest RAM = 0x8_3d0f8000..0xb_e1000000, so base the 4GB window at the
            // 1GB-aligned 0x8_40000000 (just inside RAM) -> CPU 0x8_40000000..0x9_40000000, all RAM.
            // device = CPU - _TRA (ACPI: CPU = device + _TRA). DART EBS hook maps the same transform.
            Name (_DMA, ResourceTemplate () {
                QWordMemory (ResourceProducer, PosDecode, MinFixed, MaxFixed,
                    NonCacheable, ReadWrite,
                    0x0000000000000000,       // Granularity
                    0x0000000000000000,       // Min: base 0 (unchanged)
                    0x0000000BFFFFFFFF,       // Max: NWOAS WIDE-DART -- 48GB ceiling so Windows' DMA-adapter can
                                              // allocate the DCBAA in normal HIGH RAM (the apcie DART now
                                              // identity-maps [phys_base, backing); the T8020 DART is 36-bit
                                              // capable). Generous Max is safe: Windows only allocates where RAM
                                              // exists ([phys_base, backing)), all of which the DART maps.
                                              // A/B revert: Max -> 0x00000000FFFFFFFF, Length -> 0x0000000100000000.
                    0x0000000000000000,       // Translation: _TRA=0 (device == CPU)
                    0x0000000C00000000,       // Length: 48GB (NWOAS WIDE-DART; Max-Min+1 == 0xC00000000)
                    , , , AddressRangeMemory, TypeStatic)
            })
            Device (XHC0) {
                Name (_HID, "PNP0D10")        // xHCI-compliant controller (USBXHCI.SYS binds)
                Name (_UID, Zero)
                Name (_CCA, Zero)             // NWOAS §3.2: Zero (non-coherent, truthful). _CCA=1 tested 2026-07-11 = DCBAAP=0 same, refuted.
                Method (_STA) { Return (0x0F) }
                //
                // NWOAS Stage 3: _INI (RPi4 xHCI ships this). At ACPI namespace init -- earlier
                // than our ExitBootServices DART hook -- force the FL1100 PCI Command register to
                // MSE|BME (0x6). Config space (ECAM bus2 dev0 = 0x690200000) is guest-accessible
                // (only the BAR MMIO faults), so this AML write works. Completes the RPi4 clone.
                //
                Method (_INI, 0, Serialized) {
                    OperationRegion (PCFG, SystemMemory, 0x0000000690200000, 0x8)
                    Field (PCFG, WordAcc, NoLock, Preserve) {
                        VNID, 16,
                        DVID, 16,
                        CMND, 16
                    }
                    Store (0x0006, CMND)   // Memory Space Enable | Bus Master Enable
                }
                Name (_CRS, ResourceTemplate () {
                    QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed,
                        NonCacheable, ReadWrite,
                        0x0000000000000000,   // Granularity
                        0x00000006C0000000,   // Min: FL1100 BAR0 (fixed, UEFI-assigned)
                        0x00000006C000FFFF,   // Max: +64KB
                        0x0000000000000000,   // Translation
                        0x0000000000010000,   // Length 64KB
                        , , , AddressRangeMemory, TypeStatic)
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 698 }
                })
                //
                // NWOAS §3.2 (2026-07-11 night): leaf XHC0 _DMA REMOVED to match RPi4 exactly.
                // RPi4's working Windows-ARM64 xHCI carries _DMA ONLY on the SCB0 ACPI0004
                // container (Xhci.asl), NOT on the PNP0D10 leaf; the child inherits it. NWOAS had
                // added a redundant leaf _DMA (48GB) as a workaround, but with the new IORT Named
                // Component (\_SB_.SCB0.XHC0) also carrying XHC0's DMA metadata, the double
                // specification is a structural divergence from the proven RPi4 shape and a
                // suspected reason Windows fails to build a functional DMA adapter (DCBAAP=0 with
                // IORT present). SCB0's container _DMA (48GB, above) remains and is inherited.
                // A/B revert: restore the `Name (_DMA, ...)` block here (git/backup).
                //
                // NWOAS Stage 3: USB _DSM (RPi4 ships this for the VL805). Function 6 forces the
                // USBXHCI stack to use 32-bit MMIO register access. An xHCI behind a PCIe bridge
                // (like the FL1100 here, and the VL805 on RPi4) can return wrong data on 64-bit
                // MMIO reads of its capability/operational registers, which makes USBXHCI reject /
                // fail to initialize the controller. UUID + function meanings per Microsoft's
                // "USB Device-Specific Method (_DSM)" doc.
                //
                Name (DSMU, ToUUID ("ce2ee385-00e6-48cb-9f05-2edb927c4899"))
                Method (_DSM, 4, Serialized) {
                    If (LEqual (Arg0, DSMU)) {
                        Switch (ToInteger (Arg2)) {
                            Case (0) { Return (Buffer () { 0x41 }) }  // functions 0 and 6 supported
                            Case (6) { Return (Buffer () { 0x01 }) }  // require 32-bit register access
                        }
                    }
                    Return (Buffer () { 0x00 })
                }
            }
        }

        //
        // T8101/T8103 *only* have 1 CPU die, ever, so everything in this node will comprise
        // most of the SoC.
        //
        Device(SOC) {
            Name(_HID, "ACPI0010") // all "processor containers" must have this HID
            Name(_UID, Zero) // unique identifier of the container

            //
            // E-core cluster, typically bootstrap core is here
            //
            Device(CLU0) {
                Name(_HID, "ACPI0010") // all "processor containers" must have this HID
                Name(_UID, 0x1) // unique identifier of the container

                Device(CPU0) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 0)
                    // Method (_LPI, 0, NotSerialized) {
                    // return(PLPI)
                    // }
                    Method (_STA) {
                        Return (0xF)
                    }
                }

                Device(CPU1) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 1)
                    // Method (_LPI, 0, NotSerialized) {
                    // return(PLPI)
                    // }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU2) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 2)
                    // Method (_LPI, 0, NotSerialized) {
                    // return(PLPI)
                    // }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU3) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 3)
                    // Method (_LPI, 0, NotSerialized) {
                    // return(PLPI)
                    // }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
            }

            //
            // P-core cluster.
            //
            Device(CLU1) {
                Name(_HID, "ACPI0010") // all "processor containers" must have this HID
                Name(_UID, 0x2) // unique identifier of the container
                Device(CPU4) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 4)
                    // Method (_LPI, 0, NotSerialized) {
                    // return(PLPI)
                    // }
                    Method (_STA) {
                        Return (0xF)
                    }
                }

                Device(CPU5) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 5)
                    // Method (_LPI, 0, NotSerialized) {
                    // return(PLPI)
                    // }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU6) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 6)
                    // Method (_LPI, 0, NotSerialized) {
                    // return(PLPI)
                    // }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
                Device(CPU7) {
                    Name(_HID, "ACPI0007")
                    Name(_UID, 7)
                    // Method (_LPI, 0, NotSerialized) {
                    // return(PLPI)
                    // }
                    Method (_STA) {
                        Return (0xF)
                    }
                }
            }
        }
        
    }
 }
