// hw.cpp - resources, PCI config, backplane window, TCM access, bus master.
//
// BAR0 (64 KiB): the first 4 KiB is a window onto the chip backplane, selected
// by PCI config register 0x80 (bwfm_pci_buscore_read/write).  After bring-up
// the window stays on the PCIe2 core so the mailbox/doorbell registers are at
// BAR0 + 0xC30/0xA20 (the "window" doorbell that worked on hardware 2026-11-18).
// BAR2 (8 MiB): chip RAM (TCM), firmware at 0x352000.
//
// Config space, in this order:
//  1. NWOAS control page (S259 contract research-20261118/wifi/CONTRACT-WIN.md):
//     ACPI\NWOA4378 with a third 16 KiB memory resource; CFG_OFF/CFG_DATA at
//     0x20/0x24 and the BAR0 window at 0x4C go through the hypervisor.
//  2. BUS_INTERFACE_STANDARD when Windows enumerated a PCI function.
//  3. the PCIe2 core CONFIGADDR/CONFIGDATA pair at BAR0 + 0x2000 + 0x120/0x124
//     [hypothesis: S257 read config through that pair *via the window* and it
//     matched ECAM; the fixed BAR0+0x2000 alias and writes are not verified].
#include "precomp.h"

// GUID_BUS_INTERFACE_STANDARD {496B8280-6F25-11D0-BEAF-08002BE2092F} (wdmguid.h; defined here
// so no initguid.h is needed)
static const GUID NwoaGuidBusInterfaceStandard =
    { 0x496b8280, 0x6f25, 0x11d0, { 0xbe, 0xaf, 0x08, 0x00, 0x2b, 0xe2, 0x09, 0x2f } };

NTSTATUS NwoaMapResources(PNWOA_DEVICE Dev, WDFCMRESLIST Raw, WDFCMRESLIST Trans)
{
    ULONG n = WdfCmResourceListGetCount(Trans);
    for (ULONG i = 0; i < n; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d = WdfCmResourceListGetDescriptor(Trans, i);
        PCM_PARTIAL_RESOURCE_DESCRIPTOR r = WdfCmResourceListGetDescriptor(Raw, i);
        if (!d)
            continue;
        if (d->Type == CmResourceTypeMemory || d->Type == CmResourceTypeMemoryLarge) {
            ULONGLONG len = RtlCmDecodeMemIoResource(d, NULL);
            PHYSICAL_ADDRESS pa = d->u.Memory.Start;
            NWOA_LOG("mem resource %u pa=%llx len=%llx", i, pa.QuadPart, len);
            if (len == 0x4000 && !Dev->CtlPage) {          // S259 control page (told apart by length)
                Dev->CtlPage = (PUCHAR)MmMapIoSpaceEx(pa, (SIZE_T)len, PAGE_READWRITE | PAGE_NOCACHE);
                Dev->CtlPageLen = (SIZE_T)len;
                NwoaDiagDword(Dev, L"NwoaCtlPagePaLo", (ULONG)pa.QuadPart);
            } else if (len >= 0x8000 && len <= 0x100000 && !Dev->Bar0) {
                Dev->Bar0 = (PUCHAR)MmMapIoSpaceEx(pa, (SIZE_T)len, PAGE_READWRITE | PAGE_NOCACHE);
                Dev->Bar0Len = (SIZE_T)len;
                NwoaDiagDword(Dev, L"NwoaBar0Len", (ULONG)len);
                NwoaDiagDword(Dev, L"NwoaBar0PaHi", (ULONG)(pa.QuadPart >> 32));
                NwoaDiagDword(Dev, L"NwoaBar0PaLo", (ULONG)pa.QuadPart);
            } else if (len >= 0x400000 && !Dev->Bar2) {
                Dev->Bar2 = (PUCHAR)MmMapIoSpaceEx(pa, (SIZE_T)len, PAGE_READWRITE | PAGE_NOCACHE);
                Dev->Bar2Len = (SIZE_T)len;
                NwoaDiagDword(Dev, L"NwoaBar2Len", (ULONG)len);
                NwoaDiagDword(Dev, L"NwoaBar2PaHi", (ULONG)(pa.QuadPart >> 32));
                NwoaDiagDword(Dev, L"NwoaBar2PaLo", (ULONG)pa.QuadPart);
            }
        } else if (d->Type == CmResourceTypeInterrupt && !Dev->HaveIntRes && r) {
            Dev->IntRaw = *r;
            Dev->IntTrans = *d;
            Dev->HaveIntRes = TRUE;
            NwoaDiagDword(Dev, L"NwoaIntFlags", d->Flags);
        }
    }
    NwoaDiagDword(Dev, L"NwoaHaveInterrupt", Dev->HaveIntRes);
    if (!Dev->Bar0 || !Dev->Bar2)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return STATUS_SUCCESS;
}

