#include "model.h"
static struct nwoas_control c;
static uint64_t sq_base,cq_base;
static uint16_t sq_depth,cq_depth;
static bool admin_active;
static uint64_t mem_low=0x10000,mem_high=0x100000;
static int irq_last, irq_count, setup_count, reset_count, flush_count, fail_flush, fail_setup;
static bool contains(void *p,uint64_t a,uint32_t n)
{ (void)p; return a>=mem_low && a<=mem_high && n<=mem_high-a; }
static bool setup(void *p,uint64_t s,uint16_t sd,uint64_t q,uint16_t qd)
{ (void)p;setup_count++;if(fail_setup)return false;
 sq_base=s;sq_depth=sd;cq_base=q;cq_depth=qd;admin_active=true;return true; }
static void reset(void *p){(void)p;reset_count++;admin_active=false;}
static bool flush(void *p){(void)p;flush_count++;return !fail_flush;}
static void irq(void *p,bool active){(void)p;irq_last=active;irq_count++;}
void test_init(void)
{
 mem_low=0x10000;mem_high=0x100000;
 irq_last=irq_count=setup_count=reset_count=flush_count=fail_flush=fail_setup=0;
 struct nwoas_callbacks cb={0,contains,setup,reset,flush,irq};
 nwoas_control_init(&c,UINT64_C(0x700100000),cb);
}
void test_pending(int p){nwoas_control_pending(&c,p!=0);}
void test_reset(void){nwoas_control_reset(&c);}
void test_failure(int f,int s){fail_flush=f;fail_setup=s;}
int test_counter(int n)
{switch(n){case 0:return irq_last;case 1:return irq_count;case 2:return setup_count;case 3:return reset_count;case 4:return flush_count;default:return -1;}}
int test_write(int pci,uint32_t off,unsigned w,uint64_t v)
{return pci?nwoas_pci_write(&c,off,w,v):nwoas_reg_write(&c,off,w,v);}
uint64_t test_read(int pci,uint32_t off,unsigned w)
{uint64_t v=0; if(pci)nwoas_pci_read(&c,off,w,&v);else nwoas_reg_read(&c,off,w,&v);return v;}

void test_memory(uint64_t lo,uint64_t hi){mem_low=lo;mem_high=hi;}
uint64_t test_admin(unsigned n){switch(n){case 0:return admin_active;case 1:return sq_base;case 2:return sq_depth;case 3:return cq_base;case 4:return cq_depth;default:return 0;}}
