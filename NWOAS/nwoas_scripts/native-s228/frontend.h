#ifndef NWOAS_NVME_FRONTEND_H
#define NWOAS_NVME_FRONTEND_H
#include "model.h"
#include "admin_ring.h"
/* Serialized synchronous frontend. Callbacks must not reenter. This component
 * owns PCI/control/admin state; another owner supplies I/O data/NS2 operations.
 * No mapping, device registers or physical storage is accessed directly. */
struct nwoas_frontend_ops {
    void *opaque;
    bool (*contains)(void *,uint64_t,uint32_t);
    bool (*read)(void *,uint64_t,uint8_t *,uint32_t);
    bool (*write)(void *,uint64_t,const uint8_t *,uint32_t);
    void (*barrier)(void *);
    bool (*reconcile)(void *,const struct nwoas_admin_state *,uint8_t);
    /* Cancel/drain all old I/O work before mapping/queue reuse. Failure is fatal.
       Same-thread, nonreentrant; no calls back into frontend from these hooks. */
    bool (*reset_io)(void *);
    bool (*flush)(void *);
    /* 1 success, 0 expected command failure, -1 fatal backend failure. */
    int (*set_cache)(void *,bool);
    uint32_t (*get_cache)(void *);
    void (*irq)(void *,bool);
};
struct nwoas_frontend {
    struct nwoas_control control;
    struct nwoas_admin_ring ring;
    struct nwoas_frontend_ops ops;
    bool io_pending,backend_fault,full_reset,control_busy;
};
bool nwoas_frontend_init(struct nwoas_frontend *,uint64_t,
                         struct nwoas_frontend_ops,struct nwoas_identity_policy);
/* Controller/feature reset; PCI command/probe state is retained.
 * Bus-level reset requires successful re-initialization. */
void nwoas_frontend_reset(struct nwoas_frontend *);
/* I/O pending means an interrupt-enabled I/O CQ has outstanding completions.
 * The owner must hold the same lock and reject stale generation updates. */
void nwoas_frontend_io_pending(struct nwoas_frontend *,bool);
void nwoas_frontend_fault(struct nwoas_frontend *);
enum nwoas_access nwoas_frontend_read(struct nwoas_frontend *,bool pci,uint32_t,unsigned,uint64_t *);
enum nwoas_access nwoas_frontend_write(struct nwoas_frontend *,bool pci,uint32_t,unsigned,uint64_t);
/* NOT from CQ-head handling; explicit deferred owner work only. */
int nwoas_frontend_poll(struct nwoas_frontend *,unsigned);
#endif
