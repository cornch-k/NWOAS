// events.cpp - firmware events (bwfm_rx_event / bwfm_rx_event_cb semantics).
// Runs at DISPATCH from the ring dispatcher (RingLock held).  Anything that
// needs an ioctl or a WiFiCx indication is handed to the work item.
#include "precomp.h"

void NwoaScanCacheClear(PNWOA_DEVICE Dev)
{
    KIRQL irql;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    for (int i = 0; i < NWOA_MAX_BSS; i++)
        Dev->Bss[i].Used = FALSE;
    Dev->ScanEvents = Dev->ScanRaw = 0;
    KeReleaseSpinLock(&Dev->StateLock, irql);
}

BOOLEAN NwoaScanCacheFind(PNWOA_DEVICE Dev, const UCHAR* Bssid, NWOA_BSS* Out)
{
    KIRQL irql;
    BOOLEAN found = FALSE;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    for (int i = 0; i < NWOA_MAX_BSS; i++) {
        if (Dev->Bss[i].Used && RtlEqualMemory(Dev->Bss[i].B.bssid, Bssid, 6)) {
            *Out = Dev->Bss[i];
            Out->B.ies = Out->Ies;
            found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&Dev->StateLock, irql);
    return found;
}

static void CacheAdd(PNWOA_DEVICE Dev, const nb_bss* b)
{
    KIRQL irql;
    int slot = -1, freeSlot = -1, weakest = -1;
    if (b->band != 2 && b->band != 5)
        return;                                         // 6 GHz / unknown chanspec: not reported
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    Dev->ScanRaw++;
    for (int i = 0; i < NWOA_MAX_BSS; i++) {
        if (Dev->Bss[i].Used && RtlEqualMemory(Dev->Bss[i].B.bssid, b->bssid, 6)) { slot = i; break; }
        if (!Dev->Bss[i].Used && freeSlot < 0) freeSlot = i;
        if (Dev->Bss[i].Used && (weakest < 0 || Dev->Bss[i].B.rssi < Dev->Bss[weakest].B.rssi)) weakest = i;
    }
    if (slot < 0)
        slot = freeSlot >= 0 ? freeSlot : (weakest >= 0 && Dev->Bss[weakest].B.rssi < b->rssi ? weakest : -1);
    if (slot >= 0) {
        NWOA_BSS* e = &Dev->Bss[slot];
        ULONG n = 0;
        e->B = *b;
        // keep whole IEs only, up to the cache size
        if (b->ies) {
            while (n + 2 <= b->ies_len) {
                ULONG l = 2u + b->ies[n + 1];
                if (n + l > b->ies_len || n + l > NWOA_BSS_IE_MAX)
                    break;
                n += l;
            }
            RtlCopyMemory(e->Ies, b->ies, n);
        }
        e->B.ies = e->Ies;
        e->B.ies_len = n;
        e->SeenTime = KeQueryInterruptTime();
        e->Used = TRUE;
    }
    KeReleaseSpinLock(&Dev->StateLock, irql);
}

static void ConnFail(PNWOA_DEVICE Dev, const nb_event* e)
{
    if (Dev->ConnState != CONN_JOINING)
        return;
    Dev->ConnFailEvent = e->type;
    Dev->ConnFailStatus = e->status;
    Dev->ConnFailReason = e->reason;
    InterlockedExchange(&Dev->ConnState, CONN_FAILED);
    KeSetEvent(&Dev->ConnEvent, IO_NO_INCREMENT, FALSE);
}

static void LinkLost(PNWOA_DEVICE Dev, ULONG WdiReason)
{
    if (!Dev->Connected)
        return;
    Dev->LostReason = WdiReason;
    NwoaQueueJob(Dev, JOB_LINK_LOST);
}

void NwoaHandleEvent(PNWOA_DEVICE Dev, const UCHAR* Frame, ULONG Len)
{
    nb_event e;
    if (nb_event_parse(Frame, Len, &e) != 1)
        return;
    if (e.type != NB_E_ESCAN_RESULT) {
        NWOA_EVREC* r = &Dev->EvLog[Dev->EvLogNext % NWOA_EVLOG];
        r->Type = e.type; r->Status = e.status; r->Reason = e.reason; r->Flags = e.flags;
        r->Ms = (ULONG)(KeQueryInterruptTime() / 10000);
        Dev->EvLogNext++;
    }
    switch (e.type) {
    case NB_E_ESCAN_RESULT:
        if (!Dev->ScanRunning)
            break;
        Dev->ScanEvents++;
        if (e.status == NB_E_STATUS_PARTIAL) {
            nb_escan_hdr h;
            nb_bss b;
            nb_u32 off = 12;
            if (nb_escan_hdr_parse(e.data, e.datalen, &h) != NB_OK || h.sync_id != NB_ESCAN_SYNC_ID)
                break;
            for (int guard = 0; guard < 64; guard++)
                if (nb_escan_next(e.data, min(e.datalen, h.buflen), &off, Dev->IoType, &b) != 1)
                    break;
                else
                    CacheAdd(Dev, &b);
        } else {
            Dev->ScanStatus = (LONG)e.status;
            InterlockedExchange(&Dev->ScanRunning, 0);
            NwoaQueueJob(Dev, JOB_SCAN_DONE);
        }
        break;
    case NB_E_EAPOL_MSG: {
        // the firmware forwards EAPOL to the host as an event: hand it to the RX path as an
        // Ethernet frame so the Windows supplicant sees 1/4 (frame bytes are never logged)
        int fmt = 0;
        int slot = -1;
        InterlockedIncrement(&Dev->CntEapolEv);
        Dev->EapolEvLen = e.datalen;
        if (!(Dev->Connected || Dev->ConnState == CONN_LINKED))
            break;
        for (int i = 0; i < NWOA_EVRX; i++)
            if (!(Dev->EvRxBusy & (1u << i))) { slot = i; break; }
        ULONG next = (Dev->RxQTail + 1) % NWOA_RXQ;
        if (slot < 0 || next == Dev->RxQHead) {
            InterlockedIncrement(&Dev->CntEapolEvDrop);
            break;
        }
        static const UCHAR zero6[6] = { 0 };
        const UCHAR* peer = RtlEqualMemory(e.addr, zero6, 6) ? Dev->CurBssid : e.addr;
        ULONG n = nb_eapol_from_event(e.data, e.datalen, Dev->Mac, peer, Dev->EvRxBuf[slot], NWOA_SLOT_SIZE, &fmt);
        Dev->EapolEvFmt = (ULONG)fmt;
        if (!n) {
            InterlockedIncrement(&Dev->CntEapolEvDrop);
            break;
        }
        Dev->EvRxBusy |= 1u << slot;
        Dev->RxQ[Dev->RxQTail].Local = 1;
        Dev->RxQ[Dev->RxQTail].Slot = (USHORT)slot;
        Dev->RxQ[Dev->RxQTail].Off = 0;
        Dev->RxQ[Dev->RxQTail].Len = (USHORT)n;
        Dev->RxQTail = next;
        InterlockedIncrement(&Dev->CntEapolRx);
        InterlockedIncrement(&Dev->CntRx);
        InterlockedExchange(&Dev->RxNotifyPending, 1);
        break;
    }
    case NB_E_SET_SSID:
        if (e.status != NB_E_STATUS_SUCCESS)
            ConnFail(Dev, &e);
        break;
    case NB_E_AUTH:
        if (e.status != NB_E_STATUS_SUCCESS)
            ConnFail(Dev, &e);
        break;
    case NB_E_ASSOC:
    case NB_E_REASSOC:
        if (e.status != NB_E_STATUS_SUCCESS && e.status != NB_E_STATUS_UNSOLICITED)
            ConnFail(Dev, &e);
        break;
    case NB_E_LINK:
        if (e.flags & NB_EVENT_LINK_UP) {
            if (Dev->ConnState == CONN_JOINING) {
                RtlCopyMemory(Dev->CurBssid, e.addr, 6);
                InterlockedExchange(&Dev->ConnState, CONN_LINKED);
                KeSetEvent(&Dev->ConnEvent, IO_NO_INCREMENT, FALSE);
            }
        } else {
            ConnFail(Dev, &e);
            LinkLost(Dev, WDI_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST);
        }
        break;
    case NB_E_DEAUTH:
    case NB_E_DEAUTH_IND:
        ConnFail(Dev, &e);
        LinkLost(Dev, WDI_ASSOC_STATUS_PEER_DEAUTHENTICATED);
        break;
    case NB_E_DISASSOC:
    case NB_E_DISASSOC_IND:
        ConnFail(Dev, &e);
        LinkLost(Dev, WDI_ASSOC_STATUS_PEER_DISASSOCIATED);
        break;
    default:
        break;
    }
}
