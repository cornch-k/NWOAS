/* S163 experiment: bounded spacing after an NVMe virtual EOI. This keeps
 * the level source intact and lets the interrupted DPC retire its unmask
 * epilogue before another level delivery. No timer/USB IRQ is rate-limited.
 * Default off; candidate build explicitly chooses the gap in microseconds.
 * Accessed on the fixed NVMe interrupt CPU under the existing exception path. */
#ifndef NWOAS_NVME_REASSERT_US
#define NWOAS_NVME_REASSERT_US 0
#endif
static u64 nwoas_nvme_last_eoi;
static u64 nwoas_nvme_irq_injections, nwoas_nvme_irq_eois, nwoas_nvme_irq_gated;
static u64 nwoas_nvme_irq_last_pc, nwoas_nvme_eoi_last_pc;

static void nwoas_nvme_note_eoi(u64 intd, u64 pc)
{
    if (intd != 900)
        return;
    nwoas_nvme_last_eoi = mrs(CNTPCT_EL0);
    nwoas_nvme_irq_eois++;
    nwoas_nvme_eoi_last_pc = pc;
}

void nwoas_nvme_dump_irq(void)
{
    printf("HVLOG: S163 IRQ gap_us=%u inject=%lu eoi=%lu gated=%lu "
           "last_inject_pc=%lx last_eoi_pc=%lx\n",
           (u32)NWOAS_NVME_REASSERT_US, nwoas_nvme_irq_injections,
           nwoas_nvme_irq_eois, nwoas_nvme_irq_gated,
           nwoas_nvme_irq_last_pc, nwoas_nvme_eoi_last_pc);
}
