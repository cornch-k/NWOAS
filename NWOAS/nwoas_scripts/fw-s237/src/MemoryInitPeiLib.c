/**
 * @file MemoryInitPeiLib.c
 * 
 * @author amarioguy (Arminder Singh)
 * 
 * This file implements page table setup, memory HOB setup, and MMU initialization.
 * Adapted from SurfaceDuoPkg/MemoryInitPeiLib.c
 * 
 * @version 1.0
 * @date 2022-07-31
 * 
 * @copyright Copyright (c) amarioguy (Arminder Singh) 2022.
 * 
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 * 
 **/

#include <PiPei.h>

#include <Library/ArmMmuLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/IoLib.h>
#include <Library/PrintLib.h>
#include <Library/AppleDTLib.h>

//Device memory map configuration file for UEFI (this is to help with pagetable initialization)
#include <Library/T810XFamilyVirtualMemoryMapDefines.h>
#include <Library/NwoasHandoffRanges.h>
#include <Library/NwoasGuestRam.h>   // NWOAS S161: NWOAS_DMA_WIN_BASE/SIZE + guest RAM/backing helper (shared)

#define MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS 20

//
// NWOAS Stage 4 (low-RAM DMA alias for the platform xHCI, see DSDT SCB0/XHC0):
// The m1n1 hypervisor aliases guest IPA [NWOAS_DMA_WIN_BASE, +NWOAS_DMA_WIN_SIZE) onto the
// NWOAS_DMA_WIN_SIZE bytes of real RAM that start at SystemMemoryTop (== Layout.BackingPa from
// NwoasGuestRam.h; PrePi sizes PcdSystemMemorySize with the same helper, S161).
// Because the Apple PCIe DART is 32-bit-IOVA-only and Windows' AllocateCommonBuffer caps a
// 32-bit device's *physical* allocation at 4GB (it does NOT relocate via ACPI _DMA _TRA), we
// must make real, device-reachable RAM physically appear below 4GB. We advertise this low
// window as usable system memory (Windows allocates xHCI DMA rings/DCBAA from it) and RESERVE
// the high backing so the same DRAM is never double-allocated. Must stay consistent with the
// hv map_hw() alias, the UEFI DART EBS window, and the DSDT _DMA (_TRA=0).
//
// NWOAS_DMA_WIN_BASE (0) / NWOAS_DMA_WIN_SIZE (4GB) now come from NwoasGuestRam.h -- same values.

//
// NWOAS deferred-application gate (2026-07-10 pivot, see MemoryPeim() !Found block).
//   0 (default) = DEFERRED: advertise the high system RAM [phys_base, backing) as normal
//       EFI_RESOURCE_SYSTEM_MEMORY so UEFI executes in device-reachable real RAM -- its own xHCI
//       reads work (matches the proven no-EXP-5 path that renders Windows Setup; no FL1100 HSE from
//       UEFI running in Reserved RAM). The low [0,4GB) window built further below stays the ONLY
//       sub-4GB Conventional pool, so Windows' 32-bit xHCI common buffer still lands there (DART EBS
//       static window -> backing). This is the "UEFI full RAM, kernel-only DART window" architecture.
//   1 = legacy Method A: high RAM Reserved to force Windows into the window -- but that put UEFI's
//       own stack/DMA buffers in FL1100-unreachable Reserved RAM (HSE, read stall). Kept for revert.
//
#define NWOAS_METHOD_A 0

#define DDR_ATTRIBUTES_CACHED           ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK
#define DDR_ATTRIBUTES_UNCACHED         ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED

VOID BuildMemoryTypeInformationHob(VOID);

VOID BuildVirtualMemoryMap(OUT ARM_MEMORY_REGION_DESCRIPTOR **VirtualMemoryMap);

STATIC VOID InitMmu(IN ARM_MEMORY_REGION_DESCRIPTOR *MemoryTable)
{
    VOID *MemoryTranslationTableBase;
    UINTN MemoryTranslationTableSize;
    RETURN_STATUS StatusCode;

    DEBUG(
        (DEBUG_INFO, 
        "MemoryInitPeiLib: Enabling MMU, Page Table Base: 0x%p, Page Table Size: 0x%p\n", 
        &MemoryTranslationTableBase, &MemoryTranslationTableSize)
        );
    StatusCode = ArmConfigureMmu(MemoryTable, &MemoryTranslationTableBase, &MemoryTranslationTableSize);

    if(EFI_ERROR(StatusCode))
    {
        DEBUG((DEBUG_ERROR | DEBUG_INFO, "MemoryInitPeiLib: MMU enable failed!! Status: %llx\n", StatusCode));
    }
}

