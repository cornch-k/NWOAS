#ifndef NWOAS_ADMIN_RING_H
#define NWOAS_ADMIN_RING_H
#include "admin_state.h"
/* Pure bounded ring engine with caller-owned memory callbacks. No MMIO, ANS,
 * threads or transport. Serialize all calls and prohibit callback reentry.
 * All pointers/objects are valid and disjoint. Memory callbacks return true
 * only after a complete transfer; contains accepts RAM only, never MMIO.
 * barrier orders payload/CQE body stores before the final 16-bit status store.
 * reconcile atomically applies changed descriptors/cache policy to I/O owner;
 * false is fatal, not an NVMe command error. pending is admin CQ pending only.
 * Integration must aggregate I/O pending and apply masks once. */
struct nwoas_ring_callbacks {
    void *opaque;
    bool (*contains)(void *,uint64_t,uint32_t);
    bool (*read)(void *,uint64_t,uint8_t *,uint32_t);
    bool (*write)(void *,uint64_t,const uint8_t *,uint32_t);
    void (*barrier)(void *);
    bool (*reconcile)(void *,const struct nwoas_admin_state *,uint8_t);
    void (*pending)(void *,bool);
    void (*fault)(void *);
};
struct nwoas_admin_ring {
    struct nwoas_ring_callbacks cb;
    struct nwoas_admin_callbacks admin_cb;
    struct nwoas_identity_policy policy;
    struct nwoas_admin_state admin;
    uint64_t sq_base,cq_base,lifecycle;
    uint16_t sq_depth,cq_depth,sq_head,sq_tail,cq_head,cq_tail,pending;
    uint8_t phase;
    bool active,fatal;
    uint8_t command[64],payload[4096],completion[16];
};
/* init returns false for missing mandatory callbacks or invalid identity policy.
 * No callbacks execute and no state changes on invalid init. Other entry points
 * require a successful init; an object with rejected init is unusable. */
bool nwoas_ring_init(struct nwoas_admin_ring *,struct nwoas_ring_callbacks,
                     struct nwoas_admin_callbacks,struct nwoas_identity_policy);
void nwoas_ring_reset(struct nwoas_admin_ring *,bool full_reset);
/* Configure inactive/nonfatal engine only. Failure is transactional. */
bool nwoas_ring_configure(struct nwoas_admin_ring *,uint64_t,uint16_t,uint64_t,uint16_t);
/* Tail doorbell services at most 256 commands. CQ ack never starts work.
 * poll allows an explicitly scheduled owner to drain backlog, NOT a CQ-ack DPC.
 * Tail/poll return >=0 commands consumed; ack returns 0 on success.
 * Return -1 for invalid API budget, -2 on fatal. Disabled/fatal
 * doorbells are ignored (0). The enclosing control owner gates CSTS.RDY/CFS. */
int nwoas_ring_tail(struct nwoas_admin_ring *,uint32_t tail);
int nwoas_ring_ack(struct nwoas_admin_ring *,uint32_t head);
int nwoas_ring_poll(struct nwoas_admin_ring *,unsigned budget);
#endif
