#include "admin_ring.h"
static uint16_t get16(const uint8_t *p){return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static bool range(struct nwoas_admin_ring *r,uint64_t a,uint32_t n)
{return a && n && a<=UINT64_MAX-n && r->cb.contains(r->cb.opaque,a,n);}
static int fail(struct nwoas_admin_ring *r)
{r->fatal=true;r->cb.pending(r->cb.opaque,false);r->cb.fault(r->cb.opaque);return -2;}
bool nwoas_ring_init(struct nwoas_admin_ring *r,struct nwoas_ring_callbacks cb,
                     struct nwoas_admin_callbacks ac,struct nwoas_identity_policy p)
{
    if(!r || !cb.contains || !cb.read || !cb.write || !cb.barrier || !cb.reconcile ||
       !cb.pending || !cb.fault || (!ac.set_cache != !ac.get_cache) ||
       !p.primary_lbas || p.primary_lbas>=(UINT64_C(1)<<40) || p.mdts>8 ||
       (p.paired && (!p.link_lbas || p.link_lbas>=(UINT64_C(1)<<40))))return false;
    r->cb=cb;r->admin_cb=ac;r->policy=p;nwoas_admin_init(&r->admin);
    r->sq_base=r->cq_base=r->lifecycle=0;
    r->sq_depth=r->cq_depth=r->sq_head=r->sq_tail=r->cq_head=r->cq_tail=r->pending=0;
    r->phase=1;r->active=r->fatal=false;return true;
}
void nwoas_ring_reset(struct nwoas_admin_ring *r,bool full_reset)
{
    r->active=false;r->fatal=false;r->lifecycle++;
    r->sq_base=r->cq_base=0;r->sq_depth=r->cq_depth=0;
    r->sq_head=r->sq_tail=r->cq_head=r->cq_tail=r->pending=0;r->phase=1;
    nwoas_admin_reset(&r->admin,full_reset);r->cb.pending(r->cb.opaque,false);
    /* Reset reconciliation/cancellation belongs to the enclosing control owner;
       no old-ring memory is accessed here. */
}
bool nwoas_ring_configure(struct nwoas_admin_ring *r,uint64_t sq,uint16_t ns,
                           uint64_t cq,uint16_t nc)
{
    if(r->active || r->fatal || ns<2 || ns>256 || nc<2 || nc>256 ||
       (sq&4095) || (cq&4095) || !range(r,sq,(uint32_t)ns*64) ||
       !range(r,cq,(uint32_t)nc*16))return false;
    r->sq_base=sq;r->cq_base=cq;r->sq_depth=ns;r->cq_depth=nc;
    r->sq_head=r->sq_tail=r->cq_head=r->cq_tail=r->pending=0;r->phase=1;
    r->lifecycle++;r->active=true;r->cb.pending(r->cb.opaque,false);return true;
}
struct span{uint64_t address;uint32_t bytes;};
static unsigned resolve(struct nwoas_admin_ring *r,uint32_t n,struct span spans[2])
{
    uint64_t a=get64(r->command+24),b=get64(r->command+32);
    if(!n || n>4096 || (a&3))return 0;
    uint32_t first=4096-(uint32_t)(a&4095);if(first>n)first=n;
    if(!range(r,a,first))return 0;
    spans[0]=(struct span){a,first};
    if(first==n)return 1;
    if((b&4095) || !range(r,b,n-first))return 0;
    spans[1]=(struct span){b,n-first};return 2;
}
int nwoas_ring_poll(struct nwoas_admin_ring *r,unsigned budget)
{
    if(!budget || budget>256)return -1;
    if(!r->active || r->fatal)return 0;
    int consumed=0;
    while((unsigned)consumed<budget && r->sq_head!=r->sq_tail && r->pending<r->cq_depth-1){
        uint64_t address=r->sq_base+(uint64_t)r->sq_head*64;
        if(!range(r,address,64) || !r->cb.read(r->cb.opaque,address,r->command,64))return fail(r);
        uint16_t cid=get16(r->command+2);struct nwoas_admin_result result;
        if(nwoas_admin_execute(&r->admin,&r->admin_cb,&r->policy,r->command,64,
                               r->payload,sizeof(r->payload),&result)!=1)return fail(r);
        r->sq_head=(uint16_t)((r->sq_head+1)%r->sq_depth);consumed++;
        if(result.changed && !r->cb.reconcile(r->cb.opaque,&r->admin,result.changed))return fail(r);
        if(result.deferred)continue;
        if(result.bytes){
            struct span spans[2];unsigned count=resolve(r,result.bytes,spans);
            if(!count){result.status=2;result.result=0;result.bytes=0;}
            else{uint32_t offset=0;for(unsigned i=0;i<count;i++){
                if(!r->cb.write(r->cb.opaque,spans[i].address,r->payload+offset,spans[i].bytes))return fail(r);
                offset+=spans[i].bytes;
            }}
        }
        put32(r->completion,result.result);put32(r->completion+4,0);
        put16(r->completion+8,r->sq_head);put16(r->completion+10,0);
        put16(r->completion+12,cid);put16(r->completion+14,(uint16_t)((result.status<<1)|r->phase));
        address=r->cq_base+(uint64_t)r->cq_tail*16;
        if(!range(r,address,16) || !r->cb.write(r->cb.opaque,address,r->completion,14))return fail(r);
        r->cb.barrier(r->cb.opaque);
        if(!r->cb.write(r->cb.opaque,address+14,r->completion+14,2))return fail(r);
        r->cq_tail=(uint16_t)((r->cq_tail+1)%r->cq_depth);r->pending++;
        if(!r->cq_tail)r->phase^=1;
        r->cb.pending(r->cb.opaque,true);
    }
    return consumed;
}
int nwoas_ring_tail(struct nwoas_admin_ring *r,uint32_t tail)
{
    if(!r->active || r->fatal)return 0;
    if(tail>=r->sq_depth)return fail(r);
    r->sq_tail=(uint16_t)tail;return nwoas_ring_poll(r,256);
}
int nwoas_ring_ack(struct nwoas_admin_ring *r,uint32_t head)
{
    if(!r->active || r->fatal)return 0;
    if(head>=r->cq_depth)return fail(r);
    unsigned count=(head+r->cq_depth-r->cq_head)%r->cq_depth;
    if(count>r->pending)return fail(r);
    r->cq_head=(uint16_t)head;r->pending=(uint16_t)(r->pending-count);
    r->cb.pending(r->cb.opaque,r->pending!=0);return 0;
}
