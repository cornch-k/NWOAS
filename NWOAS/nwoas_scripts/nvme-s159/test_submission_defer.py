"""Exercise actual candidate MMIO/poll C with a bounded fake completion backend.

Checks return-before-I/O, eventual progress, CQ backpressure and invalid pointers.
Does not establish real timer delivery, DMA correctness or Windows stability.
"""
from pathlib import Path
import subprocess
import tempfile
import sys

source = Path(sys.argv[1] if len(sys.argv)>1 else '/Volumes/X31/NWOAS/m1n1_windows-s159/src/hv_vm.c').read_text()
def function(name):
    pos=source.index(name+'(')
    start=source.rfind('\n',0,pos)+1
    brace=source.index('{',pos);depth=1;end=brace+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]

prelude=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
struct exc_info { int unused; };
#define NWOAS_NVME_BAR 0x700100000ULL
#define NWOAS_NVME_BAR_SIZE 0x4000ULL
static struct { bool armed,faulted,deferred; u16 sq_head,sq_tail,sq_size,cq_head,cq_size,cq_pending; u32 errors; } nwoas_nvme_fp;
static struct { u32 max_sq_backlog,cq_ack_writes; } nwoas_nvme_diag;
static unsigned calls, emitted[16], emitted_n;
static void nwoas_nvme_fastpath_update_irq(void) {}
static void nwoas_nvme_fastpath_process(struct exc_info *ctx,u32 budget) {
    (void)ctx; calls++;
    if (budget && nwoas_nvme_fp.sq_head != nwoas_nvme_fp.sq_tail && nwoas_nvme_fp.cq_pending < nwoas_nvme_fp.cq_size-1) {
        emitted[emitted_n++]=nwoas_nvme_fp.sq_head;
        nwoas_nvme_fp.sq_head=(nwoas_nvme_fp.sq_head+1)%nwoas_nvme_fp.sq_size;
        nwoas_nvme_fp.cq_pending++;
    }
    nwoas_nvme_fp.deferred=nwoas_nvme_fp.sq_head!=nwoas_nvme_fp.sq_tail;
}
'''
main=r'''
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    struct exc_info ctx={0}; u64 value=3;
    nwoas_nvme_fp.armed=true;nwoas_nvme_fp.sq_size=8;nwoas_nvme_fp.cq_size=4;
    CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+0x1008,&value,true,2));
    CHECK(calls==0 && nwoas_nvme_fp.deferred);
    nwoas_nvme_fastpath_poll(&ctx);nwoas_nvme_fastpath_poll(&ctx);nwoas_nvme_fastpath_poll(&ctx);
    CHECK(calls==3 && nwoas_nvme_fp.sq_head==3 && nwoas_nvme_fp.cq_pending==3);
    value=6;CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+0x1008,&value,true,2));
    nwoas_nvme_fastpath_poll(&ctx);
    CHECK(calls==3 && nwoas_nvme_fp.sq_head==3); /* full CQ blocks progress */
    value=2;CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+0x100c,&value,true,2));
    CHECK(calls==3 && nwoas_nvme_fp.cq_pending==1); /* ack never executes I/O */
    nwoas_nvme_fastpath_poll(&ctx);nwoas_nvme_fastpath_poll(&ctx);
    CHECK(nwoas_nvme_fp.sq_head==5 && nwoas_nvme_fp.cq_pending==3);
    value=1;CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+0x100c,&value,true,2));
    nwoas_nvme_fastpath_poll(&ctx);
    CHECK(nwoas_nvme_fp.sq_head==6 && !nwoas_nvme_fp.deferred);
    for(unsigned i=0;i<6;i++) CHECK(emitted[i]==i);
    value=8;CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+0x1008,&value,true,2));
    CHECK(nwoas_nvme_fp.errors==1 && nwoas_nvme_fp.sq_tail==6);
    value=0;CHECK(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+0x100c,&value,true,2));
    CHECK(nwoas_nvme_fp.errors==2 && nwoas_nvme_fp.cq_head==1);
    puts("PASS: deferred submit, ordered tick progress, CQ backpressure, bounds");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c=Path(tmp)/'test.c';exe=Path(tmp)/'test'
    c.write_text(prelude+function('nwoas_nvme_fastpath_poll')+'\n'+function('nwoas_nvme_fastpath_mmio')+main)
    subprocess.run(['/usr/bin/clang','-O2','-Wall','-Wextra',str(c),'-o',str(exe)],check=True)
    raise SystemExit(subprocess.run([str(exe)]).returncode)
