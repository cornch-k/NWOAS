#ifndef NWOAS_NVME_CONTROL_MODEL_H
#define NWOAS_NVME_CONTROL_MODEL_H
#include <stdint.h>
#include <stdbool.h>
/* Pure state model: callbacks are the only effects. No DMA or MMIO is issued.
 * Owner must serialize every call for one instance, including pending updates.
 * Callbacks must not reenter the model; setup must be transactional on failure.
 * This component intentionally supplies no lock or host transport. */
enum nwoas_access { NWOAS_HANDLED, NWOAS_NOT_HANDLED, NWOAS_INVALID };
struct nwoas_callbacks {
    void *opaque;
    bool (*contains)(void *, uint64_t base, uint32_t bytes);
    bool (*setup)(void *, uint64_t asq, uint16_t sq_depth,
                  uint64_t acq, uint16_t cq_depth);
    void (*reset)(void *);
    bool (*flush)(void *);
    void (*irq)(void *, bool);
};
struct nwoas_control {
    struct nwoas_callbacks cb;
    uint64_t bar, asq, acq;
    uint32_t cc, csts, aqa, mask;
    uint16_t command;
    bool probe_low, probe_high, pending;
};
void nwoas_control_init(struct nwoas_control *, uint64_t bar, struct nwoas_callbacks);
void nwoas_control_reset(struct nwoas_control *);
void nwoas_control_pending(struct nwoas_control *, bool enabled_cq_pending);
enum nwoas_access nwoas_pci_read(const struct nwoas_control *, uint32_t offset,
                               unsigned width, uint64_t *value);
enum nwoas_access nwoas_pci_write(struct nwoas_control *, uint32_t offset,
                                unsigned width, uint64_t value);
enum nwoas_access nwoas_reg_read(const struct nwoas_control *, uint32_t offset,
                               unsigned width, uint64_t *value);
enum nwoas_access nwoas_reg_write(struct nwoas_control *, uint32_t offset,
                                unsigned width, uint64_t value);
#endif
