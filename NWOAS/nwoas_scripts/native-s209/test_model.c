#include "model.h"
#include <assert.h>
#include <stdio.h>
static unsigned resets,setups,flushes,irqs;
static bool irq_level,flush_ok=true,setup_ok=true;
static bool contains(void *p,uint64_t a,uint32_t n){(void)p;return a>=0x10000 && a<=0x100000 && n<=0x100000-a;}
static bool setup(void *p,uint64_t a,uint16_t n,uint64_t b,uint16_t m)
{(void)p;assert(a==0x10000 && b==0x20000 && n==256 && m==256);setups++;return setup_ok;}
static void reset(void *p){(void)p;resets++;}
static bool flush(void *p){(void)p;flushes++;return flush_ok;}
static void irq(void *p,bool a){(void)p;irqs++;irq_level=a;}
static void configure(struct nwoas_control *c)
{
 assert(nwoas_reg_write(c,0x24,32,0xff00ff)==NWOAS_HANDLED);
 assert(nwoas_reg_write(c,0x28,64,0x10000)==NWOAS_HANDLED);
 assert(nwoas_reg_write(c,0x30,64,0x20000)==NWOAS_HANDLED);
 assert(nwoas_reg_write(c,0x14,32,0x460001)==NWOAS_HANDLED);
}
int main(void)
{
 struct nwoas_callbacks cb={0,contains,setup,reset,flush,irq};
 struct nwoas_control c;uint64_t v;
 nwoas_control_init(&c,UINT64_C(0x700100000),cb);
 assert(resets==1 && irqs==1 && !irq_level);
 configure(&c);assert(c.csts==1 && setups==1);
 nwoas_control_pending(&c,true);assert(irq_level);
 assert(nwoas_reg_write(&c,0xc,32,1)==NWOAS_HANDLED && !irq_level);
 assert(nwoas_reg_write(&c,0x10,32,1)==NWOAS_HANDLED && irq_level);
 assert(nwoas_pci_write(&c,4,16,0x400)==NWOAS_HANDLED && !irq_level);
 nwoas_control_reset(&c);assert(c.command==0x400 && !c.csts && !c.pending);
 setup_ok=false;configure(&c);assert(c.csts==2 && !irq_level);
 setup_ok=true;nwoas_control_reset(&c);configure(&c);flush_ok=false;
 assert(nwoas_reg_write(&c,0x14,32,0x464001)==NWOAS_HANDLED);
 assert(c.csts==7 && flushes==1 && !irq_level);
 nwoas_control_reset(&c);flush_ok=true;configure(&c);
 assert(nwoas_reg_write(&c,0x14,32,0x464001)==NWOAS_HANDLED && c.csts==9);
 assert(nwoas_reg_write(&c,0x14,32,0x464001)==NWOAS_HANDLED && flushes==2);
 struct nwoas_control before=c;
 assert(nwoas_reg_write(&c,0x1000,32,1)==NWOAS_NOT_HANDLED);
 assert(c.cc==before.cc && c.csts==before.csts && c.asq==before.asq);
 assert(nwoas_reg_write(&c,0x2c,64,1)==NWOAS_INVALID);
 assert(nwoas_reg_write(&c,0xc,64,UINT64_C(1)<<32)==NWOAS_INVALID);
 assert(nwoas_pci_read(&c,4095,16,&v)==NWOAS_INVALID && v==0xffff);
 assert(nwoas_reg_read(&c,0x3fff,16,&v)==NWOAS_INVALID && !v);
 assert(nwoas_reg_read(&c,0,32,0)==NWOAS_INVALID);
 /* Width/range overflow inputs: sanitizer exercises every byte extraction. */
 const unsigned widths[]={0,1,7,8,16,24,32,64,65,128,0xffffffff};
 const uint32_t offsets[]={0,3,4,5,12,20,40,44,52,4095,0x1000,0x3fff,0x4000,0xffffffff};
 for(unsigned i=0;i<sizeof(widths)/sizeof(widths[0]);i++)
  for(unsigned j=0;j<sizeof(offsets)/sizeof(offsets[0]);j++){
   (void)nwoas_pci_read(&c,offsets[j],widths[i],&v);
   (void)nwoas_reg_read(&c,offsets[j],widths[i],&v);
   (void)nwoas_pci_write(&c,offsets[j],widths[i],UINT64_MAX);
   (void)nwoas_reg_write(&c,offsets[j],widths[i],UINT64_MAX);
  }
 /* Missing callbacks fail closed on enable/flush, never pretend a queue exists. */
 struct nwoas_callbacks absent={0};nwoas_control_init(&c,0,absent);configure(&c);assert(c.csts==2);
 puts("PASS: callback failures, queue enable/shutdown, mask/INTx, unsupported doorbells and malformed access matrix");
}
