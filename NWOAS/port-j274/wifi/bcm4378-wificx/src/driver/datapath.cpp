// datapath.cpp - NetAdapterCx TX/RX queues.
//
// The firmware's data path is Ethernet II (msgbuf TX_POST flag FRAME_802_3,
// RX_CMPLT frames with an Ethernet header; bwfm_pci_txdata / bwfm_rx).
// TX: every packet is copied into a DMA slot (msgbuf.cpp NwoaTxFrame) and
// returned to the OS at once.  A frame the OS hands over in 802.11 format
// (to-DS data + LLC/SNAP) is converted to Ethernet first.
// RX: frames are copied from the firmware RX buffers into the OS fragments;
// RxFrameFormat 0 = deliver Ethernet II (default), 1 = deliver 802.11 + LLC,
// 2 = follow what TX uses.  [hypothesis: WiFiCx wants 802.3 here - checked
// on hardware by the counters NwoaCntTx80211 / NwoaCntRx]
#include "precomp.h"

static void EvtSetReceiveFilter(NETADAPTER Adapter, NETRECEIVEFILTER Filter)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Filter);                 // the firmware filters; we pass everything up
}

NTSTATUS NwoaAdapterConfigure(PNWOA_DEVICE Dev, NETADAPTER Adapter)
{
    NET_ADAPTER_LINK_LAYER_CAPABILITIES ll;
    NET_ADAPTER_LINK_LAYER_CAPABILITIES_INIT(&ll, 433000000ull, 433000000ull);
    NetAdapterSetLinkLayerCapabilities(Adapter, &ll);
    NetAdapterSetLinkLayerMtuSize(Adapter, 1500);

    NET_ADAPTER_TX_CAPABILITIES tx;
    NET_ADAPTER_TX_CAPABILITIES_INIT(&tx, 1);
    NET_ADAPTER_RX_CAPABILITIES rx;
    NET_ADAPTER_RX_CAPABILITIES_INIT_SYSTEM_MANAGED(&rx, NWOA_SLOT_SIZE, 1);
    NetAdapterSetDataPathCapabilities(Adapter, &tx, &rx);

    NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES rf;
    NET_ADAPTER_RECEIVE_FILTER_CAPABILITIES_INIT(&rf, EvtSetReceiveFilter);
    rf.SupportedPacketFilters = NetPacketFilterFlagDirected | NetPacketFilterFlagMulticast |
                                NetPacketFilterFlagAllMulticast | NetPacketFilterFlagBroadcast;
    rf.MaximumMulticastAddresses = 32;
    NetAdapterSetReceiveFilterCapabilities(Adapter, &rf);

    NET_ADAPTER_LINK_LAYER_ADDRESS mac;
    NET_ADAPTER_LINK_LAYER_ADDRESS_INIT(&mac, 6, Dev->Mac);
    NetAdapterSetPermanentLinkLayerAddress(Adapter, &mac);
    NetAdapterSetCurrentLinkLayerAddress(Adapter, &mac);
    NwoaDiagDword(Dev, L"NwoaAdapterMacValid", Dev->MacValid);
    return STATUS_SUCCESS;
}

// NetAdapterCx starts the data path (creates the TX/RX queues) only after the adapter reports
// MediaConnectStateConnected; the MS WiFiCx sample's netv adapter reports it at init.  We report
// it when the firmware is associated and drop it on disconnect / link loss.
void NwoaReportLink(PNWOA_DEVICE Dev, BOOLEAN Up)
{
    NET_ADAPTER_LINK_STATE ls;
    if (!Dev->Adapter)
        return;
    NET_ADAPTER_LINK_STATE_INIT(&ls, Up ? 433000000ull : NDIS_LINK_SPEED_UNKNOWN,
                                Up ? MediaConnectStateConnected : MediaConnectStateDisconnected,
                                Up ? MediaDuplexStateFull : MediaDuplexStateUnknown,
                                NetAdapterPauseFunctionTypeUnsupported, NetAdapterAutoNegotiationFlagNone);
    NetAdapterSetLinkState(Dev->Adapter, &ls);
    Dev->LinkUpReported = Up;
    if (Up)
        InterlockedIncrement(&Dev->CntLinkUp);
}

// ---------------------------------------------------------------- TX
typedef struct _NWOA_TXQ {
    PNWOA_DEVICE Dev;
    NET_EXTENSION Va;
    UCHAR Buf[NWOA_SLOT_SIZE + 64];
} NWOA_TXQ;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NWOA_TXQ, NwoaGetTxq);

