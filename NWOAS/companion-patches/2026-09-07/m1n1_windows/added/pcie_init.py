# pcie_init.py (NWOAS) — run_guest -m hook.
#
# Bring up the M1 PCIe controller via m1n1's own pcie_init() BEFORE the guest (UEFI) boots.
# Root cause found 2026-07-01: run_guest never sends P_PCIE_INIT, so the PCIe controller is
# left uninitialised in the tethered-hv path. The UEFI's AppleSiliconPciPlatformDxe assumes
# m1n1 already brought up the controller ("brought up by m1n1 itself, including tunables"),
# so without this the guest's first PciBus access to the PCIe MMIO window (0x6c0000000)
# takes an SError (L2C bus error) and the hv drops to its shell.
#
# This mirrors what m1n1 does on an Asahi boot (kboot.c:2821 pcie_init()).
import time

print("[pcie] calling m1n1 pcie_init() (P_PCIE_INIT) to bring up the PCIe controller + tunables...")
try:
    ret = p.pcie_init()
    print("[pcie] pcie_init() -> %r  (0=ok, -1=all controllers failed)" % ret)
except Exception as e:
    print("[pcie] pcie_init() raised: %s" % e)
time.sleep(0.5)
print("[pcie] done -- starting guest. UEFI PciBus should now reach devices in 0x6c0000000.")
