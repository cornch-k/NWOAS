// chip.cpp - chip ID, EROM, AI core control, CR4 halt/start, watchdog reset,
// TCM size, OTP.  Ported from OpenBSD bwfm (bwfm.c bwfm_chip_*,
// if_bwfm_pci.c bwfm_pci_buscore_*) through the Python version that passed on
// the Mac mini (nwoas_scripts/wifi-s257/wifi_up.py, ISC; see core/nwbc_core.h).
#include "precomp.h"

static void Usleep(ULONG us)
{
    if (KeGetCurrentIrql() == PASSIVE_LEVEL && us >= 1000) {
        LARGE_INTEGER t;
        t.QuadPart = -(LONGLONG)us * 10;
        KeDelayExecutionThread(KernelMode, FALSE, &t);
    } else {
        KeStallExecutionProcessor(us);
    }
}

static nb_u32 ReadCb(void* ctx, nb_u32 addr)
{
    return NwoaBpRead32((PNWOA_DEVICE)ctx, addr);
}

const nb_core* NwoaCore(PNWOA_DEVICE Dev, USHORT Id, int Idx)
{
    return nb_core_find(Dev->Cores, Dev->NumCores, Id, Idx);
}

static void AiDisable(PNWOA_DEVICE Dev, const nb_core* c, ULONG prereset, ULONG reset)
{
    ULONG w = c->wrap;
    if ((NwoaBpRead32(Dev, w + NB_AGENT_RESET_CTL) & NB_RESET_CTL_RESET) == 0) {
        NwoaBpWrite32(Dev, w + NB_AGENT_IOCTL, prereset | NB_IOCTL_FGC | NB_IOCTL_CLK);
        (void)NwoaBpRead32(Dev, w + NB_AGENT_IOCTL);
        NwoaBpWrite32(Dev, w + NB_AGENT_RESET_CTL, NB_RESET_CTL_RESET);
        KeStallExecutionProcessor(20);
        for (int i = 0; i < 300; i++)
            if (NwoaBpRead32(Dev, w + NB_AGENT_RESET_CTL) == NB_RESET_CTL_RESET)
                break;
    }
    NwoaBpWrite32(Dev, w + NB_AGENT_IOCTL, reset | NB_IOCTL_FGC | NB_IOCTL_CLK);
    (void)NwoaBpRead32(Dev, w + NB_AGENT_IOCTL);
}

static void AiReset(PNWOA_DEVICE Dev, const nb_core* c, ULONG prereset, ULONG reset, ULONG postreset)
{
    ULONG w = c->wrap;
    AiDisable(Dev, c, prereset, reset);
    for (int i = 0; i < 50; i++) {
        if ((NwoaBpRead32(Dev, w + NB_AGENT_RESET_CTL) & NB_RESET_CTL_RESET) == 0)
            break;
        NwoaBpWrite32(Dev, w + NB_AGENT_RESET_CTL, 0);
        KeStallExecutionProcessor(60);
    }
    NwoaBpWrite32(Dev, w + NB_AGENT_IOCTL, postreset | NB_IOCTL_CLK);
    (void)NwoaBpRead32(Dev, w + NB_AGENT_IOCTL);
}

void NwoaChipPassive(PNWOA_DEVICE Dev)
{
    const nb_core* cr4 = NwoaCore(Dev, NB_CORE_CR4);
    if (!cr4)
        return;
    ULONG v = NwoaBpRead32(Dev, cr4->wrap + NB_AGENT_IOCTL);
    AiReset(Dev, cr4, v & NB_IOCTL_CR4_CPUHALT, NB_IOCTL_CR4_CPUHALT, NB_IOCTL_CR4_CPUHALT);
    for (int i = 0;; i++) {
        const nb_core* d11 = NwoaCore(Dev, NB_CORE_80211, i);
        if (!d11)
            break;
        AiDisable(Dev, d11, NB_D11_PHYRESET | NB_D11_PHYCLOCKEN, NB_D11_PHYCLOCKEN);
    }
}

void NwoaChipActive(PNWOA_DEVICE Dev, ULONG ResetVector)
{
    NwoaTcmWrite32(Dev, 0, ResetVector);                    // bwfm_pci_buscore_activate
    AiReset(Dev, NwoaCore(Dev, NB_CORE_CR4), NB_IOCTL_CR4_CPUHALT, 0, 0);
}