void NwoaUnmapResources(PNWOA_DEVICE Dev)
{
    if (Dev->CtlPage) { MmUnmapIoSpace(Dev->CtlPage, Dev->CtlPageLen); Dev->CtlPage = NULL; }
    Dev->UseCtlPage = FALSE;
    if (Dev->Bar0) { MmUnmapIoSpace(Dev->Bar0, Dev->Bar0Len); Dev->Bar0 = NULL; }
    if (Dev->Bar2) { MmUnmapIoSpace(Dev->Bar2, Dev->Bar2Len); Dev->Bar2 = NULL; }
    if (Dev->HaveBusIf && Dev->BusIf.InterfaceDereference) {
        Dev->BusIf.InterfaceDereference(Dev->BusIf.Context);
        Dev->HaveBusIf = FALSE;
    }
}

static ULONG MirrorRead(PNWOA_DEVICE Dev, ULONG Off)
{
    WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->Bar0 + NB_BAR0_PCIE_ENUM + NB_PCIE2_CONFIGADDR), Off);
    return READ_REGISTER_ULONG((volatile ULONG*)(Dev->Bar0 + NB_BAR0_PCIE_ENUM + NB_PCIE2_CONFIGDATA));
}

static void MirrorWrite(PNWOA_DEVICE Dev, ULONG Off, ULONG Val)
{
    WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->Bar0 + NB_BAR0_PCIE_ENUM + NB_PCIE2_CONFIGADDR), Off);
    WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->Bar0 + NB_BAR0_PCIE_ENUM + NB_PCIE2_CONFIGDATA), Val);
}

// control page: 32-bit accesses only; every access traps to the hypervisor (ms)
#define CTL_MAGIC     0x00
#define CTL_VERSION   0x04
#define CTL_STATE     0x08
#define CTL_ERROR     0x0C
#define CTL_IDS       0x1C
#define CTL_CFG_OFF   0x20
#define CTL_CFG_DATA  0x24
#define CTL_CFG_ERR   0x28
#define CTL_BAR0      0x2C
#define CTL_BAR2      0x30
#define CTL_BAR0WIN   0x4C

ULONG NwoaCtlRead(PNWOA_DEVICE Dev, ULONG Off)
{
    if (!Dev->CtlPage || Off + 4 > Dev->CtlPageLen)
        return 0xFFFFFFFF;
    return READ_REGISTER_ULONG((volatile ULONG*)(Dev->CtlPage + Off));
}

static void CtlWrite(PNWOA_DEVICE Dev, ULONG Off, ULONG Val)
{
    if (!Dev->CtlPage || Off + 4 > Dev->CtlPageLen || Off == 0x10)   // CMD (0x10) is ACPI only
        return;
    WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->CtlPage + Off), Val);
}

ULONG NwoaCfgRead32(PNWOA_DEVICE Dev, ULONG Off)
{
    ULONG v = 0xFFFFFFFF;
    if (Dev->UseCtlPage) {
        if (Off == NB_CFG_BAR0_WINDOW)
            return NwoaCtlRead(Dev, CTL_BAR0WIN);
        CtlWrite(Dev, CTL_CFG_OFF, Off);
        return NwoaCtlRead(Dev, CTL_CFG_DATA);
    }
    if (Dev->HaveBusIf && !Dev->CfgViaMirror) {
        if (Dev->BusIf.GetBusData(Dev->BusIf.Context, PCI_WHICHSPACE_CONFIG, &v, Off, 4) != 4)
            v = 0xFFFFFFFF;
        return v;
    }
    return MirrorRead(Dev, Off);
}

