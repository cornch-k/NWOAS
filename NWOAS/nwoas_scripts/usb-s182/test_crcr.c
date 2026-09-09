#include "crcr_latch.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 struct nwoas_crcr_latch s={0}; uint64_t p=999; unsigned n=0;
 assert(!nwoas_crcr_pointer(&s,&p) && p==999);n++;
 nwoas_crcr_write(&s,0x18,4,0xfe900001,true);
 assert(!nwoas_crcr_pointer(&s,&p));n++;
 nwoas_crcr_write(&s,0x1c,4,0,true);
 assert(nwoas_crcr_pointer(&s,&p)&&p==0xfe900000);n++;
 nwoas_crcr_reset(&s); assert(!nwoas_crcr_pointer(&s,&p));n++;
 nwoas_crcr_write(&s,0x1c,4,0xa,true);
 assert(!nwoas_crcr_pointer(&s,&p));n++;
 nwoas_crcr_write(&s,0x18,4,0xf2340041,true);
 assert(nwoas_crcr_pointer(&s,&p)&&p==UINT64_C(0xaf2340040));n++;
 nwoas_crcr_write(&s,0x18,8,UINT64_C(0x84000403f),true);
 assert(nwoas_crcr_pointer(&s,&p)&&p==UINT64_C(0x840004000));n++;
 nwoas_crcr_write(&s,0x18,8,0,false);
 assert(nwoas_crcr_pointer(&s,&p)&&p==UINT64_C(0x840004000));n++;
 for(unsigned off=0;off<0x40;off++)for(unsigned bytes=1;bytes<=8;bytes*=2) {
  if((off==0x18&&(bytes==4||bytes==8))||(off==0x1c&&bytes==4))continue;
  nwoas_crcr_write(&s,off,bytes,0,true);
  assert(nwoas_crcr_pointer(&s,&p)&&p==UINT64_C(0x840004000));n++;
 }
 nwoas_crcr_write(&s,0x18,8,0x3f,true);
 assert(!nwoas_crcr_pointer(&s,&p));n++;
 nwoas_crcr_reset(&s); nwoas_crcr_write(&s,0x1c,4,~UINT64_C(0),true);
 nwoas_crcr_write(&s,0x18,4,~UINT64_C(0),true);
 assert(nwoas_crcr_pointer(&s,&p)&&p==UINT64_C(0xffffffffffffffc0));n++;
 printf("PASS %u CRCR latch boundary checks\n",n);
 return 0;
}
