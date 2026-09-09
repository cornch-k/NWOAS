#!/usr/bin/env python3
"""Build the S131 eight-core UEFI candidate while preserving S129 behavior."""

from pathlib import Path
import hashlib
import json
import os
import subprocess


ROOT = Path("/Volumes/X31/NWOAS")
REPO = ROOT / "apple_silicon_platforms_mu"
OUT = ROOT / "nwoas_scripts/smp-s131"
BASE = REPO / "Silicon/Apple/AppleSiliconPkg"

HIDE = BASE / "Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c"
BOOT_MANAGER = BASE / "Library/DeviceBootManagerLib/DeviceBootManagerLib.c"
BOOT_MANAGER_INF = BOOT_MANAGER.with_suffix(".inf")
PCI_IO = REPO / "MU_BASECORE/MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceIo.c"
BOOT_OPTIONS = BASE / "Library/MsBootOptionsLib/MsBootOptionsLib.c"
MADT = REPO / "Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc"
FILES = (HIDE, BOOT_MANAGER, BOOT_MANAGER_INF, PCI_IO, BOOT_OPTIONS, MADT)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


original = {path: path.read_bytes() for path in FILES}
candidate = {}

assert original[HIDE].count(b"#define NWOAS_UEFI_NVME 0 ") == 1
candidate[HIDE] = original[HIDE].replace(
    b"#define NWOAS_UEFI_NVME 0 ", b"#define NWOAS_UEFI_NVME 1 ", 1
)

text = original[BOOT_MANAGER].decode()
function = "EFI_HANDLE\nEFIAPI\nDeviceBootManagerBeforeConsole ("
assert text.count(function) == 1
text = text.replace(
    "#include <Uefi.h>",
    "#include <Uefi.h>\n#include <Guid/NonDiscoverableDevice.h>\n#include <Protocol/NonDiscoverableDevice.h>",
    1,
)
text = text.replace(
    function,
    (ROOT / "nwoas_scripts/uefi-s129/connect_nvme.c.inc").read_text() + "\n" + function,
    1,
)
text = text.replace(
    "  MsBootOptionsLibRegisterDefaultBootOptions ();",
    "  NwoasConnectNvme ();\n  MsBootOptionsLibRegisterDefaultBootOptions ();",
    1,
)
candidate[BOOT_MANAGER] = text.encode()

text = original[BOOT_MANAGER_INF].decode()
text = text.replace("[Guids]", "[Guids]\n  gEdkiiNonDiscoverableNvmeDeviceGuid", 1)
text = text.replace("[Protocols]", "[Protocols]\n  gEdkiiNonDiscoverableDeviceProtocolGuid", 1)
candidate[BOOT_MANAGER_INF] = text.encode()

text = original[PCI_IO].decode()
needle = "    Dev->ConfigSpace.Hdr.ClassCode[0] = 0x2; // PCI_IF_NVMHCI"
assert text.count(needle) == 1
text = text.replace(
    needle,
    """    // S128+: give the synthetic NVMe controller a concrete PCI identity.
    if (Dev->Device->Resources[0].AddrRangeMin == 0x700100000ULL) {
      Dev->ConfigSpace.Hdr.VendorId = 0x1234;
      Dev->ConfigSpace.Hdr.DeviceId = 0x0010;
      DEBUG ((DEBUG_ERROR, \"HVLOG: S128 NVMe PCI identity 1234:0010\\n\"));
    }
""" + needle,
    1,
)
candidate[PCI_IO] = text.encode()

text = original[BOOT_OPTIONS].decode()
usb = "  RegisterFvBootOption (&gMsBootPolicyFileGuid, MS_USB_BOOT, (UINTN)-1, LOAD_OPTION_ACTIVE, (UINT8 *)MS_USB_BOOT_PARM, sizeof (MS_USB_BOOT_PARM));"
ssd = "  RegisterFvBootOption (&gMsBootPolicyFileGuid, MS_SDD_BOOT, (UINTN)-1, LOAD_OPTION_ACTIVE, (UINT8 *)MS_SDD_BOOT_PARM, sizeof (MS_SDD_BOOT_PARM));"
assert text.count(usb + "\n" + ssd) == 1
text = text.replace(
    usb + "\n" + ssd,
    '  DEBUG ((DEBUG_ERROR, "HVLOG: S131 internal SSD first, 8 CPUs enabled\\n"));\n' + ssd + "\n" + usb,
    1,
)
candidate[BOOT_OPTIONS] = text.encode()

madt = original[MADT]
disabled = b"0 /* NWOAS: AP disabled for uniprocessor boot */"
assert madt.count(disabled) == 7
candidate[MADT] = madt.replace(disabled, b"EFI_ACPI_6_3_GIC_ENABLED")

OUT.mkdir(parents=True, exist_ok=True)
for path in FILES:
    (OUT / (path.name + ".before")).write_bytes(original[path])
    (OUT / (path.name + ".candidate")).write_bytes(candidate[path])

env = os.environ.copy()
env.update(CLANG_BIN="/opt/homebrew/opt/llvm/bin/", CLANG_HOST_BIN="/usr/bin/")
env["PATH"] = str(REPO / "venv/bin") + ":/opt/homebrew/opt/llvm/bin:" + env["PATH"]

try:
    for path in FILES:
        path.write_bytes(candidate[path])
    with (OUT / "build.log").open("w") as log:
        result = subprocess.run(
            [
                str(REPO / "venv/bin/stuart_build"),
                "-c",
                "Platform/MacMini2020Pkg/PlatformBuild.py",
                "TARGET=DEBUG",
                "TOOL_CHAIN_TAG=CLANGPDB",
            ],
            cwd=REPO,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
    if result.returncode:
        raise SystemExit(f"Build failed {result.returncode}; see {OUT / 'build.log'}")

    fd = (REPO / "Build/MacMini2020-AARCH64/DEBUG_CLANGPDB/FV/J274MACMINI2020_EFI.fd").read_bytes()
    old = (ROOT / "m1n1_windows/m1n1-payload-s129-ssd-first.bin").read_bytes()
    assert len(old) == 32342016 and len(fd) == len(old) - 1376256
    payload = old[:1376256] + fd
    target = ROOT / "m1n1_windows/m1n1-payload-s131-8cpu.bin"
    target.write_bytes(payload)
    manifest = {
        "payload": str(target),
        "sha256": sha256(payload),
        "bytes": len(payload),
        "base": "S129 SSD-first UEFI/NVMe candidate",
        "change": "Enable all eight M1 GICC entries in MADT; preserve 4 GiB USB DMA safety window",
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
finally:
    for path in FILES:
        if path.read_bytes() != candidate[path]:
            raise RuntimeError(f"Concurrent source edit detected: {path}; not overwriting")
    for path in FILES:
        path.write_bytes(original[path])
