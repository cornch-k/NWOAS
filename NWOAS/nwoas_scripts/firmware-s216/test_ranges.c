#include "NwoasHandoffRanges.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
 unsigned long long n;
 assert(NwoasHandoffAdtSource(0xfffffe0010000000ULL,0xfffffe0011000000ULL,0x83c000000ULL,0x2a4fcc000ULL,0x5c000,&n) && n==0x83d000000ULL);
 assert(!NwoasHandoffAdtSource(0xfffffe0010000000ULL,0,0x83c000000ULL,0x2a4fcc000ULL,0x5c000,&n));
 assert(!NwoasHandoffAdtSource(100,200,0,100,1,&n));
 assert(!NwoasHandoffAdtSource(0,0,~0ULL,1,1,&n));
 assert(NwoasHandoffRange(0x83c000000ULL,0x2a4fcc000ULL,0x840200000ULL,0x1e00000,0x840000000ULL,0x840004000ULL,0x5c000,&n) && n==0x60000);
 assert(!NwoasHandoffRange(0x83c000000ULL,0x2a4fcc000ULL,0x840000000ULL,0x1e00000,0x840000000ULL,0x840004000ULL,0x5c000,&n));
 assert(!NwoasHandoffRange(0x83c000000ULL,0x2a4fcc000ULL,0x83fe00000ULL,0x1e00000,0x840000000ULL,0x840004000ULL,0x5c000,&n));
 assert(!NwoasHandoffRange(~0ULL,1,0,1,0,0x4000,1,&n));
 assert(!NwoasHandoffRange(0x840004000ULL,0x100000,0,1,0x840000000ULL,0x840004000ULL,1,&n));
 assert(!NwoasHandoffRange(0x83c000000ULL,0x2a4fcc000ULL,0x842000000ULL,0x1e00000,0x840000000ULL,0x840004000ULL,0x100001,&n));
 assert(!NwoasHandoffRange(0,~0ULL,0,1,~0ULL-0x3fff,0,1,&n));
 for(unsigned long long size=1;size<=0x100000;size++){
  assert(NwoasHandoffRange(0x83c000000ULL,0x2a4fcc000ULL,0x842000000ULL,0x1e00000,0x840000000ULL,0x840004000ULL,size,&n));
  assert(n>=size+0x4000 && n<size+0x8000 && !(n&0x3fff));
 }
 puts("PASS: 1048576 ADT sizes, FD overlaps, exact reservation bounds and overflow rejection");
}
