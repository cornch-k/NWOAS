/* SPDX-License-Identifier: MIT */
#ifndef AFK_RING_H
#define AFK_RING_H

#include <stddef.h>

/* Three equal header blocks precede the ring data. Zero means invalid.
 * [FACT] M1/Tahoe first-ring measurement: total=0x4000, data=0x3e80.
 * Require a power of two because the transport uses ALIGN_UP.
 */
static inline size_t afk_ring_block_size(size_t total, size_t data)
{
    if (data >= total)
        return 0;
    size_t overhead = total - data;
    if (overhead % 3)
        return 0;
    size_t block = overhead / 3;
    if (block < 64 || (block & (block - 1)) || data < block || data % block)
        return 0;
    return block;
}

#endif
