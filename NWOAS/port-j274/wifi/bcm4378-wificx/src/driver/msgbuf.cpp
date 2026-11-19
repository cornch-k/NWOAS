// msgbuf.cpp - PCIe msgbuf protocol: DMA memory, rings, ioctl/iovar, message
// dispatch, TX flowring, RX buffers, interrupt and poll timer.
// From OpenBSD if_bwfm_pci.c (ISC) through nwoas_scripts/wifi-s257/wifi_scan.py,
// which ran it on hardware (PASS_SCAN 2026-11-18 12:13).  Layout and encoders
// are in core/nwbc_core.c (unit tested).
#include "precomp.h"

#define ID_IOCTLRESP 0x1000
#define ID_EVENT     0x2000
#define ID_RX        0x3000
#define N_RX_WANT    256

static void Dbell(PNWOA_DEVICE Dev)
{
    InterlockedIncrement(&Dev->CntDbell);                  // s258o: counted
    NwoaRegWrite(Dev, Dev->PciReg64 ? NB_PCIE2_64_H2D_MB0 : NB_PCIE2_H2D_MB0, 1);
}

// ------------------------------------------------------------ DMA memory
// Device addresses.  Hardware 2026-11-18 14:2x: with every buffer in high RAM (4..48 GiB,
// identity-mapped by the UEFI WIDE-DART table) the firmware reported "H2D DMA data transfer
// error" right after start.  What is proven on this port is a 32-bit IOVA: S257 PASS_SCAN used
// IOVA 0x10000000, the ADT dart-apcie0 window is vm-base 0x100000 + vm-size 0xBFE00000, and the
// FL1100 on the same DART table is kept to 32-bit DMA (S259 README, NWOAS_FORCE_FL32).  The UEFI
// table maps IOVA [0, 4 GiB) onto the guest low window (IOVA = guest PA there, _TRA 0), so:
//   DmaMode 0 (default): non-cached contiguous RAM with guest PA in [1 MiB, 0xC0000000)
//                        (inside the ADT window, below the PCIe bridge windows and the MSI page)
//   DmaMode 1: WDF common buffer wherever Windows puts it
//   DmaMode 2: non-cached contiguous RAM in [4 GiB, 48 GiB) (the s258e behaviour)
#define DMA_LO32_LOW   0x00100000ull
#define DMA_LO32_HIGH  0xC0000000ull
#define DMA_HI_LOW     0x100000000ull
#define DMA_HI_HIGH    0xC00000000ull

static void FreeCb(NWOA_DMABUF* B)
{
    if (B->Cb)
        WdfObjectDelete(B->Cb);
    if (B->Contig)
        MmFreeContiguousMemorySpecifyCache(B->Contig, B->Len, MmNonCached);
    RtlZeroMemory(B, sizeof(*B));
}