//borrowed from edk2-platforms:Armada7k8kMemoryInitPeiLib
STATIC BOOLEAN ReserveMemoryRegion ( IN EFI_PHYSICAL_ADDRESS ReservedRegionBase, IN UINT32 ReservedRegionSize)
{
  EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttributes;
  EFI_PHYSICAL_ADDRESS         ReservedRegionTop;
  EFI_PHYSICAL_ADDRESS         ResourceTop;
  EFI_PEI_HOB_POINTERS         NextHob;
  UINT64                       ResourceLength;

  ReservedRegionTop = ReservedRegionBase + ReservedRegionSize;

  //
  // Search for System Memory Hob that covers the reserved region,
  // and punch a hole in it
  //
  for (NextHob.Raw = GetHobList ();
       NextHob.Raw != NULL;
       NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR,
                                 NextHob.Raw)) {

    if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
        (ReservedRegionBase >= NextHob.ResourceDescriptor->PhysicalStart) &&
        (ReservedRegionTop <= NextHob.ResourceDescriptor->PhysicalStart +
                      NextHob.ResourceDescriptor->ResourceLength))
    {
      ResourceAttributes = NextHob.ResourceDescriptor->ResourceAttribute;
      ResourceLength = NextHob.ResourceDescriptor->ResourceLength;
      ResourceTop = NextHob.ResourceDescriptor->PhysicalStart + ResourceLength;

      if (ReservedRegionBase == NextHob.ResourceDescriptor->PhysicalStart) {
        //
        // This region starts right at the start of the reserved region, so we
        // can simply move its start pointer and reduce its length by the same
        // value
        //
        NextHob.ResourceDescriptor->PhysicalStart += ReservedRegionSize;
        NextHob.ResourceDescriptor->ResourceLength -= ReservedRegionSize;

      } else if ((NextHob.ResourceDescriptor->PhysicalStart +
                  NextHob.ResourceDescriptor->ResourceLength) ==
                  ReservedRegionTop) {

        //
        // This region ends right at the end of the reserved region, so we
        // can simply reduce its length by the size of the region.
        //
        NextHob.ResourceDescriptor->ResourceLength -= ReservedRegionSize;

      } else {
        //
        // This region covers the reserved region. So split it into two regions,
        // each one touching the reserved region at either end, but not covering
        // it.
        //
        NextHob.ResourceDescriptor->ResourceLength =
                 ReservedRegionBase - NextHob.ResourceDescriptor->PhysicalStart;

        // Create the System Memory HOB for the remaining region (top of the FD)
        BuildResourceDescriptorHob (EFI_RESOURCE_SYSTEM_MEMORY,
                                    ResourceAttributes,
                                    ReservedRegionTop,
                                    ResourceTop - ReservedRegionTop);
      }

      //
      // Reserve the memory space.
      //
      BuildResourceDescriptorHob (EFI_RESOURCE_MEMORY_RESERVED,
        0,
        ReservedRegionBase,
        ReservedRegionSize);

      return TRUE;
    }
    NextHob.Raw = GET_NEXT_HOB (NextHob);
  }
  return FALSE;
}


