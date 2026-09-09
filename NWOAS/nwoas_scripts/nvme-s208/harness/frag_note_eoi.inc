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