static NTSTATUS AllocCb(PNWOA_DEVICE Dev, SIZE_T Len, NWOA_DMABUF* B)
{
    NTSTATUS st;
    RtlZeroMemory(B, sizeof(*B));
    if (Dev->Cfg.DmaMode == 1) {
        WDF_COMMON_BUFFER_CONFIG cfg;
        WDF_COMMON_BUFFER_CONFIG_INIT(&cfg, 4095);          // 4 KiB aligned
        st = WdfCommonBufferCreateWithConfig(Dev->DmaEnabler, Len, &cfg, WDF_NO_OBJECT_ATTRIBUTES, &B->Cb);
        if (!NT_SUCCESS(st))
            return st;
        B->Va = (PUCHAR)WdfCommonBufferGetAlignedVirtualAddress(B->Cb);
        B->La = (ULONG64)WdfCommonBufferGetAlignedLogicalAddress(B->Cb).QuadPart;
        B->Len = Len;
        RtlZeroMemory(B->Va, Len);
        return STATUS_SUCCESS;
    }
    PHYSICAL_ADDRESS lo, hi, bnd;
    BOOLEAN high = Dev->Cfg.DmaMode == 2;
    lo.QuadPart = (LONGLONG)(high ? DMA_HI_LOW : DMA_LO32_LOW);
    hi.QuadPart = (LONGLONG)((high ? DMA_HI_HIGH : DMA_LO32_HIGH) - 1);
    bnd.QuadPart = 0;
    B->Contig = MmAllocateContiguousMemorySpecifyCache(Len, lo, hi, bnd, MmNonCached);
    if (!B->Contig)
        return STATUS_INSUFFICIENT_RESOURCES;
    B->Va = (PUCHAR)B->Contig;
    B->La = (ULONG64)MmGetPhysicalAddress(B->Contig).QuadPart;
    B->Len = Len;
    if (B->La < (ULONG64)lo.QuadPart || B->La + Len > (ULONG64)hi.QuadPart + 1) {
        FreeCb(B);                                           // never hand the chip an address outside the range
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(B->Va, Len);
    return STATUS_SUCCESS;
}

NTSTATUS NwoaDmaInit(PNWOA_DEVICE Dev)
{
    NTSTATUS st;
    if (!Dev->DmaEnabler) {
        WDF_DMA_ENABLER_CONFIG dc;
        WDF_DMA_ENABLER_CONFIG_INIT(&dc, WdfDmaProfileScatterGather64Duplex, 64 * 1024);
        st = WdfDmaEnablerCreate(Dev->Device, &dc, WDF_NO_OBJECT_ATTRIBUTES, &Dev->DmaEnabler);
        if (!NT_SUCCESS(st))
            return st;
    }
    if (nb_dma_layout(&Dev->Sh, 4096, &Dev->L) != NB_OK)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Dev->NumRx = min((ULONG)N_RX_WANT, Dev->Sh.max_rxbufpost);
    st = AllocCb(Dev, Dev->L.size, &Dev->Ctl);
    if (NT_SUCCESS(st))
        st = AllocCb(Dev, (SIZE_T)Dev->NumRx * NWOA_SLOT_SIZE, &Dev->RxPool);
    if (NT_SUCCESS(st))
        st = AllocCb(Dev, (SIZE_T)NWOA_TX_SLOTS * NWOA_SLOT_SIZE, &Dev->TxPool);
    NwoaDiagDword(Dev, L"NwoaDmaStatus", (ULONG)st);
    if (NT_SUCCESS(st)) {
        NwoaDiagDword(Dev, L"NwoaDmaCtlLaHi", (ULONG)(Dev->Ctl.La >> 32));
        NwoaDiagDword(Dev, L"NwoaDmaCtlLaLo", (ULONG)Dev->Ctl.La);
        NwoaDiagDword(Dev, L"NwoaDmaCtlLen", (ULONG)Dev->Ctl.Len);
        NwoaDiagDword(Dev, L"NwoaDmaContig", (Dev->Ctl.Contig ? 1 : 0) | (Dev->RxPool.Contig ? 2 : 0) | (Dev->TxPool.Contig ? 4 : 0));
        NwoaDiagDword(Dev, L"NwoaDmaRxLaHi", (ULONG)(Dev->RxPool.La >> 32));
        NwoaDiagDword(Dev, L"NwoaDmaRxLaLo", (ULONG)Dev->RxPool.La);
        NwoaDiagDword(Dev, L"NwoaDmaTxLaHi", (ULONG)(Dev->TxPool.La >> 32));
        NwoaDiagDword(Dev, L"NwoaDmaTxLaLo", (ULONG)Dev->TxPool.La);
        NwoaDiagDword(Dev, L"NwoaDmaModeUsed", Dev->Cfg.DmaMode);
    }
    return st;
}

void NwoaDmaFree(PNWOA_DEVICE Dev)
{
    FreeCb(&Dev->Ctl);
    FreeCb(&Dev->RxPool);
    FreeCb(&Dev->TxPool);
}

// ----------------------------------------------------------------- indices
// which: 0 h2d_w, 1 h2d_r, 2 d2h_w, 3 d2h_r
static ULONG IdxGet(PNWOA_DEVICE Dev, ULONG which, ULONG slot)
{
    if (Dev->Sh.idx_sz) {
        PUCHAR p = Dev->Ctl.Va + nb_idx_off(&Dev->L, &Dev->Sh, which, slot);
        if (Dev->Sh.idx_sz == 4)
            return *(volatile ULONG*)p & 0xFFFF;
        return *(volatile USHORT*)p;
    }
    return NwoaTcmRead32(Dev, Dev->Sh.tcm_idx[which] + slot * 4) & 0xFFFF;
}

static void IdxSet(PNWOA_DEVICE Dev, ULONG which, ULONG slot, ULONG v)
{
    if (Dev->Sh.idx_sz) {
        PUCHAR p = Dev->Ctl.Va + nb_idx_off(&Dev->L, &Dev->Sh, which, slot);
        if (Dev->Sh.idx_sz == 4)
            *(volatile ULONG*)p = v;
        else
            *(volatile USHORT*)p = (USHORT)v;
        return;
    }
    NwoaTcmWrite32(Dev, Dev->Sh.tcm_idx[which] + slot * 4, v);
}

// H2D: write items, publish w, ring.  Caller holds RingLock.
static NTSTATUS Submit(PNWOA_DEVICE Dev, NWOA_RING* R, const UCHAR* Items, ULONG Count)
{
    if (!Dev->RingsUp || !Dev->Ctl.Va)
        return STATUS_DEVICE_NOT_READY;
    ULONG r = IdxGet(Dev, 1, R->D.idx);
    if (r >= R->D.nitem) {
        InterlockedExchange(&Dev->Dead, 1);
        return STATUS_DEVICE_DATA_ERROR;
    }
    R->RPtr = r;
    if (nb_ring_write_space(r, R->WPtr, R->D.nitem) < Count)
        return STATUS_DEVICE_BUSY;
    for (ULONG i = 0; i < Count; i++) {
        RtlCopyMemory(R->Va + (SIZE_T)R->WPtr * R->D.itemsz, Items + (SIZE_T)i * R->D.itemsz, R->D.itemsz);
        R->WPtr = (R->WPtr + 1) % R->D.nitem;
    }
    KeMemoryBarrier();
    IdxSet(Dev, 0, R->D.idx, R->WPtr);
    KeMemoryBarrier();
    Dbell(Dev);
    if (R == &Dev->Flow)
        InterlockedIncrement(&Dev->CntFlowDbell);          // s258o: doorbells after a connected scan
    return STATUS_SUCCESS;
}

static NTSTATUS SubmitCtl(PNWOA_DEVICE Dev, const UCHAR* Item)
{
    return Submit(Dev, &Dev->Ring[NB_RING_CTRL_SUBMIT], Item, 1);
}

static void PostCtlBuf(PNWOA_DEVICE Dev, ULONG Id)
{
    UCHAR it[64];
    ULONG off;
    UCHAR type;
    if (Id >= ID_EVENT && Id < ID_EVENT + NB_N_EVENT) {
        off = Dev->L.event_off + (Id - ID_EVENT) * NB_CTL_PKT_SIZE;
        type = NB_MSG_EVENT_BUF_POST;
    } else {
        off = Dev->L.ioctl_resp_off + (Id - ID_IOCTLRESP) * NB_CTL_PKT_SIZE;
        type = NB_MSG_IOCTLRESP_BUF_POST;
    }
    nb_enc_buf_post(it, Dev->Ring[NB_RING_CTRL_SUBMIT].D.itemsz, type, Id, NB_CTL_PKT_SIZE, Dev->Ctl.La + off);
    // s258n: up to s258m a failed post (ring full) lost the buffer for good; brcmfmac counts
    // its posted buffers and refills (brcmf_msgbuf_rxbuf_ctrl_post).  Retried in RetryPending.
    ULONG bit = (Id >= ID_EVENT) ? 8 + (Id - ID_EVENT) : (Id - ID_IOCTLRESP);
    if (NT_SUCCESS(SubmitCtl(Dev, it))) {
        Dev->CtlPend &= ~(1u << bit);
    } else if (Dev->RingsUp) {
        if (!(Dev->CtlPend & (1u << bit)))
            InterlockedIncrement(&Dev->CntCtlPostFail);
        Dev->CtlPend |= 1u << bit;
    }
}

static BOOLEAN RxRepostOnce(PNWOA_DEVICE Dev, USHORT Slot)
{
    UCHAR it[64];
    nb_enc_rxbuf_post(it, Dev->Ring[NB_RING_RXPOST].D.itemsz, ID_RX + Slot, NWOA_SLOT_SIZE,
                      Dev->RxPool.La + (ULONG64)Slot * NWOA_SLOT_SIZE);
    return NT_SUCCESS(Submit(Dev, &Dev->Ring[NB_RING_RXPOST], it, 1));
}

// caller holds RingLock.  s258n: a buffer whose repost fails is kept in RxPend and retried
// (brcmfmac brcmf_msgbuf_rxbuf_data_fill refills up to max_rxbufpost) instead of being lost.
void NwoaRxRepost(PNWOA_DEVICE Dev, USHORT Slot)
{
    if (Slot >= NWOA_RX_SLOTS_MAX)
        return;
    if (RxRepostOnce(Dev, Slot)) {
        Dev->RxPend[Slot / 32] &= ~(1u << (Slot % 32));
        return;
    }
    if (Dev->RingsUp) {
        InterlockedIncrement(&Dev->CntRxRepostFail);
        Dev->RxPend[Slot / 32] |= 1u << (Slot % 32);
    }
}

// caller holds RingLock
static void RetryPending(PNWOA_DEVICE Dev)
{
    for (ULONG w = 0; w < NWOA_RX_SLOTS_MAX / 32; w++) {
        ULONG m = Dev->RxPend[w];
        for (ULONG b = 0; m && b < 32; b++) {
            if (!(m & (1u << b)))
                continue;
            if (!RxRepostOnce(Dev, (USHORT)(w * 32 + b)))
                return;                                       // ring still full: next poll
            m &= ~(1u << b);
            Dev->RxPend[w] = m;
            InterlockedIncrement(&Dev->CntRxRepostRetry);
        }
    }
    if (Dev->CtlPend) {
        for (ULONG b = 0; b < 16; b++) {
            if (!(Dev->CtlPend & (1u << b)))
                continue;
            ULONG id = b < 8 ? ID_IOCTLRESP + b : ID_EVENT + (b - 8);
            PostCtlBuf(Dev, id);
            if (Dev->CtlPend & (1u << b))
                return;
            InterlockedIncrement(&Dev->CntCtlPostRetry);
        }
    }
}

// EAPOL received on the data path and again in an E_EAPOL_MSG event is the same frame twice
// (hardware 2026-11-18: CntEapolRx = 2 x CntEapolEv).  Counts the message type (flags only)
// and returns TRUE for a copy already delivered in the last 2 s.
BOOLEAN NwoaEapolRxDup(PNWOA_DEVICE Dev, const UCHAR* Eth, ULONG Len)
{
    int k = nb_eapol_key_msg(Eth, Len);
    ULONG crc = Len > 14 ? nb_crc32(Eth + 14, Len - 14) : 0;
    ULONG64 now = KeQueryInterruptTime();
    for (int i = 0; i < 4; i++)
        if (Dev->EapolRecent[i].Len == Len && Dev->EapolRecent[i].Crc == crc && now - Dev->EapolRecent[i].T < 20000000ull) {
            InterlockedIncrement(&Dev->CntEapolDup);
            return TRUE;
        }
    Dev->EapolRecent[Dev->EapolRecentNext % 4].Crc = crc;
    Dev->EapolRecent[Dev->EapolRecentNext % 4].Len = Len;
    Dev->EapolRecent[Dev->EapolRecentNext % 4].T = now;
    Dev->EapolRecentNext++;
    if (k >= 0 && k < 8)
        InterlockedIncrement(&Dev->EapolRxMsg[k]);
    InterlockedIncrement(&Dev->CntEapolRx);
    return FALSE;
}

void NwoaRxItemDone(PNWOA_DEVICE Dev, const NWOA_RXITEM* It)
{
    if (It->Local)
        Dev->EvRxBusy &= ~(1u << It->Slot);
    else if (Dev->RingsUp)
        NwoaRxRepost(Dev, It->Slot);
}

// Frames held for a queue that never came: give the buffers back to the firmware.
void NwoaRxFlushHeld(PNWOA_DEVICE Dev)
{
    KIRQL irql;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    if (!Dev->RxQueue) {
        while (Dev->RxQHead != Dev->RxQTail) {
            NwoaRxItemDone(Dev, &Dev->RxQ[Dev->RxQHead]);
            Dev->RxQHead = (Dev->RxQHead + 1) % NWOA_RXQ;
        }
    }
    KeReleaseSpinLock(&Dev->RingLock, irql);
}

// ------------------------------------------------------------- ring setup
NTSTATUS NwoaRingsInit(PNWOA_DEVICE Dev)
{
    nb_shared* s = &Dev->Sh;
    ULONG ri = s->ring_info, sh = Dev->SharedAddr;
    KIRQL irql;

    for (int k = 0; k < NB_NUM_COMMON_RINGS; k++) {
        NWOA_RING* R = &Dev->Ring[k];
        R->D = Dev->L.ring[k];
        R->Va = Dev->Ctl.Va + R->D.off;
        R->La = Dev->Ctl.La + R->D.off;
        R->WPtr = R->RPtr = 0;
    }
    Dev->Flow.D = Dev->L.flow[0];
    Dev->Flow.Va = Dev->Ctl.Va + Dev->Flow.D.off;
    Dev->Flow.La = Dev->Ctl.La + Dev->Flow.D.off;
    Dev->FlowState = 0;
    if (s->idx_sz) {
        for (ULONG k = 0; k < 4; k++) {
            ULONG64 a = Dev->Ctl.La + Dev->L.idx_off + k * Dev->L.idx_stride;
            NwoaTcmWrite32(Dev, ri + NB_RI_HOSTADDR + 8 * k, (ULONG)a);
            NwoaTcmWrite32(Dev, ri + NB_RI_HOSTADDR + 8 * k + 4, (ULONG)(a >> 32));
        }
    }
    BOOLEAN ok = TRUE;
    for (int k = 0; k < NB_NUM_COMMON_RINGS; k++) {
        NWOA_RING* R = &Dev->Ring[k];
        ULONG d = s->ringmem + NB_RING_MEM_SZ * k;
        ULONG mi = R->D.nitem | ((ULONG)R->D.itemsz << 16);
        NwoaTcmWrite32(Dev, d + NB_RING_MAX_ITEM, mi);
        NwoaTcmWrite32(Dev, d + NB_RING_BASE_LO, (ULONG)R->La);
        NwoaTcmWrite32(Dev, d + NB_RING_BASE_LO + 4, (ULONG)(R->La >> 32));
        if (!s->idx_sz) {
            IdxSet(Dev, R->D.is_h2d ? 0 : 2, R->D.idx, 0);
            IdxSet(Dev, R->D.is_h2d ? 1 : 3, R->D.idx, 0);
        }
        ok = ok && NwoaTcmRead32(Dev, d + NB_RING_MAX_ITEM) == mi &&
             NwoaTcmRead32(Dev, d + NB_RING_BASE_LO) == (ULONG)R->La &&
             NwoaTcmRead32(Dev, d + NB_RING_BASE_LO + 4) == (ULONG)(R->La >> 32);
    }
    {
        ULONG64 sa = Dev->Ctl.La + Dev->L.scratch_off, ua = Dev->Ctl.La + Dev->L.ringupd_off;
        NwoaTcmWrite32(Dev, sh + NB_SH_SCRATCH_LO, (ULONG)sa);
        NwoaTcmWrite32(Dev, sh + NB_SH_SCRATCH_HI, (ULONG)(sa >> 32));
        NwoaTcmWrite32(Dev, sh + NB_SH_SCRATCH_LEN, NB_SCRATCH_LEN);
        NwoaTcmWrite32(Dev, sh + NB_SH_RINGUPD_LO, (ULONG)ua);
        NwoaTcmWrite32(Dev, sh + NB_SH_RINGUPD_HI, (ULONG)(ua >> 32));
        NwoaTcmWrite32(Dev, sh + NB_SH_RINGUPD_LEN, NB_RINGUPD_LEN);
    }
    if (s->version >= 6) {
        NwoaTcmWrite32(Dev, sh + NB_SH_HOST_CAP, nb_host_cap(s));
        NwoaTcmWrite32(Dev, sh + NB_SH_HOST_CAP2, 0);
    }
    NwoaDiagDword(Dev, L"NwoaHostCap", nb_host_cap(s));
    NwoaDiagDword(Dev, L"NwoaRingDescOk", ok);
    if (!ok)
        return STATUS_DEVICE_DATA_ERROR;

    // TX slot pool
    Dev->TxFreeCount = 0;
    for (ULONG i = 0; i < NWOA_TX_SLOTS; i++) {
        Dev->TxFree[Dev->TxFreeCount++] = NWOA_TX_SLOTS - 1 - i;
        Dev->TxEapol[i] = FALSE;
        Dev->TxBusy[i] = FALSE;
    }
    Dev->EapolPending = 0;
    Dev->RxQHead = Dev->RxQTail = 0;
    RtlZeroMemory(Dev->RxPend, sizeof(Dev->RxPend));
    Dev->CtlPend = 0;
    Dev->TxMaxOut = 0;

    NwoaFixDevCtl(Dev);
    NwoaBusMaster(Dev, TRUE);
    if (!NwoaSelectPcie2(Dev))                               // window parked on PCIe2 from here on
        return STATUS_DEVICE_DATA_ERROR;
    NwoaRegWrite(Dev, Dev->PciReg64 ? NB_PCIE2_64_MAILBOXMASK : NB_PCIE2_MAILBOXMASK, 0);
    {
        ULONG mi = Dev->PciReg64 ? NB_PCIE2_64_MAILBOXINT : NB_PCIE2_MAILBOXINT;
        ULONG st = NwoaRegRead(Dev, mi);
        if (st && st != 0xFFFFFFFF)
            NwoaRegWrite(Dev, mi, st);
    }
    if (s->hostrdy_db1)
        NwoaRegWrite(Dev, Dev->PciReg64 ? NB_PCIE2_64_H2D_MB1 : NB_PCIE2_H2D_MB1, 1);

    KeAcquireSpinLock(&Dev->RingLock, &irql);
    Dev->RingsUp = TRUE;
    for (ULONG i = 0; i < NB_N_IOCTLRESP; i++)
        PostCtlBuf(Dev, ID_IOCTLRESP + i);
    for (ULONG i = 0; i < NB_N_EVENT; i++)
        PostCtlBuf(Dev, ID_EVENT + i);
    for (ULONG i = 0; i < Dev->NumRx; i++)
        NwoaRxRepost(Dev, (USHORT)i);
    KeReleaseSpinLock(&Dev->RingLock, irql);
    return STATUS_SUCCESS;
}

void NwoaRingsStop(PNWOA_DEVICE Dev)
{
    KIRQL irql;
    NwoaIntrEnable(Dev, FALSE);
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    Dev->RingsUp = FALSE;
    Dev->RxQHead = Dev->RxQTail = 0;                      // firmware buffers are not reposted any more
    Dev->EvRxBusy = 0;
    RtlZeroMemory(Dev->RxPend, sizeof(Dev->RxPend));
    Dev->CtlPend = 0;
    KeReleaseSpinLock(&Dev->RingLock, irql);
    if (Dev->Bar0 && Dev->BusMasterOn)
        NwoaBusMaster(Dev, FALSE);
}

// -------------------------------------------------------- message dispatch
static void OnTxStatus(PNWOA_DEVICE Dev, ULONG ReqId, LONG FwStatus)
{
    KIRQL irql;
    ULONG slot = ReqId - 1;
    if (ReqId == 0 || slot >= NWOA_TX_SLOTS)
        return;
    KeAcquireSpinLock(&Dev->TxLock, &irql);
    if (!Dev->TxBusy[slot]) {                             // duplicate / stale status
        KeReleaseSpinLock(&Dev->TxLock, irql);
        return;
    }
    Dev->TxBusy[slot] = FALSE;
    if (Dev->TxEapol[slot]) {
        Dev->TxEapol[slot] = FALSE;
        InterlockedDecrement(&Dev->EapolPending);
    }
    if (Dev->TxFreeCount < NWOA_TX_SLOTS)
        Dev->TxFree[Dev->TxFreeCount++] = slot;
    KeReleaseSpinLock(&Dev->TxLock, irql);
    InterlockedIncrement(&Dev->CntTxStatus);
    Dev->LastTxDoneTime = KeQueryInterruptTime();          // s258m: NwoaDiscDiag
    if (FwStatus) {                                        // s258n: firmware TX status field
        InterlockedIncrement(&Dev->CntTxStatusNz);
        Dev->TxStatusLast = FwStatus;
    }
}

static void OnRx(PNWOA_DEVICE Dev, const nb_msg* m)
{
    ULONG slot = m->req_id - ID_RX;
    ULONG off = m->data_offset ? m->data_offset : Dev->Sh.rx_dataoffset;
    if (slot >= Dev->NumRx)
        return;
    PUCHAR f = Dev->RxPool.Va + (SIZE_T)slot * NWOA_SLOT_SIZE;
    if (off + m->data_len > NWOA_SLOT_SIZE || m->data_len < 14) {
        InterlockedIncrement(&Dev->CntRxDrop);
        NwoaRxRepost(Dev, (USHORT)slot);
        return;
    }
    if (nb_be16(f + off + 12) == NB_ETHERTYPE_LINK_CTL) {
        InterlockedIncrement(&Dev->CntRxAsEvent);
        NwoaHandleEvent(Dev, f + off, m->data_len);
        NwoaRxRepost(Dev, (USHORT)slot);
        return;
    }
    if (nb_be16(f + off + 12) == NB_ETHERTYPE_EAPOL && NwoaEapolRxDup(Dev, f + off, m->data_len)) {
        NwoaRxRepost(Dev, (USHORT)slot);
        return;
    }
    ULONG next = (Dev->RxQTail + 1) % NWOA_RXQ;
    ULONG used = (Dev->RxQTail + NWOA_RXQ - Dev->RxQHead) % NWOA_RXQ;
    // the queues are only created once NetAdapterCx sees the link up: hold up to 32 frames
    // (EAPOL 1/4 arrives right after the association) instead of dropping them
    BOOLEAN hold = !Dev->RxQueue && (Dev->ConnState == CONN_LINKED || Dev->Connected) && used < 32;
    if (hold)
        InterlockedIncrement(&Dev->CntRxHeld);
    if ((!Dev->RxQueue && !hold) || next == Dev->RxQHead) {
        InterlockedIncrement(&Dev->CntRxDrop);
        NwoaRxRepost(Dev, (USHORT)slot);
        return;
    }
    Dev->RxQ[Dev->RxQTail].Local = 0;
    Dev->RxQ[Dev->RxQTail].Slot = (USHORT)slot;
    Dev->RxQ[Dev->RxQTail].Off = (USHORT)off;
    Dev->RxQ[Dev->RxQTail].Len = m->data_len;
    Dev->RxQTail = next;
    InterlockedIncrement(&Dev->CntRx);
    Dev->LastRxTime = KeQueryInterruptTime();              // s258m: NwoaDiscDiag
}

static void Dispatch(PNWOA_DEVICE Dev, const UCHAR* It, ULONG Sz, BOOLEAN* RxQueued, BOOLEAN* TxFreed)
{
    nb_msg m;
    if (nb_dec_msg(It, Sz, &m) != NB_OK)
        return;
    InterlockedIncrement(&Dev->CntMsgs);
    switch (m.type) {
    case NB_MSG_IOCTLPTR_REQ_ACK:
        if (m.status && Dev->IoctlPending) {
            Dev->IoctlStatus = m.status;
            Dev->IoctlRetLen = 0;
            InterlockedExchange(&Dev->IoctlPending, 0);
            KeSetEvent(&Dev->IoctlEvent, IO_NO_INCREMENT, FALSE);
        }
        break;
    case NB_MSG_IOCTL_CMPLT: {
        ULONG id = m.req_id;
        if (id >= ID_IOCTLRESP && id < ID_IOCTLRESP + NB_N_IOCTLRESP) {
            if (Dev->IoctlPending && m.trans_id == Dev->IoctlTrans) {
                ULONG n = min((ULONG)m.resp_len, Dev->IoctlOutCap);
                n = min(n, (ULONG)NB_CTL_PKT_SIZE);
                if (n)
                    RtlCopyMemory(Dev->IoRsp, Dev->Ctl.Va + Dev->L.ioctl_resp_off +
                                  (id - ID_IOCTLRESP) * NB_CTL_PKT_SIZE, n);
                Dev->IoctlRetLen = n;
                Dev->IoctlStatus = m.status;
                InterlockedExchange(&Dev->IoctlPending, 0);
                KeSetEvent(&Dev->IoctlEvent, IO_NO_INCREMENT, FALSE);
            }
            PostCtlBuf(Dev, id);
        }
        break;
    }
    case NB_MSG_WL_EVENT: {
        ULONG id = m.req_id;
        if (id >= ID_EVENT && id < ID_EVENT + NB_N_EVENT) {
            ULONG off = Dev->L.event_off + (id - ID_EVENT) * NB_CTL_PKT_SIZE + Dev->Sh.rx_dataoffset;
            ULONG n = min((ULONG)m.event_len, (ULONG)NB_CTL_PKT_SIZE - Dev->Sh.rx_dataoffset);
            InterlockedIncrement(&Dev->CntEvents);
            NwoaHandleEvent(Dev, Dev->Ctl.Va + off, n);
            PostCtlBuf(Dev, id);
        }
        break;
    }
    case NB_MSG_RX_CMPLT:
        OnRx(Dev, &m);
        *RxQueued = TRUE;
        break;
    case NB_MSG_TX_STATUS:
        OnTxStatus(Dev, m.req_id, m.status);
        *TxFreed = TRUE;
        break;
    case NB_MSG_FLOW_RING_CREATE_CMPLT:
        if (m.flow_ring_id == Dev->Flow.D.idx && Dev->FlowState == 1) {
            InterlockedExchange(&Dev->FlowState, m.status ? 4 : 2);
            KeSetEvent(&Dev->FlowEvent, IO_NO_INCREMENT, FALSE);
        }
        break;
    case NB_MSG_FLOW_RING_DELETE_CMPLT:
        if (m.flow_ring_id == Dev->Flow.D.idx && Dev->FlowState == 3) {
            InterlockedExchange(&Dev->FlowState, 0);
            KeSetEvent(&Dev->FlowEvent, IO_NO_INCREMENT, FALSE);
        }
        break;
    case NB_MSG_D2H_MAILBOX_DATA:
        InterlockedIncrement(&Dev->CntMbData);
        if (m.mb_data & NB_D2H_DEV_FWHALT)
            InterlockedExchange(&Dev->Dead, 1);
        if ((m.mb_data & NB_D2H_DEV_DS_ENTER_REQ) && Dev->Sh.mb_via_ctl) {
            UCHAR it[64];
            nb_enc_h2d_mailbox(it, Dev->Ring[NB_RING_CTRL_SUBMIT].D.itemsz, NB_H2D_HOST_DS_ACK);
            (void)SubmitCtl(Dev, it);
        }
        break;
    case NB_MSG_FLOW_RING_RESUME_CMPLT:                   // s258p: answer to NwoaFlowResume
        InterlockedIncrement(&Dev->CntRsmCmplt);
        Dev->RsmLastStatus = m.status;
        Dev->RsmLastRing = m.flow_ring_id;
        break;
    case NB_MSG_GEN_STATUS:                               // s258n: counted (ignored before)
        InterlockedIncrement(&Dev->CntGenStatus);
        Dev->GenStatusLast = m.status;
        break;
    case NB_MSG_RING_STATUS:
        InterlockedIncrement(&Dev->CntRingStatus);
        Dev->RingStatusLast = m.status;
        Dev->RingStatusRing = m.flow_ring_id;
        break;
    default:
        InterlockedIncrement(&Dev->CntUnknown);
        break;
    }
}

static void Drain(PNWOA_DEVICE Dev, int Which, BOOLEAN* RxQueued, BOOLEAN* TxFreed)
{
    NWOA_RING* R = &Dev->Ring[Which];
    ULONG w = IdxGet(Dev, 2, R->D.idx), guard = 0;
    if (w >= R->D.nitem) {
        InterlockedExchange(&Dev->Dead, 1);
        return;
    }
    KeMemoryBarrier();
    if (R->RPtr != w)
        Dev->LastActivity = (LONG64)KeQueryInterruptTime();
    while (R->RPtr != w && guard++ < R->D.nitem) {
        Dispatch(Dev, R->Va + (SIZE_T)R->RPtr * R->D.itemsz, R->D.itemsz, RxQueued, TxFreed);
        R->RPtr = (R->RPtr + 1) % R->D.nitem;
        if ((R->RPtr & 15) == 0)
            IdxSet(Dev, 3, R->D.idx, R->RPtr);
    }
    IdxSet(Dev, 3, R->D.idx, R->RPtr);
}

void NwoaProcess(PNWOA_DEVICE Dev)
{
    KIRQL irql;
    BOOLEAN rx = FALSE, tx = FALSE;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    if (Dev->RingsUp && !Dev->Dead) {
        Drain(Dev, NB_RING_CTRL_COMPLETE, &rx, &tx);
        Drain(Dev, NB_RING_RX_COMPLETE, &rx, &tx);
        Drain(Dev, NB_RING_TX_COMPLETE, &rx, &tx);
        RetryPending(Dev);
    }
    KeReleaseSpinLock(&Dev->RingLock, irql);
    if (InterlockedExchange(&Dev->RxNotifyPending, 0))
        rx = TRUE;
    if (rx)
        NwoaRxDeliverNotify(Dev);
    if (tx)
        NwoaTxSlotsFreed(Dev);
}

// -------------------------------------------------------------- ioctl
NTSTATUS NwoaIoctl(PNWOA_DEVICE Dev, ULONG Cmd, PUCHAR Buf, ULONG Len, ULONG OutCap, ULONG* RetLen, LONG* FwStatus)
{
    NTSTATUS st = STATUS_SUCCESS;
    ULONG n = max(Len, OutCap);
    KIRQL irql;
    UCHAR it[64];
    LARGE_INTEGER t;

    if (FwStatus) *FwStatus = 0;
    if (RetLen) *RetLen = 0;
    if (!n || n > NB_IOCTL_MAX_LEN || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    if (!Dev->RingsUp || Dev->Dead)
        return STATUS_DEVICE_NOT_READY;
    ExAcquireFastMutex(&Dev->IoctlMutex);
    if (!Dev->RingsUp || Dev->Dead) {
        ExReleaseFastMutex(&Dev->IoctlMutex);
        return STATUS_DEVICE_NOT_READY;
    }
    RtlZeroMemory(Dev->Ctl.Va + Dev->L.ioctl_req_off, NB_IOCTL_MAX_LEN);
    if (Len)
        RtlCopyMemory(Dev->Ctl.Va + Dev->L.ioctl_req_off, Buf, Len);
    KeClearEvent(&Dev->IoctlEvent);
    Dev->IoctlOutCap = OutCap;
    Dev->IoctlRetLen = 0;
    Dev->IoctlStatus = 0;
    Dev->TransId = (USHORT)(Dev->TransId + 1);
    Dev->IoctlTrans = Dev->TransId;
    nb_enc_ioctl_req(it, Dev->Ring[NB_RING_CTRL_SUBMIT].D.itemsz, Cmd, Dev->IoctlTrans, (USHORT)n,
                     Dev->Ctl.La + Dev->L.ioctl_req_off);
    KeMemoryBarrier();
    InterlockedExchange(&Dev->IoctlPending, 1);
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    st = SubmitCtl(Dev, it);
    KeReleaseSpinLock(&Dev->RingLock, irql);
    InterlockedIncrement(&Dev->CntIoctl);
    if (NT_SUCCESS(st)) {
        ULONG64 deadline = KeQueryInterruptTime() + 3ull * 10000000ull;   // 3 s (bwfm 5 s, brcmfmac 2 s)
        t.QuadPart = -2 * 10000;                             // wait, then poll the rings
        st = STATUS_IO_TIMEOUT;
        while (KeQueryInterruptTime() < deadline) {
            if (KeWaitForSingleObject(&Dev->IoctlEvent, Executive, KernelMode, FALSE, &t) == STATUS_SUCCESS ||
                !Dev->IoctlPending) {
                st = STATUS_SUCCESS;
                break;
            }
            NwoaProcess(Dev);
            if (!Dev->IoctlPending) { st = STATUS_SUCCESS; break; }
            if (Dev->Dead) { st = STATUS_DEVICE_NOT_CONNECTED; break; }
        }
    }
    if (st == STATUS_IO_TIMEOUT) {
        InterlockedIncrement(&Dev->CntIoctlTimeout);
        if (++Dev->IoctlTimeoutsInRow >= 2)
            InterlockedExchange(&Dev->Dead, 1);             // firmware not answering: stop waiting on it
    } else if (NT_SUCCESS(st)) {
        Dev->IoctlTimeoutsInRow = 0;
    }
    InterlockedExchange(&Dev->IoctlPending, 0);
    if (NT_SUCCESS(st)) {
        if (FwStatus) *FwStatus = Dev->IoctlStatus;
        if (RetLen) *RetLen = Dev->IoctlRetLen;
        if (OutCap && Dev->IoctlRetLen)
            RtlCopyMemory(Buf, Dev->IoRsp, min(OutCap, Dev->IoctlRetLen));
        if (Dev->IoctlStatus)
            st = STATUS_UNSUCCESSFUL;
    }
    ExReleaseFastMutex(&Dev->IoctlMutex);
    return st;
}

NTSTATUS NwoaSetInt(PNWOA_DEVICE Dev, ULONG Cmd, ULONG Val)
{
    UCHAR b[4];
    nb_st32(b, Val);
    return NwoaIoctl(Dev, Cmd, b, 4, 0, NULL, NULL);
}

NTSTATUS NwoaGetInt(PNWOA_DEVICE Dev, ULONG Cmd, ULONG* Val)
{
    UCHAR b[4] = { 0 };
    NTSTATUS st = NwoaIoctl(Dev, Cmd, b, 4, 4, NULL, NULL);
    *Val = NT_SUCCESS(st) ? nb_ld32(b) : 0;
    return st;
}

NTSTATUS NwoaSetVar(PNWOA_DEVICE Dev, PCSTR Name, const void* Data, ULONG Len)
{
    PUCHAR b = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, NB_IOCTL_MAX_LEN, NWOA_TAG);
    NTSTATUS st;
    if (!b)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG n = nb_iovar(b, NB_IOCTL_MAX_LEN, Name, Data, Len);
    st = n ? NwoaIoctl(Dev, NB_C_SET_VAR, b, n, 0, NULL, NULL) : STATUS_BUFFER_OVERFLOW;
    ExFreePoolWithTag(b, NWOA_TAG);
    return st;
}

NTSTATUS NwoaSetVarInt(PNWOA_DEVICE Dev, PCSTR Name, ULONG Val)
{
    UCHAR v[4];
    nb_st32(v, Val);
    return NwoaSetVar(Dev, Name, v, 4);
}

NTSTATUS NwoaGetVar(PNWOA_DEVICE Dev, PCSTR Name, PUCHAR Out, ULONG Len)
{
    ULONG q = (ULONG)strlen(Name) + 1;
    NTSTATUS st;
    if (q + Len > NB_IOCTL_MAX_LEN)
        return STATUS_BUFFER_OVERFLOW;
    PUCHAR b = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, NB_IOCTL_MAX_LEN, NWOA_TAG);
    if (!b)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(b, Name, q);
    st = NwoaIoctl(Dev, NB_C_GET_VAR, b, q + Len, q + Len, NULL, NULL);
    if (NT_SUCCESS(st))
        RtlCopyMemory(Out, b, Len);                          // reply starts at offset 0
    ExFreePoolWithTag(b, NWOA_TAG);
    return st;
}

