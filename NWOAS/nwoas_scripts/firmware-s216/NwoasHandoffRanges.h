#ifndef NWOAS_HANDOFF_RANGES_H
#define NWOAS_HANDOFF_RANGES_H
/* Pure arithmetic for the T810X fixed BootArgs + ADT handoff allocation.
 * No guest data is read here. Call before copying, and before HOB reservation. */
static inline int NwoasHandoffRange(unsigned long long ram_base,unsigned long long ram_size,
 unsigned long long fd_base,unsigned long long fd_size,
 unsigned long long ba_base,unsigned long long adt_base,unsigned long long adt_size,
 unsigned long long *reserve_size)
{
 const unsigned long long max=~0ULL;
 if(!reserve_size || !ram_size || ram_size>max-ram_base || !fd_size || fd_size>max-fd_base ||
    (ba_base&0x3fff) || ba_base>max-0x4000 || adt_base!=ba_base+0x4000 ||
    !adt_size || adt_size>0x100000 || adt_size>max-adt_base)return 0;
 unsigned long long end=adt_base+adt_size;
 if(end>max-0x3fff)return 0;
 end=(end+0x3fff)&~0x3fffULL;
 if(ba_base<ram_base || end>ram_base+ram_size ||
    (ba_base<fd_base+fd_size && end>fd_base))return 0;
 *reserve_size=end-ba_base;
 return *reserve_size<=0xffffffffULL;
}
static inline int NwoasHandoffAdtSource(unsigned long long virt_base,unsigned long long devtree,
 unsigned long long ram_base,unsigned long long ram_size,unsigned long long adt_size,
 unsigned long long *source)
{
 const unsigned long long max=~0ULL;
 if(!source || !adt_size || ram_size>max-ram_base || devtree<virt_base)return 0;
 unsigned long long delta=devtree-virt_base;
 if(delta>ram_size || adt_size>ram_size-delta)return 0;
 *source=ram_base+delta;
 return 1;
}
#endif
