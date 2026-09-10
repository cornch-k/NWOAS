/* Mock physical callbacks; actual adapter/control/MMIO functions are included. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
/* Silence inherited target-only diagnostics in the host ABI harness. */
#define printf(...) (target_printf_calls++)
static unsigned target_printf_calls;
typedef uint64_t u64;typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;
#define BIT(n) (1ULL<<(n))
#define NWOAS_NVME_BAR 0x700100000UL
#define NWOAS_NVME_BAR_SIZE 0x4000UL
#define NWOAS_NVME_NAMESPACE_LBAS 61279344UL
#define NWOAS_NVME_MAX_IO_DEPTH 256
struct nwoas_nvme_guest_cmd {u8 data[64];};
struct nwoas_nvme_guest_cqe {u8 data[16];};
struct exc_info {int unused;};
static _Alignas(16384) u8 ram[0x100000];
static u64 nvme_guest_low_backing,nvme_guest_high_min,nvme_guest_high_max;
static struct {u64 base,size;} mcc_carveouts[2];
static size_t mcc_carveout_count;
#define SZ_4K 4096
#include "page_actual.inc"
#include "state_actual.inc"
static bool fast_level,host_level,fail_flush,mismatch;
static unsigned flush_count,barriers,process_count;
static u64 nwoas_ipa_to_pa(u64 ipa){u64 page=nvme_guest_page_pa(ipa&~0xfffULL);return mismatch?0:page+(ipa&0xfff);}
static bool nwoas_in_dram(u64 a,size_t n){return a>=(uintptr_t)ram && a<=(uintptr_t)ram+sizeof(ram) && n<=(uintptr_t)ram+sizeof(ram)-a;}
static void *nwoas_nvme_guest_ptr(u64 a,size_t n){u64 pa=nwoas_ipa_to_pa(a);return pa && nwoas_in_dram(pa,n)?(void *)pa:0;}
static void dma_wmb(void){barriers++;}
static bool nvme_flush(int ns){assert(ns==1);flush_count++;return !fail_flush;}
void nwoas_nvme_set_irq(u64 v){host_level=v;}
void nwoas_nvme_set_fast_irq(u64 v){fast_level=v;}
static bool s229_fast_irq_update(void);
static void nwoas_nvme_fastpath_update_irq(void){if(s229_fast_irq_update())return;fast_level=nwoas_nvme_fp.armed && nwoas_nvme_fp.cq_pending;}
static void nwoas_nvme_fastpath_process(struct exc_info *ctx,u32 budget){(void)ctx;(void)budget;process_count++;}
static void nwoas_nvme_probe_cq_map(struct exc_info *ctx){(void)ctx;}
static void nwoas_nvme_dump_cq_map(void){}
#include "adapter.inc"
#include "control_actual.inc"
#include "mmio_actual.inc"
static void write_reg(u32 offset,u64 v){struct exc_info ctx={0};assert(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+offset,&v,true,2));}
static u64 read_reg(u32 offset){struct exc_info ctx={0};u64 v=0;assert(nwoas_nvme_fastpath_mmio(&ctx,NWOAS_NVME_BAR+offset,&v,false,2));return v;}
static void put32(u8 *p,u32 v){for(unsigned i=0;i<4;i++)p[i]=(u8)(v>>(i*8));}
static void put64(u8 *p,u64 v){for(unsigned i=0;i<8;i++)p[i]=(u8)(v>>(i*8));}
static void fresh(void)
{
 memset(ram,0,sizeof(ram));memset(&nwoas_nvme_fp,0,sizeof(nwoas_nvme_fp));memset(&s229_front,0,sizeof(s229_front));
 s229_enabled=false;s229_sq_generation=s229_cq_generation=0;
 nvme_guest_low_backing=(uintptr_t)ram;nvme_guest_high_min=(uintptr_t)ram;nvme_guest_high_max=(uintptr_t)ram+sizeof(ram);
 mcc_carveout_count=0;mismatch=fail_flush=fast_level=host_level=false;flush_count=process_count=barriers=0;
 assert(nwoas_nvme_fastpath_control(17,0,0,0,0,0)==0x5332323900000001ULL);
 assert(nwoas_nvme_fastpath_control(16,8448,8,0,0,0)==1);
 write_reg(0x24,0x000f000f);write_reg(0x28,0x10000);write_reg(0x30,0x20000);write_reg(0x14,0x460001);assert(read_reg(0x1c)==1);
}
static void submit(u8 op,u32 a,u32 b,u64 base)
{
 u8 *cmd=ram+s229_front.ring.sq_base+s229_front.ring.sq_tail*64;memset(cmd,0,64);cmd[0]=op;put32(cmd+40,a);put32(cmd+44,b);put64(cmd+24,base);
 if(op==6)put64(cmd+32,0x50000);
 write_reg(0x1000,(s229_front.ring.sq_tail+1)%s229_front.ring.sq_depth);
}
static void ack_admin(void){write_reg(0x1004,s229_front.ring.cq_tail);}
int main(void)
{
 fresh();assert(!host_level);
 unsigned prints=target_printf_calls;
 for(unsigned i=0;i<15;i++)submit(8,0,0,0);
 submit(5,0xf0001,3,0x60000);submit(1,0xf0001,0x10001,0x70000);
 ack_admin();s229_poll();s229_poll();assert(nwoas_nvme_fp.armed && target_printf_calls==prints);
 fresh();
 assert(s229_contains(0,0x13000,16384)); /* ASQ may straddle a 16 KiB S2 page. */
 assert(!s229_contains(0,UINT64_MAX-1,16));assert(!s229_contains(0,0x100000,1));
 mismatch=true;assert(!s229_contains(0,0x10000,64));mismatch=false;
 mcc_carveouts[0].base=(uintptr_t)ram+0x50000;mcc_carveouts[0].size=4096;mcc_carveout_count=1;
 submit(6,1,0,0x40ffc);assert((ram[0x20000+14]>>1)==2);assert(ram[0x40ffc]==0);ack_admin();mcc_carveout_count=0;
 submit(6,1,0,0x40ffc);assert((ram[0x20010+14]>>1)==0 && barriers==2);ack_admin();
 submit(5,0xf0001,3,0x60000);ack_admin();assert(s229_front.ring.admin.cq.present && !nwoas_nvme_fp.armed);
 submit(1,0xf0001,0x10001,0x70000);ack_admin();assert(nwoas_nvme_fp.armed);
 write_reg(0x1008,1);assert(process_count==1);
 nwoas_nvme_fp.cq_pending=1;nwoas_nvme_fp.cq_tail=1;nwoas_nvme_fastpath_update_irq();assert(fast_level);
 write_reg(0xc,1);assert(!fast_level && nwoas_nvme_fp.mask==1);write_reg(0x10,1);assert(fast_level);
 submit(0,1,0,0);ack_admin();assert(!nwoas_nvme_fp.armed && nwoas_nvme_fp.cq_pending==1 && fast_level);
 unsigned count=process_count;write_reg(0x100c,1);assert(nwoas_nvme_fp.cq_pending==0 && !fast_level && process_count==count);
 /* Truncated generation equality must not preserve old cursor state. */
 s229_front.ring.admin.sq_generation=s229_sq_generation+4096;
 nwoas_nvme_fp.sq_head=7;nwoas_nvme_fp.sq_tail=8;
 assert(s229_reconcile(0,&s229_front.ring.admin,1));assert(nwoas_nvme_fp.sq_head==0 && nwoas_nvme_fp.sq_tail==0);
 submit(1,0xf0001,0x10001,0x70000);ack_admin();assert(nwoas_nvme_fp.armed);
 submit(9,6,0,0);ack_admin();assert(flush_count==1 && !s229_cache && !nwoas_nvme_fp.cache_enabled);
 write_reg(0x14,0x464001);assert((read_reg(0x1c)&12)==8 && flush_count==2 && !nwoas_nvme_fp.armed);
 write_reg(0x14,0x460001);unsigned old_process=process_count;write_reg(0x1008,1);assert(process_count==old_process && !(read_reg(0x1c)&2));
 fresh();nwoas_nvme_fp.link_busy=true;write_reg(0x14,0);assert((read_reg(0x1c)&2) && !fast_level);
 fresh();fail_flush=true;submit(9,6,0,0);assert((read_reg(0x1c)&2) && nwoas_nvme_fp.faulted && !s229_front.ring.pending && !fast_level);
 fresh();submit(5,0xf0001,3,0x60000);ack_admin();submit(1,0xf0001,0x10001,0x70000);ack_admin();write_reg(0x100c,2);assert(read_reg(0x1c)&2);
 fresh();submit(12,0,0,0);assert(s229_front.ring.admin.aer_present);s229_poll();write_reg(0x14,0);assert(!s229_front.ring.admin.aer_present);
 puts("PASS actual S230 adapter including no tick-path printf + S149 control/MMIO: page/carveout checks, admin publication, CQ ack without SQ, generation wrap, masks, shutdown and fatal backend");
}