// ------------------------------------------------------------- flowring
NTSTATUS NwoaFlowringCreate(PNWOA_DEVICE Dev, const UCHAR* Da)
{
    UCHAR it[64];
    KIRQL irql;
    NTSTATUS st;
    LARGE_INTEGER t;
    if (Dev->FlowState == 2 && RtlEqualMemory(Dev->FlowDa, Da, 6))
        return STATUS_SUCCESS;
    if (Dev->FlowState == 2)
        NwoaFlowringDelete(Dev);
    if (Dev->Flow.D.idx >= Dev->Sh.max_sub)
        return STATUS_NOT_SUPPORTED;
    if (!Dev->RingsUp || Dev->Dead)
        return STATUS_DEVICE_NOT_READY;
    {
        // new flowring: every TX slot is free again (items left on a deleted ring may never get a TX status)
        KIRQL tl;
        KeAcquireSpinLock(&Dev->TxLock, &tl);
        Dev->TxFreeCount = 0;
        for (ULONG i = 0; i < NWOA_TX_SLOTS; i++) {
            Dev->TxFree[Dev->TxFreeCount++] = NWOA_TX_SLOTS - 1 - i;
            Dev->TxEapol[i] = FALSE;
            Dev->TxBusy[i] = FALSE;
        }
        Dev->EapolPending = 0;
        KeReleaseSpinLock(&Dev->TxLock, tl);
    }
    RtlZeroMemory(Dev->Flow.Va, (SIZE_T)Dev->Flow.D.nitem * Dev->Flow.D.itemsz);
    Dev->Flow.WPtr = Dev->Flow.RPtr = 0;
    IdxSet(Dev, 0, Dev->Flow.D.idx, 0);
    IdxSet(Dev, 1, Dev->Flow.D.idx, 0);
    RtlCopyMemory(Dev->FlowDa, Da, 6);
    KeClearEvent(&Dev->FlowEvent);
    InterlockedExchange(&Dev->FlowState, 1);
    nb_enc_flowring_create(it, Dev->Ring[NB_RING_CTRL_SUBMIT].D.itemsz, 0, Dev->Flow.D.idx, Da, Dev->Mac,
                           0, Dev->Flow.D.nitem, Dev->Flow.D.itemsz, Dev->Flow.La);
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    st = SubmitCtl(Dev, it);
    KeReleaseSpinLock(&Dev->RingLock, irql);
    if (!NT_SUCCESS(st)) {
        InterlockedExchange(&Dev->FlowState, 0);
        return st;
    }
    t.QuadPart = -2 * 10000;
    for (ULONG64 dl = KeQueryInterruptTime() + 2ull * 10000000ull; Dev->FlowState == 1 && KeQueryInterruptTime() < dl;) {   // 2 s
        KeWaitForSingleObject(&Dev->FlowEvent, Executive, KernelMode, FALSE, &t);
        NwoaProcess(Dev);
    }
    NwoaDiagDword(Dev, L"NwoaFlowState", (ULONG)Dev->FlowState);
    return Dev->FlowState == 2 ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

void NwoaFlowringDelete(PNWOA_DEVICE Dev)
{
    UCHAR it[64];
    KIRQL irql;
    LARGE_INTEGER t;
    if (Dev->FlowState != 2) {
        InterlockedExchange(&Dev->FlowState, 0);
        return;
    }
    KeClearEvent(&Dev->FlowEvent);
    InterlockedExchange(&Dev->FlowState, 3);
    nb_enc_flowring_delete(it, Dev->Ring[NB_RING_CTRL_SUBMIT].D.itemsz, 0, Dev->Flow.D.idx);
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    (void)SubmitCtl(Dev, it);
    KeReleaseSpinLock(&Dev->RingLock, irql);
    t.QuadPart = -2 * 10000;
    for (ULONG64 dl = KeQueryInterruptTime() + 2ull * 10000000ull; Dev->FlowState == 3 && KeQueryInterruptTime() < dl;) {
        KeWaitForSingleObject(&Dev->FlowEvent, Executive, KernelMode, FALSE, &t);
        NwoaProcess(Dev);
    }
    InterlockedExchange(&Dev->FlowState, 0);
}

// One Ethernet frame (<= 2048 bytes): copy into a TX slot and post TX_POST.
// Any IRQL <= DISPATCH.  STATUS_DEVICE_BUSY = no slot / ring full (retry later).
NTSTATUS NwoaTxFrame(PNWOA_DEVICE Dev, const UCHAR* Eth, ULONG Len)
{
    KIRQL irql;
    ULONG slot;
    UCHAR it[64];
    NTSTATUS st;
    BOOLEAN eapol;
    if (Len < 14 || Len > NWOA_SLOT_SIZE)
        return STATUS_INVALID_PARAMETER;
    if (Dev->FlowState != 2 || !Dev->RingsUp || Dev->Dead)
        return STATUS_DEVICE_NOT_READY;
    if (Dev->TxHold) {                                     // s258o ScanTxHold: the frame stays in the OS ring
        InterlockedIncrement(&Dev->CntTxHeld);
        return STATUS_DEVICE_BUSY;
    }
    eapol = nb_be16(Eth + 12) == NB_ETHERTYPE_EAPOL;
    if (eapol) {
        int k = nb_eapol_key_msg(Eth, Len);
        InterlockedIncrement(&Dev->CntEapolTx);
        if (k >= 0 && k < 8)
            InterlockedIncrement(&Dev->EapolTxMsg[k]);
    }
    KeAcquireSpinLock(&Dev->TxLock, &irql);
    if (!Dev->TxFreeCount) {
        KeReleaseSpinLock(&Dev->TxLock, irql);
        return STATUS_DEVICE_BUSY;
    }
    slot = Dev->TxFree[--Dev->TxFreeCount];
    Dev->TxBusy[slot] = TRUE;
    Dev->TxEapol[slot] = eapol;
    Dev->TxPostTime[slot] = KeQueryInterruptTime();       // s258n: age of the oldest outstanding TX
    if (NWOA_TX_SLOTS - Dev->TxFreeCount > Dev->TxMaxOut)
        Dev->TxMaxOut = NWOA_TX_SLOTS - Dev->TxFreeCount;
    if (eapol)
        InterlockedIncrement(&Dev->EapolPending);
    KeReleaseSpinLock(&Dev->TxLock, irql);

    PUCHAR b = Dev->TxPool.Va + (SIZE_T)slot * NWOA_SLOT_SIZE;
    RtlCopyMemory(b, Eth, Len);
    nb_enc_tx_post(it, Dev->Flow.D.itemsz, 0, slot + 1, Eth, 0,
                   Dev->TxPool.La + (ULONG64)slot * NWOA_SLOT_SIZE + 14, (USHORT)(Len - 14));
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    st = Dev->FlowState == 2 ? Submit(Dev, &Dev->Flow, it, 1) : STATUS_DEVICE_NOT_READY;
    KeReleaseSpinLock(&Dev->RingLock, irql);
    if (!NT_SUCCESS(st)) {
        OnTxStatus(Dev, slot + 1, 0);                          // give the slot back
        return st;
    }
    InterlockedIncrement(&Dev->CntTx);
    Dev->LastActivity = (LONG64)KeQueryInterruptTime();
    Dev->LastTxTime = (ULONG64)Dev->LastActivity;          // s258m: NwoaDiscDiag
    if (Dev->PollIdle && Dev->PollRunning && Dev->PollTimer && InterlockedExchange(&Dev->PollIdle, 0))
        WdfTimerStart(Dev->PollTimer, WDF_REL_TIMEOUT_IN_MS(Dev->Cfg.PollIntervalMs));   // wake the poll now
    return STATUS_SUCCESS;
}

// --------------------------------------------------------- interrupt/poll
static BOOLEAN NwoaIsr(WDFINTERRUPT Interrupt, ULONG MessageId)
{
    UNREFERENCED_PARAMETER(MessageId);
    PNWOA_DEVICE Dev = NwoaGetDevice(WdfInterruptGetDevice(Interrupt));
    if (!Dev->RingsUp || !Dev->IntrEnabled)
        return FALSE;
    ULONG mi = Dev->PciReg64 ? NB_PCIE2_64_MAILBOXINT : NB_PCIE2_MAILBOXINT;
    ULONG st = NwoaRegRead(Dev, mi);
    if (st == 0 || st == 0xFFFFFFFF) {
        // level-triggered line (S259 contract 4) asserted with nothing to clear: after 200 in a
        // row mask the mailbox and fall back to polling instead of storming
        if (InterlockedIncrement(&Dev->IsrUnclaimedRow) > 200) {
            NwoaIntrEnable(Dev, FALSE);
            InterlockedExchange(&Dev->IrqMode, 4);
            return TRUE;
        }
        return FALSE;
    }
    Dev->IsrUnclaimedRow = 0;
    NwoaRegWrite(Dev, mi, st);
    InterlockedIncrement(&Dev->CntIsr);
    if (Dev->IrqMode == 1)
        InterlockedExchange(&Dev->IrqMode, 2);
    WdfInterruptQueueDpcForIsr(Interrupt);
    return TRUE;
}

static void NwoaDpc(WDFINTERRUPT Interrupt, WDFOBJECT Object)
{
    UNREFERENCED_PARAMETER(Object);
    NwoaProcess(NwoaGetDevice(WdfInterruptGetDevice(Interrupt)));
}

NTSTATUS NwoaInterruptCreate(PNWOA_DEVICE Dev)
{
    WDF_INTERRUPT_CONFIG ic;
    if (!Dev->HaveIntRes || !Dev->Cfg.UseInterrupt || Dev->Interrupt)
        return STATUS_SUCCESS;
    WDF_INTERRUPT_CONFIG_INIT(&ic, NwoaIsr, NwoaDpc);
    ic.InterruptRaw = &Dev->IntRaw;
    ic.InterruptTranslated = &Dev->IntTrans;
    NTSTATUS st = WdfInterruptCreate(Dev->Device, &ic, WDF_NO_OBJECT_ATTRIBUTES, &Dev->Interrupt);
    NwoaDiagDword(Dev, L"NwoaInterruptCreate", (ULONG)st);
    if (!NT_SUCCESS(st))
        Dev->Interrupt = NULL;                                // poll timer only
    return STATUS_SUCCESS;
}

void NwoaIntrEnable(PNWOA_DEVICE Dev, BOOLEAN On)
{
    if (!Dev->Bar0 || !Dev->Pcie2Base)
        return;
    BOOLEAN want = On && Dev->Interrupt != NULL && Dev->RingsUp;
    ULONG off = Dev->PciReg64 ? NB_PCIE2_64_MAILBOXMASK : NB_PCIE2_MAILBOXMASK;
    ULONG val = want ? (Dev->PciReg64 ? NB_PCIE2_64_MB_D2H_DB : (NB_PCIE2_MB_D2H_DB | NB_PCIE2_MB_FN0)) : 0;
    Dev->IntrEnabled = want;
    if (Dev->RingsUp && Dev->WindowValid && Dev->CurWindow == Dev->Pcie2Base)
        NwoaRegWrite(Dev, off, val);                      // window parked on PCIe2
    // before the rings are up the mask is never written (S257 order; see chip.cpp)
}

void NwoaEvtPollTimer(WDFTIMER Timer)
{
    PNWOA_DEVICE Dev = NwoaGetDevice(WdfTimerGetParentObject(Timer));
    static const ULONG DiagEveryMs = 2000;
    static ULONG64 lastDiag;
    ULONG64 now;
    InterlockedIncrement(&Dev->CntPoll);
    NwoaProcess(Dev);
    now = KeQueryInterruptTime();
    // interrupt on trial: no ISR within 3 s of rings up although messages were handled -> poll only
    if (Dev->IrqMode == 1 && now - Dev->RingsUpTime > 30000000ull && Dev->CntMsgs > 0 && Dev->CntIsr == 0) {
        NwoaIntrEnable(Dev, FALSE);
        InterlockedExchange(&Dev->IrqMode, 3);
    }
    if (Dev->ScanRunning && KeQueryInterruptTime() > Dev->ScanDeadline) {
        Dev->ScanStatus = -2;                                 // timeout
        InterlockedExchange(&Dev->ScanRunning, 0);
        NwoaQueueJob(Dev, JOB_SCAN_DONE);
    }
    if (now - lastDiag > DiagEveryMs * 10000ull) {
        lastDiag = now;
        NwoaQueueJob(Dev, JOB_DIAG);
    }
    // adaptive interval: fast while messages/TX flowed in the last 200 ms or work is pending
    BOOLEAN busy = now - (ULONG64)Dev->LastActivity < 2000000ull || Dev->IoctlPending || Dev->ScanRunning ||
                   Dev->ConnState == CONN_JOINING || Dev->FlowState == 1 || Dev->FlowState == 3 ||
                   Dev->RxQHead != Dev->RxQTail;
    ULONG ms = busy ? Dev->Cfg.PollIntervalMs : (Dev->IrqMode == 2 ? 200 : Dev->Cfg.PollIdleMs);
    if (Dev->IrqMode == 2 && busy)
        ms = 20;                                                  // interrupts carry the traffic; safety poll only
    InterlockedExchange(&Dev->PollIdle, busy ? 0 : 1);
    if (Dev->PollRunning)
        WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_MS(ms));
}