static void TxAdvance(NETPACKETQUEUE Queue)
{
    NWOA_TXQ* q = NwoaGetTxq(Queue);
    PNWOA_DEVICE Dev = q->Dev;
    NET_RING_COLLECTION const* rc = NetTxQueueGetRingCollection(Queue);
    NET_RING* pr = NetRingCollectionGetPacketRing(rc);
    NET_RING* fr = NetRingCollectionGetFragmentRing(rc);

    while (pr->BeginIndex != pr->EndIndex) {
        NET_PACKET* pkt = NetRingGetPacketAtIndex(pr, pr->BeginIndex);
        if (!pkt->Ignore && pkt->FragmentCount) {
            ULONG len = 0;
            BOOLEAN fits = TRUE;
            UINT32 fi = pkt->FragmentIndex;
            for (UINT32 k = 0; k < pkt->FragmentCount; k++) {
                NET_FRAGMENT* f = NetRingGetFragmentAtIndex(fr, fi);
                NET_FRAGMENT_VIRTUAL_ADDRESS* va = NetExtensionGetFragmentVirtualAddress(&q->Va, fi);
                if (len + f->ValidLength > NWOA_SLOT_SIZE) { fits = FALSE; break; }
                RtlCopyMemory(q->Buf + len, (PUCHAR)va->VirtualAddress + f->Offset, (SIZE_T)f->ValidLength);
                len += (ULONG)f->ValidLength;
                fi = NetRingIncrementIndex(fr, fi);
            }
            NTSTATUS st = STATUS_INVALID_PARAMETER;
            volatile LONG* why = &Dev->CntTxDropSize;
            if (fits) {
                if (pkt->Layout.Layer2Type == NetPacketLayer2TypeIeee80211 || nb_frame_kind(q->Buf, len) == NB_FRAME_80211) {
                    ULONG n = nb_80211_to_8023(q->Buf, len, q->Buf, sizeof(q->Buf));
                    InterlockedIncrement(&Dev->CntTx80211);
                    if (Dev->Cfg.RxFrameFormat == 2)
                        Dev->Cfg.RxFrameFormat = 3;      // auto: TX is 802.11, deliver RX the same way
                    len = n;
                    why = &Dev->CntTxDropConv;
                }
                st = len ? NwoaTxFrame(Dev, q->Buf, len) : STATUS_INVALID_PARAMETER;
                if (len && !NT_SUCCESS(st))
                    why = st == STATUS_DEVICE_NOT_READY ? &Dev->CntTxDropNotReady : &Dev->CntTxDropOther;
            }
            if (st == STATUS_DEVICE_BUSY)
                break;                                   // no slot: keep the packet, retry on TX status
            if (!NT_SUCCESS(st)) {
                InterlockedIncrement(&Dev->CntTxDrop);
                InterlockedIncrement(why);
            }
        }
        if (pkt->FragmentCount)
            fr->BeginIndex = NetRingAdvanceIndex(fr, pkt->FragmentIndex, pkt->FragmentCount);
        pr->BeginIndex = NetRingIncrementIndex(pr, pr->BeginIndex);
    }
    pr->NextIndex = pr->BeginIndex;
    fr->NextIndex = fr->BeginIndex;
}

static void TxSetNotification(NETPACKETQUEUE Queue, BOOLEAN Enabled)
{
    PNWOA_DEVICE Dev = NwoaGetTxq(Queue)->Dev;
    InterlockedExchange(&Dev->TxNotify, Enabled);
}

static void TxCancel(NETPACKETQUEUE Queue)
{
    NET_RING_COLLECTION const* rc = NetTxQueueGetRingCollection(Queue);
    NET_RING* pr = NetRingCollectionGetPacketRing(rc);
    NET_RING* fr = NetRingCollectionGetFragmentRing(rc);
    pr->BeginIndex = pr->NextIndex = pr->EndIndex;
    fr->BeginIndex = fr->NextIndex = fr->EndIndex;
}

static void TxDestroy(WDFOBJECT Object)
{
    PNWOA_DEVICE Dev = NwoaGetTxq((NETPACKETQUEUE)Object)->Dev;
    Dev->TxQueue = NULL;
}

void NwoaTxSlotsFreed(PNWOA_DEVICE Dev)
{
    NETPACKETQUEUE q = Dev->TxQueue;
    if (q && InterlockedExchange(&Dev->TxNotify, FALSE))
        NetTxQueueNotifyMoreCompletedPacketsAvailable(q);
}

