#include "admin_ring.h"
#include <string.h>
#define BASE 0x10000u
#define SIZE 0x90000u
static uint8_t memory[SIZE];
static struct nwoas_admin_ring r;
static bool cache=true,cache_fail,reconcile_fail,irq;
static unsigned faults,reads,writes,barriers,reconciles,flushes,sets;
static unsigned fail_read,fail_write;
static bool contains(void *p,uint64_t a,uint32_t n)
{(void)p;return a>=BASE && a<=BASE+SIZE && n<=BASE+SIZE-a;}
static bool read_cb(void *p,uint64_t a,uint8_t *out,uint32_t n)
{reads++;if(reads==fail_read || !contains(p,a,n))return false;memcpy(out,memory+(a-BASE),n);return true;}
static bool write_cb(void *p,uint64_t a,const uint8_t *in,uint32_t n)
{writes++;if(writes==fail_write || !contains(p,a,n))return false;memcpy(memory+(a-BASE),in,n);return true;}
static void barrier(void *p){(void)p;barriers++;}
static bool reconcile(void *p,const struct nwoas_admin_state *s,uint8_t changed)
{(void)p;(void)s;(void)changed;reconciles++;return !reconcile_fail;}
static void pending(void *p,bool v){(void)p;irq=v;}
static void fault(void *p){(void)p;faults++;}
static bool set_cache(void *p,bool v)
{(void)p;sets++;if(cache && !v){flushes++;if(cache_fail)return false;}cache=v;return true;}
static uint32_t get_cache(void *p){(void)p;return cache;}
int initialize(void)
{
 memset(memory,0,sizeof(memory));cache=true;cache_fail=reconcile_fail=irq=false;
 faults=reads=writes=barriers=reconciles=flushes=sets=fail_read=fail_write=0;
 struct nwoas_ring_callbacks cb={0,contains,read_cb,write_cb,barrier,reconcile,pending,fault};
 struct nwoas_admin_callbacks ac={0,contains,set_cache,get_cache};
 struct nwoas_identity_policy p={61279344,8448,8,true,false,true};
 return nwoas_ring_init(&r,cb,ac,p);
}
int configure(uint64_t sq,unsigned ns,uint64_t cq,unsigned nc)
{if(ns>65535 || nc>65535)return 0;return nwoas_ring_configure(&r,sq,(uint16_t)ns,cq,(uint16_t)nc);}
void reset_state(int full){nwoas_ring_reset(&r,full);}
int tail(unsigned v){return nwoas_ring_tail(&r,v);}
int ack(unsigned v){return nwoas_ring_ack(&r,v);}
int poll_ring(unsigned budget){return nwoas_ring_poll(&r,budget);}
void inject(unsigned read_at,unsigned write_at,unsigned rec_fail,unsigned flush_fail)
{fail_read=read_at;fail_write=write_at;reconcile_fail=rec_fail;cache_fail=flush_fail;}
int guest_write(uint64_t a,const uint8_t *in,unsigned n)
{if(!contains(0,a,n))return 0;memcpy(memory+(a-BASE),in,n);return 1;}
const uint8_t *memory_view(void){return memory;}
uint64_t inspect(unsigned k)
{
 switch(k){case 0:return r.sq_head;case 1:return r.sq_tail;case 2:return r.cq_head;case 3:return r.cq_tail;
 case 4:return r.pending;case 5:return r.phase;case 6:return r.active;case 7:return r.fatal;case 8:return r.lifecycle;
 case 9:return irq;case 10:return faults;case 11:return reads;case 12:return writes;case 13:return barriers;case 14:return reconciles;
 case 15:return r.admin.aer_present;case 16:return r.admin.aer_cid;case 17:return cache;case 18:return flushes;case 19:return sets;
 case 20:return r.admin.sq.present;case 21:return r.admin.sq.base;case 22:return r.admin.sq.depth;case 23:return r.admin.sq.cqid;
 case 24:return r.admin.cq.present;case 25:return r.admin.cq.base;case 26:return r.admin.cq.depth;case 27:return r.admin.cq.ien;
 case 28:return r.admin.sq_generation;case 29:return r.admin.cq_generation;
 default:return k>=32 && k<44?r.admin.features[k-32]:0;}
}
