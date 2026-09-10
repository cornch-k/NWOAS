#ifndef NWOAS_ADMIN_STATE_H
#define NWOAS_ADMIN_STATE_H
#include "admin_payload.h"
struct nwoas_io_queue { uint64_t base; uint16_t depth, cqid; bool present, ien; };
struct nwoas_admin_state {
    struct nwoas_io_queue sq, cq;
    uint32_t features[12];
    uint64_t sq_generation, cq_generation;
    uint16_t aer_cid;
    bool aer_present;
};
struct nwoas_admin_callbacks {
    void *opaque;
    bool (*contains)(void *,uint64_t,uint32_t); /* NULL disables Create SQ/CQ */
    bool (*set_cache)(void *,bool); /* transactional: false leaves cache state unchanged */
    uint32_t (*get_cache)(void *); /* 0 or 1; provided together with set_cache */
};
struct nwoas_admin_result {
    uint32_t result, bytes;
    uint16_t status;
    uint8_t changed; /* bit0 SQ1, bit1 CQ1 descriptor; bit2 cache policy needs resync */
    bool deferred; /* held AER: caller must NOT publish a completion */
};
/* No guest pointers, DMA, submission/completion ring, IRQ or transport.
 * All access to one state and callbacks must be serialized by its owner. */
void nwoas_admin_init(struct nwoas_admin_state *);
void nwoas_admin_reset(struct nwoas_admin_state *,bool full_reset);
/* -1 invalid API; 1 command result. Output must be disjoint and >=4096 bytes.
 * Descriptor validation checks the existing synthetic queue policy only.
 * Caller must atomically reconcile changed descriptors with actual I/O owner. */
int nwoas_admin_execute(struct nwoas_admin_state *,const struct nwoas_admin_callbacks *,
    const struct nwoas_identity_policy *,const uint8_t *cmd,size_t cmd_bytes,
    uint8_t *out,size_t out_capacity,struct nwoas_admin_result *);
#endif