NTSTATUS NwoaEvtCreateTxQueue(NETADAPTER Adapter, NETTXQUEUE_INIT* Init)
{
    PNWOA_DEVICE Dev = NwoaGetAdapterCtx(Adapter)->Dev;
    NET_PACKET_QUEUE_CONFIG cfg;
    WDF_OBJECT_ATTRIBUTES a;
    NETPACKETQUEUE q;
    NET_PACKET_QUEUE_CONFIG_INIT(&cfg, TxAdvance, TxSetNotification, TxCancel);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&a, NWOA_TXQ);
    a.EvtDestroyCallback = TxDestroy;
    NTSTATUS st = NetTxQueueCreate(Init, &a, &cfg, &q);
    NwoaDiagDword(Dev, L"NwoaTxQueueCreate", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;
    NWOA_TXQ* ctx = NwoaGetTxq(q);
    ctx->Dev = Dev;
    NET_EXTENSION_QUERY eq;
    NET_EXTENSION_QUERY_INIT(&eq, NET_FRAGMENT_EXTENSION_VIRTUAL_ADDRESS_NAME,
                             NET_FRAGMENT_EXTENSION_VIRTUAL_ADDRESS_VERSION_1, NetExtensionTypeFragment);
    NetTxQueueGetExtension(q, &eq, &ctx->Va);
    if (!ctx->Va.Enabled)
        return STATUS_NOT_SUPPORTED;
    Dev->TxQueue = q;
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------- RX
typedef struct _NWOA_RXQ_CTX {
    PNWOA_DEVICE Dev;
    NET_EXTENSION Va;
} NWOA_RXQ_CTX;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NWOA_RXQ_CTX, NwoaGetRxq);

static void RxAdvance(NETPACKETQUEUE Queue)
{
    NWOA_RXQ_CTX* q = NwoaGetRxq(Queue);
    PNWOA_DEVICE Dev = q->Dev;
    NET_RING_COLLECTION const* rc = NetRxQueueGetRingCollection(Queue);
    NET_RING* pr = NetRingCollectionGetPacketRing(rc);
    NET_RING* fr = NetRingCollectionGetFragmentRing(rc);
    KIRQL irql;
    BOOLEAN as80211 = Dev->Cfg.RxFrameFormat == 1 || Dev->Cfg.RxFrameFormat == 3;

    KeAcquireSpinLock(&Dev->RingLock, &irql);
    while (Dev->RxQHead != Dev->RxQTail && fr->BeginIndex != fr->EndIndex && pr->BeginIndex != pr->EndIndex) {
        NWOA_RXITEM it = Dev->RxQ[Dev->RxQHead];
        Dev->RxQHead = (Dev->RxQHead + 1) % NWOA_RXQ;
        UINT32 fi = fr->BeginIndex;
        NET_FRAGMENT* f = NetRingGetFragmentAtIndex(fr, fi);
        NET_FRAGMENT_VIRTUAL_ADDRESS* va = NetExtensionGetFragmentVirtualAddress(&q->Va, fi);
        PUCHAR src = it.Local ? Dev->EvRxBuf[it.Slot] + it.Off
                             : Dev->RxPool.Va + (SIZE_T)it.Slot * NWOA_SLOT_SIZE + it.Off;
        PUCHAR dst = (PUCHAR)va->VirtualAddress;
        ULONG n = 0;
        if (as80211) {
            // EAPOL after the pairwise key is installed was received encrypted (the firmware
            // decrypted it): mark it Protected.  Supplicants discard unprotected group-key /
            // rekey messages once a PTK exists (same rule as wpa_supplicant's "unencrypted
            // EAPOL-Key after PTK" check).  Hardware 2026-11-18 14:xx: the AP retried its
            // rekey message ten times, Windows never answered, the AP disassociated.
            // Only EAPOL gets the bit (RxProtectedBit=2 marks every frame, 0 none).
            BOOLEAN isEapol = it.Len >= 14 && nb_be16(src + 12) == NB_ETHERTYPE_EAPOL;
            int prot = Dev->PtkInstalled && Dev->Conn.Rsn &&
                       ((Dev->Cfg.RxProtectedBit == 1 && isEapol) || Dev->Cfg.RxProtectedBit == 2);
            n = nb_8023_to_80211_p(src, it.Len, Dev->CurBssid, prot, dst, (nb_u32)f->Capacity);
        } else if (it.Len <= f->Capacity) {
            RtlCopyMemory(dst, src, it.Len);
            n = it.Len;
        }
        NwoaRxItemDone(Dev, &it);
        if (!n) {
            InterlockedIncrement(&Dev->CntRxDrop);
            continue;
        }
        f->ValidLength = n;
        f->Offset = 0;
        NET_PACKET* pkt = NetRingGetPacketAtIndex(pr, pr->BeginIndex);
        pkt->FragmentIndex = fi;
        pkt->FragmentCount = 1;
        RtlZeroMemory(&pkt->Layout, sizeof(pkt->Layout));
        pkt->Layout.Layer2Type = as80211 ? NetPacketLayer2TypeIeee80211 : NetPacketLayer2TypeEthernet;
        fr->BeginIndex = NetRingIncrementIndex(fr, fr->BeginIndex);
        pr->BeginIndex = NetRingIncrementIndex(pr, pr->BeginIndex);
    }
    KeReleaseSpinLock(&Dev->RingLock, irql);
    pr->NextIndex = pr->BeginIndex;
    fr->NextIndex = fr->BeginIndex;
}