// ------------------------------------------------------------- s258n
ULONG NwoaTxOutstanding(PNWOA_DEVICE Dev, ULONG64* OldestPost)
{
    KIRQL irql;
    ULONG n = 0;
    ULONG64 oldest = 0;
    KeAcquireSpinLock(&Dev->TxLock, &irql);
    for (ULONG i = 0; i < NWOA_TX_SLOTS; i++) {
        if (!Dev->TxBusy[i])
            continue;
        n++;
        if (!oldest || Dev->TxPostTime[i] < oldest)
            oldest = Dev->TxPostTime[i];
    }
    KeReleaseSpinLock(&Dev->TxLock, irql);
    if (OldestPost)
        *OldestPost = oldest;
    return n;
}

// "cs<w>/<fw r> rp<w>/<fw r> cc<fw w>/<r> tc<fw w>/<r> rc<fw w>/<r> fl<w>/<fw r>|-" (host copy / firmware index)
void NwoaRingSnapshot(PNWOA_DEVICE Dev, PCHAR B, SIZE_T Cap, ULONG* FlowW, ULONG* FlowR)
{
    static const char* nm[NB_NUM_COMMON_RINGS] = { "cs", "rp", "cc", "tc", "rc" };
    KIRQL irql;
    ULONG v[NB_NUM_COMMON_RINGS][2], fw = 0, fr = 0;
    BOOLEAN up, flow;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    up = Dev->RingsUp && Dev->Ctl.Va;
    flow = up && Dev->FlowState == 2;
    for (int k = 0; k < NB_NUM_COMMON_RINGS; k++) {
        NWOA_RING* R = &Dev->Ring[k];
        if (!up) { v[k][0] = v[k][1] = 0; continue; }
        if (R->D.is_h2d) { v[k][0] = R->WPtr; v[k][1] = IdxGet(Dev, 1, R->D.idx); }
        else             { v[k][0] = IdxGet(Dev, 2, R->D.idx); v[k][1] = R->RPtr; }
    }
    if (flow) { fw = Dev->Flow.WPtr; fr = IdxGet(Dev, 1, Dev->Flow.D.idx); }
    KeReleaseSpinLock(&Dev->RingLock, irql);
    B[0] = 0;
    for (int k = 0; k < NB_NUM_COMMON_RINGS; k++) {
        CHAR one[32];
        RtlStringCchPrintfA(one, sizeof(one), "%s%lu/%lu ", nm[k], v[k][0], v[k][1]);
        RtlStringCchCatA(B, Cap, one);
    }
    if (flow) {
        CHAR one[32];
        RtlStringCchPrintfA(one, sizeof(one), "fl%lu/%lu", fw, fr);
        RtlStringCchCatA(B, Cap, one);
    } else {
        RtlStringCchCatA(B, Cap, "fl-");
    }
    if (FlowW) *FlowW = flow ? fw : 0xFFFFFFFFu;
    if (FlowR) *FlowR = flow ? fr : 0xFFFFFFFFu;
}