//Borrowed from ArmPlatformPkg
EFI_STATUS EFIAPI MemoryPeim(IN EFI_PHYSICAL_ADDRESS UefiMemoryBase, IN UINT64 UefiMemorySize)
{
  ARM_MEMORY_REGION_DESCRIPTOR  *MemoryTable;
  EFI_RESOURCE_ATTRIBUTE_TYPE   ResourceAttributes;
  UINT64                        ResourceLength;
  EFI_PEI_HOB_POINTERS          NextHob;
  EFI_PHYSICAL_ADDRESS          FdTop;
  EFI_PHYSICAL_ADDRESS          SystemMemoryTop;
  EFI_PHYSICAL_ADDRESS          ResourceTop;
  BOOLEAN                       Found;

  // build up virtual memory map
  BuildVirtualMemoryMap(&MemoryTable);

  // Ensure PcdSystemMemorySize has been set
  ASSERT (PcdGet64 (PcdSystemMemorySize) != 0);

  //
  // Now, the permanent memory has been installed, we can call AllocatePages()
  //
  ResourceAttributes = (
                        EFI_RESOURCE_ATTRIBUTE_PRESENT |
                        EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
                        EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
                        EFI_RESOURCE_ATTRIBUTE_TESTED
                        );

  //
  // Check if the resource for the main system memory has been declared
  //
  Found       = FALSE;
  NextHob.Raw = GetHobList ();
  while ((NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, NextHob.Raw)) != NULL) {
    if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
        (PcdGet64 (PcdSystemMemoryBase) >= NextHob.ResourceDescriptor->PhysicalStart) &&
        (NextHob.ResourceDescriptor->PhysicalStart + NextHob.ResourceDescriptor->ResourceLength <= PcdGet64 (PcdSystemMemoryBase) + PcdGet64 (PcdSystemMemorySize)))
    {
      Found = TRUE;
      break;
    }

    NextHob.Raw = GET_NEXT_HOB (NextHob);
  }

  if (!Found) {
#if NWOAS_METHOD_A
    //
    // NWOAS EXP-5 5c Method A (design §4.4 approach 1): advertise the MAIN (high) system RAM as
    // EFI_RESOURCE_MEMORY_RESERVED, NOT EFI_RESOURCE_SYSTEM_MEMORY. Windows reclaims Conventional/
    // BootServices/Loader memory after ExitBootServices but NEVER Reserved memory; making high RAM
    // Reserved hides it from Windows so it cannot host an xHCI common buffer at a >4GB PA that the
    // 32-bit Apple PCIe DART cannot reach. The low DMA window (built further below at
    // NWOAS_DMA_WIN_BASE) is then the ONLY EFI_RESOURCE_SYSTEM_MEMORY -> the ONLY Conventional pool
    // -> Windows is forced to place the common buffer in the window. Attribute arg is 0 to match the
    // reserved-resource convention used by ReserveMemoryRegion() above.
    // SUPERSEDED: this forced UEFI's own stack/DMA buffers into unreachable Reserved RAM -> FL1100
    // HSE / UEFI read stall (never reached Windows). Replaced by the deferred approach (#else).
    //
    BuildResourceDescriptorHob (
      EFI_RESOURCE_MEMORY_RESERVED,
      0,
      PcdGet64 (PcdSystemMemoryBase),
      PcdGet64 (PcdSystemMemorySize)
      );
#else
    //
    // NWOAS deferred approach (2026-07-10 pivot, NWOAS_METHOD_A==0): advertise the high system RAM
    // [PcdSystemMemoryBase, +PcdSystemMemorySize) == [phys_base, backing) as normal SYSTEM_MEMORY.
    // PcdSystemMemorySize is Layout.GuestRamSize from AdtParser EarlySetup (NwoasGuestRam.h, S161:
    // S102 == boot_args.mem_size as-is, legacy == mem_size - 4GB once), so this range ends exactly at
    // the backing and leaves the 4GB backing slice out of the Conventional pool -- exactly the
    // range the low [0,4GB) window (built below) is aliased onto by the hv, so the same DRAM is never
    // double-allocated. Result: UEFI/DXE run in device-reachable real RAM (FL1100 reaches UEFI's own
    // xHCI buffers via the dynamic per-Map DART -> UEFI USB reads work like no-EXP-5), while the low
    // window remains the sole sub-4GB Conventional pool for Windows' 32-bit xHCI common buffer.
    //
    BuildResourceDescriptorHob (
      EFI_RESOURCE_SYSTEM_MEMORY,
      ResourceAttributes,
      PcdGet64 (PcdSystemMemoryBase),
      PcdGet64 (PcdSystemMemorySize)
      );
#endif
  }

  //
  // Reserved the memory space occupied by the firmware volume
  //

  SystemMemoryTop = (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdSystemMemoryBase) + (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdSystemMemorySize);
  FdTop           = (EFI_PHYSICAL_ADDRESS)PcdGet64 (PcdFdBaseAddress) + (EFI_PHYSICAL_ADDRESS)PcdGet32 (PcdFdSize);

  // EDK2 does not have the concept of boot firmware copied into DRAM. To avoid the DXE
  // core to overwrite this area we must create a memory allocation HOB for the region,
  // but this only works if we split off the underlying resource descriptor as well.
  if ((PcdGet64 (PcdFdBaseAddress) >= PcdGet64 (PcdSystemMemoryBase)) && (FdTop <= SystemMemoryTop)) {
    Found = FALSE;

    // Search for System Memory Hob that contains the firmware
    NextHob.Raw = GetHobList ();
    while ((NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, NextHob.Raw)) != NULL) {
      if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
          (PcdGet64 (PcdFdBaseAddress) >= NextHob.ResourceDescriptor->PhysicalStart) &&
          (FdTop <= NextHob.ResourceDescriptor->PhysicalStart + NextHob.ResourceDescriptor->ResourceLength))
      {
        ResourceAttributes = NextHob.ResourceDescriptor->ResourceAttribute;
        ResourceLength     = NextHob.ResourceDescriptor->ResourceLength;
        ResourceTop        = NextHob.ResourceDescriptor->PhysicalStart + ResourceLength;

        if (PcdGet64 (PcdFdBaseAddress) == NextHob.ResourceDescriptor->PhysicalStart) {
          if (SystemMemoryTop != FdTop) {
            // Create the System Memory HOB for the firmware
            BuildResourceDescriptorHob (
              EFI_RESOURCE_SYSTEM_MEMORY,
              ResourceAttributes,
              PcdGet64 (PcdFdBaseAddress),
              PcdGet32 (PcdFdSize)
              );

            // Top of the FD is system memory available for UEFI
            NextHob.ResourceDescriptor->PhysicalStart  += PcdGet32 (PcdFdSize);
            NextHob.ResourceDescriptor->ResourceLength -= PcdGet32 (PcdFdSize);
          }
        } else {
          // Create the System Memory HOB for the firmware
          BuildResourceDescriptorHob (
            EFI_RESOURCE_SYSTEM_MEMORY,
            ResourceAttributes,
            PcdGet64 (PcdFdBaseAddress),
            PcdGet32 (PcdFdSize)
            );

          // Update the HOB
          NextHob.ResourceDescriptor->ResourceLength = PcdGet64 (PcdFdBaseAddress) - NextHob.ResourceDescriptor->PhysicalStart;

          // If there is some memory available on the top of the FD then create a HOB
          if (FdTop < NextHob.ResourceDescriptor->PhysicalStart + ResourceLength) {
            // Create the System Memory HOB for the remaining region (top of the FD)
            BuildResourceDescriptorHob (
              EFI_RESOURCE_SYSTEM_MEMORY,
              ResourceAttributes,
              FdTop,
              ResourceTop - FdTop
              );
          }
        }

        // Mark the memory covering the Firmware Device as boot services data
        BuildMemoryAllocationHob (
          PcdGet64 (PcdFdBaseAddress),
          PcdGet32 (PcdFdSize),
          EfiBootServicesData
          );

        Found = TRUE;
        break;
      }

      NextHob.Raw = GET_NEXT_HOB (NextHob);
    }

    //
    // NWOAS EXP-5 5c Method A: under approach 1 the main RAM resource is now
    // EFI_RESOURCE_MEMORY_RESERVED (see the !Found block above), so this SYSTEM_MEMORY search over
    // the FD range legitimately finds nothing (Found stays FALSE) and the split / BootServicesData
    // carve above is skipped. That is EXPECTED and safe: the FD lives in reserved RAM, which DXE
    // never hands out, so the firmware volume is already protected without the carve. The stock
    // ASSERT(Found) assumed a SYSTEM_MEMORY main resource; downgrade it to a non-fatal note so the
    // Method-A map does not wedge here.
    //
    if (!Found) {
      DEBUG ((DEBUG_ERROR, "NWOAS: FD (0x%lx) not in a SYSTEM_MEMORY resource (Method A: high RAM reserved) -- FD protected by reservation\n", (UINT64)PcdGet64 (PcdFdBaseAddress)));
    }
  }
  
  // S216: DT libraries retain these fixed copies beyond PrePi. Keep them
  // out of DXE allocation and the OS map, independently of FD placement.
  {
    struct boot_args *BootArgs = (struct boot_args *)(UINTN)PcdGet64 (PcdBootArgsPointer);
    unsigned long long ReserveSize;
    if (!NwoasHandoffRange (PcdGet64 (PcdSystemMemoryBase), PcdGet64 (PcdSystemMemorySize),
                           PcdGet64 (PcdFdBaseAddress), PcdGet32 (PcdFdSize),
                           PcdGet64 (PcdBootArgsPointer), PcdGet64 (PcdAdtPointer),
                           BootArgs->devtree_size, &ReserveSize) ||
        !ReserveMemoryRegion (PcdGet64 (PcdBootArgsPointer), (UINT32)ReserveSize)) {
      DEBUG ((DEBUG_ERROR, "HVLOG: S216 could not reserve handoff copies -- halt\n"));
      CpuDeadLoop ();
      return EFI_DEVICE_ERROR;
    }
    DEBUG ((DEBUG_ERROR, "HVLOG: S216 reserved handoff base=%llx bytes=%llx\n",
            PcdGet64 (PcdBootArgsPointer), ReserveSize));
  }

  //reserve secondary stacks carveouts passed into cpm-impl-reg 
  for(int i = 0; i < PcdGet32(PcdCoreCount); i++){
    CHAR8 CpuNodeName[14];
    UINTN CarveoutLength = 0;

    AsciiSPrint(CpuNodeName, ARRAY_SIZE(CpuNodeName), "/cpus/cpu%d", i);
    dt_node_t *CpuNode = dt_get(CpuNodeName);
    UINT32 *Carveout = (UINT32 *)dt_node_prop(CpuNode, "cpm-impl-reg", &CarveoutLength);

    ReserveMemoryRegion (
      ((UINT64)Carveout[1] << 32) | Carveout[0],
      ((UINT64)Carveout[3] << 32) | Carveout[2]
    );
  }

  //
  // NWOAS EXP-5 5c Method A (design §4.4): the low DMA-alias window is now the ONLY
  // EFI_RESOURCE_SYSTEM_MEMORY resource -> the ONLY Conventional pool Windows sees. Backing =
  // [SystemMemoryTop, +NWOAS_DMA_WIN_SIZE); SystemMemoryTop == PcdSystemMemoryBase+PcdSystemMemorySize
  // == Layout.GuestRamEnd (S161, NwoasGuestRam.h), so all four consumers (hv alias / this HOB / DART EBS window /
  // DSDT _DMA) agree on the same backing physical range. High RAM is now EFI_RESOURCE_MEMORY_RESERVED
  // (see the !Found block above), so Windows cannot use it -> the xHCI common buffer is forced here.
  //
  BuildResourceDescriptorHob (
    EFI_RESOURCE_SYSTEM_MEMORY,
    ResourceAttributes,
    NWOAS_DMA_WIN_BASE,
    NWOAS_DMA_WIN_SIZE
    );
  //
  // NWOAS S237: withhold the PCIe root-port memory window from the low window.
  // Guest-physical == PCI bus address inside [NWOAS_DMA_WIN_BASE, +NWOAS_DMA_WIN_SIZE)
  // (DSDT SCB0 _DMA, _TRA = 0; DART flat window). PciBusDxe assigns 32-bit BARs from
  // the bottom of the PcdPciMmio32Base aperture, so the FL1100 BAR is bus 0xC0000000
  // and root port 1 decodes [0xC0000000, 0xC00FFFFF] (S235W ECAM read). A device
  // read/write whose bus address falls inside its own upstream bridge window is not
  // forwarded to the DART/RAM (S235V: bulk-IN data for TRB buffers 0xC0010000..
  // 0xC00EFFFF never arrived). That 1 MiB must not be RAM for UEFI or any OS.
  //
  // Not read from the live bridge: this runs in PEI, before PciBusDxe programs the
  // windows, and later reservation cannot guarantee the pages are still free. The
  // base therefore follows the aperture PCD; the size is the PCI-PCI bridge memory
  // window granularity (1 MiB), which covers every BAR currently behind root port 1
  // (one 64 KiB BAR). If another non-prefetchable BAR or bridge is ever assigned
  // above it, this size must grow with it (check the S235W "S235 ECAM" lines).
  //
  {
    EFI_PHYSICAL_ADDRESS  BridgeWindowBase = (EFI_PHYSICAL_ADDRESS)FixedPcdGet32 (PcdPciMmio32Base);
    UINT32                BridgeWindowSize = SIZE_1MB;

    if ((BridgeWindowBase >= NWOAS_DMA_WIN_BASE) &&
        (BridgeWindowBase + BridgeWindowSize <= NWOAS_DMA_WIN_BASE + NWOAS_DMA_WIN_SIZE) &&
        ReserveMemoryRegion (BridgeWindowBase, BridgeWindowSize)) {
      DEBUG ((DEBUG_ERROR, "HVLOG: S237 reserved PCIe bridge window base=%llx bytes=%x\n",
              (UINT64)BridgeWindowBase, BridgeWindowSize));
    } else {
      DEBUG ((DEBUG_ERROR, "HVLOG: S237 could NOT reserve PCIe bridge window base=%llx bytes=%x -- USB-A DMA into it is unsafe\n",
              (UINT64)BridgeWindowBase, BridgeWindowSize));
    }
  }
  //
  // NWOAS EXP-5 5c Method A (design §4.4 recommends REMOVAL): the window's
  // BuildMemoryAllocationHob(..., EfiBootServicesData) is REMOVED so the window is a PURE Conventional
  // pool (free from before ExitBootServices, both usable AND reclaimable by Windows). The prior 5b
  // reservation as BootServicesData was a workaround for a window-BAR abort seen when high RAM was
  // still Conventional and DMA buffers strayed there; under Method A high RAM is Reserved and the
  // §4.3 window-aware DART Map (AppleDartIoMmuDxe) translates boot-time window DMA to real backing,
  // so the window no longer needs to be withheld from the free pool.
  //   (removed) BuildMemoryAllocationHob (NWOAS_DMA_WIN_BASE, NWOAS_DMA_WIN_SIZE, EfiBootServicesData);
  //
  // NWOAS S161: SystemMemoryTop == PcdSystemMemoryBase + PcdSystemMemorySize == Layout.GuestRamEnd ==
  // the BACKING START (PrePi sizes the PCD with the same helper). The old range printed [Top-4GB, Top),
  // i.e. the last 4GB of ADVERTISED RAM, which under S102 was never the backing (stale).
  DEBUG ((DEBUG_ERROR,
    "NWOAS: low DMA window 0x%lx..0x%lx (Conventional, sole sub-4GB pool) aliased to backing 0x%lx..0x%lx (guest RAM 0x%lx..0x%lx, mode=%d)\n",
    NWOAS_DMA_WIN_BASE, NWOAS_DMA_WIN_BASE + NWOAS_DMA_WIN_SIZE,
    (UINT64)SystemMemoryTop, (UINT64)SystemMemoryTop + NWOAS_DMA_WIN_SIZE,
    (UINT64)PcdGet64 (PcdSystemMemoryBase), (UINT64)SystemMemoryTop, NWOAS_WINDOW_AFTER_RAM));

  // Build Memory Allocation Hob
  InitMmu (MemoryTable);

  if (FeaturePcdGet (PcdPrePiProduceMemoryTypeInformationHob)) {
    // Optional feature that helps prevent EFI memory map fragmentation.
    BuildMemoryTypeInformationHob ();
  }

  return EFI_SUCCESS;
}

