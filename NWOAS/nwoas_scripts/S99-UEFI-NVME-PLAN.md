# S99 plan: make the emulated NVMe visible to UEFI (Project Mu) for first boot

Source: Fable 5.1 research 2026-09-07 (read-only code survey; HARDWARE-UNVERIFIED).

## Recommended route: NonDiscoverablePciDeviceDxe registration (no new root bridge)
- Mu already ships NonDiscoverablePciDeviceDxe (MacMini2020.fdf:159-162) and uses it for dwc3 xHCI
  (AppleUsbTypeCBringupDxe.c:235-241 RegisterNonDiscoverableMmioDevice). NVMe type supported
  (NonDiscoverablePciDeviceDxe.c:26-29; class code 01/08/02 synthesized at NonDiscoverablePciDeviceIo.c:2009-2014).
- DMA: DmaTypeCoherent → DeviceAddress == HostAddress (CoherentPciIoMap :724/:814), IoMmuLibNull for this driver
  (AppleSiliconPkg.dsc.inc:433) → AppleDartIoMmuDxe not involved; matches guest_module GuestMemory.pa.
- Stage-1 page tables already map 0x700000000 (4 GB, DEVICE) in MemoryInitPeiLib.c:497-500.
- Stage-2 HOOK tracers are installed before hv.start (run_guest.py:97→103-105→120), so UEFI accesses trap too
  (log evidence: PT[700100000:700104000] -> HOOK.RW s93-nvme-bar printed before guest start).
- NvmExpressDxe polls completions (NvmExpressPassthru.c:822-834): no UEFI-side IRQ needed.
- Controller emulation compatible: CC MPS=0/CSS=0/IOSQES=6/IOCQES=4, AQA 2 entries (controller.py:123,138 accept),
  admin cmds Identify CNS0/1, SetFeatures FID7, Create CQ/SQ all handled.

## Changes
1. Silicon/Apple/AppleSiliconPkg/AppleSiliconPkg.dsc.inc:688 — uncomment NvmExpressDxe.inf
2. Platform/MacMini2020Pkg/MacMini2020.fdf:167 — uncomment NvmExpressDxe
3. NwoasHideHighRamDxe.c NwoasHideHighRamDxeInitialize (:468): add
   RegisterNonDiscoverableMmioDevice(NonDiscoverableDeviceTypeNvme, NonDiscoverableDeviceDmaTypeCoherent,
                                     NULL, NULL, 1, 0x700100000ULL, 0x4000ULL);
   + NonDiscoverableDeviceRegistrationLib in NwoasHideHighRamDxe.inf [LibraryClasses]
4. guest_module.py: no change expected.

## Why not a real segment-1 root bridge
AppleSiliconPciHostBridgeLib has one fixed root bridge (Segment 0, Count=1 asserted), PciSegmentLib is
BasePciSegmentLibPci (asserts segment==0), and PciHostBridgeDxe is the only scope with the real IoMmuLib
→ segment-1 PciIo->Map would go through DART and break the host-copy backend.

## Risks (hypotheses)
- NvmExpressDxe leaves controller EN=1 at ExitBootServices; Windows stornvme resets CC.EN=0 (controller.py:119-120 clears queues) — expected fine.
- UEFI transfers respect MDTS (=64 KiB advertised) — verify no >16-block commands reach backend.
- NVRAM is emulated (PcdEmuVariableNvModeEnable=TRUE): bcdboot Boot#### entries will not persist; rely on BDS
  auto-enumeration + \EFI\BOOT\BOOTAA64.EFI fallback (bcdboot writes it? verify).
- Fallback if this route fails on hardware: custom UEFI BlockIo driver (~400-800 lines).

## Hardware validation without touching macOS partitions
1. Serial log shows UEFI-stage MMIO traps + `CREATE CQ/SQ 1 depth=2` before Windows.
2. UEFI Shell `map -r` lists an NVMe block device with 4 partitions.
3. Shell `dblk` of LBA 1 shows "EFI PART" (read-only).

## Build 2026-09-07 18:00 (HARDWARE-UNVERIFIED)
- Changes applied: dsc.inc:688 + fdf:167 uncommented; NwoasHideHighRamDxe.c NWOAS_UEFI_NVME=1 registration; .inf lib added.
- stuart_build DEBUG success (20 s incremental). NvmExpressDxe.efi packed into FVMAIN (Ffs/5BE3BDF4-…NvmExpressDxe).
- Payload `m1n1_windows/m1n1-payload-s99-uefinvme.bin` = first 1376256 B of s93 payload + new FD; 32342016 B;
  sha256 fee62d5a09fbfe63814569d6d6657528b5d84d9360a738b760738f304fe69875. Harness `nvme-s99-guest-test.sh` (hv S98).
- Previous FD backed up in session scratchpad only; s93 payload untouched.
