#include "model.h"
static bool width_ok(unsigned w) { return w==8 || w==16 || w==32 || w==64; }
static uint64_t width_mask(unsigned w) { return w==64 ? UINT64_MAX : (UINT64_C(1)<<w)-1; }
static bool fits(uint32_t off, unsigned w, uint32_t size)
{ return width_ok(w) && off<=size && w/8<=size-off; }
static void irq_update(struct nwoas_control *c)
{ if(c->cb.irq) c->cb.irq(c->cb.opaque,c->pending && !(c->mask&1) && !(c->command&0x400)); }
static void queues_reset(struct nwoas_control *c)
{ c->pending=false; if(c->cb.reset)c->cb.reset(c->cb.opaque); }
void nwoas_control_reset(struct nwoas_control *c)
{
    c->cc=c->csts=c->aqa=c->mask=0; c->asq=c->acq=0;
    queues_reset(c); irq_update(c);
}
void nwoas_control_init(struct nwoas_control *c,uint64_t bar,struct nwoas_callbacks cb)
{
    c->cb=cb; c->bar=bar; c->command=0;
    c->probe_low=c->probe_high=false; nwoas_control_reset(c);
}
void nwoas_control_pending(struct nwoas_control *c,bool pending)
{ c->pending=pending; irq_update(c); }
static uint8_t byte_of(uint64_t v,unsigned byte) { return (uint8_t)(v>>(8*byte)); }
static uint8_t pci_byte(const struct nwoas_control *c,uint32_t o)
{
    if(o<4) return byte_of(UINT64_C(0x00101234),o);
    if(o<6) return byte_of(c->command,o-4);
    if(o<8) return byte_of(c->pending?8:0,o-6);
    if(o<12) return byte_of(UINT64_C(0x01080201),o-8);
    if(o>=0x10 && o<0x14)
        return byte_of(c->probe_low?UINT64_C(0xffffc004):(c->bar&UINT32_MAX)|4,o-0x10);
    if(o>=0x14 && o<0x18) return byte_of(c->probe_high?UINT32_MAX:c->bar>>32,o-0x14);
    if(o>=0x2c && o<0x30) return byte_of(UINT64_C(0x00101234),o-0x2c);
    return o==0x3d?1:0;
}
enum nwoas_access nwoas_pci_read(const struct nwoas_control *c,uint32_t o,unsigned w,uint64_t *v)
{
    if(!v || !width_ok(w))return NWOAS_INVALID;
    *v=width_mask(w); if(!fits(o,w,4096))return NWOAS_INVALID;
    *v=0; for(unsigned i=0;i<w/8;i++)*v|=(uint64_t)pci_byte(c,o+i)<<(8*i);
    return NWOAS_HANDLED;
}
enum nwoas_access nwoas_pci_write(struct nwoas_control *c,uint32_t o,unsigned w,uint64_t v)
{
    if(!fits(o,w,4096))return NWOAS_INVALID;
    v&=width_mask(w);
    /* Preserve Python's overlap predicate, including ignored offset-5 writes. */
    if(o<=4 && 4-o<w/8){
        for(unsigned i=0;i<w/8;i++)if(o+i>=4 && o+i<6){
            unsigned shift=8*(o+i-4);
            c->command=(uint16_t)((c->command&~(0xffu<<shift))|((unsigned)byte_of(v,i)<<shift));
        }
        c->command&=0x407; irq_update(c);
    }else if(w==32 && (o==0x10 || o==0x14)){
        if(o==0x10)c->probe_low=v==UINT32_MAX; else c->probe_high=v==UINT32_MAX;
    }
    return NWOAS_HANDLED;
}
static uint8_t reg_byte(const struct nwoas_control *c,uint32_t o)
{
    if(o<8)return byte_of(UINT64_C(255)|(UINT64_C(1)<<16)|(UINT64_C(20)<<24)|(UINT64_C(1)<<37),o);
    if(o<12)return byte_of(0x10300,o-8);
    if(o<16)return byte_of(c->mask,o-12);
    if(o<20)return byte_of(c->mask,o-16);
    if(o<24)return byte_of(c->cc,o-20);
    if(o>=0x1c && o<0x20)return byte_of(c->csts,o-0x1c);
    if(o>=0x24 && o<0x28)return byte_of(c->aqa,o-0x24);
    if(o>=0x28 && o<0x30)return byte_of(c->asq,o-0x28);
    if(o>=0x30 && o<0x38)return byte_of(c->acq,o-0x30);
    return 0;
}
enum nwoas_access nwoas_reg_read(const struct nwoas_control *c,uint32_t o,unsigned w,uint64_t *v)
{
    if(!v)return NWOAS_INVALID;
    *v=0; if(!fits(o,w,0x4000))return NWOAS_INVALID;
    for(unsigned i=0;i<w/8;i++)*v|=(uint64_t)reg_byte(c,o+i)<<(8*i);
    return NWOAS_HANDLED;
}
static void fatal(struct nwoas_control *c)
{ c->csts|=2; if(c->cb.irq)c->cb.irq(c->cb.opaque,false); }
static bool queue_valid(struct nwoas_control *c,uint64_t base,uint16_t depth,uint32_t stride)
{
    uint32_t bytes=(uint32_t)depth*stride;
    return depth>=2 && depth<=256 && base && !(base&4095) &&
           base<=UINT64_MAX-bytes && c->cb.contains && c->cb.contains(c->cb.opaque,base,bytes);
}
enum nwoas_access nwoas_reg_write(struct nwoas_control *c,uint32_t o,unsigned w,uint64_t v)
{
    if(!fits(o,w,0x4000) || (w!=32 && w!=64) || (o&3))return NWOAS_INVALID;
    if(o>=0x1000)return NWOAS_NOT_HANDLED; /* Owner handles SQ/CQ doorbells. */
    /* Defined dword registers reject nonzero upper halves instead of storing
       Python integers which later make struct.pack('<I') throw. */
    if(w==64 && o!=0x28 && o!=0x30 && (v>>32))return NWOAS_INVALID;
    if(w==64 && (o==0x2c || o==0x34))return NWOAS_INVALID;
    v&=width_mask(w);
    if(o==0xc){c->mask|=(uint32_t)v;irq_update(c);}
    else if(o==0x10){c->mask&=~(uint32_t)v;irq_update(c);}
    else if(o==0x14){
        uint32_t old=c->cc; c->cc=(uint32_t)v;
        if(!(v&1)){queues_reset(c);c->csts=0;irq_update(c);}
        else if(!(old&1)){
            uint16_t sq=(uint16_t)((c->aqa&0xfff)+1),cq=(uint16_t)(((c->aqa>>16)&0xfff)+1);
            if(((v>>7)&15) || ((v>>4)&7) || ((v>>16)&15)!=6 || ((v>>20)&15)!=4 ||
               !queue_valid(c,c->asq,sq,64) || !queue_valid(c,c->acq,cq,16) ||
               !c->cb.setup || !c->cb.setup(c->cb.opaque,c->asq,sq,c->acq,cq)){
                fatal(c);return NWOAS_HANDLED;
            }
            c->csts=1;
        }
        if(((v>>14)&3) && !(c->csts&2) && (c->csts&12)!=8){
            c->csts=(c->csts&~12u)|4;
            if(!c->cb.flush || !c->cb.flush(c->cb.opaque)){fatal(c);return NWOAS_HANDLED;}
            c->csts=(c->csts&~12u)|8;
        }
    }else if(o==0x24 && !(c->cc&1))c->aqa=(uint32_t)v;
    else if((o==0x28 || o==0x2c || o==0x30 || o==0x34) && !(c->cc&1)){
        uint64_t *base=o<0x30?&c->asq:&c->acq;
        if(w==64)*base=v;
        else {unsigned shift=(o&4)?32:0;*base=(*base&~(UINT64_C(0xffffffff)<<shift))|(v<<shift);}
    }
    return NWOAS_HANDLED;
}
