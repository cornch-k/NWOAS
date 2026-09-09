/* Only the FAT partition of the already authenticated host-RAM disk may be scratch. */
#ifndef S177_SCRATCH_H
#define S177_SCRATCH_H
#include <stdint.h>
static uint32_t s177_u32(const unsigned char *b) { return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); }
static uint64_t s177_u64(const unsigned char *b) { return s177_u32(b)|((uint64_t)s177_u32(b+4)<<32); }
static int s177_extent_matches(const unsigned char *b, uint32_t bytes, uint32_t disk) {
 return b && bytes>=32 && s177_u32(b)==1 && s177_u32(b+8)==disk &&
 s177_u64(b+16)==1048576 && s177_u64(b+24)==33554432;
}
#endif
