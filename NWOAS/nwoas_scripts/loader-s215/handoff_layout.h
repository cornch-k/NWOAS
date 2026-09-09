/* NWOAS T8103 Project Mu handoff placement; source-only pure range policy.
 * Keep synchronized with T810XFamilyPkg.dsc.inc. The compiled helper is shared
 * with the host regression tests, not reimplemented in the tests. */
#ifndef NWOAS_HANDOFF_LAYOUT_H
#define NWOAS_HANDOFF_LAYOUT_H
#define NWOAS_BOOTARGS_COPY 0x840000000ULL
#define NWOAS_ADT_COPY 0x840004000ULL
#define NWOAS_IMAGE_ALIGN 0x200000ULL
static int nwoas_handoff_layout(unsigned long long ram_base,
 unsigned long long ram_size,unsigned long long adt_size,
 unsigned long long prefix_base,unsigned long long prefix_end,
 unsigned long long heap,unsigned long long image_size,
 unsigned long long *image_base,unsigned long long *handoff_end)
{
 const unsigned long long max=~0ULL;
 if(!image_base || !handoff_end || !adt_size || !image_size ||
    ram_size>max-ram_base || adt_size>max-NWOAS_ADT_COPY)return 0;
 unsigned long long end=NWOAS_ADT_COPY+adt_size;
 if(end>max-0x3fff)return 0;
 end=(end+0x3fff)&~0x3fffULL;
 unsigned long long ram_end=ram_base+ram_size;
 if(NWOAS_BOOTARGS_COPY<ram_base || end>ram_end || prefix_end<prefix_base ||
    (prefix_base<end && prefix_end>NWOAS_BOOTARGS_COPY) ||
    heap<ram_base || heap>ram_end)return 0;
 /* Force all following monotonic allocations beyond the handoff copies, even
    when a smaller image could temporarily fit before them. */
 unsigned long long base=heap>end?heap:end;
 if(base>max-(NWOAS_IMAGE_ALIGN-1))return 0;
 base=(base+NWOAS_IMAGE_ALIGN-1)&~(NWOAS_IMAGE_ALIGN-1);
 if(base>ram_end || image_size>ram_end-base)return 0;
 *image_base=base;*handoff_end=end;return 1;
}
#endif
