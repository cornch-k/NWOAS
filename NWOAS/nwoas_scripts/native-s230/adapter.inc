/* S230 adapter; included after the existing S149 fastpath code. All calls run
 * under bhl. No new physical write path: existing NVMe window guard remains. */
#include "nwoas_frontend/frontend.h"
extern u64 nvme_guest_page_pa(u64 gpa);
extern void nwoas_nvme_set_irq(u64 level);
u64 nwoas_nvme_fastpath_control(u64,u64,u64,u64,u64,u64);
static struct nwoas_frontend s229_front;
static bool s229_enabled,s229_cache=true,s229_owner_callback;
static u64 s229_reg_reads,s229_reg_writes,s229_admin_fetches,s229_admin_completions;
static u64 s229_sq_generation,s229_cq_generation;
static u64 s229_control_local(u64 action,u64 sq,u64 ns,u64 cq,u64 nc,u64 flags)
{
    bool saved=s229_owner_callback;s229_owner_callback=true;
    u64 result=nwoas_nvme_fastpath_control(action,sq,ns,cq,nc,flags);
    s229_owner_callback=saved;return result;
}
static bool s229_contains(void *opaque,uint64_t address,uint32_t bytes)
{
    (void)opaque;if(!bytes || address>UINT64_MAX-bytes)return false;
    while(bytes){
        u64 page=address&~0xfffUL,pa=nvme_guest_page_pa(page);
        u32 count=0x1000-(address&0xfff);if(count>bytes)count=bytes;
        if(!pa || nwoas_ipa_to_pa(page)!=pa || !nwoas_in_dram(pa,0x1000))return false;
        address+=count;bytes-=count;
    }
    return true;
}
static bool s229_read(void *opaque,uint64_t address,uint8_t *out,uint32_t bytes)
{
    if(!s229_contains(opaque,address,bytes))return false;
    while(bytes){u32 n=0x1000-(address&0xfff);if(n>bytes)n=bytes;
        u64 pa=nvme_guest_page_pa(address&~0xfffUL)+(address&0xfff);
        memcpy(out,(void *)pa,n);address+=n;out+=n;bytes-=n;
    }
    s229_admin_fetches++;return true;
}
static bool s229_write(void *opaque,uint64_t address,const uint8_t *in,uint32_t bytes)
{
    if(!s229_contains(opaque,address,bytes))return false;
    if(bytes==2 && address==s229_front.ring.cq_base+(u64)s229_front.ring.cq_tail*16+14){
        u64 pa=nvme_guest_page_pa(address&~0xfffUL)+(address&0xfff);
        *(volatile u16 *)pa=(u16)in[0]|(u16)in[1]<<8;
        s229_admin_completions++;return true;
    }
    while(bytes){u32 n=0x1000-(address&0xfff);if(n>bytes)n=bytes;
        u64 pa=nvme_guest_page_pa(address&~0xfffUL)+(address&0xfff);
        memcpy((void *)pa,in,n);address+=n;in+=n;bytes-=n;
    }
    return true;
}
static void s229_barrier(void *opaque){(void)opaque;dma_wmb();}
static void s229_irq(void *opaque,bool level)
{
    (void)opaque;
    nwoas_nvme_fp.mask=s229_front.control.mask;
    nwoas_nvme_fp.irq_enabled=s229_front.ring.admin.cq.ien && !(s229_front.control.command&0x400);
    nwoas_nvme_fp.cache_enabled=s229_cache;
    nwoas_nvme_fp.local_mask_allowed=false;
    if((s229_front.control.csts&2) || s229_front.backend_fault){
        nwoas_nvme_fp.armed=false;nwoas_nvme_fp.deferred=false;level=false;
    }
    nwoas_nvme_set_fast_irq(level);
}
static bool s229_reset_io(void *opaque)
{
    (void)opaque;
    if(nwoas_nvme_fp.link_busy || nwoas_nvme_fp.faulted)return false;
    s229_control_local(0,0,0,0,0,0);
    nwoas_nvme_fp.sq_base=nwoas_nvme_fp.cq_base=0;
    nwoas_nvme_fp.sq_size=nwoas_nvme_fp.cq_size=0;
    nwoas_nvme_fp.sq_head=nwoas_nvme_fp.sq_tail=0;
    nwoas_nvme_fp.cq_head=nwoas_nvme_fp.cq_tail=nwoas_nvme_fp.cq_pending=0;
    nwoas_nvme_fp.cq_phase=1;return true;
}
static bool s229_flush(void *opaque)
{
    (void)opaque;
    if(nwoas_nvme_fp.link_busy || nwoas_nvme_fp.faulted || !nvme_flush(1)){
        nwoas_nvme_fp.faulted=true;nwoas_nvme_fp.errors++;return false;
    }
    return true;
}
static int s229_set_cache(void *opaque,bool value)
{if(s229_cache && !value && !s229_flush(opaque))return -1;s229_cache=value;return 1;}
static uint32_t s229_get_cache(void *opaque){(void)opaque;return s229_cache;}
static bool s229_reconcile(void *opaque,const struct nwoas_admin_state *state,uint8_t changed)
{
    (void)opaque;
    if(nwoas_nvme_fp.link_busy || nwoas_nvme_fp.faulted)return false;
    bool sq_changed=s229_sq_generation!=state->sq_generation;
    bool cq_changed=s229_cq_generation!=state->cq_generation;
    if(sq_changed || cq_changed)s229_control_local(0,0,0,0,0,0);
    /* Full generation comparison precedes the 12-bit encoding. Clearing cursor
       state explicitly prevents equality after truncation from reusing a ring. */
    if(sq_changed){nwoas_nvme_fp.sq_head=nwoas_nvme_fp.sq_tail=0;}
    if(cq_changed){nwoas_nvme_fp.cq_head=nwoas_nvme_fp.cq_tail=nwoas_nvme_fp.cq_pending=0;nwoas_nvme_fp.cq_phase=1;}
    s229_sq_generation=state->sq_generation;s229_cq_generation=state->cq_generation;
    nwoas_nvme_fp.sq_base=state->sq.base;nwoas_nvme_fp.sq_size=state->sq.depth;
    nwoas_nvme_fp.cq_base=state->cq.base;nwoas_nvme_fp.cq_size=state->cq.depth;
    nwoas_nvme_fp.sq_generation=state->sq_generation&0xfff;
    nwoas_nvme_fp.cq_generation=state->cq_generation&0xfff;
    if(state->sq.present && state->cq.present && !((s229_front.control.cc>>14)&3)){
        u64 flags=((u64)s229_front.control.mask<<32)|((state->cq_generation&0xfff)<<20)|
            ((state->sq_generation&0xfff)<<8)|(state->cq.ien && !(s229_front.control.command&0x400)?2:0)|
            (s229_cache?4:0)|1;
        if(!nwoas_nvme_fp.armed && !s229_control_local(1,state->sq.base,state->sq.depth,state->cq.base,state->cq.depth,flags))return false;
    }
    (void)changed;return true;
}
static bool s229_enable(u64 link_lbas,u64 mdts)
{
    if(s229_enabled || nwoas_nvme_fp.armed || nwoas_nvme_fp.faulted || mdts>8 || !link_lbas)return false;
    struct nwoas_frontend_ops ops={0,s229_contains,s229_read,s229_write,s229_barrier,s229_reconcile,
        s229_reset_io,s229_flush,s229_set_cache,s229_get_cache,s229_irq};
    struct nwoas_identity_policy policy={NWOAS_NVME_NAMESPACE_LBAS,link_lbas,(uint8_t)mdts,true,false,true};
    s229_cache=true;
    if(!nwoas_frontend_init(&s229_front,NWOAS_NVME_BAR,ops,policy))return false;
    s229_enabled=true;nwoas_nvme_set_irq(0);
    printf("NWOAS-S229 local control/admin owner enabled\n");return true;
}
static bool s229_fast_irq_update(void)
{
    if(!s229_enabled)return false;
    if(s229_owner_callback)return true;
    if(nwoas_nvme_fp.faulted)nwoas_frontend_fault(&s229_front);
    nwoas_frontend_io_pending(&s229_front,nwoas_nvme_fp.cq_pending!=0);
    return true;
}
static void s229_poll(void)
{if(s229_enabled)nwoas_frontend_poll(&s229_front,1);}
static bool s229_cq_ack_unarmed(u64 ipa)
{return s229_enabled && s229_front.ring.admin.cq.present && ipa==NWOAS_NVME_BAR+0x100c;}
static bool s229_mmio(u64 ipa,u64 *value,bool write,u64 width)
{
    if(!s229_enabled)return false;
    bool pci=ipa>=0x700000000UL && ipa<0x700100000UL;
    if(!pci && (ipa<NWOAS_NVME_BAR || ipa>=NWOAS_NVME_BAR+NWOAS_NVME_BAR_SIZE))return false;
    if(width>3){nwoas_frontend_fault(&s229_front);if(!write)*value=0;return true;}
    u32 offset=ipa-(pci?0x700000000UL:NWOAS_NVME_BAR);unsigned bits=8u<<width;
    if(write)s229_reg_writes++;else s229_reg_reads++;
    if(pci && offset>=4096){if(!write)*value=width==3?UINT64_MAX:((1UL<<bits)-1);return true;}
    if(!pci && write && width==2 && (offset==0x1008 || offset==0x100c)){
        if(!(s229_front.control.csts&1) || (s229_front.control.csts&2) || s229_front.backend_fault)return true;
        if(offset==0x1008){
            if((s229_front.control.csts&12) || ((s229_front.control.cc>>14)&3))return true;
            if(!s229_front.ring.admin.sq.present || *value>=nwoas_nvme_fp.sq_size){nwoas_frontend_fault(&s229_front);return true;}
        }else if(!s229_front.ring.admin.cq.present || *value>=nwoas_nvme_fp.cq_size ||
                 (*value+nwoas_nvme_fp.cq_size-nwoas_nvme_fp.cq_head)%nwoas_nvme_fp.cq_size>nwoas_nvme_fp.cq_pending){
            nwoas_frontend_fault(&s229_front);return true;
        }
        return false; /* Existing S149 data/CQ path, including NS2 transport. */
    }
    if(!pci && write && offset==0x14 && (!(*value&1) || ((*value>>14)&3)))
        s229_control_local(0,0,0,0,0,0);
    if(write)nwoas_frontend_write(&s229_front,pci,offset,bits,*value);
    else nwoas_frontend_read(&s229_front,pci,offset,bits,value);
    return true;
}
