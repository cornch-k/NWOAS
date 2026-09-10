#include "admin_state.h"
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t get64(const uint8_t *p){return get32(p)|((uint64_t)get32(p+4)<<32);}
static void clear_queue(struct nwoas_io_queue *q)
{q->base=0;q->depth=0;q->cqid=0;q->present=false;q->ien=false;}
void nwoas_admin_reset(struct nwoas_admin_state *s,bool full)
{
    if(s->sq.present)s->sq_generation++;
    if(s->cq.present)s->cq_generation++;
    clear_queue(&s->sq);clear_queue(&s->cq);s->aer_present=false;s->aer_cid=0;
    if(full){for(unsigned i=0;i<12;i++)s->features[i]=0;s->features[6]=1;}
}
void nwoas_admin_init(struct nwoas_admin_state *s)
{
    clear_queue(&s->sq);clear_queue(&s->cq);s->sq_generation=s->cq_generation=0;
    nwoas_admin_reset(s,true);
}
static bool queue_valid(const struct nwoas_admin_callbacks *cb,uint64_t base,
                        uint32_t depth,uint32_t stride)
{
    if(depth<2 || depth>256 || !base || (base&4095) || !cb->contains)return false;
    uint32_t bytes=depth*stride;
    return base<=UINT64_MAX-bytes && cb->contains(cb->opaque,base,bytes);
}
int nwoas_admin_execute(struct nwoas_admin_state *s,const struct nwoas_admin_callbacks *cb,
    const struct nwoas_identity_policy *policy,const uint8_t *cmd,size_t n,
    uint8_t *out,size_t cap,struct nwoas_admin_result *r)
{
    if(!r)return -1;
    r->result=0;r->bytes=0;r->status=2;r->changed=0;r->deferred=false;
    if(!s || !cb || !cmd || n!=64 || !policy || !out || cap<4096 ||
       (!!cb->set_cache != !!cb->get_cache))return -1;
    struct nwoas_admin_payload_result payload;
    int handled=nwoas_admin_payload(cmd,n,policy,out,cap,&payload);
    if(handled<0)return -1;
    if(cmd[1] || get64(cmd+16))return 1;
    if(handled){r->status=payload.status;r->bytes=payload.bytes;return 1;}
    uint32_t ns=get32(cmd+4),dw[6];for(unsigned i=0;i<6;i++)dw[i]=get32(cmd+40+4*i);
    unsigned op=cmd[0];
    if(op==1 || op==5){
        uint32_t qid=dw[0]&65535,depth=(dw[0]>>16)+1;uint64_t base=get64(cmd+24);
        if(ns || qid!=1 || !(dw[1]&1) || !queue_valid(cb,base,depth,op==5?16:64))return 1;
        if(op==5){
            if(s->cq.present || (dw[1]>>16))return 1;
            s->cq=(struct nwoas_io_queue){base,(uint16_t)depth,0,true,!!(dw[1]&2)};
            s->cq_generation++;r->changed=2;
        }else{
            if(s->sq.present || (dw[1]>>16)!=1 || !s->cq.present)return 1;
            s->sq=(struct nwoas_io_queue){base,(uint16_t)depth,1,true,false};
            s->sq_generation++;r->changed=1;
        }
        r->status=0;return 1;
    }
    if(op==0 || op==4){
        if((dw[0]&65535)!=1)return 1;
        struct nwoas_io_queue *q=op==0?&s->sq:&s->cq;
        if(!q->present)return 1;
        if(op==4 && s->sq.present){r->status=0x10c;return 1;}
        clear_queue(q);if(op==0){s->sq_generation++;r->changed=1;}else{s->cq_generation++;r->changed=2;}
        r->status=0;return 1;
    }
    if(op==9 || op==10){
        unsigned fid=dw[0]&255;
        if(fid==7){r->status=0;return 1;} /* MAX_Q=1 => zero-based result 0 */
        if(fid!=1 && fid!=2 && fid!=4 && fid!=5 && fid!=6 && fid!=8 && fid!=9 && fid!=10 && fid!=11)return 1;
        if(fid==6 && cb->set_cache){
            if((dw[0]&~0x7ffu) || dw[2] || dw[3] || dw[4] || dw[5])return 1;
            unsigned sel=(dw[0]>>8)&7;
            if(op==10){
                if(sel==1){r->status=0;r->result=1;return 1;}
                if(sel==3){r->status=0;r->result=4;return 1;}
                if(sel)return 1;
            }
            if(op==9 && (sel || (dw[1]&~1u)))return 1;
            if(op==9){
                if(!cb->set_cache(cb->opaque,!!dw[1])){r->status=6;return 1;}
                r->changed=4; /* owner must reconcile target cache policy */
            }
            r->status=0;r->result=cb->get_cache(cb->opaque);return 1;
        }
        if(op==9)s->features[fid]=dw[1];r->status=0;r->result=s->features[fid];return 1;
    }
    if(op==12){
        if(s->aer_present){r->status=0x105;return 1;}
        s->aer_present=true;s->aer_cid=(uint16_t)cmd[2]|((uint16_t)cmd[3]<<8);
        r->status=0;r->deferred=true;return 1;
    }
    if(op==8){r->status=0;r->result=1;return 1;}
    r->status=1;return 1;
}
