#include "frontend.h"
#include <string.h>
#define BASE 0x10000u
#define SIZE 0x90000u
static uint8_t memory[SIZE];
static struct nwoas_frontend f;
#define r f.ring
static bool reset_fail,cache_fatal;
static unsigned resets;
static bool cache=true,cache_fail,reconcile_fail,irq;
static unsigned reads,writes,barriers,reconciles,flushes,sets;
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
static bool reset_io(void *p){(void)p;resets++;return !reset_fail;}
static bool flush_cb(void *p){(void)p;flushes++;return !cache_fail;}
static int set_cache(void *p,bool v)
{(void)p;sets++;if(cache_fatal)return -1;if(cache && !v){flushes++;if(cache_fail)return false;}cache=v;return true;}
static uint32_t get_cache(void *p){(void)p;return cache;}
int initialize(void)
{
 memset(memory,0,sizeof(memory));cache=true;cache_fail=reconcile_fail=irq=false;
 reads=writes=barriers=reconciles=flushes=sets=fail_read=fail_write=0;
 reset_fail=cache_fatal=false;resets=0;
 struct nwoas_frontend_ops ops={0,contains,read_cb,write_cb,barrier,reconcile,reset_io,flush_cb,set_cache,get_cache,pending};
 struct nwoas_identity_policy p={61279344,8448,8,true,false,true};
 return nwoas_frontend_init(&f,0x700100000,ops,p);
}
int configure(uint64_t sq,unsigned ns,uint64_t cq,unsigned nc)
{
 if(ns<2 || ns>256 || nc<2 || nc>256)return 0;
 nwoas_frontend_write(&f,false,0x24,32,(ns-1)|((nc-1)<<16));
 nwoas_frontend_write(&f,false,0x28,64,sq);nwoas_frontend_write(&f,false,0x30,64,cq);
 nwoas_frontend_write(&f,false,0x14,32,0x460001);return f.control.csts==1;
}
void reset_state(int full){if(full)nwoas_frontend_reset(&f);else nwoas_frontend_write(&f,false,0x14,32,0);}
int tail(unsigned v){return nwoas_frontend_write(&f,false,0x1000,32,v);}
int ack(unsigned v){return nwoas_frontend_write(&f,false,0x1004,32,v);}
int poll_ring(unsigned budget){return nwoas_frontend_poll(&f,budget);}
int access_write(int pci,unsigned off,unsigned width,uint64_t value)
{return nwoas_frontend_write(&f,pci,off,width,value);}
uint64_t access_read(int pci,unsigned off,unsigned width)
{uint64_t value=0;nwoas_frontend_read(&f,pci,off,width,&value);return value;}
void io_pending(int value){nwoas_frontend_io_pending(&f,value);}
void reset_failure(int value){reset_fail=value;}
void force_fault(void){nwoas_frontend_fault(&f);}
void inject(unsigned read_at,unsigned write_at,unsigned rec_fail,unsigned flush_fail)
{fail_read=read_at;fail_write=write_at;reconcile_fail=rec_fail;cache_fail=flush_fail;}
int guest_write(uint64_t a,const uint8_t *in,unsigned n)
{if(!contains(0,a,n))return 0;memcpy(memory+(a-BASE),in,n);return 1;}
const uint8_t *memory_view(void){return memory;}
uint64_t inspect(unsigned k)
{
 switch(k){case 0:return r.sq_head;case 1:return r.sq_tail;case 2:return r.cq_head;case 3:return r.cq_tail;
 case 4:return r.pending;case 5:return r.phase;case 6:return r.active;case 7:return r.fatal;case 8:return r.lifecycle;
 case 9:return irq;case 10:return f.backend_fault;case 11:return reads;case 12:return writes;case 13:return barriers;case 14:return reconciles;
 case 15:return r.admin.aer_present;case 16:return r.admin.aer_cid;case 17:return cache;case 18:return flushes;case 19:return sets;
 case 20:return r.admin.sq.present;case 21:return r.admin.sq.base;case 22:return r.admin.sq.depth;case 23:return r.admin.sq.cqid;
 case 24:return r.admin.cq.present;case 25:return r.admin.cq.base;case 26:return r.admin.cq.depth;case 27:return r.admin.cq.ien;
 case 28:return r.admin.sq_generation;case 29:return r.admin.cq_generation;
 case 44:return f.control.csts;case 45:return f.control.mask;case 46:return f.control.command;case 47:return resets;case 48:return f.io_pending;
 default:return k>=32 && k<44?r.admin.features[k-32]:0;}
}