void NwoaPoolSnapshot(PNWOA_DEVICE Dev, PCHAR B, SIZE_T Cap)
{
    KIRQL irql;
    ULONG inq = 0, local = 0, pend = 0, ctlpend;
    ULONG out = NwoaTxOutstanding(Dev, NULL);
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    for (ULONG i = Dev->RxQHead; i != Dev->RxQTail; i = (i + 1) % NWOA_RXQ) {
        if (Dev->RxQ[i].Local) local++; else inq++;
    }
    for (ULONG w = 0; w < NWOA_RX_SLOTS_MAX / 32; w++)
        for (ULONG m = Dev->RxPend[w]; m; m &= m - 1)
            pend++;
    ctlpend = Dev->CtlPend;
    KeReleaseSpinLock(&Dev->RingLock, irql);
    ULONG atfw = Dev->NumRx >= inq + pend ? Dev->NumRx - inq - pend : 0;
    RtlStringCchPrintfA(B, Cap, "txout=%lu txfree=%lu txmax=%lu rxnum=%lu rxfw=%lu rxq=%lu rxqlocal=%lu rxpend=%lu "
        "rxfail=%ld rxretry=%ld ctlpend=%lx ctlfail=%ld ctlretry=%ld txnz=%ld/%ld gen=%ld/%ld ring=%ld/%ld/%lu",
        out, NWOA_TX_SLOTS - out, Dev->TxMaxOut, Dev->NumRx, atfw, inq, local, pend,
        Dev->CntRxRepostFail, Dev->CntRxRepostRetry, ctlpend, Dev->CntCtlPostFail, Dev->CntCtlPostRetry,
        Dev->CntTxStatusNz, Dev->TxStatusLast, Dev->CntGenStatus, Dev->GenStatusLast,
        Dev->CntRingStatus, Dev->RingStatusLast, Dev->RingStatusRing);
}