NTSTATUS NwoaChipAttach(PNWOA_DEVICE Dev)
{
    Dev->BpFault = 0;
    Dev->BpBadAddr = 0;
    Dev->EromBase = 0;
    Dev->NumCores = 0;                                      // allowlist: chipcommon only until the EROM is read
    NwoaWindowInvalidate(Dev);
    ULONG cid = NwoaBpRead32(Dev, NB_CHIP_BASE + NB_CC_CHIPID);
    Dev->ChipIdReg = cid;
    Dev->ChipRev = nb_chipid_rev(cid);
    NwoaDiagDword(Dev, L"NwoaChipId", cid);
    if (nb_chipid_chip(cid) != NB_BRCM_CC_4378 || nb_chipid_type(cid) != 1)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (Dev->ChipRev != 3)                                  // B1: firmware C-4378__s-B1 only
        return STATUS_NOT_SUPPORTED;
    ULONG erom = NwoaBpRead32(Dev, NB_CHIP_BASE + NB_CC_EROMPTR);
    NwoaDiagDword(Dev, L"NwoaEromPtr", erom);
    if (erom == 0xFFFFFFFF || (erom & 0xFFF00000u) != 0x18100000u)   // S259 log: 0x18106000
        return STATUS_DEVICE_DATA_ERROR;
    Dev->EromBase = erom;
    Dev->NumCores = 0;
    int n = 0;
    int rc = nb_erom_scan(ReadCb, Dev, erom, Dev->Cores, NWOA_MAX_CORES, &n);
    Dev->NumCores = n;
    NwoaDiagDword(Dev, L"NwoaCores", (ULONG)Dev->NumCores);
    if (rc != NB_OK || Dev->BpFault)
        return STATUS_DEVICE_DATA_ERROR;
    const nb_core* p2 = NwoaCore(Dev, NB_CORE_PCIE2);
    if (!p2 || !NwoaCore(Dev, NB_CORE_CC) || !NwoaCore(Dev, NB_CORE_CR4) || !NwoaCore(Dev, NB_CORE_GCI))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Dev->Pcie2Base = p2->base;
    Dev->Pcie2Rev = p2->rev;
    Dev->PciReg64 = p2->rev >= 64;
    NwoaDiagDword(Dev, L"NwoaPcie2Rev", p2->rev);
    return STATUS_SUCCESS;
}

