/* Include fake-memory bridge only: no hardware. */
#include "test_bridge.c"
#include <assert.h>
#include <stdio.h>
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static void submit(unsigned op,uint32_t a,uint32_t b,uint64_t base)
{
 uint8_t cmd[64]={0};cmd[0]=(uint8_t)op;put32(cmd+40,a);put32(cmd+44,b);put64(cmd+24,base);
 assert(guest_write(r.sq_base+(uint64_t)r.sq_tail*64,cmd,64));assert(tail((r.sq_tail+1)%r.sq_depth)==NWOAS_HANDLED);
}
static void fresh(void){assert(initialize());assert(configure(0x10000,16,0x20000,16));}
int main(void)
{
 fresh();submit(5,0x000f0001,3,0x60000);assert(!r.fatal && r.admin.cq.present);ack(r.cq_tail);
 io_pending(1);assert(irq && f.control.pending);
 access_write(0,0xc,32,1);assert(!irq && f.control.pending);
 access_write(0,0x10,32,1);assert(irq);
 access_write(1,4,16,0x400);assert(!irq && (access_read(1,6,16)&8));
 access_write(1,4,16,7);assert(irq);
 submit(4,1,0,0);assert(!f.io_pending && !r.admin.cq.present && irq);ack(r.cq_tail);assert(!irq);
 io_pending(1);assert(!f.io_pending && !irq);
 submit(5,0x000f0001,1,0x60000);ack(r.cq_tail);io_pending(1);assert(!irq && !f.io_pending);
 fresh();submit(9,1,0xfeed,0);ack(r.cq_tail);assert(r.admin.features[1]==0xfeed);
 reset_state(0);assert(r.admin.features[1]==0xfeed && !r.active);
 assert(configure(0x10000,16,0x20000,16));reset_state(1);assert(!r.admin.features[1] && r.admin.features[6]==1 && !r.active);
 fresh();submit(8,0,0,0);reset_fail=true;reset_state(0);assert(f.backend_fault && f.control.csts==2 && !irq && !r.active);
 assert(!configure(0x10000,16,0x20000,16));assert(!irq);
 reset_fail=false;reset_state(0);assert(!f.backend_fault && !f.control.csts);assert(configure(0x10000,16,0x20000,16));
 fresh();cache_fail=true;access_write(0,0x14,32,0x464001);assert(f.control.csts&2);assert(!irq);
 fresh();access_write(0,0x14,32,0x464001);assert((f.control.csts&12)==8 && flushes==1);
 access_write(0,0x14,32,0x464001);assert(flushes==1);
 fresh();cache_fatal=true;submit(9,6,0,0);assert(f.backend_fault && r.fatal && (f.control.csts&2) && !writes && !irq);
 fresh();cache_fail=true;submit(9,6,0,0);assert(!f.backend_fault && !r.fatal && f.control.csts==1 && writes==2);
 fresh();fail_read=1;submit(8,0,0,0);assert((f.control.csts&2) && !writes && !irq);
 for(unsigned at=1;at<=2;at++){fresh();fail_write=at;submit(8,0,0,0);assert((f.control.csts&2) && !irq && !r.pending);}
 fresh();reconcile_fail=true;submit(5,0x000f0001,3,0x60000);assert((f.control.csts&2) && !irq && !writes);
 fresh();submit(5,0x000f0001,3,0x60000);ack(r.cq_tail);io_pending(1);force_fault();assert(!irq);
 access_write(1,4,16,0);access_write(0,0x10,32,0xffffffff);io_pending(1);assert(!irq);
 unsigned n=reads;tail(5);ack(5);assert(poll_ring(1)==0 && reads==n);
 reset_state(0);assert(!f.backend_fault && !f.control.csts && !f.io_pending);
 assert(access_write(0,0x1008,32,0)==NWOAS_HANDLED); /* disabled doorbell ignored */
 assert(configure(0x10000,16,0x20000,16));assert(access_write(0,0x1008,32,0)==NWOAS_NOT_HANDLED);
 assert(access_write(0,0x100c,32,0)==NWOAS_NOT_HANDLED);
 assert(access_write(0,0x1001,32,0)==NWOAS_INVALID);
 assert(access_write(0,0x2c,64,1)==NWOAS_INVALID);

 for(unsigned enabled=0;enabled<2;enabled++){
  if(!enabled)reset_state(0);else assert(configure(0x10000,16,0x20000,16));
  assert(access_write(0,0x1000,64,0)==NWOAS_HANDLED);
  assert(access_write(0,0x1008,64,0)==NWOAS_HANDLED);
  assert(access_write(0,0x1010,32,0)==NWOAS_HANDLED);
  assert(access_write(0,0x1ffc,32,0)==NWOAS_HANDLED);
 }
 fresh();reset_fail=true;reset_state(1);assert(f.backend_fault && f.control.csts==2 && !irq && !r.active);
 fresh();reset_fail=true;assert(!nwoas_frontend_init(&f,0x700100000,f.ops,r.policy));assert(f.backend_fault && f.control.csts==2 && !irq);
 fresh();for(unsigned i=0;i<3;i++){uint8_t c[64]={8};assert(guest_write(r.sq_base+i*64,c,64));}
 fail_write=3;tail(3);assert(r.sq_head==2 && r.cq_tail==1 && r.pending==1 && f.control.csts&2 && !irq);
 /* Malformed offsets/widths exercise the actual composed frontend under sanitizers. */
 uint32_t seed=228;for(unsigned i=0;i<50000;i++){
  seed=seed*1664525u+1013904223u;unsigned off=seed;uint64_t value=((uint64_t)seed<<32)|seed;
  unsigned widths[8]={0,8,16,32,64,128,255,UINT32_MAX};unsigned width=widths[i%8];uint64_t out;
  nwoas_frontend_read(&f,i&1,off,width,&out);nwoas_frontend_write(&f,i&1,off,width,value);
 }
 puts("PASS composed frontend: masks, aggregate IRQ, cache fatal/command errors, reset cancellation, shutdown, 50000 bounds cases");
}
