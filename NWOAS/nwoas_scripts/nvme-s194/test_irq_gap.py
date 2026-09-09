#!/usr/bin/env python3
"""Compile actual IRQ bridge/EOI functions with a fake clock and fake vGIC.
Checks eventual redelivery, exact boundary, live-level cancellation, dedup,
affinity, capacity, and counter rollover. Not a hardware stability proof."""
from pathlib import Path
import subprocess,tempfile,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'nvme-s160'))
from test_local_mask import extract_c
p=Path(__file__).resolve().parent
s=(p/'hv_exc-s194.c').read_text()
inc=(p/'irq-gap.inc').read_text()
preamble=r"""
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
typedef unsigned long u64;typedef uint32_t u32;
enum {CNTPCT_EL0,CNTFRQ_EL0};
static u64 now=1000,freq=24000000;
#define mrs(x) ((x)==CNTPCT_EL0?now:freq)
static int hv_pinned_cpu=-1,boot_cpu_idx=0,cpu=0,free_lr=0;
static bool enabled=true,outstanding=false;
static unsigned delivered=0;
static u32 smp_id(void){return cpu;}
static bool hv_vgic3_spi_enabled(u32 i){assert(i==900);return enabled;}
static bool hv_vgic_virq_outstanding(u32 i){assert(i==900);return outstanding;}
static int hv_vgic3_get_free_lr(void){return free_lr;}
static u64 hv_get_elr(void){return 0x1234;}
static unsigned hv_vgic3_get_priority(u32 i){assert(i==900);return 0x50;}
static void hv_vgic3_inject_irq(u32 i,unsigned priority,bool active,bool pending,bool hw,u64 irq){
 assert(i==900&&priority==0x50&&!active&&pending&&!hw&&!irq);delivered++;outstanding=true;
}
static volatile bool nwoas_nvme_irq_host_level,nwoas_nvme_irq_fast_level;
"""
main=r"""
int main(void){
 hv_bridge_nvme_irq();assert(delivered==0);
 nwoas_nvme_irq_fast_level=true;cpu=1;hv_bridge_nvme_irq();assert(delivered==0);cpu=0;
 enabled=false;hv_bridge_nvme_irq();assert(delivered==0);enabled=true;
 free_lr=-1;hv_bridge_nvme_irq();assert(delivered==0);free_lr=0;
 hv_bridge_nvme_irq();assert(delivered==1&&nwoas_nvme_irq_injections==1);
 hv_bridge_nvme_irq();assert(delivered==1);
 nwoas_nvme_note_eoi(698,0x9999);assert(nwoas_nvme_irq_eois==0);
 outstanding=false;nwoas_nvme_note_eoi(900,0xabcd);assert(nwoas_nvme_irq_eois==1);
 u64 interval=freq*NWOAS_NVME_REASSERT_US/1000000;
 hv_bridge_nvme_irq();
 if(interval){assert(delivered==1);now+=interval-1;hv_bridge_nvme_irq();assert(delivered==1);now++;}
 else {assert(delivered==2);outstanding=false;}
 nwoas_nvme_irq_fast_level=false;hv_bridge_nvme_irq();assert(!outstanding);
 nwoas_nvme_irq_host_level=true;hv_bridge_nvme_irq();assert(outstanding);
 unsigned saved=delivered;hv_bridge_nvme_irq();assert(delivered==saved);
 outstanding=false;now=UINT64_MAX-100;nwoas_nvme_note_eoi(900,0xabcd);
 now+=interval;if(!interval)now++;
 hv_bridge_nvme_irq();assert(delivered==saved+1);
 assert(nwoas_nvme_irq_injections==delivered);
 assert(nwoas_nvme_irq_eois==2&&nwoas_nvme_eoi_last_pc==0xabcd);
 puts("PASS actual IRQ bridge: bounded gap and eventual level redelivery");
 return 0;
}
"""
with tempfile.TemporaryDirectory() as t:
 path=Path(t)
 (path/'test.c').write_text(preamble+inc+extract_c(s,'hv_bridge_nvme_irq')+main)
 for us in (0,25,50,200):
  subprocess.run(['/usr/bin/clang','-Wall','-Wextra','-Werror',
                  f'-DNWOAS_NVME_REASSERT_US={us}',str(path/'test.c'),'-o',str(path/'test')],check=True)
  subprocess.run([str(path/'test')],check=True)
