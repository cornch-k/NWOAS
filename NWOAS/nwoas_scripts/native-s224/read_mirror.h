#ifndef NWOAS_S224_READ_MIRROR_H
#define NWOAS_S224_READ_MIRROR_H
#include <stdint.h>
#include <stdbool.h>
/* Read-only projection. Python remains the sole control/admin writer.
 * Publish and read are serialized by the HV big lock / proxy rendezvous.
 * No guest pointers, DMA, allocation, or physical device access here. */
struct nwoas_read_mirror {
    uint64_t cc_csts, aqa_mask, asq, acq, flags;
    uint64_t pci_reads, register_reads, publications;
    bool enabled;
};
static void nwoas_mirror_publish(struct nwoas_read_mirror *m, uint64_t cc_csts,
    uint64_t aqa_mask, uint64_t asq, uint64_t acq, uint64_t flags)
{
    m->enabled = false;
    m->cc_csts = cc_csts; m->aqa_mask = aqa_mask;
    m->asq = asq; m->acq = acq; m->flags = flags;
    m->publications++;
    m->enabled = (flags >> 63) != 0;
}
static uint8_t nwoas_mirror_byte(uint64_t value, unsigned byte)
{ return (uint8_t)(value >> (8 * byte)); }
static uint8_t nwoas_mirror_pci_byte(const struct nwoas_read_mirror *m, uint32_t off)
{
    if (off < 4) return nwoas_mirror_byte(UINT64_C(0x00101234), off);
    if (off < 6) return nwoas_mirror_byte(m->flags & 0xffff, off - 4);
    if (off < 8) return nwoas_mirror_byte((m->flags & (UINT64_C(1)<<16)) ? 8 : 0, off - 6);
    if (off < 12) return nwoas_mirror_byte(UINT64_C(0x01080201), off - 8);
    if (off >= 0x10 && off < 0x14)
        return nwoas_mirror_byte((m->flags & (UINT64_C(1)<<17)) ? 0xffffc004 : 0x100004, off - 0x10);
    if (off >= 0x14 && off < 0x18)
        return nwoas_mirror_byte((m->flags & (UINT64_C(1)<<18)) ? UINT32_MAX : 7, off - 0x14);
    if (off >= 0x2c && off < 0x30) return nwoas_mirror_byte(UINT64_C(0x00101234), off - 0x2c);
    return off == 0x3d ? 1 : 0;
}
static uint8_t nwoas_mirror_reg_byte(const struct nwoas_read_mirror *m, uint32_t off,
                                   uint32_t mask)
{
    if (off < 8) return nwoas_mirror_byte(UINT64_C(255)|(UINT64_C(1)<<16)|
                                       (UINT64_C(20)<<24)|(UINT64_C(1)<<37), off);
    if (off < 12) return nwoas_mirror_byte(0x10300, off - 8);
    if (off < 16) return nwoas_mirror_byte(mask, off - 12);
    if (off < 20) return nwoas_mirror_byte(mask, off - 16);
    if (off < 24) return nwoas_mirror_byte((uint32_t)m->cc_csts, off - 20);
    if (off >= 0x1c && off < 0x20) return nwoas_mirror_byte(m->cc_csts >> 32, off - 0x1c);
    if (off >= 0x24 && off < 0x28) return nwoas_mirror_byte((uint32_t)m->aqa_mask, off - 0x24);
    if (off >= 0x28 && off < 0x30) return nwoas_mirror_byte(m->asq, off - 0x28);
    if (off >= 0x30 && off < 0x38) return nwoas_mirror_byte(m->acq, off - 0x30);
    return 0;
}
static bool nwoas_mirror_read(struct nwoas_read_mirror *m, uint64_t address,
    unsigned width_log2, bool fp_armed, bool fp_faulted, uint32_t fp_mask, uint64_t *out)
{
    if (!m->enabled || !out || width_log2 > 3) return false;
    uint32_t bytes = 1u << width_log2;
    uint64_t value = 0;
    if (address >= UINT64_C(0x700000000) && address < UINT64_C(0x700100000)) {
        uint32_t off = (uint32_t)(address - UINT64_C(0x700000000));
        if (off >= 4096 || bytes > 4096 - off) {
            value = bytes == 8 ? UINT64_MAX : (UINT64_C(1) << (bytes * 8)) - 1;
        } else {
            for (uint32_t i=0; i<bytes; i++) value |= (uint64_t)nwoas_mirror_pci_byte(m, off+i) << (8*i);
        }
        m->pci_reads++;
    } else if (address >= UINT64_C(0x700100000) && address < UINT64_C(0x700104000)) {
        uint32_t off = (uint32_t)(address - UINT64_C(0x700100000));
        if (bytes > 0x4000 - off) return false;
        /* Preserve S149's existing 32-bit fatal-CSTS fast-path override. */
        if (fp_armed && fp_faulted && off == 0x1c && width_log2 == 2) value = 3;
        else {
            uint32_t mask = fp_armed ? fp_mask : (uint32_t)(m->aqa_mask >> 32);
            for (uint32_t i=0; i<bytes; i++) value |= (uint64_t)nwoas_mirror_reg_byte(m, off+i, mask) << (8*i);
        }
        m->register_reads++;
    } else return false;
    *out = value;
    return true;
}
#endif
