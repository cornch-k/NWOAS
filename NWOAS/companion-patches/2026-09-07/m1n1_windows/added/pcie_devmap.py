# pcie_devmap.py (NWOAS) — run_guest -m hook. THE MEMORY-ATTRIBUTE FIX.
#
# Root cause (hardware-confirmed 2026-07-01, retest5/6):
#   The USB-A xHCI is a Fresco Logic FL1100 (PCI 1B73:1100) and DOES respond
#   (SYNC-traced read of BAR+0x8330 = 0x401). But the guest's *direct* access
#   SErrors with an L2C bus error, because m1n1's hv_map_hw maps guest MMIO with
#   PTE_MEMATTR_UNCHANGED (stage-2 Normal-WB, the least-restrictive attr), so the
#   guest's stage-1 attribute wins. The UEFI maps the PCIe MMIO window CACHEABLE,
#   so a cacheable access to MMIO -> L2 cache error -> SError. (Other MMIO like the
#   UART/AIC works because the UEFI maps THOSE as Device.)
#
# Fix here (hv side, no UEFI rebuild, no kmutil): after pcie_init(), re-map the
# PCIe MMIO windows with a stage-2 Device attribute so the combined attr is Device
# regardless of the guest's cacheable stage-1 mapping. Device is the most
# restrictive memory type, so it dominates the stage-1 attr.
import time

print("[devmap] pcie_init() (P_PCIE_INIT) ...")
try:
    print("[devmap] pcie_init ->", p.pcie_init())
except Exception as e:
    print("[devmap] pcie_init err:", e)

# stage-2 MemAttr is 4 bits at [5:2]. 0b1111 = Normal WB (unchanged). Device-nGnRE = 0b0001.
PTE_MEMATTR_DEVICE_nGnRE = 0b0001 << 2
DEV_ATTR = hv.PTE_ACCESS | hv.PTE_SH_NS | hv.PTE_S2AP_RW | PTE_MEMATTR_DEVICE_nGnRE

# PCIe MMIO windows (CPU addresses, from T810XFamilyPkg.dsc.inc + translation):
#   32-bit: PciBase 0xc0000000 + Translation 0x600000000 = 0x6c0000000, size 0x40000000
#   64-bit: 0x400000000, size 0x20000000
for base, size, tag in ((0x6c0000000, 0x40000000, "MMIO32"), (0x400000000, 0x20000000, "MMIO64")):
    print(f"[devmap] re-mapping PCIe {tag} {base:#x}+{size:#x} as Device-nGnRE (stage-2) ...")
    try:
        ret = p.hv_map(base, base | DEV_ATTR | hv.PTE_VALID, size, 1)
        print(f"[devmap]   hv_map -> {ret}")
    except Exception as e:
        print(f"[devmap]   hv_map err: {e}")

time.sleep(0.3)
print("[devmap] done -- starting guest with PCIe MMIO forced to Device stage-2 attr.")