/**
 * BuildVirtualMemoryMap
 * 
 * This will build up the memory map of the platform used to initialize the MMU and page tables.
 * 
 * @param VirtualMemoryMap - A pointer to a pointer to be used for page table setup.
 * 
 * does not return anything as the value will be stored in a pointer accessible by MemoryPeim.
 * 
 */
VOID BuildVirtualMemoryMap(OUT ARM_MEMORY_REGION_DESCRIPTOR **VirtualMemoryMap)
{
  ARM_MEMORY_REGION_ATTRIBUTES CacheAttributes;
  UINTN Index = 0;
  ARM_MEMORY_REGION_DESCRIPTOR *VirtualMemoryTable;

  //ensure we actually have a valid memory map pointer
  ASSERT(VirtualMemoryMap != NULL);

  DEBUG((DEBUG_INFO, "Allocating virtual memory table pages\n"));
  VirtualMemoryTable = (ARM_MEMORY_REGION_DESCRIPTOR *)AllocatePages (EFI_SIZE_TO_PAGES (sizeof (ARM_MEMORY_REGION_DESCRIPTOR) * MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS));
  if (VirtualMemoryTable == NULL) {
    DEBUG((DEBUG_INFO, "Unexpected failure to allocate VirtualMemoryTable\n"));
    return;
  }

  CacheAttributes = DDR_ATTRIBUTES_CACHED;

  /**
   * NOTE - On Apple silicon platforms, non PCIe MMIO regions *must* use nGnRnE mappings, 
   * while all PCIe regions *must* use nGnRE mappings.
   * by default EDK2 sets up the MMIO as nGnRnE, good for core system devices
   * though we will need to add an attribute for nGnRE mappings at some point.
   * 
   * TODO: add ARM_MEMORY_REGION_ATTRIBUTE_DEVICE_POSTED_WRITE
   **/

  //MMIO - PMGR/AIC/Core System Peripherals and PCIe
  VirtualMemoryTable[Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_1_BASE;
  VirtualMemoryTable[Index].VirtualBase  = APPLE_CORE_SYSTEM_MMIO_RANGE_1_BASE;
  VirtualMemoryTable[Index].Length       = APPLE_CORE_SYSTEM_MMIO_RANGE_1_SIZE;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_2_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_2_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_2_SIZE;
  VirtualMemoryTable[Index].Attributes   = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_1_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_1_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_1_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_2_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_2_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_2_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_3_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_3_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_3_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_3_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_3_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_3_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_4_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_4_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_4_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_4_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_4_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_4_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_5_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_5_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_5_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_5_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_5_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_5_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_6_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_6_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_6_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_6_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_6_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_6_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_PCIE_MMIO_RANGE_7_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_PCIE_MMIO_RANGE_7_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_PCIE_MMIO_RANGE_7_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  VirtualMemoryTable[++Index].PhysicalBase = APPLE_CORE_SYSTEM_MMIO_RANGE_7_BASE;
  VirtualMemoryTable[Index].VirtualBase    = APPLE_CORE_SYSTEM_MMIO_RANGE_7_BASE;
  VirtualMemoryTable[Index].Length         = APPLE_CORE_SYSTEM_MMIO_RANGE_7_SIZE;
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_DEVICE;

  //System DRAM
  VirtualMemoryTable[++Index].PhysicalBase = PcdGet64(PcdSystemMemoryBase);
  VirtualMemoryTable[Index].VirtualBase    = PcdGet64(PcdSystemMemoryBase);
  VirtualMemoryTable[Index].Length         = PcdGet64(PcdSystemMemorySize);
  VirtualMemoryTable[Index].Attributes     = CacheAttributes;


  DEBUG ((
    DEBUG_ERROR,
    "%a: Dumping System DRAM Memory Map:\n"
    "\tPhysicalBase: 0x%lX\n"
    "\tVirtualBase: 0x%lX\n"
    "\tLength: 0x%lX\n"
    "\tTop of system RAM: 0x%lX\n",
    __FUNCTION__,
    VirtualMemoryTable[Index].PhysicalBase,
    VirtualMemoryTable[Index].VirtualBase,
    VirtualMemoryTable[Index].Length,
    VirtualMemoryTable[Index].PhysicalBase + VirtualMemoryTable[Index].Length
    ));

  //NWOAS Stage 4: low DMA-alias window (cached RAM; the m1n1 hv aliases this IPA onto real
  //RAM backing, so it must be mapped like DRAM for UEFI/Windows to read/write it).
  VirtualMemoryTable[++Index].PhysicalBase = NWOAS_DMA_WIN_BASE;
  VirtualMemoryTable[Index].VirtualBase    = NWOAS_DMA_WIN_BASE;
  VirtualMemoryTable[Index].Length         = NWOAS_DMA_WIN_SIZE;
  VirtualMemoryTable[Index].Attributes     = CacheAttributes;

  //Framebuffer
  VirtualMemoryTable[++Index].PhysicalBase = PcdGet64(PcdFrameBufferAddress);
  VirtualMemoryTable[Index].VirtualBase    = PcdGet64(PcdFrameBufferAddress);
  VirtualMemoryTable[Index].Length         = PcdGet64(PcdFrameBufferSize);
  VirtualMemoryTable[Index].Attributes     = ARM_MEMORY_REGION_ATTRIBUTE_UNCACHED_UNBUFFERED;

  // End of Table
  VirtualMemoryTable[++Index].PhysicalBase  = 0;
  VirtualMemoryTable[Index].VirtualBase     = 0;
  VirtualMemoryTable[Index].Length          = 0;
  VirtualMemoryTable[Index].Attributes      = (ARM_MEMORY_REGION_ATTRIBUTES)0;

  ASSERT((Index + 1) <= MAX_VIRTUAL_MEMORY_MAP_DESCRIPTORS);

  *VirtualMemoryMap = VirtualMemoryTable;
}