void NwoaCfgWrite32(PNWOA_DEVICE Dev, ULONG Off, ULONG Val)
{
    if (Dev->UseCtlPage) {
        if (Off == NB_CFG_BAR0_WINDOW) {
            CtlWrite(Dev, CTL_BAR0WIN, Val);
            return;
        }
        CtlWrite(Dev, CTL_CFG_OFF, Off);
        CtlWrite(Dev, CTL_CFG_DATA, Val);
        if (NwoaCtlRead(Dev, CTL_CFG_ERR))
            Dev->CfgErrors++;                               // refused by the S259 allow table
        return;
    }
    if (Dev->HaveBusIf && !Dev->CfgViaMirror) {
        Dev->BusIf.SetBusData(Dev->BusIf.Context, PCI_WHICHSPACE_CONFIG, &Val, Off, 4);
        return;
    }
    MirrorWrite(Dev, Off, Val);
}

NTSTATUS NwoaConfigInit(PNWOA_DEVICE Dev)
{
    KeInitializeSpinLock(&Dev->WinLock);
    Dev->WindowValid = FALSE;
    if (Dev->CtlPage) {
        // contract 6.1: check MAGIC/VERSION/STATE/BARs before touching the BARs
        ULONG magic = NwoaCtlRead(Dev, CTL_MAGIC), ver = NwoaCtlRead(Dev, CTL_VERSION);
        ULONG state = NwoaCtlRead(Dev, CTL_STATE), err = NwoaCtlRead(Dev, CTL_ERROR);
        ULONG b0 = NwoaCtlRead(Dev, CTL_BAR0), b2 = NwoaCtlRead(Dev, CTL_BAR2), ids = NwoaCtlRead(Dev, CTL_IDS);
        NwoaDiagDword(Dev, L"NwoaCtlMagic", magic);
        NwoaDiagDword(Dev, L"NwoaCtlState", state);
        NwoaDiagDword(Dev, L"NwoaCtlError", err);
        NwoaDiagDword(Dev, L"NwoaCtlBar0", b0);
        NwoaDiagDword(Dev, L"NwoaCtlBar2", b2);
        NwoaDiagDword(Dev, L"NwoaCtlIds", ids);
        if (magic != 0x4657574E || ver != 1 || state != 1 || !b0 || !b2 || ids != 0x442514E4)
            return STATUS_DEVICE_NOT_READY;                 // chip not powered/linked: BARs untouched
        Dev->UseCtlPage = TRUE;
        NwoaDiagDword(Dev, L"NwoaCfgVia", 1);
    } else {
        NTSTATUS st = WdfFdoQueryForInterface(Dev->Device, &NwoaGuidBusInterfaceStandard,
                                          (PINTERFACE)&Dev->BusIf, sizeof(BUS_INTERFACE_STANDARD), 1, NULL);
        ULONG id;
        Dev->HaveBusIf = NT_SUCCESS(st);
        Dev->CfgViaMirror = FALSE;
        id = Dev->HaveBusIf ? NwoaCfgRead32(Dev, 0) : 0xFFFFFFFF;
        if (id != 0x442514E4) {
            ULONG m = MirrorRead(Dev, 0);
            NwoaDiagDword(Dev, L"NwoaCfgIdBus", id);
            NwoaDiagDword(Dev, L"NwoaCfgIdMirror", m);
            if (m != 0x442514E4)
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            Dev->CfgViaMirror = TRUE;
        }
        NwoaDiagDword(Dev, L"NwoaCfgVia", Dev->CfgViaMirror ? 3 : 2);
    }
    NwoaDiagDword(Dev, L"NwoaCfgId", NwoaCfgRead32(Dev, 0));
    // PCIe capability: walk the list (S240: 0xAC on this chip)
    {
        ULONG p = (NwoaCfgRead32(Dev, 0x34) & 0xFC), guard = 0;
        Dev->PcieCap = 0;
        while (p >= 0x40 && p < 0x100 && guard++ < 48) {
            ULONG h = NwoaCfgRead32(Dev, p);
            if ((h & 0xFF) == 0x10) { Dev->PcieCap = p; break; }
            p = (h >> 8) & 0xFC;
        }
        NwoaDiagDword(Dev, L"NwoaPcieCap", Dev->PcieCap);
    }
    return STATUS_SUCCESS;
}

