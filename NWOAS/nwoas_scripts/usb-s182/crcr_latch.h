/* SPDX-License-Identifier: MIT
 * Software record of pre-Run CRCR writes. CRCR pointer reads can return zero.
 * This is validation evidence only: never changes a guest register value.
 */
#ifndef NWOAS_CRCR_LATCH_H
#define NWOAS_CRCR_LATCH_H
#include <stdint.h>
#include <stdbool.h>
struct nwoas_crcr_latch { uint64_t value; unsigned seen; };
static inline void nwoas_crcr_reset(struct nwoas_crcr_latch *s)
{ s->value=0; s->seen=0; }
static inline void nwoas_crcr_write(struct nwoas_crcr_latch *s,
    unsigned offset, unsigned bytes, uint64_t value, bool stopped)
{
    if (!stopped) return;
    if (offset==0x18 && bytes==8) {s->value=value; s->seen=3;}
    else if (offset==0x18 && bytes==4) {
        s->value=(s->value & UINT64_C(0xffffffff00000000)) | (uint32_t)value;
        s->seen |= 1;
    } else if (offset==0x1c && bytes==4) {
        s->value=(s->value & UINT64_C(0xffffffff)) | ((uint64_t)(uint32_t)value<<32);
        s->seen |= 2;
    }
}
static inline bool nwoas_crcr_pointer(const struct nwoas_crcr_latch *s, uint64_t *p)
{
    if (s->seen!=3 || !(s->value & ~UINT64_C(63))) return false;
    *p=s->value & ~UINT64_C(63); return true;
}
#endif
