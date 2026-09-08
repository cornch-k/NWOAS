from pathlib import Path
import os,subprocess,hashlib,json
root=Path('/Volumes/X31/NWOAS');repo=root/'apple_silicon_platforms_mu';out=root/'nwoas_scripts/uefi-s128'
base=repo/'Silicon/Apple/AppleSiliconPkg'
hide=base/'Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c'
bm=base/'Library/DeviceBootManagerLib/DeviceBootManagerLib.c'
inf=bm.with_suffix('.inf')
pci=repo/'MU_BASECORE/MdeModulePkg/Bus/Pci/NonDiscoverablePciDeviceDxe/NonDiscoverablePciDeviceIo.c'
original={p:p.read_bytes() for p in (hide,bm,inf,pci)}
assert original[hide].count(b'#define NWOAS_UEFI_NVME 0 ')==1
candidate={hide:original[hide].replace(b'#define NWOAS_UEFI_NVME 0 ',b'#define NWOAS_UEFI_NVME 1 ',1)}
s=original[bm].decode();needle='EFI_HANDLE\nEFIAPI\nDeviceBootManagerBeforeConsole (';assert s.count(needle)==1
s=s.replace('#include <Uefi.h>','#include <Uefi.h>\n#include <Guid/NonDiscoverableDevice.h>\n#include <Protocol/NonDiscoverableDevice.h>',1)
s=s.replace(needle,(out/'connect_nvme.c.inc').read_text()+'\n'+needle,1)
s=s.replace('  MsBootOptionsLibRegisterDefaultBootOptions ();','  NwoasConnectNvme ();\n  MsBootOptionsLibRegisterDefaultBootOptions ();',1)
candidate[bm]=s.encode()
s=original[inf].decode().replace('[Guids]','[Guids]\n  gEdkiiNonDiscoverableNvmeDeviceGuid',1).replace('[Protocols]','[Protocols]\n  gEdkiiNonDiscoverableDeviceProtocolGuid',1)
candidate[inf]=s.encode()
s=original[pci].decode()
needle='    Dev->ConfigSpace.Hdr.ClassCode[0] = 0x2; // PCI_IF_NVMHCI'
assert s.count(needle)==1
s=s.replace(needle,'''    // S128: this platform's synthetic controller has a concrete PCI identity.
    // Mu NVMe rejects the generic non-discoverable 0xffff (absent device) VID.
    if (Dev->Device->Resources[0].AddrRangeMin == 0x700100000ULL) {
      Dev->ConfigSpace.Hdr.VendorId = 0x1234;
      Dev->ConfigSpace.Hdr.DeviceId = 0x0010;
      DEBUG ((DEBUG_ERROR, "HVLOG: S128 NVMe PCI identity 1234:0010\\n"));
    }
'''+needle,1)
candidate[pci]=s.encode()
for p,v in original.items(): (out/(p.name+'.before')).write_bytes(v)
for p,v in candidate.items(): (out/(p.name+'.candidate')).write_bytes(v)
env=os.environ.copy();env.update(CLANG_BIN='/opt/homebrew/opt/llvm/bin/',CLANG_HOST_BIN='/usr/bin/')
env['PATH']=str(repo/'venv/bin')+':/opt/homebrew/opt/llvm/bin:'+env['PATH']
try:
 for p,v in candidate.items(): p.write_bytes(v)
 with (out/'build.log').open('w') as log:
  r=subprocess.run([str(repo/'venv/bin/stuart_build'),'-c','Platform/MacMini2020Pkg/PlatformBuild.py','TARGET=DEBUG','TOOL_CHAIN_TAG=CLANGPDB'],cwd=repo,env=env,stdout=log,stderr=subprocess.STDOUT)
 if r.returncode: raise SystemExit(f'Build failed {r.returncode}; see build.log')
 fd=(repo/'Build/MacMini2020-AARCH64/DEBUG_CLANGPDB/FV/J274MACMINI2020_EFI.fd').read_bytes()
 old=(root/'m1n1_windows/m1n1-payload-s102-exclwin.bin').read_bytes()
 assert len(old)==32342016 and len(fd)==len(old)-1376256
 payload=old[:1376256]+fd;target=root/'m1n1_windows/m1n1-payload-s128-uefinvme-id.bin';target.write_bytes(payload)
 manifest={'payload':str(target),'sha256':hashlib.sha256(payload).hexdigest(),'bytes':len(payload),'change':'S127 plus synthetic NVMe PCI VID/DID 1234:0010 only at BAR 0x700100000; avoids generic ffff absent-device sentinel; original sources restored; hardware untested'}
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2));print(json.dumps(manifest))
finally:
 for p,v in candidate.items():
  if p.read_bytes()!=v: raise RuntimeError(f'Concurrent source edit detected: {p}; not overwriting')
 for p,v in original.items(): p.write_bytes(v)