// delete + create the TX flowring to the current AP (the firmware returns what it holds on delete)
NTSTATUS NwoaFlowringRecreate(PNWOA_DEVICE Dev)
{
    UCHAR da[6];
    RtlCopyMemory(da, Dev->FlowDa, 6);
    NwoaFlowringDelete(Dev);
    return NwoaFlowringCreate(Dev, da);
}

// PASSIVE, 2 s diag job: the PCIe mailbox interrupt status and the D2H mailbox word in TCM are
// what brcmfmac's interrupt handler reads (brcmf_pcie_handle_mb_data).  Polled here, read only.
void NwoaMbPollTick(PNWOA_DEVICE Dev)
{
    if (!Dev->Cfg.MbPoll || !Dev->RingsUp || Dev->Dead || !Dev->Bar0)
        return;
    if (Dev->WindowValid && Dev->CurWindow == Dev->Pcie2Base) {
        ULONG mi = NwoaRegRead(Dev, Dev->PciReg64 ? NB_PCIE2_64_MAILBOXINT : NB_PCIE2_MAILBOXINT);
        if (mi && mi != 0xFFFFFFFF)
            InterlockedIncrement(&Dev->CntMbIntNz);
        Dev->MbIntLast = mi;
    }
    if (Dev->Sh.dtoh_mb) {
        ULONG d = NwoaTcmRead32(Dev, Dev->Sh.dtoh_mb);
        if (d && d != 0xFFFFFFFF)
            InterlockedIncrement(&Dev->CntDtohMbNz);
        Dev->DtohMbLast = d;
    }
}

