/* Host-side validation of negotiated geometry, including malformed inputs. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../m1n1_windows/src/afk_ring.h"

int main(void)
{
    assert(afk_ring_block_size(0x4000, 0x3f40) == 64);
    assert(afk_ring_block_size(0x4000, 0x3e80) == 128);
    assert(afk_ring_block_size(0x4000, 0x3d00) == 256);
    assert(!afk_ring_block_size(0x4000, 0x4001));
    assert(!afk_ring_block_size(0x4000, 0x4000));
    assert(!afk_ring_block_size(0x4000, 0x3e81));
    assert(!afk_ring_block_size(0x4000, 0));
    assert(!afk_ring_block_size(0x4000, 0x3dc0)); /* 192: not power of two */
    assert(!afk_ring_block_size(SIZE_MAX, SIZE_MAX - 3));
    for (size_t total = 1; total < 1024; total++) {
        for (size_t data = 0; data <= total + 1; data++) {
            size_t block = afk_ring_block_size(total, data);
            if (!block)
                continue;
            assert(block >= 64 && !(block & (block - 1)));
            assert(total == data + 3 * block);
            assert(data >= block && !(data % block));
        }
    }
    puts("PASS: legacy/Tahoe geometry, malformed inputs and layout invariants");
}
