#include "frontend.h"
static bool contains(void *p,uint64_t a,uint32_t n)
{struct nwoas_frontend *f=p;return f->ops.contains(f->ops.opaque,a,n);}
static bool read_memory(void *p,uint64_t a,uint8_t *v,uint32_t n)
{struct nwoas_frontend *f=p;return !f->backend_fault && f->ops.read(f->ops.opaque,a,v,n);}
static bool write_memory(void *p,uint64_t a,const uint8_t *v,uint32_t n)
{struct nwoas_frontend *f=p;return !f->backend_fault && f->ops.write(f->ops.opaque,a,v,n);}
static void barrier(void *p){struct nwoas_frontend *f=p;f->ops.barrier(f->ops.opaque);}
static void irq(void *p,bool v)
{struct nwoas_frontend *f=p;f->ops.irq(f->ops.opaque,v && !(f->control.csts&2) && !f->backend_fault && !f->ring.fatal);}
static void update_pending(struct nwoas_frontend *f)
{if(f->control_busy)return;nwoas_control_pending(&f->control,f->io_pending || (f->ring.active && f->ring.pending));}
static void pending(void *p,bool v)
{struct nwoas_frontend *f=p;(void)v;update_pending(f);}
void nwoas_frontend_fault(struct nwoas_frontend *f)
{f->backend_fault=true;f->control.csts|=2;f->ops.irq(f->ops.opaque,false);}
static void fault(void *p){nwoas_frontend_fault(p);}
static bool reconcile(void *p,const struct nwoas_admin_state *s,uint8_t changed)
{
    struct nwoas_frontend *f=p;
    if(!f->ops.reconcile(f->ops.opaque,s,changed))return false;
    /* Every CQ descriptor transition creates an empty queue or removes it. */
    if(changed&2)f->io_pending=false;
    update_pending(f);return true;
}
static bool set_cache(void *p,bool v)
{struct nwoas_frontend *f=p;int result=f->ops.set_cache(f->ops.opaque,v);if(result<0)nwoas_frontend_fault(f);return result==1;}
static uint32_t get_cache(void *p)
{struct nwoas_frontend *f=p;return f->ops.get_cache(f->ops.opaque);}
static bool flush(void *p)
{struct nwoas_frontend *f=p;return f->ops.flush(f->ops.opaque);}
static void reset(void *p)
{
    struct nwoas_frontend *f=p;bool ok=f->ops.reset_io(f->ops.opaque);
    f->io_pending=false;f->backend_fault=!ok;
    nwoas_ring_reset(&f->ring,f->full_reset);
    /* The control model sets CSTS after this callback. The write/reset wrapper
       reapplies fatal state if cancellation failed. */
}
static bool setup(void *p,uint64_t sq,uint16_t ns,uint64_t cq,uint16_t nc)
{struct nwoas_frontend *f=p;return !f->backend_fault && nwoas_ring_configure(&f->ring,sq,ns,cq,nc);}
bool nwoas_frontend_init(struct nwoas_frontend *f,uint64_t bar,
                         struct nwoas_frontend_ops ops,struct nwoas_identity_policy policy)
{
    if(!f || !ops.contains || !ops.read || !ops.write || !ops.barrier ||
       !ops.reconcile || !ops.reset_io || !ops.flush || !ops.irq ||
       (!ops.set_cache != !ops.get_cache) || !bar || (bar&0x3fff) ||
       !policy.primary_lbas || policy.primary_lbas>=(UINT64_C(1)<<40) || policy.mdts>8 ||
       (policy.paired && (!policy.link_lbas || policy.link_lbas>=(UINT64_C(1)<<40))))return false;
    f->ops=ops;f->io_pending=f->backend_fault=f->full_reset=f->control_busy=false;
    struct nwoas_ring_callbacks rc={f,contains,read_memory,write_memory,barrier,reconcile,pending,fault};
    struct nwoas_admin_callbacks ac={f,contains,ops.set_cache?set_cache:0,ops.get_cache?get_cache:0};
    if(!nwoas_ring_init(&f->ring,rc,ac,policy))return false;
    struct nwoas_callbacks cc={f,contains,setup,reset,flush,irq};
    f->control_busy=true;nwoas_control_init(&f->control,bar,cc);f->control_busy=false;update_pending(f);
    if(f->backend_fault)nwoas_frontend_fault(f);
    return !f->backend_fault;
}
void nwoas_frontend_reset(struct nwoas_frontend *f)
{
    f->full_reset=true;f->control_busy=true;nwoas_control_reset(&f->control);f->control_busy=false;f->full_reset=false;update_pending(f);
    if(f->backend_fault)nwoas_frontend_fault(f);
}
void nwoas_frontend_io_pending(struct nwoas_frontend *f,bool v)
{
    /* Caller reports interrupt-enabled pending, but never resurrect absent CQ. */
    f->io_pending=v && f->ring.admin.cq.present && f->ring.admin.cq.ien;
    update_pending(f);
}
enum nwoas_access nwoas_frontend_read(struct nwoas_frontend *f,bool pci,uint32_t o,unsigned w,uint64_t *v)
{return pci?nwoas_pci_read(&f->control,o,w,v):nwoas_reg_read(&f->control,o,w,v);}
enum nwoas_access nwoas_frontend_write(struct nwoas_frontend *f,bool pci,uint32_t o,unsigned w,uint64_t v)
{
    enum nwoas_access result;
    if(pci){f->control_busy=true;result=nwoas_pci_write(&f->control,o,w,v);f->control_busy=false;update_pending(f);}
    else{
        f->control_busy=true;result=nwoas_reg_write(&f->control,o,w,v);f->control_busy=false;update_pending(f);
        if(result==NWOAS_NOT_HANDLED){
            if(w!=32 || o>=0x1010)return NWOAS_HANDLED;
            if(!(f->control.csts&1) || (f->control.csts&2) || f->backend_fault)return NWOAS_HANDLED;
            if(o==0x1000){nwoas_ring_tail(&f->ring,(uint32_t)v);result=NWOAS_HANDLED;}
            else if(o==0x1004){nwoas_ring_ack(&f->ring,(uint32_t)v);result=NWOAS_HANDLED;}
            /* SQ1/CQ1 operations belong to the separate I/O owner. */
        }
    }
    if(f->backend_fault)nwoas_frontend_fault(f);
    return result;
}
int nwoas_frontend_poll(struct nwoas_frontend *f,unsigned budget)
{
    if(!(f->control.csts&1) || (f->control.csts&2) || f->backend_fault)return 0;
    return nwoas_ring_poll(&f->ring,budget);
}
