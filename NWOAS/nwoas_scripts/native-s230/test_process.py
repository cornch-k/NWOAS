"""Extract production process/execute/IRQ functions; mock physical and NS2 boundaries only."""
from pathlib import Path
import subprocess,hashlib,json
h=Path(__file__).resolve().parent;w=Path('/Volumes/X31/NWOAS/m1n1-admin-s230')
t=(w/'src/hv_vm.c').read_text();out=h/'out';out.mkdir(exist_ok=True)
def section(a,b):return t[t.index(a):t.index(b,t.index(a))]
s=(h/'test_adapter.c').read_text()
s=s[:s.index('int main(void)')]
s=s.replace('#define NWOAS_NVME_NAMESPACE_LBAS 61279344UL','').replace('#define NWOAS_NVME_MAX_IO_DEPTH 256','')
a=s.index('struct nwoas_nvme_guest_cmd');b=s.index('struct exc_info',a)
s=s[:a]+section('#define NWOAS_NVME_NAMESPACE_LBAS','static struct {')+s[b:]
a=s.index('static void nwoas_nvme_fastpath_update_irq(void)');b=s.index('static void nwoas_nvme_probe_cq_map',a)
s=s[:a]+section('static void nwoas_nvme_fastpath_update_irq(void)','static void *nwoas_nvme_guest_ptr')+s[b:]
a=s.index('#include "adapter.inc"')
stubs=r"""
#define NWOAS_NVME_EXEC_FATAL (-1)
#define NWOAS_NVME_EXEC_CANCELLED (-2)
static void s229_poll(void);
static u64 fake_ticks,physical_result=1;
static unsigned physical_calls,link_calls;
static int link_mode;
#define START_HV 0
#define HV_NWOAS_NVME_LINK 1
#define mrs(x) (++fake_ticks)
static void dma_rmb(void){}
static u64 nvme_rw_guest(bool write,u64 lba,u32 n,u64 p1,u64 p2){
 (void)write;(void)lba;(void)n;(void)p1;(void)p2;physical_calls++;return physical_result;
}
static void hv_exc_proxy(struct exc_info *ctx,int start,int event,struct hv_nwoas_nvme_link_req *r){
 (void)ctx;assert(start==START_HV && event==HV_NWOAS_NVME_LINK && nwoas_nvme_fp.link_busy);link_calls++;
 r->response=HV_NWOAS_NVME_LINK_DONE;r->status=0;r->result=0x12345678;
 if(link_mode==1)nwoas_nvme_fp.lifecycle_epoch++;
 if(link_mode==2)r->response=0;
 if(link_mode==3)r->sequence++;
 if(link_mode==4)r->status=0x8000;
}

"""
nv=(w/'src/nvme.h').read_text()
stubs='\n'.join(x for x in nv.splitlines() if x.startswith('#define NVME_GUEST_'))+'\n'+stubs
hv=(w/'src/hv.h').read_text();a1=hv.index('#define HV_NWOAS_NVME_LINK_MAGIC');b1=hv.index('/* VM */',a1)
stubs='#define PACKED __attribute__((packed))\n'+hv[a1:b1]+stubs
s=s[:a]+stubs+section('static int nwoas_nvme_link_execute','#include "nwoas_s229_adapter.inc"')+s[a:]
a=s.index('static void *nwoas_nvme_guest_ptr');b=s.index('static void dma_wmb',a)
s=s[:a]+'#define MASK(n) ((1ULL<<(n))-1)\n#define VADDR_L3_OFFSET_BITS 14\n#define PAGE_SIZE 16384\n'+section('static void *nwoas_nvme_guest_ptr','#define NWOAS_NVME_EXEC_FATAL')+s[b:]
s+='' + (h/'test_process_cases.inc').read_text()
(out/'process-harness.c').write_text(s)
cmd=['/usr/bin/clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),'-I'+str(h),'-I'+str(w/'src')]
for n in [209,225,226,227,228]:cmd+=['-I'+str(h.parent/f'native-s{n}')]
cmd+=[str(out/'process-harness.c')]
for n,f in [(228,'frontend'),(209,'model'),(227,'admin_ring'),(226,'admin_state'),(225,'admin_payload')]:cmd+=[str(h.parent/f'native-s{n}'/(f+'.c'))]
cmd+=['-o',str(out/'test-process')]
subprocess.run(cmd,check=True)
r=subprocess.run([str(out/'test-process')],capture_output=True,text=True,check=True)
(h/'process-result.json').write_text(json.dumps({'status':'PASS','output':r.stdout.strip(),'hv_vm_sha256':hashlib.sha256(t.encode()).hexdigest(),'header_sha256':{f:hashlib.sha256((w/'src'/f).read_bytes()).hexdigest() for f in ['nvme.h','hv.h']},'directed_cases':28,'scope':'Actual execute/process/poll/IRQ/link/guest_ptr code; mocked physical callbacks, hv_exc_proxy response, clock, IPA translation and DRAM range. Production NVME_GUEST constants extracted from nvme.h; no physical device fault injection'},indent=2)+'\n')
print(r.stdout,end='')
