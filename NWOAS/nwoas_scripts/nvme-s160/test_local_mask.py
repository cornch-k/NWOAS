"""Actual candidate C register handler and actual host fallback functions, with fakes.
No hardware is opened. These checks do not establish guest interrupt stability.
"""
from pathlib import Path
import ast, subprocess, tempfile, types, unittest
ROOT=Path(__file__).resolve().parent

def extract_c(source,name):
    pos=source.index(name+'(');start=source.rfind('\n',0,pos)+1
    brace=source.index('{',pos);end=brace+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

class Tests(unittest.TestCase):
 def test_actual_c_mask_handler(self):
    source=(ROOT/'hv_vm-s160.c').read_text()
    c=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
typedef unsigned long u64;typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;
struct exc_info {int unused;};
#define NWOAS_NVME_BAR 0x700100000ULL
#define NWOAS_NVME_BAR_SIZE 0x4000ULL
#define BIT(n) (1ULL<<(n))
#define NWOAS_NVME_MAX_IO_DEPTH 256
struct nwoas_nvme_guest_cmd {u8 bytes[64];};
struct nwoas_nvme_guest_cqe {u8 bytes[16];};
static void *nwoas_nvme_guest_ptr(u64 ptr,unsigned long len) {(void)len;return ptr?(void*)1:0;}
static struct {bool armed,faulted,deferred,local_mask_allowed,irq_enabled,cache_enabled,link_busy;u32 mask,errors,commands,lifecycle_epoch;u64 sq_base,cq_base;u16 sq_head,sq_tail,sq_size,cq_head,cq_tail,cq_size,cq_pending,sq_generation,cq_generation;u8 cq_phase;} nwoas_nvme_fp;
static struct {u32 max_sq_backlog,cq_ack_writes,local_mask_reads,local_mask_writes;u64 max_ns1_ticks,max_ns2_ticks;} nwoas_nvme_diag;
static unsigned executed;static bool level;
static void nwoas_nvme_fastpath_process(struct exc_info *ctx,u32 budget) {(void)ctx;(void)budget;executed++;}
static void nwoas_nvme_fastpath_update_irq(void) {level=nwoas_nvme_fp.irq_enabled && !(nwoas_nvme_fp.mask&1) && nwoas_nvme_fp.cq_pending;}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);return 1;}}while(0)
'''+extract_c(source,'nwoas_nvme_fastpath_control')+extract_c(source,'nwoas_nvme_fastpath_mmio')+r'''
int main(void) {
 struct exc_info ctx={0};u64 v=1;
 nwoas_nvme_fp.armed=true;nwoas_nvme_fp.local_mask_allowed=true;nwoas_nvme_fp.irq_enabled=true;nwoas_nvme_fp.cq_pending=2;
 CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+12,&v,true,2));CHECK(nwoas_nvme_fp.mask==1&&!level);
 v=0x80000000;CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+12,&v,true,2));CHECK(nwoas_nvme_fp.mask==0x80000001);
 v=1;CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+16,&v,true,2));CHECK(nwoas_nvme_fp.mask==0x80000000&&level);
 v=0;CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+12,&v,false,2));CHECK(v==0x80000000);
 CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+16,&v,false,2));CHECK(v==0x80000000);
 nwoas_nvme_fp.local_mask_allowed=false;v=1;CHECK(!nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+12,&v,true,2));CHECK(nwoas_nvme_fp.mask==0x80000000);
 CHECK(!nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+16,&v,false,2));
 nwoas_nvme_fp.local_mask_allowed=true;CHECK(!nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+12,&v,true,3));
 nwoas_nvme_fp.faulted=true;CHECK(!nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+12,&v,true,2));nwoas_nvme_fp.faulted=false;
 nwoas_nvme_fp.armed=false;CHECK(!nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+12,&v,true,2));
 CHECK(nwoas_nvme_fastpath_control(1,0x1000,8,0x2000,8,BIT(3)|BIT(1)|1)==1);CHECK(nwoas_nvme_fp.local_mask_allowed);
 CHECK(nwoas_nvme_fastpath_control(8,0,0,0,0,0)==(0x53313630UL<<32));
 CHECK(nwoas_nvme_fastpath_control(0,0,0,0,0,0)==1);CHECK(!nwoas_nvme_fp.armed);
 CHECK(nwoas_nvme_fastpath_control(1,0x1000,8,0x2000,8,(0x80000001UL<<32)|BIT(1)|1)==1);CHECK(!nwoas_nvme_fp.local_mask_allowed);CHECK(nwoas_nvme_fp.mask==0x80000001);
 CHECK(nwoas_nvme_fastpath_control(2,0,0,0,0,(0x80000001UL<<32)|BIT(3)|BIT(1))==1);CHECK(nwoas_nvme_fp.local_mask_allowed);
 CHECK(nwoas_nvme_fastpath_control(8,0,0,0,0,0)==((0x53313630UL<<32)|0x80000001UL));
 CHECK(nwoas_nvme_fastpath_control(9,0,0,0,0,0)==((3UL<<32)|2));
 CHECK(executed==0);CHECK(nwoas_nvme_diag.local_mask_writes==3&&nwoas_nvme_diag.local_mask_reads==2);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as t:
      p=Path(t);(p/'test.c').write_text(c)
      subprocess.run(['/usr/bin/clang','-Wall','-Wextra','-O2',str(p/'test.c'),'-o',str(p/'test')],check=True)
      subprocess.run([str(p/'test')],check=True)

 def host(self,enabled=True):
    tree=ast.parse((ROOT/'guest_module.py').read_text());names={'_fast_pull_mask','_fast_flags','_fast_ready','_fast_arm_if_ready','pci_read','pci_write','mmio_read','mmio_write'}
    self.events=[]
    class Controller:
      mask=0;command=0;cc=1;csts=1
      cq={0:types.SimpleNamespace(ien=True,pending=0),1:types.SimpleNamespace(ien=True,pending=0)}
      sq={1:object()}
      def read(c,off,width):self.events.append(('read',c.mask));return c.mask
      def write(c,off,value,width):
        self.events.append(('write',c.mask))
        if off==12:c.mask|=value
        if off==16:c.mask&=~value
        if off==0x1000:c.cq[0].pending=1;c.update_irq()
        if off==0x1004:c.cq[0].pending=0;c.update_irq()
      def pci_read(c,off,width):self.events.append(('pci_read',c.mask));return 0
      def pci_write(c,off,value,width):self.events.append(('pci_write',c.mask));c.command=value
    c=Controller()
    base_tree=ast.parse((ROOT.parent/'nvme-s124/controller.py').read_text())
    base_cls=next(n for n in base_tree.body if isinstance(n,ast.ClassDef) and n.name=='Controller')
    irq_func=next(n for n in base_cls.body if isinstance(n,ast.FunctionDef) and n.name=='update_irq')
    namespace={};exec(compile(ast.Module(body=[irq_func],type_ignores=[]),'<actual controller IRQ>','exec'),namespace)
    c.update_irq=types.MethodType(namespace['update_irq'],c)
    c.irq=lambda level:self.events.append(('host_irq',level))
    def query(action,*args,**kwargs):self.events.append(('query',action));return (0x53313630<<32)|0x80000001
    env=dict(c=c,p=types.SimpleNamespace(nwoas_nvme_fastpath=query),FAST_MASK_ENABLED=enabled,FAST_MASK_SIGNATURE=0x53313630,FASTPATH_ENABLED=True,fast_armed=True,fast_sq_generation=2,fast_cq_generation=3,_namespace=types.SimpleNamespace(cache_enabled=True),ECAM=0x700000000,BAR=0x700100000,log=lambda x:None,_enter=lambda:0,_leave=lambda x:None,_fast_sync=lambda:self.events.append(('sync',c.mask)),_fast_disable=lambda:None,st={'db':0})
    exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[]),'<candidate functions>','exec'),env)
    return env

 def test_fallback_pulls_before_mask_mutation(self):
    e=self.host();e['mmio_write'](e['BAR']+16,1,32)
    self.assertEqual(e['c'].mask,0x80000000)
    self.assertEqual(self.events[:2],[('query',8),('write',0x80000001)])
    self.assertEqual(self.events[-1],('sync',0x80000000))

 def test_admin_pending_disables_local_mask(self):
    e=self.host();self.assertTrue(e['_fast_flags']()&8)
    e['c'].cq[0].pending=1;self.assertFalse(e['_fast_flags']()&8)
    e['c'].cq[0].pending=0;self.assertTrue(e['_fast_flags']()&8)

 def test_all_fallbacks_pull_and_pci_disable_preserves_mask(self):
    e=self.host()
    self.assertEqual(e['mmio_read'](e['BAR']+12,32),0x80000001)
    e['pci_read'](e['ECAM']+4,16);e['pci_write'](e['ECAM']+4,0x400,16)
    self.assertEqual(sum(x==('query',8) for x in self.events),3)
    self.assertFalse(e['_fast_flags']()&2)
    self.assertEqual(e['_fast_flags']()>>32,0x80000001)

 def test_admin_completion_uses_pulled_unmask_before_irq(self):
    e=self.host();e['c'].mask=1
    e['p'].nwoas_nvme_fastpath=lambda action:(0x53313630<<32)
    e['mmio_write'](e['BAR']+0x1000,1,32)
    self.assertIn(('host_irq',True),self.events)
    self.assertFalse(e['_fast_flags']()&8)
    e['mmio_write'](e['BAR']+0x1004,1,32)
    self.assertIn(('host_irq',False),self.events)
    self.assertTrue(e['_fast_flags']()&8)

 def test_feature_off_does_not_query_new_abi(self):
    e=self.host(False);e['mmio_read'](e['BAR']+12,32)
    self.assertFalse(any(x[0]=='query' for x in self.events));self.assertFalse(e['_fast_flags']()&8)

 def test_arm_rejects_missing_target_capability(self):
    e=self.host();e['fast_armed']=False;e['p'].nwoas_nvme_fastpath=lambda action:0
    with self.assertRaisesRegex(RuntimeError,'capability'):e['_fast_arm_if_ready']()
    self.assertFalse(e['fast_armed'])

 def test_wrong_capability_rejected_before_mask_changes(self):
    e=self.host();e['p'].nwoas_nvme_fastpath=lambda action:0
    with self.assertRaisesRegex(RuntimeError,'capability'):e['_fast_pull_mask']()
    self.assertEqual(e['c'].mask,0)

if __name__=='__main__':unittest.main()