void NwoaWindowInvalidate(PNWOA_DEVICE Dev) { Dev->WindowValid = FALSE; }

// Backplane allowlist = exactly what nwoas_scripts/wifi-s257/wifi_up.py touched before
// the firmware runs (chip/otp/fw stages, clean on hardware 2026-11-18):
//   chipcommon page 0x18000000: reads (CHIPID, EROMPTR, CAPS), write WATCHDOG only
//   EROM pages: reads only
//   CR4 core: CAP/BANKINFO reads, BANKIDX write; CR4 + 802.11 wrappers: IOCTL 0x408, RESET_CTL 0x800
//   PCIe2 core: CONFIGADDR/CONFIGDATA 0x120/0x124, MAILBOXINT 0x048/0xC30 (read + write-back)
//   GCI core (2 pages): reads only (OTP shadow at +0x1120)
// Hardware 2026-11-18 13:29: one write the script never made (PCIe2 MAILBOXMASK 0xC34 before
// the firmware ran, L2C_ERR_ADR ...6e0800c34) raised an SError and every backplane access
// after it faulted too.  Anything outside this table is refused and counted in BpFault.
static BOOLEAN BpAllowed(PNWOA_DEVICE Dev, ULONG Addr, BOOLEAN Write)
{
    ULONG page = Addr & ~(NB_BAR0_WINDOW_SIZE - 1), off = Addr & (NB_BAR0_WINDOW_SIZE - 1);
    if (page == NB_CHIP_BASE)
        return !Write || off == NB_CC_WATCHDOG;
    // S257 _ai_isup read IOCTL/RESET_CTL of every EROM core, wrap 0 included (page 0x0)
    if (!Write && (off == NB_AGENT_IOCTL || off == NB_AGENT_RESET_CTL)) {
        for (int i = 0; i < Dev->NumCores; i++)
            if (page == Dev->Cores[i].wrap)
                return TRUE;
    }
    if (Dev->EromBase && page >= (Dev->EromBase & ~0xFFFu) && page < (Dev->EromBase & ~0xFFFu) + 0x4000)
        return !Write;
    for (int i = 0; i < Dev->NumCores; i++) {
        const nb_core* c = &Dev->Cores[i];
        if (c->wrap && page == c->wrap && (c->id == NB_CORE_CR4 || c->id == NB_CORE_80211))
            return off == NB_AGENT_IOCTL || off == NB_AGENT_RESET_CTL;
        if (!c->base)
            continue;
        if (c->id == NB_CORE_CR4 && page == c->base)
            return Write ? off == NB_CR4_BANKIDX : (off == NB_CR4_CAP || off == NB_CR4_BANKINFO);
        if (c->id == NB_CORE_PCIE2 && page == c->base)
            return off == NB_PCIE2_CONFIGADDR || off == NB_PCIE2_CONFIGDATA ||
                   off == (ULONG)(Dev->PciReg64 ? NB_PCIE2_64_MAILBOXINT : NB_PCIE2_MAILBOXINT);
        if (c->id == NB_CORE_GCI && (page == c->base || page == c->base + 0x1000))
            return !Write;
    }
    return FALSE;
}

static void BpRefuse(PNWOA_DEVICE Dev, ULONG Addr)
{
    if (InterlockedIncrement(&Dev->BpFault) == 1)
        Dev->BpBadAddr = Addr;
}

