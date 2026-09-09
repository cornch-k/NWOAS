#include "handoff_layout.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
 unsigned long long b,e;
 assert(nwoas_handoff_layout(0x83c000000ULL,0x2a4fcc000ULL,0x5c000,0x83d000000ULL,0x83d20c000ULL,0x83f900000ULL,0x1e00000,&b,&e));
 assert(b==0x840200000ULL && e==0x840060000ULL);
 assert(nwoas_handoff_layout(0x83c000000ULL,0x2a4fcc000ULL,0x5c000,0x83d000000ULL,0x83d20c000ULL,0x840128000ULL,0x100e0000,&b,&e));
 assert(b==0x840200000ULL);
 assert(!nwoas_handoff_layout(0x83c000000ULL,0x2a4fcc000ULL,0x5c000,0x83ffff000ULL,0x840002000ULL,0x840300000ULL,0x1e00000,&b,&e));
 assert(!nwoas_handoff_layout(0,~0ULL,~0ULL,0,1,0,0x1e00000,&b,&e));
 assert(!nwoas_handoff_layout(~0ULL-1,4,1,0,1,0,1,&b,&e));
 assert(!nwoas_handoff_layout(0x83c000000ULL,0x4000000,0x5c000,0x83d000000ULL,0x83d20c000ULL,0x83f900000ULL,0x1e00000,&b,&e));
 for(unsigned long long h=0x83f000000ULL;h<0x842000000ULL;h+=0x4000){
  for(unsigned long long size=0x4000;size<=0x100000;size+=0x4000){
   assert(nwoas_handoff_layout(0x83c000000ULL,0x2a4fcc000ULL,size,0x83d000000ULL,0x83d20c000ULL,h,0x1e00000,&b,&e));
   assert(b>=h && b>=e && !(b&(NWOAS_IMAGE_ALIGN-1)));
  }
 }
 puts("PASS: observed collision boundaries, prefix overlap, overflow, RAM exhaustion, 196608 placement cases");
}
