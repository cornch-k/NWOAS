#include "admin_payload.h"
#include <assert.h>
#include <string.h>
struct guarded { uint8_t before[32], bytes[4096], after[32]; };
int main(void) {
    uint8_t cmd[64]={0};struct guarded g;struct nwoas_identity_policy p={61279344,8448,8,true,false,true};struct nwoas_admin_payload_result r;
    memset(&g,0xa5,sizeof(g));
    assert(nwoas_admin_payload(0,64,&p,g.bytes,4096,&r)==-1);
    assert(nwoas_admin_payload(cmd,0,&p,g.bytes,4096,&r)==-1);
    assert(nwoas_admin_payload(cmd,65,&p,g.bytes,4096,&r)==-1);
    assert(nwoas_admin_payload(cmd,64,0,g.bytes,4096,&r)==-1);
    assert(nwoas_admin_payload(cmd,64,&p,0,4096,&r)==-1);
    assert(nwoas_admin_payload(cmd,64,&p,g.bytes,4096,0)==-1);
    for(size_t cap=0;cap<4096;cap++)assert(nwoas_admin_payload(cmd,64,&p,g.bytes,cap,&r)==-1);
    for(unsigned op=0;op<256;op++)for(unsigned field=0;field<256;field++) {
        cmd[0]=(uint8_t)op;cmd[40]=(uint8_t)field;
        int result=nwoas_admin_payload(cmd,64,&p,g.bytes,4096,&r);
        assert(result>=0);assert(r.bytes<=4096);
        for(unsigned i=0;i<32;i++)assert(g.before[i]==0xa5 && g.after[i]==0xa5);
    }
    memset(cmd,0xff,64);cmd[0]=2;assert(nwoas_admin_payload(cmd,64,&p,g.bytes,4096,&r)==1 && r.status==2 && !r.bytes);
    cmd[1]=0;memset(cmd+16,0,8);assert(nwoas_admin_payload(cmd,64,&p,g.bytes,4096,&r)==1 && r.status==2 && !r.bytes);
    p.primary_lbas=UINT64_MAX;assert(nwoas_admin_payload(cmd,64,&p,g.bytes,4096,&r)==-1);
    p.primary_lbas=1;p.link_lbas=UINT64_C(1)<<40;
    assert(nwoas_admin_payload(cmd,64,&p,g.bytes,4096,&r)==-1);
    p.link_lbas=1;p.mdts=9;
    assert(nwoas_admin_payload(cmd,64,&p,g.bytes,4096,&r)==-1);
    p.mdts=8;p.primary_lbas=0;
    assert(nwoas_admin_payload(cmd,64,&p,g.bytes,4096,&r)==-1);
    return 0;
}