// Select the window; FALSE (and no access) unless the read-back shows the page.
static BOOLEAN BpWindow(PNWOA_DEVICE Dev, ULONG Addr)
{
    ULONG page = Addr & ~(NB_BAR0_WINDOW_SIZE - 1);
    if (Dev->WindowValid && Dev->CurWindow == page)
        return TRUE;
    Dev->WindowValid = FALSE;
    for (int i = 0; i < 2; i++) {
        NwoaCfgWrite32(Dev, NB_CFG_BAR0_WINDOW, page);
        if (NwoaCfgRead32(Dev, NB_CFG_BAR0_WINDOW) == page) {
            Dev->CurWindow = page;
            Dev->WindowValid = TRUE;
            return TRUE;
        }
    }
    BpRefuse(Dev, Addr | 1);                                 // bit 0 = window not confirmed
    return FALSE;
}

// Backplane access is only used at PASSIVE during bring-up (config through the
// bus interface needs <= DISPATCH; the spin lock keeps window + access atomic).
// Once BpFault is set every further access is refused (fail closed).
static void BpDelay(PNWOA_DEVICE Dev)
{
    if (Dev->Cfg.AccessDelayUs)
        KeStallExecutionProcessor(Dev->Cfg.AccessDelayUs);
}

ULONG NwoaBpRead32(PNWOA_DEVICE Dev, ULONG Addr)
{
    KIRQL irql;
    ULONG v = 0xFFFFFFFF;
    if (Dev->BpFault || !BpAllowed(Dev, Addr, FALSE)) { BpRefuse(Dev, Addr); return v; }
    KeAcquireSpinLock(&Dev->WinLock, &irql);
    BpDelay(Dev);
    if (BpWindow(Dev, Addr))
        v = READ_REGISTER_ULONG((volatile ULONG*)(Dev->Bar0 + (Addr & (NB_BAR0_WINDOW_SIZE - 1))));
    KeReleaseSpinLock(&Dev->WinLock, irql);
    return v;
}

void NwoaBpWrite32(PNWOA_DEVICE Dev, ULONG Addr, ULONG Val)
{
    KIRQL irql;
    if (Dev->BpFault || !BpAllowed(Dev, Addr, TRUE)) { BpRefuse(Dev, Addr); return; }
    KeAcquireSpinLock(&Dev->WinLock, &irql);
    BpDelay(Dev);
    if (BpWindow(Dev, Addr)) {
        volatile ULONG* r = (volatile ULONG*)(Dev->Bar0 + (Addr & (NB_BAR0_WINDOW_SIZE - 1)));
        WRITE_REGISTER_ULONG(r, Val);
        // a posted write must reach the chip before the window moves on (the window is changed
        // through the hypervisor control page, not through this mapping)
        // (not after the chipcommon watchdog write: the chip resets a few ticks later)
        if (Dev->Cfg.PostWriteRead && Addr != NB_CHIP_BASE + NB_CC_WATCHDOG)
            Dev->LastWriteBack = READ_REGISTER_ULONG(r);
    }
    KeReleaseSpinLock(&Dev->WinLock, irql);
}

USHORT NwoaBpRead16(PNWOA_DEVICE Dev, ULONG Addr)
{
    KIRQL irql;
    USHORT v = 0xFFFF;
    if (Dev->BpFault || !BpAllowed(Dev, Addr, FALSE)) { BpRefuse(Dev, Addr); return v; }
    KeAcquireSpinLock(&Dev->WinLock, &irql);
    BpDelay(Dev);
    if (BpWindow(Dev, Addr))
        v = READ_REGISTER_USHORT((volatile USHORT*)(Dev->Bar0 + (Addr & (NB_BAR0_WINDOW_SIZE - 1))));
    KeReleaseSpinLock(&Dev->WinLock, irql);
    return v;
}

// Park the window on the PCIe2 core (after the firmware is up, as wifi_scan.py did).
BOOLEAN NwoaSelectPcie2(PNWOA_DEVICE Dev)
{
    KIRQL irql;
    BOOLEAN ok;
    if (Dev->BpFault || !Dev->Pcie2Base)
        return FALSE;
    KeAcquireSpinLock(&Dev->WinLock, &irql);
    ok = BpWindow(Dev, Dev->Pcie2Base);
    KeReleaseSpinLock(&Dev->WinLock, irql);
    return ok;
}