static void RxSetNotification(NETPACKETQUEUE Queue, BOOLEAN Enabled)
{
    PNWOA_DEVICE Dev = NwoaGetRxq(Queue)->Dev;
    InterlockedExchange(&Dev->RxNotify, Enabled);
    if (Enabled && Dev->RxQHead != Dev->RxQTail && InterlockedExchange(&Dev->RxNotify, FALSE))
        NetRxQueueNotifyMoreReceivedPacketsAvailable(Queue);
}

static void RxCancel(NETPACKETQUEUE Queue)
{
    PNWOA_DEVICE Dev = NwoaGetRxq(Queue)->Dev;
    NET_RING_COLLECTION const* rc = NetRxQueueGetRingCollection(Queue);
    NET_RING* pr = NetRingCollectionGetPacketRing(rc);
    NET_RING* fr = NetRingCollectionGetFragmentRing(rc);
    KIRQL irql;
    while (pr->BeginIndex != pr->EndIndex) {
        NetRingGetPacketAtIndex(pr, pr->BeginIndex)->Ignore = 1;
        pr->BeginIndex = NetRingIncrementIndex(pr, pr->BeginIndex);
    }
    pr->NextIndex = pr->BeginIndex;
    fr->BeginIndex = fr->NextIndex = fr->EndIndex;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    while (Dev->RxQHead != Dev->RxQTail) {           // give the firmware its buffers back
        NwoaRxItemDone(Dev, &Dev->RxQ[Dev->RxQHead]);
        Dev->RxQHead = (Dev->RxQHead + 1) % NWOA_RXQ;
    }
    KeReleaseSpinLock(&Dev->RingLock, irql);
}

static void RxDestroy(WDFOBJECT Object)
{
    PNWOA_DEVICE Dev = NwoaGetRxq((NETPACKETQUEUE)Object)->Dev;
    KIRQL irql;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    Dev->RxQueue = NULL;
    KeReleaseSpinLock(&Dev->RingLock, irql);
}

void NwoaRxDeliverNotify(PNWOA_DEVICE Dev)
{
    NETPACKETQUEUE q = Dev->RxQueue;
    if (q && InterlockedExchange(&Dev->RxNotify, FALSE))
        NetRxQueueNotifyMoreReceivedPacketsAvailable(q);
}

NTSTATUS NwoaEvtCreateRxQueue(NETADAPTER Adapter, NETRXQUEUE_INIT* Init)
{
    PNWOA_DEVICE Dev = NwoaGetAdapterCtx(Adapter)->Dev;
    NET_PACKET_QUEUE_CONFIG cfg;
    WDF_OBJECT_ATTRIBUTES a;
    NETPACKETQUEUE q;
    KIRQL irql;
    NET_PACKET_QUEUE_CONFIG_INIT(&cfg, RxAdvance, RxSetNotification, RxCancel);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&a, NWOA_RXQ_CTX);
    a.EvtDestroyCallback = RxDestroy;
    NTSTATUS st = NetRxQueueCreate(Init, &a, &cfg, &q);
    NwoaDiagDword(Dev, L"NwoaRxQueueCreate", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;
    NWOA_RXQ_CTX* ctx = NwoaGetRxq(q);
    ctx->Dev = Dev;
    NET_EXTENSION_QUERY eq;
    NET_EXTENSION_QUERY_INIT(&eq, NET_FRAGMENT_EXTENSION_VIRTUAL_ADDRESS_NAME,
                             NET_FRAGMENT_EXTENSION_VIRTUAL_ADDRESS_VERSION_1, NetExtensionTypeFragment);
    NetRxQueueGetExtension(q, &eq, &ctx->Va);
    if (!ctx->Va.Enabled)
        return STATUS_NOT_SUPPORTED;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    Dev->RxQueue = q;
    KeReleaseSpinLock(&Dev->RingLock, irql);
    // frames held before the queue existed are delivered once the OS enables notification
    return STATUS_SUCCESS;
}
