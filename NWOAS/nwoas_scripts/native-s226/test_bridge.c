#include "admin_state.h"
static struct nwoas_admin_state state;
static bool cache=true,fail_cache=false;
static unsigned flushes,set_calls;
static bool cache_callbacks=true;
static bool contains(void *p,uint64_t a,uint32_t n)
{(void)p;return a>=0x10000 && a<=0x100000 && n<=0x100000-a;}
static bool set_cache(void *p,bool enabled)
{(void)p;set_calls++;if(cache && !enabled){flushes++;if(fail_cache)return false;}cache=enabled;return true;}
static uint32_t get_cache(void *p){(void)p;return cache;}
void initialize(void){nwoas_admin_init(&state);cache=true;fail_cache=false;flushes=0;set_calls=0;cache_callbacks=true;}
void reset_state(int full){nwoas_admin_reset(&state,full);}
void use_cache_callbacks(int enabled){cache_callbacks=enabled;}
void fail_flush(int fail){fail_cache=fail;}
int execute(const uint8_t *cmd,uint8_t *out,struct nwoas_admin_result *r)
{
 struct nwoas_admin_callbacks cb={0,contains,cache_callbacks?set_cache:0,cache_callbacks?get_cache:0};
 struct nwoas_identity_policy policy={61279344,8448,8,true,false,true};
 return nwoas_admin_execute(&state,&cb,&policy,cmd,64,out,4096,r);
}
uint64_t inspect(unsigned key)
{
 switch(key){case 0:return state.sq.present;case 1:return state.sq.base;case 2:return state.sq.depth;case 3:return state.sq.cqid;
 case 4:return state.cq.present;case 5:return state.cq.base;case 6:return state.cq.depth;case 7:return state.cq.ien;
 case 8:return state.aer_present;case 9:return state.aer_cid;case 10:return cache;case 11:return flushes;
 case 12:return state.sq_generation;case 13:return state.cq_generation;case 14:return set_calls;
 default:return key>=32 && key<44?state.features[key-32]:0;}
}
