"""Create an isolated S224 build tree from pinned S208 companion sources."""
from pathlib import Path
import hashlib,json,subprocess
R=Path('/Volumes/X31/NWOAS');S=R/'nwoas_scripts/native-s224';W=R/'m1n1-read-s224'
base='bddf7f06f033a7411834ac61381d8c997034f532'
patch=R/'nwoas_scripts/publish-s217/companion/m1n1_windows.patch'
assert hashlib.sha256(patch.read_bytes()).hexdigest()=='676dbc416c51c7bec2a9ef24ae9d2a4a4adbdc2ef80b059de709c8728abeb1d7'
if W.exists():raise SystemExit('Refuse to replace existing S224 worktree')
subprocess.run(['git','-C',str(R/'m1n1_windows'),'worktree','add','--detach',str(W),base],check=True)
subprocess.run(['git','apply',str(patch)],cwd=W,check=True)
vendor='4eccb50d011146fbed20e133d33b22f3c27292e7'
archive=subprocess.check_output(['git','-C',str(R/'m1n1_windows/.git/modules/rust/vendor/rust-fatfs'),'archive',vendor])
subprocess.run(['tar','-x','-C',str(W/'rust/vendor/rust-fatfs')],input=archive,check=True)
p=W/'src/hv_vm.c';t=p.read_text()
anchor='/* action 0=disable, 1=arm, 2=sync policy, 3=query counters, 4=query lifecycle,'
assert t.count(anchor)==1
t=t.replace(anchor,'#include "nwoas_read_mirror.h"\nstatic struct nwoas_read_mirror nwoas_read_projection;\n\n'+anchor)
anchor='''    if (action == 0) {
        nwoas_nvme_fp.armed = false;'''
assert t.count(anchor)==1
t=t.replace(anchor,'''    /* S224: action11 publishes under the existing proxy rendezvous. Reads
     * below and publication are serialized by the HV big lock. */
    if (action == 11) {
        nwoas_mirror_publish(&nwoas_read_projection, sq_base, sq_size, cq_base, cq_size, flags);
        return 0x5332323400000001ULL;
    }
    if (action == 12) return 0x5332323400000001ULL;
    if (action == 13) return nwoas_read_projection.pci_reads;
    if (action == 14) return nwoas_read_projection.register_reads;
    if (action == 15) return nwoas_read_projection.publications;
'''+anchor)
anchor='''    if (!nwoas_nvme_fp.armed || ipa < NWOAS_NVME_BAR ||'''
assert t.count(anchor)==1
t=t.replace(anchor,'''    if (!write && nwoas_mirror_read(&nwoas_read_projection, ipa, width,
            nwoas_nvme_fp.armed, nwoas_nvme_fp.faulted, nwoas_nvme_fp.mask, val))
        return true;
'''+anchor)
p.write_text(t);(W/'src/nwoas_read_mirror.h').write_bytes((S/'read_mirror.h').read_bytes())
(S/'manifest.json').write_text(json.dumps({'base_commit':base,'baseline_patch_sha256':hashlib.sha256(patch.read_bytes()).hexdigest(),'hv_vm_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'projection_sha256':hashlib.sha256((S/'read_mirror.h').read_bytes()).hexdigest(),'status':'Source-only candidate; no hardware qualification','scope':'Read projection only. Host control/admin writes and NS2 remain.','hv_flags':'-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50'},indent=2)+'\n')
print('Prepared isolated S224 tree')
