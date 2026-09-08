from pathlib import Path
import os,subprocess,hashlib,json
root=Path('/Volumes/X31/NWOAS');repo=root/'apple_silicon_platforms_mu';out=root/'nwoas_scripts/uefi-s125'
src=repo/'Silicon/Apple/AppleSiliconPkg/Drivers/NwoasHideHighRamDxe/NwoasHideHighRamDxe.c'
original=src.read_bytes();needle=b'#define NWOAS_UEFI_NVME 0 '
assert original.count(needle)==1
candidate=original.replace(needle,b'#define NWOAS_UEFI_NVME 1 ',1)
(out/'NwoasHideHighRamDxe.before.c').write_bytes(original)
env=os.environ.copy();env.update(CLANG_BIN='/opt/homebrew/opt/llvm/bin/',CLANG_HOST_BIN='/usr/bin/')
env['PATH']=str(repo/'venv/bin')+':/opt/homebrew/opt/llvm/bin:'+env['PATH']
try:
 src.write_bytes(candidate)
 with (out/'build.log').open('w') as log:
  result=subprocess.run([str(repo/'venv/bin/stuart_build'),'-c','Platform/MacMini2020Pkg/PlatformBuild.py','TARGET=DEBUG','TOOL_CHAIN_TAG=CLANGPDB'],cwd=repo,env=env,stdout=log,stderr=subprocess.STDOUT)
 if result.returncode:raise SystemExit(f'Build failed {result.returncode}; see build.log')
 fd=(repo/'Build/MacMini2020-AARCH64/DEBUG_CLANGPDB/FV/J274MACMINI2020_EFI.fd').read_bytes()
 old=(root/'m1n1_windows/m1n1-payload-s102-exclwin.bin').read_bytes()
 assert len(old)==32342016 and len(fd)==len(old)-1376256
 payload=old[:1376256]+fd
 target=root/'m1n1_windows/m1n1-payload-s125-uefinvme.bin';target.write_bytes(payload)
 (out/'manifest.json').write_text(json.dumps({'payload':str(target),'sha256':hashlib.sha256(payload).hexdigest(),'bytes':len(payload),'source_before_sha256':hashlib.sha256(original).hexdigest(),'change':'NWOAS_UEFI_NVME 0->1 for candidate build only; original source restored; hardware untested'},indent=2))
 print('Built candidate',target,hashlib.sha256(payload).hexdigest())
finally:
 if src.read_bytes()!=candidate:raise RuntimeError('Concurrent source edit detected; not restoring over it')
 src.write_bytes(original)
