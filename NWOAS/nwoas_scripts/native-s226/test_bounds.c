#include "admin_state.h"
#include <assert.h>
#include <string.h>
static bool contains(void *p,uint64_t a,uint32_t n)
{(void)p;return a>=0x10000 && a<=0x100000 && n<=0x100000-a;}
static bool cache=true,fail_cache=false;
static unsigned set_calls,range_calls;
static bool counted_contains(void *p,uint64_t a,uint32_t n){range_calls++;return contains(p,a,n);}
static bool set_cache(void *p,bool v){(void)p;set_calls++;if(cache && !v && fail_cache)return false;cache=v;return true;}
static uint32_t get_cache(void *p){(void)p;return cache;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint32_t rnd(uint32_t *s){*s=*s*1664525u+1013904223u;return *s;}
int main(void){
 struct nwoas_admin_state s;struct nwoas_admin_callbacks cb={0,counted_contains,set_cache,get_cache};
 struct nwoas_identity_policy p={61279344,8448,8,true,false,true};struct nwoas_admin_result r;
 struct {uint8_t a[32],out[4096],b[32];} g;uint8_t cmd[64]={0};uint32_t seed=226;
 memset(&g,0xa5,sizeof(g));nwoas_admin_init(&s);
 assert(nwoas_admin_execute(0,&cb,&p,cmd,64,g.out,4096,&r)==-1);
 assert(nwoas_admin_execute(&s,0,&p,cmd,64,g.out,4096,&r)==-1);
 assert(nwoas_admin_execute(&s,&cb,&p,0,64,g.out,4096,&r)==-1);
 assert(nwoas_admin_execute(&s,&cb,&p,cmd,63,g.out,4096,&r)==-1);
 assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4095,&r)==-1);
 /* Directed sanitized lifecycle: the uniform malformed fuzz below cannot
  * be relied on to generate QID1, valid addresses or cache transitions. */
 for(unsigned k=0;k<100;k++){
  nwoas_admin_reset(&s,true);memset(cmd,0,64);cmd[0]=5;put32(cmd+40,0x00ff0001);put32(cmd+44,3);put32(cmd+24,0x30000);
  assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4096,&r)==1 && r.status==0 && r.changed==2);
  cmd[0]=1;put32(cmd+44,0x10001);put32(cmd+24,0x40000);
  assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4096,&r)==1 && r.status==0 && r.changed==1);
  cmd[0]=4;put32(cmd+40,1);
  assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4096,&r)==1 && r.status==0x10c);
  cmd[0]=0;assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4096,&r)==1 && !r.status);
  cmd[0]=4;assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4096,&r)==1 && !r.status);
  memset(cmd,0,64);cmd[0]=9;put32(cmd+40,6);put32(cmd+44,1);
  assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4096,&r)==1 && !r.status && r.changed==4);
  fail_cache=true;put32(cmd+44,0);
  assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4096,&r)==1 && r.status==6 && !r.changed && cache);
  fail_cache=false;assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4096,&r)==1 && !r.status && r.changed==4 && !cache);
  memset(cmd,0,64);cmd[0]=5;put32(cmd+40,0x00ff0001);put32(cmd+44,3);put32(cmd+24,0xfffff000);put32(cmd+28,0xffffffff);
  assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4096,&r)==1 && r.status==2);
 }
 assert(set_calls==300 && range_calls==200);
 struct nwoas_admin_callbacks absent=cb;absent.contains=0;
 memset(cmd,0,64);cmd[0]=5;put32(cmd+40,0x00ff0001);put32(cmd+44,3);put32(cmd+24,0x30000);
 assert(nwoas_admin_execute(&s,&absent,&p,cmd,64,g.out,4096,&r)==1 && r.status==2);
 cmd[0]=10;put32(cmd+40,7);
 assert(nwoas_admin_execute(&s,&absent,&p,cmd,64,g.out,4096,&r)==1 && !r.status);
 absent=cb;absent.set_cache=0;
 assert(nwoas_admin_execute(&s,&absent,&p,cmd,64,g.out,4096,&r)==-1);

 for(unsigned k=0;k<100000;k++){
  for(unsigned i=0;i<64;i++)cmd[i]=(uint8_t)(rnd(&seed)>>24);
  if(k&1){cmd[1]=0;memset(cmd+16,0,8);}
  assert(nwoas_admin_execute(&s,&cb,&p,cmd,64,g.out,4096,&r)==1);
  assert(r.bytes<=4096 && r.changed<8);
  if(r.deferred)assert(!r.bytes && r.status==0);
  for(unsigned i=0;i<32;i++)assert(g.a[i]==0xa5 && g.b[i]==0xa5);
  if(!(k%29))nwoas_admin_reset(&s,k&1);
 }
 return 0;
}