// ------------------------------------------------------------- s258o
// TX flowring host write pointer / firmware read index (read from the DMA index buffer)
BOOLEAN NwoaFlowIdx(PNWOA_DEVICE Dev, ULONG* W, ULONG* R)
{
    KIRQL irql;
    BOOLEAN ok;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    ok = Dev->RingsUp && Dev->Ctl.Va && Dev->FlowState == 2 && !Dev->Dead;
    *W = ok ? Dev->Flow.WPtr : 0xFFFFFFFFu;
    *R = ok ? IdxGet(Dev, 1, Dev->Flow.D.idx) : 0xFFFFFFFFu;
    KeReleaseSpinLock(&Dev->RingLock, irql);
    return ok;
}

// The same commit again: w index rewritten, barrier, doorbell (brcmf_commonring_write_complete does
// write_wptr + ring_bell for every commit; this repeats it once without a new item)
void NwoaFlowKick(PNWOA_DEVICE Dev)
{
    KIRQL irql;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    if (Dev->RingsUp && Dev->Ctl.Va && Dev->FlowState == 2 && !Dev->Dead) {
        KeMemoryBarrier();
        IdxSet(Dev, 0, Dev->Flow.D.idx, Dev->Flow.WPtr);
        KeMemoryBarrier();
        Dbell(Dev);
        InterlockedIncrement(&Dev->CntFlowDbell);
    }
    KeReleaseSpinLock(&Dev->RingLock, irql);
}