// PCIe2 core register at BAR0 + Off.  Valid once NwoaSelectPcie2 ran (the
// window is not moved again after bring-up).  Safe at any IRQL.
ULONG NwoaRegRead(PNWOA_DEVICE Dev, ULONG Off)
{
    return READ_REGISTER_ULONG((volatile ULONG*)(Dev->Bar0 + Off));
}

void NwoaRegWrite(PNWOA_DEVICE Dev, ULONG Off, ULONG Val)
{
    WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->Bar0 + Off), Val);
}

ULONG NwoaTcmRead32(PNWOA_DEVICE Dev, ULONG Off)
{
    if (Off + 4 > Dev->Bar2Len)
        return 0xFFFFFFFF;
    return READ_REGISTER_ULONG((volatile ULONG*)(Dev->Bar2 + Off));
}

void NwoaTcmWrite32(PNWOA_DEVICE Dev, ULONG Off, ULONG Val)
{
    if (Off + 4 > Dev->Bar2Len)
        return;
    WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->Bar2 + Off), Val);
}

NTSTATUS NwoaTcmWriteBuf(PNWOA_DEVICE Dev, ULONG Off, const UCHAR* Buf, ULONG Len)
{
    if ((Off & 3) || (ULONGLONG)Off + Len > Dev->Bar2Len)
        return STATUS_INVALID_PARAMETER;
    ULONG i = 0;
    for (; i + 4 <= Len; i += 4)
        WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->Bar2 + Off + i), nb_ld32(Buf + i));
    if (i < Len) {
        UCHAR t[4] = { 0 };
        RtlCopyMemory(t, Buf + i, Len - i);
        WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->Bar2 + Off + i), nb_ld32(t));
    }
    return STATUS_SUCCESS;
}

NTSTATUS NwoaTcmReadBuf(PNWOA_DEVICE Dev, ULONG Off, PUCHAR Buf, ULONG Len)
{
    ULONG a0 = Off & ~3u;
    if ((ULONGLONG)Off + Len > Dev->Bar2Len)
        return STATUS_INVALID_PARAMETER;
    for (ULONG p = a0; p < Off + Len; p += 4) {
        ULONG v = READ_REGISTER_ULONG((volatile ULONG*)(Dev->Bar2 + p));
        for (ULONG k = 0; k < 4; k++) {
            ULONG a = p + k;
            if (a >= Off && a < Off + Len)
                Buf[a - Off] = (UCHAR)(v >> (8 * k));
        }
    }
    return STATUS_SUCCESS;
}

// wifi-s257 / plan-20261115 wifi.md 4.6: No Snoop and Relaxed Ordering off before DMA.
void NwoaFixDevCtl(PNWOA_DEVICE Dev)
{
    if (!Dev->PcieCap || !Dev->Cfg.ClearNoSnoop)
        return;
    ULONG v = NwoaCfgRead32(Dev, Dev->PcieCap + 8);
    NwoaDiagDword(Dev, L"NwoaDevCtlBefore", v & 0xFFFF);
    // upper half = DevSta (write 1 to clear): write 0 there
    NwoaCfgWrite32(Dev, Dev->PcieCap + 8, (v & 0xFFFF) & ~0x0810u);
    NwoaDiagDword(Dev, L"NwoaDevCtlAfter", NwoaCfgRead32(Dev, Dev->PcieCap + 8) & 0xFFFF);
}

// Memory decode + bus master.  Windows' PCI driver normally sets these; the
// explicit write also covers the ACPI-node case (config through the mirror).
void NwoaBusMaster(PNWOA_DEVICE Dev, BOOLEAN On)
{
    ULONG v = NwoaCfgRead32(Dev, 0x04) & 0xFFFF;
    ULONG nv = On ? (v | 0x6) : ((v & ~0x4u) | 0x2);
    if (nv != v)
        NwoaCfgWrite32(Dev, 0x04, nv);
    Dev->BusMasterOn = On;
    NwoaDiagDword(Dev, L"NwoaPciCommand", NwoaCfgRead32(Dev, 0x04) & 0xFFFF);
}
