#include "admin_ring.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define BASE 0x10000u
#define SIZE 0x90000u
static uint8_t memory[SIZE];
static struct nwoas_admin_ring r;
static unsigned reads,writes,barriers,faults,reconciles,failread,failwrite;
static bool irq,recfail,barrier_seen,deny_cq,deny_second,cache=true,cachefail;
static uint8_t lastchanged;
static unsigned tested;
static bool contains(void *p,uint64_t a,uint32_t n)
{(void)p;if((deny_cq && a>=0x20000 && a<0x21000)||(deny_second && a==0x50000))return false;return a>=BASE && a<=BASE+SIZE && n && n<=BASE+SIZE-a;}
static bool read_cb(void *p,uint64_t a,uint8_t *b,uint32_t n)
{reads++;if(reads==failread || !contains(p,a,n))return false;memcpy(b,memory+a-BASE,n);return true;}
static bool write_cb(void *p,uint64_t a,const uint8_t *b,uint32_t n)
{
 writes++;if(writes==failwrite || !contains(p,a,n))return false;
 if(a>=0x20000 && a<0x21000){
  if((a&15)==14){assert(n==2 && barrier_seen);barrier_seen=false;}
  else{assert((a&15)==0 && n==14);assert(!barrier_seen);}
 }
 memcpy(memory+a-BASE,b,n);return true;
}
static void barrier(void *p){(void)p;assert(!barrier_seen);barrier_seen=true;barriers++;}
static bool reconcile(void *p,const struct nwoas_admin_state *s,uint8_t changed)
{(void)p;(void)s;reconciles++;lastchanged=changed;return !recfail;}
static void pending(void *p,bool v){(void)p;irq=v;}
static void fault(void *p){(void)p;faults++;}
static bool setcache(void *p,bool v){(void)p;if(cachefail)return false;cache=v;return true;}
static uint32_t getcache(void *p){(void)p;return cache;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8));}
static struct nwoas_ring_callbacks callbacks(void)
{return (struct nwoas_ring_callbacks){0,contains,read_cb,write_cb,barrier,reconcile,pending,fault};}
static struct nwoas_admin_callbacks admincallbacks(void)
{return (struct nwoas_admin_callbacks){0,contains,setcache,getcache};}
static struct nwoas_identity_policy policy(void)
{return (struct nwoas_identity_policy){61279344,8448,8,true,false,true};}
static void setup(unsigned ns,unsigned nc)
{
 memset(memory,0,sizeof(memory));reads=writes=barriers=faults=reconciles=failread=failwrite=lastchanged=0;
 irq=recfail=barrier_seen=deny_cq=deny_second=cachefail=false;cache=true;
 assert(nwoas_ring_init(&r,callbacks(),admincallbacks(),policy()));
 assert(nwoas_ring_configure(&r,0x10000,(uint16_t)ns,0x20000,(uint16_t)nc));tested++;
}
static uint8_t *command(unsigned index,unsigned op)
{uint8_t *p=memory+index*64;memset(p,0,64);p[0]=(uint8_t)op;p[2]=0x34;p[3]=0x12;return p;}
static void identify(void)
{uint8_t *p=command(0,6);put32(p+40,1);put64(p+24,0x40ffc);put64(p+32,0x50000);}
int main(void)
{
 setup(8,8);struct nwoas_admin_ring saved=r;
 assert(!nwoas_ring_init(0,callbacks(),admincallbacks(),policy()));
 for(unsigned k=0;k<7;k++){
  struct nwoas_ring_callbacks cb=callbacks();
  switch(k){case 0:cb.contains=0;break;case 1:cb.read=0;break;case 2:cb.write=0;break;case 3:cb.barrier=0;break;case 4:cb.reconcile=0;break;case 5:cb.pending=0;break;default:cb.fault=0;}
  assert(!nwoas_ring_init(&r,cb,admincallbacks(),policy()));assert(!memcmp(&r,&saved,sizeof(r)));
 }
 assert(!nwoas_ring_configure(&r,0x30000,8,0x40000,8));assert(!memcmp(&r,&saved,sizeof(r)));
 nwoas_ring_reset(&r,false);saved=r;
 for(unsigned depth=0;depth<300;depth++)if(depth<2 || depth>256){assert(!nwoas_ring_configure(&r,0x10000,(uint16_t)depth,0x20000,8));assert(!memcmp(&r,&saved,sizeof(r)));}
 for(unsigned bit=0;bit<64;bit++){
  uint64_t a=UINT64_C(1)<<bit;
  if(a<BASE || a>=BASE+SIZE || (a&4095)){assert(!nwoas_ring_configure(&r,a,8,0x20000,8));assert(!memcmp(&r,&saved,sizeof(r)));}
 }
 assert(!nwoas_ring_configure(&r,UINT64_MAX-4095,256,0x20000,8));
 for(unsigned at=1;at<=4;at++){
  setup(8,8);identify();failwrite=at;assert(nwoas_ring_tail(&r,1)==-2);assert(r.fatal && faults==1 && !irq && r.pending==0);
  assert(memory[0x10000+14]==0 && memory[0x10000+15]==0);
  unsigned before=reads;assert(nwoas_ring_tail(&r,2)==0);assert(nwoas_ring_ack(&r,0)==0);assert(reads==before);
 }
 setup(8,8);identify();failread=1;assert(nwoas_ring_tail(&r,1)==-2);assert(!writes && !r.sq_head);
 setup(8,8);identify();deny_second=true;assert(nwoas_ring_tail(&r,1)==1);assert(writes==2 && r.pending==1 && memory[0x10000+14]==5);
 for(unsigned i=0;i<4096;i++)assert(memory[0x30000+i]==0);
 setup(8,8);identify();deny_cq=true;assert(nwoas_ring_tail(&r,1)==-2);assert(r.fatal && !r.pending);
 setup(8,8);uint8_t *cmd=command(0,5);put32(cmd+40,0x00070001);put32(cmd+44,3);put64(cmd+24,0x60000);recfail=true;
 assert(nwoas_ring_tail(&r,1)==-2 && reconciles==1 && lastchanged==2 && !writes);
 nwoas_ring_reset(&r,false);assert(!r.admin.cq.present && !r.admin.sq.present && !r.admin.aer_present);
 setup(8,8);cmd=command(0,9);put32(cmd+40,6);put32(cmd+44,0);cachefail=true;
 assert(nwoas_ring_tail(&r,1)==1 && !r.fatal && !reconciles && memory[0x10000+14]==13);
 setup(8,8);cmd=command(0,9);put32(cmd+40,6);put32(cmd+44,1);
 assert(nwoas_ring_tail(&r,1)==1 && reconciles==1 && lastchanged==4);
 setup(8,2);command(0,8);command(1,8);command(2,8);assert(nwoas_ring_tail(&r,3)==1 && reads==1);
 assert(nwoas_ring_ack(&r,1)==0 && reads==1);assert(nwoas_ring_poll(&r,1)==1 && reads==2);
 assert(nwoas_ring_ack(&r,0)==0 && reads==2);assert(nwoas_ring_poll(&r,1)==1 && reads==3);
 assert(barriers==3 && r.phase==0 && irq);
 setup(8,8);command(0,12);assert(nwoas_ring_tail(&r,1)==1 && r.sq_head==1 && !writes && !irq);
 nwoas_ring_reset(&r,false);assert(!r.admin.aer_present && !r.active && r.lifecycle==2);
 unsigned before=reads;assert(nwoas_ring_tail(&r,9)==0 && nwoas_ring_ack(&r,9)==0 && reads==before);
 assert(nwoas_ring_poll(&r,0)==-1 && nwoas_ring_poll(&r,257)==-1);
 setup(8,8);assert(nwoas_ring_tail(&r,8)==-2 && !reads && faults==1);
 setup(8,8);assert(nwoas_ring_ack(&r,1)==-2 && !reads && faults==1);
 setup(8,8);assert(nwoas_ring_ack(&r,8)==-2 && !reads && faults==1);
 /* Repeated valid queue wraps with deterministic malformed commands under sanitizers. */
 uint32_t random=227;setup(16,16);
 for(unsigned i=0;i<50000;i++){
  uint8_t *p=command(r.sq_tail,0);for(unsigned j=0;j<64;j++){random=random*1664525u+1013904223u;p[j]=(uint8_t)(random>>24);}
  unsigned tail=(r.sq_tail+1)%r.sq_depth;assert(nwoas_ring_tail(&r,tail)==1);assert(!r.fatal);assert(nwoas_ring_ack(&r,r.cq_tail)==0);
 }
 assert(barriers==50000 && reads==50000 && !faults);
 /* Biased legal dispatch traffic complements malformed-preamble fuzz. */
 for(unsigned i=0;i<10000;i++){
  unsigned ops[10]={5,1,0,4,9,10,6,2,12,8};unsigned op=ops[i%10];uint8_t *p=command(r.sq_tail,op);
  if(op==5 || op==1){put32(p+40,0x00070001);put32(p+44,op==5?3:0x10001);put64(p+24,op==5?0x60000:0x70000);}
  if(op==0 || op==4)put32(p+40,1);
  if(op==9 || op==10){put32(p+40,6);put32(p+44,(i/10)&1);}
  if(op==6 || op==2){put32(p+40,op==6?1:0x3ff0002);put64(p+24,0x40ffc);put64(p+32,0x50000);}
  assert(nwoas_ring_tail(&r,(r.sq_tail+1)%r.sq_depth)==1);assert(!r.fatal);assert(nwoas_ring_ack(&r,r.cq_tail)==0);
 }
 assert(reads==60000 && barriers==59999 && reconciles>=4000 && r.admin.aer_present && !faults);
 printf("PASS %u directed fixtures, 50000 malformed and 10000 dispatch-biased sanitized ring commands\n",tested);
}
