#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define STATIC static
#define VOID void
#define IN
#define TRUE 1
#define FALSE 0
#define BOOLEAN int
#define EFI_RESOURCE_SYSTEM_MEMORY 1
#define EFI_RESOURCE_MEMORY_RESERVED 2
#define EFI_HOB_TYPE_RESOURCE_DESCRIPTOR 3
#define UINT32 uint32_t
#define UINT64 uint64_t
#define EFI_RESOURCE_ATTRIBUTE_TYPE uint64_t
#define EFI_PHYSICAL_ADDRESS uint64_t
struct hob { uint32_t ResourceType; uint64_t ResourceAttribute,PhysicalStart,ResourceLength; };
typedef union {void *Raw;struct hob *ResourceDescriptor;} EFI_PEI_HOB_POINTERS;
static struct hob hobs[16];static unsigned count;
static void *GetHobList(void){return hobs;}
static void *GetNextHob(int type,void *p){assert(type==3);return (struct hob *)p<hobs+count?p:NULL;}
#define GET_NEXT_HOB(p) ((p).ResourceDescriptor+1)
static void BuildResourceDescriptorHob(uint32_t t,uint64_t a,uint64_t b,uint64_t n)
{assert(count<16);hobs[count++]=(struct hob){t,a,b,n};}
#include "reserve_actual.inc"
static void initial(void){count=0;BuildResourceDescriptorHob(1,0x55,0x10000,0x10000);}
static void check_partition(uint64_t base,uint64_t size)
{
 uint64_t total=0, reserved=0;
 for(unsigned i=0;i<count;i++){
  assert(hobs[i].PhysicalStart>=0x10000 && hobs[i].PhysicalStart+hobs[i].ResourceLength<=0x20000);
  total+=hobs[i].ResourceLength;
  if(hobs[i].ResourceType==2){reserved+=hobs[i].ResourceLength;assert(hobs[i].PhysicalStart==base && hobs[i].ResourceLength==size);}
  else assert(hobs[i].ResourceAttribute==0x55);
  for(unsigned j=i+1;j<count;j++)assert(hobs[i].PhysicalStart+hobs[i].ResourceLength<=hobs[j].PhysicalStart || hobs[j].PhysicalStart+hobs[j].ResourceLength<=hobs[i].PhysicalStart);
 }
 assert(total==0x10000 && reserved==size);
}
int main(void)
{
 initial();assert(ReserveMemoryRegion(0x10000,0x4000));check_partition(0x10000,0x4000);
 initial();assert(ReserveMemoryRegion(0x1c000,0x4000));check_partition(0x1c000,0x4000);
 initial();assert(ReserveMemoryRegion(0x14000,0x4000));check_partition(0x14000,0x4000);
 initial();assert(ReserveMemoryRegion(0x10000,0x10000));check_partition(0x10000,0x10000);
 initial();assert(!ReserveMemoryRegion(0xf000,0x2000));assert(count==1 && hobs[0].ResourceLength==0x10000);
 initial();assert(!ReserveMemoryRegion(0x1f000,0x2000));assert(count==1 && hobs[0].ResourceLength==0x10000);
 initial();assert(ReserveMemoryRegion(0x14000,0x4000));assert(!ReserveMemoryRegion(0x14000,0x4000));check_partition(0x14000,0x4000);
 puts("PASS: actual reservation helper handles start/end/middle/whole resource and fails without a containing system-memory HOB");
}