// The rest of wifi_up.py _chip_id_and_cores, in its order, before any wrapper write:
//   for every EROM core (EROM order): read wrap+0x408 and wrap+0x800 (_ai_isup)
//   PCIe2 CONFIGADDR=0x00 / read CONFIGDATA, CONFIGADDR=0x80 / read CONFIGDATA (config mirror)
// Hardware 2026-11-18 13:48: the driver went straight from the EROM to the CR4 wrapper and
// that first wrapper read raised an SError; S257 had read the chipcommon and 802.11
// wrappers before it.  Which difference matters is not known: StopAfter bisects it.
NTSTATUS NwoaChipProbeLikeS257(PNWOA_DEVICE Dev)
{
    ULONG upMask = 0;
    for (int i = 0; i < Dev->NumCores && i < 32; i++) {
        const nb_core* c = &Dev->Cores[i];
        ULONG io = NwoaBpRead32(Dev, c->wrap + NB_AGENT_IOCTL);
        ULONG rs = NwoaBpRead32(Dev, c->wrap + NB_AGENT_RESET_CTL);
        if (Dev->BpFault)
            return STATUS_DEVICE_DATA_ERROR;
        if ((io & (NB_IOCTL_FGC | NB_IOCTL_CLK)) == NB_IOCTL_CLK && (rs & NB_RESET_CTL_RESET) == 0)
            upMask |= 1u << i;
    }
    NwoaDiagDword(Dev, L"NwoaCoresUpMask", upMask);        // S257: cc, cr4, pcie2, gci, pmu up
    // config mirror (S257 "config mirror via PCIe2 core ... match=True").  On 2026-11-18 14:05 the
    // second CONFIGADDR write did not take (Win read back the ID) and the first SError address
    // was this register (L2C_ERR_ADR ...6e0800120): both values are now checked and stop the
    // bring-up before any wrapper write.
    ULONG m0, m80, a0 = 0, a80 = 0;
    NwoaBpWrite32(Dev, Dev->Pcie2Base + NB_PCIE2_CONFIGADDR, 0x00);
    a0 = Dev->LastWriteBack;
    m0 = NwoaBpRead32(Dev, Dev->Pcie2Base + NB_PCIE2_CONFIGDATA);
    NwoaBpWrite32(Dev, Dev->Pcie2Base + NB_PCIE2_CONFIGADDR, NB_CFG_BAR0_WINDOW);
    a80 = Dev->LastWriteBack;
    m80 = NwoaBpRead32(Dev, Dev->Pcie2Base + NB_PCIE2_CONFIGDATA);
    NwoaDiagDword(Dev, L"NwoaCfgMirrorId", m0);            // S257: 0x442514e4
    NwoaDiagDword(Dev, L"NwoaCfgMirrorWin", m80);          // S257: 0x18001000 (the window itself)
    NwoaDiagDword(Dev, L"NwoaCfgAddrBack0", a0);
    NwoaDiagDword(Dev, L"NwoaCfgAddrBack80", a80);
    if (Dev->BpFault || m0 != 0x442514E4 || m80 != Dev->Pcie2Base ||
        (Dev->Cfg.PostWriteRead && (a0 != 0 || a80 != NB_CFG_BAR0_WINDOW))) {
        if (!Dev->BpFault) {
            InterlockedExchange(&Dev->BpFault, 1);
            Dev->BpBadAddr = Dev->Pcie2Base + NB_PCIE2_CONFIGADDR;
        }
        return STATUS_DEVICE_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NwoaChipHalt(PNWOA_DEVICE Dev)
{
    NwoaChipPassive(Dev);
    return Dev->BpFault ? STATUS_DEVICE_DATA_ERROR : STATUS_SUCCESS;
}

// chip watchdog reset (bwfm_pci_buscore_reset), TCM size, OTP.
NTSTATUS NwoaChipResetAndOtp(PNWOA_DEVICE Dev)
{
    const nb_core* cc = NwoaCore(Dev, NB_CORE_CC);
    const nb_core* cr4 = NwoaCore(Dev, NB_CORE_CR4);
    const nb_core* gci = NwoaCore(Dev, NB_CORE_GCI);
    ULONG mbox = Dev->PciReg64 ? NB_PCIE2_64_MAILBOXINT : NB_PCIE2_MAILBOXINT;

    // (no MAILBOXMASK write here: wifi_up.py never touches it before the firmware runs, and
    //  that write raised the SError on hardware 2026-11-18 13:29; the mask is written once the
    //  firmware is up, in NwoaRingsInit, exactly where wifi_scan.py writes it)
    // CR4 halt already done by NwoaChipHalt (S257: "CR4 halted (passive)" before the reset)
    ULONG lc = Dev->PcieCap ? Dev->PcieCap + 0x10 : NB_CFG_LINK_CTRL;
    ULONG lnk = NwoaCfgRead32(Dev, lc);
    NwoaCfgWrite32(Dev, lc, lnk & ~0x3u);                   // ASPM off around the reset
    NwoaBpWrite32(Dev, cc->base + NB_CC_WATCHDOG, 4);
    Usleep(100000);
    NwoaWindowInvalidate(Dev);
    NwoaCfgWrite32(Dev, lc, lnk);
    // core rev >= 14 keeps the config; the S257 run saw lost=False (rev 0x44)
    ULONG st = NwoaBpRead32(Dev, Dev->Pcie2Base + mbox);
    if (st != 0xFFFFFFFF)
        NwoaBpWrite32(Dev, Dev->Pcie2Base + mbox, st);
    ULONG cid = NwoaBpRead32(Dev, NB_CHIP_BASE + NB_CC_CHIPID);
    if (nb_chipid_chip(cid) != NB_BRCM_CC_4378 || Dev->BpFault)
        return STATUS_DEVICE_NOT_CONNECTED;
    NwoaChipPassive(Dev);

    ULONG cap = NwoaBpRead32(Dev, cr4->base + NB_CR4_CAP);
    ULONG nb = nb_cr4_nbanks(cap), size = 0;
    NwoaDiagDword(Dev, L"NwoaCr4Cap", cap);                // S257: 0x23117a6 (16 banks)
    for (ULONG i = 0; i < nb && i < 64; i++) {
        NwoaBpWrite32(Dev, cr4->base + NB_CR4_BANKIDX, i);
        if (Dev->Cfg.PostWriteRead && Dev->LastWriteBack != i) {
            // 2026-11-18 14:07: BANKIDX writes did not take, every read gave bank 0 (ramsize 0xe0000)
            InterlockedExchange(&Dev->BpFault, 1);
            Dev->BpBadAddr = cr4->base + NB_CR4_BANKIDX;
            break;
        }
        size += nb_cr4_bank_bytes(NwoaBpRead32(Dev, cr4->base + NB_CR4_BANKINFO));
    }
    Dev->RamBase = NB_RAMBASE_4378;
    Dev->RamSize = size;
    NwoaDiagDword(Dev, L"NwoaRamSize", size);
    // fail closed: the J274 chip measured 0x1ce000 (S257 PASS_FW_UP); anything else means the
    // backplane accesses are not doing what they should, so no firmware is written
    if (Dev->BpFault || size != 0x1CE000 || Dev->RamBase + size > Dev->Bar2Len)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    UCHAR otp[NB_OTP_WORDS_4378 * 2];
    for (ULONG i = 0; i < NB_OTP_WORDS_4378; i++)
        nb_st16(otp + 2 * i, NwoaBpRead16(Dev, gci->base + NB_OTP_BASE_4378 + 2 * i));
    nb_otp_parse(otp, sizeof(otp), &Dev->Otp);
    NwoaDiagSz(Dev, L"NwoaOtpModule", Dev->Otp.module);
    NwoaDiagSz(Dev, L"NwoaOtpVendor", Dev->Otp.vendor);
    NwoaDiagSz(Dev, L"NwoaOtpModrev", Dev->Otp.modrev);

    // bwfm_pci_preinit: CONFIGADDR 0x4e0 read/write back
    NwoaBpWrite32(Dev, Dev->Pcie2Base + NB_PCIE2_CONFIGADDR, NB_CFG_REG_BAR2_CONFIG);
    ULONG v = NwoaBpRead32(Dev, Dev->Pcie2Base + NB_PCIE2_CONFIGDATA);
    if (v != 0xFFFFFFFF)
        NwoaBpWrite32(Dev, Dev->Pcie2Base + NB_PCIE2_CONFIGDATA, v);
    return Dev->BpFault ? STATUS_DEVICE_DATA_ERROR : STATUS_SUCCESS;
}
