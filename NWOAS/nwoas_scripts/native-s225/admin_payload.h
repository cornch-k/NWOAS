#ifndef NWOAS_ADMIN_PAYLOAD_H
#define NWOAS_ADMIN_PAYLOAD_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* Pure response construction. No command submission, DMA, guest pointers,
 * physical backend, queue state, allocation or transport. */
/* Policy: 1 <= primary_lbas < 2^40; when paired, 1 <= link_lbas < 2^40;
 * mdts is limited to 0..8 (the current transport uses 4 or 8). */
struct nwoas_identity_policy {
    uint64_t primary_lbas, link_lbas;
    uint8_t mdts;
    bool paired, primary_readonly, volatile_cache;
};
struct nwoas_admin_payload_result { uint16_t status; uint32_t bytes; };
/* -1: invalid API arguments/policy; 0: command belongs to another handler;
 * 1: handled response (status may be an NVMe command error).
 * out_capacity >= 4096 is required; unsupported commands leave out untouched.
 * Inspect result only when return == 1; on 0/-1 (if result is non-NULL)
 * status is initialized to Invalid Field and bytes to 0, NOT a completion.
 * Caller supplies disjoint buffers and publishes completions itself.
 * Import-free ARM64 builds require -ffreestanding -fno-builtin and nm check. */
int nwoas_admin_payload(const uint8_t *command, size_t command_bytes,
    const struct nwoas_identity_policy *policy, uint8_t *out, size_t out_capacity,
    struct nwoas_admin_payload_result *result);
#endif