// s258p: FLOW_RING_RESUME (bcmdhd dhd_prot_flow_ring_resume: if_id, request_id 0, flow_ring_id) on the
// control submit ring.  [hypothesis] the firmware holds this flowring as suspended after the scan; a
// firmware without idle flowring management answers with an error status or not at all.
NTSTATUS NwoaFlowResume(PNWOA_DEVICE Dev)
{
    UCHAR it[64];
    KIRQL irql;
    NTSTATUS st = STATUS_DEVICE_NOT_READY;
    if (Dev->FlowState != 2 || !Dev->RingsUp || Dev->Dead)
        return st;
    if (!nb_enc_flowring_resume(it, Dev->Ring[NB_RING_CTRL_SUBMIT].D.itemsz, 0, Dev->Flow.D.idx))
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    st = SubmitCtl(Dev, it);
    KeReleaseSpinLock(&Dev->RingLock, irql);
    if (NT_SUCCESS(st))
        InterlockedIncrement(&Dev->CntRsmSent);
    return st;
}

// [hypothesis about their meaning] ring_info h2d_w/h2d_r_idx_ptr still point at the firmware's own
// index arrays in TCM when DMA indices are used (brcmfmac ignores them then).  Read only, 4 bytes
// per ring as in TCM index mode; compared with the control submit ring (slot 0) to see what they hold.
void NwoaFwIdxView(PNWOA_DEVICE Dev, ULONG Slot, ULONG* FwW, ULONG* FwR)
{
    *FwW = *FwR = 0xFFFFFFFFu;
    if (!Dev->RingsUp || Dev->Dead || !Dev->Bar2)
        return;
    ULONG a = Dev->Sh.tcm_idx[0], b = Dev->Sh.tcm_idx[1];
    if (a && a + Slot * 4 + 4 <= Dev->Bar2Len)
        *FwW = NwoaTcmRead32(Dev, a + Slot * 4);
    if (b && b + Slot * 4 + 4 <= Dev->Bar2Len)
        *FwR = NwoaTcmRead32(Dev, b + Slot * 4);
}

ULONG NwoaHostWIdx(PNWOA_DEVICE Dev, ULONG Slot)
{
    KIRQL irql;
    ULONG v = 0xFFFFFFFFu;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    if (Dev->RingsUp && Dev->Ctl.Va && !Dev->Dead)
        v = Slot == 0 ? Dev->Ring[NB_RING_CTRL_SUBMIT].WPtr : (Slot == Dev->Flow.D.idx ? Dev->Flow.WPtr : IdxGet(Dev, 0, Slot));
    KeReleaseSpinLock(&Dev->RingLock, irql);
    return v;
}
