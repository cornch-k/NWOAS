#!/usr/bin/env python3
"""S208: run the existing S163 real IRQ-bridge test against the ACTUAL candidate
source (nvme-s208/hv_exc-s208.c, a verbatim copy of the m1n1-diag-s208 worktree
src/hv_exc.c), for gaps 0, 25, 50 and 200 microseconds.

This is the same bounded harness as nvme-s163/test_irq_gap.py: it compiles the
real `hv_bridge_nvme_irq` and `nwoas_nvme_note_eoi` (extracted from the candidate
source) against a fake clock / fake vGIC and checks affinity, disabled line,
capacity, dedup, exact deadline, live-level cancellation, eventual redelivery
and 64-bit counter wrap.

Source-extraction limit (why this does not, by itself, exercise the S208 fix):
the S208 change is at the CALL SITE inside `hv_exc_irq`
(`nwoas_nvme_note_eoi(intd, hv_get_elr())`), not inside `hv_bridge_nvme_irq` or
`nwoas_nvme_note_eoi`, which are byte-identical to S163. `hv_exc_irq` cannot be
compiled standalone (it pulls in the whole vGIC/AIC/exception stack), so the
changed line is validated separately by harness/harness.c, which extracts and
compiles the real call-site region. This test's job is the complementary one:
prove the candidate source still satisfies every gap/mask/queue/delivery
invariant, i.e. the fix disturbed nothing. It is NOT a hardware stability proof.
"""
from pathlib import Path
import subprocess, tempfile, sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'nvme-s160'))
from test_local_mask import extract_c

here = Path(__file__).resolve().parent
src = (here / 'hv_exc-s208.c').read_text()

# Slice the candidate's define + counters + nwoas_nvme_note_eoi verbatim.
ifndef = src.index('#ifndef NWOAS_NVME_REASSERT_US')
note = extract_c(src, 'nwoas_nvme_note_eoi')
state_and_note = src[ifndef: src.index(note) + len(note)]

bridge = extract_c(src, 'hv_bridge_nvme_irq')

preamble = r"""
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

main = r"""
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

CC = '/opt/homebrew/opt/llvm/bin/clang'
with tempfile.TemporaryDirectory() as t:
    path = Path(t)
    (path / 'test.c').write_text(preamble + state_and_note + bridge + main)
    for us in (0, 25, 50, 200):
        print(f'--- gap {us}us (candidate source) ---')
        subprocess.run([CC, '-Wall', '-Wextra', '-Werror',
                        f'-DNWOAS_NVME_REASSERT_US={us}',
                        str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
print('ALL GAPS PASS (0/25/50/200) against candidate source')
