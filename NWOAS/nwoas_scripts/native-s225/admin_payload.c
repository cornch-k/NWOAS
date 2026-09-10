#include "admin_payload.h"
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t le64(const uint8_t *p) { return le32(p) | ((uint64_t)le32(p+4)<<32); }
static void put(uint8_t *p, uint64_t value, unsigned n)
{ for (unsigned i=0;i<n;i++) p[i]=(uint8_t)(value>>(i*8)); }
static void zero(uint8_t *p, unsigned n) { for(unsigned i=0;i<n;i++)p[i]=0; }
static void text(uint8_t *p, const char *s, unsigned n)
{ bool ended=false; for(unsigned i=0;i<n;i++){if(!ended && !*s)ended=true;p[i]=ended?' ':(uint8_t)*s++;} }
int nwoas_admin_payload(const uint8_t *cmd, size_t n,
    const struct nwoas_identity_policy *p, uint8_t *out, size_t capacity,
    struct nwoas_admin_payload_result *r)
{
    if (!r) return -1;
    r->status=2;r->bytes=0;
    if (!cmd || n!=64 || !p || !out || capacity<4096 ||
        !p->primary_lbas || p->primary_lbas>=(UINT64_C(1)<<40) || p->mdts>8 ||
        (p->paired && (!p->link_lbas || p->link_lbas>=(UINT64_C(1)<<40)))) return -1;
    if (cmd[0]!=6 && cmd[0]!=2) return 0;
    if (cmd[1] || le64(cmd+16)) return 1;
    uint32_t nsid=le32(cmd+4),dw[6];for(unsigned i=0;i<6;i++)dw[i]=le32(cmd+40+i*4);
    if (cmd[0]==6) {
        if ((dw[0]>>8) || dw[1] || dw[2] || dw[3] || dw[4] || dw[5])return 1;
        unsigned cns=dw[0]&255;
        if (cns==0) {
            if(nsid!=1 && !(p->paired && nsid==2)){r->status=0xb;return 1;}
            uint64_t lbas=nsid==1?p->primary_lbas:p->link_lbas;
            zero(out,4096);put(out,lbas,8);put(out+8,lbas,8);put(out+16,lbas,8);
            out[130]=12;out[99]=(nsid==1 && p->primary_readonly)?1:0;
        } else if (cns==1) {
            if(nsid!=0 && nsid!=UINT32_MAX){r->status=0xb;return 1;}
            zero(out,4096);put(out,0x1234,2);put(out+2,0x1234,2);
            text(out+4,"NWOAS-RO-00000000001",20);
            text(out+24,"NWOAS ANS2 READ ONLY BRIDGE",40);text(out+64,"S92",8);
            out[77]=p->mdts;put(out+78,1,2);put(out+80,0x10300,4);
            out[512]=0x66;out[513]=0x44;put(out+516,p->paired?2:1,4);
            out[525]=p->volatile_cache?1:0;
        } else if (cns==2) {
            zero(out,4096);unsigned i=0;
            if(nsid<1)put(out+4*i++,1,4);
            if(p->paired && nsid<2)put(out+4*i++,2,4);
        } else return 1;
        r->status=0;r->bytes=4096;return 1;
    }
    unsigned lid=dw[0]&255;
    uint64_t numd=(dw[0]>>16)|((uint64_t)(dw[1]&0xffff)<<16);
    uint64_t bytes=(numd+1)*4;
    if ((lid!=1 && lid!=2 && lid!=3) || bytes>4096 || dw[2] || dw[3])return 1;
    zero(out,(unsigned)bytes);
    if(lid==2){put(out+1,300,2);out[3]=100;}
    if(lid==3 && bytes>=16){out[0]=1;text(out+8,"S93",8);}
    r->status=0;r->bytes=(uint32_t)bytes;return 1;
}
