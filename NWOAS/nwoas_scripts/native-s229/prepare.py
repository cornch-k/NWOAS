from pathlib import Path
import subprocess,hashlib,json,shutil
R=Path('/Volumes/X31/NWOAS');S=R/'nwoas_scripts';H=S/'native-s229';W=R/'m1n1-admin-s229'
base='bddf7f06f033a7411834ac61381d8c997034f532';patch=S/'publish-s217/companion/m1n1_windows.patch'
assert hashlib.sha256(patch.read_bytes()).hexdigest()=='676dbc416c51c7bec2a9ef24ae9d2a4a4adbdc2ef80b059de709c8728abeb1d7'
if W.exists():raise SystemExit('Refuse to replace existing S229 worktree')
subprocess.run(['git','-C',str(R/'m1n1_windows'),'worktree','add','--detach',str(W),base],check=True)
subprocess.run(['git','apply',str(patch)],cwd=W,check=True)
a=subprocess.check_output(['git','-C',str(R/'m1n1_windows/.git/modules/rust/vendor/rust-fatfs'),'archive','4eccb50d011146fbed20e133d33b22f3c27292e7']);subprocess.run(['tar','-x','-C',str(W/'rust/vendor/rust-fatfs')],input=a,check=True)
d=W/'src/nwoas_frontend';d.mkdir()
for stage,files in {'native-s209':['model.c','model.h'],'native-s225':['admin_payload.c','admin_payload.h'],'native-s226':['admin_state.c','admin_state.h'],'native-s227':['admin_ring.c','admin_ring.h'],'native-s228':['frontend.c','frontend.h']}.items():
 for name in files:shutil.copyfile(S/stage/name,d/name)
shutil.copyfile(H/'adapter.inc',W/'src/nwoas_s229_adapter.inc')
p=W/'Makefile';t=p.read_text().replace('hv.o hv_vm.o','nwoas_frontend/model.o nwoas_frontend/admin_payload.o nwoas_frontend/admin_state.o nwoas_frontend/admin_ring.o nwoas_frontend/frontend.o \\\n\thv.o hv_vm.o');p.write_text(t)
p=W/'src/nvme.c';t=p.read_text();assert t.count('static u64 nvme_guest_page_pa(u64 gpa)')==1;t=t.replace('static u64 nvme_guest_page_pa(u64 gpa)','u64 nvme_guest_page_pa(u64 gpa)');p.write_text(t)
p=W/'src/hv_vm.c';t=p.read_text()
a='static void nwoas_nvme_fastpath_update_irq(void)\n{';assert t.count(a)==1;t=t.replace(a,'static bool s229_fast_irq_update(void);\nstatic void s229_poll(void);\n'+a+'\n    if (s229_fast_irq_update())return;')
a='void nwoas_nvme_fastpath_poll(struct exc_info *ctx)\n{';assert t.count(a)==1;t=t.replace(a,a+'\n    s229_poll();')
a='/* action 0=disable, 1=arm, 2=sync policy, 3=query counters, 4=query lifecycle,';assert t.count(a)==1;t=t.replace(a,'#include "nwoas_s229_adapter.inc"\n\n'+a)
a='    if (action == 0) {\n        nwoas_nvme_fp.armed = false;';assert t.count(a)==1;t=t.replace(a,'''    if(action==16)return s229_enable(sq_base,sq_size);
    if(action==17)return 0x5332323900000001ULL;
    if(action==18)return s229_reg_reads;
    if(action==19)return s229_reg_writes;
    if(action==20)return s229_admin_fetches;
    if(action==21)return s229_admin_completions;
    if(action==22)return s229_front.control.csts | ((u64)s229_front.backend_fault<<32);
'''+a)
a='    if (!nwoas_nvme_fp.armed || ipa < NWOAS_NVME_BAR ||';assert t.count(a)==1;t=t.replace(a,'''    if(s229_mmio(ipa,val,write,width))return true;
    if ((!nwoas_nvme_fp.armed && !s229_cq_ack_unarmed(ipa)) || ipa < NWOAS_NVME_BAR ||''')
p.write_text(t)
manifest={'stage':'S229','base_commit':base,'status':'Build/integration candidate, not hardware qualified','sources':{str(p.relative_to(W)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [W/'Makefile',W/'src/nvme.c',W/'src/hv_vm.c',W/'src/nwoas_s229_adapter.inc',*sorted(d.iterdir())]},'scope':'Local PCI/control/admin owner; existing ANS data path, NS2 and host boot setup retained'}
(H/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print('S229 worktree prepared')
