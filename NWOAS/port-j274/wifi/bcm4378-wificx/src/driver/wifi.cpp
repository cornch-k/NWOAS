// wifi.cpp - WiFiCx control path: capabilities, commands (M1/M3/M4),
// indications, firmware configuration, the work item that runs ioctls.
//
// WPA2-PSK: WiFiCx never hands the passphrase/PMK to the driver (no PMK TLV
// in dot11wificxtypes.hpp; only PMKID), so the Windows supplicant does the
// 4-way handshake over the data path (EAPOL frames) and plumbs the keys with
// WDI_SET_ADD_CIPHER_KEYS.  This is also what OpenBSD bwfm does on this chip:
// "sup_wpa" 0 (bwfm_init), keys through "wsec_key" (bwfm_set_key_cb).
#include "precomp.h"

// ------------------------------------------------------------ helpers
// Unsolicited indications go to the station port: the PortId of the last station
// command (scan/connect/disconnect/reset), never an adapter-level command's port.

static void SendIndication(PNWOA_DEVICE Dev, UINT16 Id, UINT32 TransId, NDIS_STATUS Status,
                           const WDI_MESSAGE_HEADER* Orig, const UCHAR* Tlv, ULONG TlvLen)
{
    WDFMEMORY mem;
    PUCHAR buf;
    WDF_OBJECT_ATTRIBUTES a;
    SIZE_T n = sizeof(WDI_MESSAGE_HEADER) + TlvLen;
    WDF_OBJECT_ATTRIBUTES_INIT(&a);
    a.ParentObject = Dev->Device;
    if (!NT_SUCCESS(WdfMemoryCreate(&a, NonPagedPoolNx, NWOA_TAG, n, &mem, (PVOID*)&buf)))
        return;
    RtlZeroMemory(buf, n);
    PWDI_MESSAGE_HEADER h = (PWDI_MESSAGE_HEADER)buf;
    h->PortId = Orig ? Orig->PortId : Dev->StaPortId;
    h->Status = Status;
    h->TransactionId = TransId;
    h->IhvSpecificId = Orig ? Orig->IhvSpecificId : 0;
    if (TlvLen && Tlv)
        RtlCopyMemory(buf + sizeof(WDI_MESSAGE_HEADER), Tlv, TlvLen);
    WifiDeviceReceiveIndication(Dev->Device, Id, mem);
    WdfObjectDelete(mem);
}

static void Unsolicited(PNWOA_DEVICE Dev, UINT16 Id, const UCHAR* Tlv, ULONG Len)
{
    SendIndication(Dev, Id, 0, NDIS_STATUS_SUCCESS, NULL, Tlv, Len);   // TransactionId 0
}

static void TaskBegin(NWOA_TASK* T, UINT16 Msg, UINT16 Done, const WDI_MESSAGE_HEADER* H)
{
    T->Active = TRUE;
    T->MessageId = Msg;
    T->CompleteId = Done;
    T->Hdr = *H;
}

static void TaskEnd(PNWOA_DEVICE Dev, NWOA_TASK* T, NDIS_STATUS Status)
{
    if (!T->Active)
        return;
    T->Active = FALSE;
    SendIndication(Dev, T->CompleteId, T->Hdr.TransactionId, Status, &T->Hdr, NULL, 0);
}

static void M3(WIFIREQUEST Req, NTSTATUS St)
{
    WifiRequestComplete(Req, St, sizeof(WDI_MESSAGE_HEADER));
}

void NwoaQueueJob(PNWOA_DEVICE Dev, LONG Job)
{
    InterlockedOr(&Dev->Jobs, Job);
    if (Dev->Work)
        WdfWorkItemEnqueue(Dev->Work);
}

static UINT32 LinkQuality(int rssi)
{
    if (rssi >= -50) return 100;
    if (rssi <= -100) return 0;
    return (UINT32)(2 * (rssi + 100));
}

static WDI_BAND_ID WdiBand(UCHAR band) { return band == 5 ? WDI_BAND_ID_5000 : WDI_BAND_ID_2400; }

// ------------------------------------------------ s258m diagnostics helpers
static ULONG NowMs() { return (ULONG)(KeQueryInterruptTime() / 10000); }
static ULONG SinceMs(ULONG64 Now, ULONG64 T)
{
    if (!T || T > Now) return 0xFFFFFFFFu;
    ULONG64 d = (Now - T) / 10000;
    return d > 0xFFFFFFF0ull ? 0xFFFFFFF0u : (ULONG)d;
}
static ULONG CipherBit(ULONG c) { return c < 32 ? (1u << c) : 0x80000000u; }

// s258n: firmware PM read back (-1 = failed)
static LONG ReadPm(PNWOA_DEVICE Dev)
{
    ULONG pm = 0;
    Dev->FwPm = NT_SUCCESS(NwoaGetInt(Dev, NB_C_GET_PM, &pm)) ? (LONG)pm : -1;
    return Dev->FwPm;
}

// s258n: change PM and set it back.  [hypothesis] a PM change makes the firmware tell the AP its
// power state again (null frame with the new PM bit); a scan made while connected leaves the AP
// believing we doze / the firmware holding TX.  Used after such a scan and by the stall watchdog.
// s258p: the PM value in force (PM2 while a ScanPm / candidate PM2 is applied, else PowerSave)
static ULONG PmTarget(PNWOA_DEVICE Dev)
{
    return (Dev->CandOn & NB_CAND_PM2) ? NB_PM_FAST_PS : Dev->Cfg.PowerSave;
}

static LONG PmKick(PNWOA_DEVICE Dev)
{
    ULONG pm = PmTarget(Dev);
    InterlockedIncrement(&Dev->CntPmKick);
    (void)NwoaSetInt(Dev, NB_C_SET_PM, pm ? 0 : 1);
    (void)NwoaSetInt(Dev, NB_C_SET_PM, pm);
    return ReadPm(Dev);
}

// ------------------------------------------------ s258p candidates (Linux brcmfmac differences)
// Bits in force: CandidateCycle=1 -> the rotating candidate (nb_cand_bits), else the static switches.
static ULONG CandBits(PNWOA_DEVICE Dev)
{
    if (Dev->Cfg.CandidateCycle)
        return nb_cand_bits(Dev->CandCur);
    return (Dev->Cfg.ScanPm == 2 ? NB_CAND_PM2 : 0) |
           (Dev->Cfg.ScanChanList == 1 ? NB_CAND_CH1 : Dev->Cfg.ScanChanList == 2 ? NB_CAND_CH2 : 0) |
           (Dev->Cfg.FlowResume ? NB_CAND_RSM : 0);
}

// brcmfmac iovars we do not set (brcmf_c_preinit_dcmds txbf, brcmf_configure_arp_nd_offload(true),
// brcmf_config_dongle FAKEFRAG, brcmf_dongle_roam bcn_timeout (roamoff), brcmf_cfg80211_attach obss_coex)
static const struct { PCSTR Name; ULONG Val; } IovTab[7] = {
    { "txbf", 1 }, { "arp_ol", 9 }, { "arpoe", 1 }, { "ndoe", 1 }, { NULL, 1 } /* FAKEFRAG */,
    { "bcn_timeout", 4 }, { "obss_coex", 0xFFFFFFFFu }
};

static NTSTATUS IovGet(PNWOA_DEVICE Dev, ULONG i, ULONG* V)
{
    UCHAR b[8];
    NTSTATUS st;
    if (!IovTab[i].Name)
        return NwoaGetInt(Dev, NB_C_GET_FAKEFRAG, V);
    RtlZeroMemory(b, sizeof(b));
    st = NwoaGetVar(Dev, IovTab[i].Name, b, 4);
    *V = NT_SUCCESS(st) ? nb_ld32(b) : 0;
    return st;
}

static NTSTATUS IovSet(PNWOA_DEVICE Dev, ULONG i, ULONG V)
{
    return IovTab[i].Name ? NwoaSetVarInt(Dev, IovTab[i].Name, V) : NwoaSetInt(Dev, NB_C_SET_FAKEFRAG, V);
}

// apply the firmware side of bits (PM2, IOV) not applied yet; PASSIVE (work item)
static void CandApply(PNWOA_DEVICE Dev, ULONG Bits)
{
    ULONG add = Bits & ~Dev->CandOn & (NB_CAND_PM2 | NB_CAND_IOV);
    if (!Dev->RingsUp || Dev->Dead || !Dev->RadioOn)
        return;
    if (add & NB_CAND_PM2) {
        NTSTATUS a = NwoaSetInt(Dev, NB_C_SET_PM, NB_PM_FAST_PS);
        NTSTATUS b = NwoaSetVarInt(Dev, "pm2_sleep_ret", 2000);   // min(timeout, BRCMF_PS_MAX_TIMEOUT_MS)
        Dev->CandPmSt = (NT_SUCCESS(a) ? 1 : 0) | (NT_SUCCESS(b) ? 2 : 0);
        Dev->CandOn |= NB_CAND_PM2;
        (void)ReadPm(Dev);
    }
    if (add & NB_CAND_IOV) {
        Dev->IovOldOk = Dev->IovSetOk = Dev->IovBackOk = 0;
        for (ULONG i = 0; i < 7; i++) {
            if (NT_SUCCESS(IovGet(Dev, i, &Dev->IovOld[i])))
                Dev->IovOldOk |= 1u << i;
            if (NT_SUCCESS(IovSet(Dev, i, IovTab[i].Val)))
                Dev->IovSetOk |= 1u << i;
        }
        Dev->CandOn |= NB_CAND_IOV;
    }
}

// put back what CandApply changed and Bits no longer wants
static void CandRevert(PNWOA_DEVICE Dev, ULONG Bits)
{
    ULONG drop = Dev->CandOn & ~Bits & (NB_CAND_PM2 | NB_CAND_IOV);
    if (!Dev->RingsUp || Dev->Dead || !Dev->RadioOn)
        return;
    if (drop & NB_CAND_PM2) {
        Dev->CandOn &= ~NB_CAND_PM2;
        NTSTATUS a = NwoaSetInt(Dev, NB_C_SET_PM, Dev->Cfg.PowerSave);
        Dev->CandPmSt |= NT_SUCCESS(a) ? 4 : 8;
        (void)ReadPm(Dev);
    }
    if (drop & NB_CAND_IOV) {
        Dev->CandOn &= ~NB_CAND_IOV;
        for (ULONG i = 0; i < 7; i++)
            if ((Dev->IovSetOk & (1u << i)) && (Dev->IovOldOk & (1u << i)) &&
                NT_SUCCESS(IovSet(Dev, i, Dev->IovOld[i])))
                Dev->IovBackOk |= 1u << i;
    }
}

// a scan verdict was recorded: rotate to the next candidate (CandidateCycle) and bring the firmware
// to its settings.  Static switches: just make sure the wanted bits are applied while connected.
static void CandService(PNWOA_DEVICE Dev)
{
    if (Dev->Cfg.CandidateCycle && Dev->CandAdvance && !Dev->ScanRunning) {
        Dev->CandAdvance = FALSE;
        Dev->CandCur = (Dev->CandCur + 1) % NB_CAND_N;
        InterlockedIncrement(&Dev->CntCandAdv);
        CandRevert(Dev, CandBits(Dev));
    }
    if (Dev->Connected && !Dev->ScanRunning)
        CandApply(Dev, CandBits(Dev));
}

// ------------------------------------------------ s258q test-scan shapes
// ShapeCycle: after a test-scan verdict the next test scan uses the next shape (0..5)
static void ShapeService(PNWOA_DEVICE Dev)
{
    if (Dev->Cfg.ShapeCycle && Dev->ShapeAdvance && !Dev->ScanRunning && !Dev->ChunkActive) {
        Dev->ShapeAdvance = FALSE;
        Dev->ShapeCur = (Dev->ShapeCur + 1) % NB_SHAPE_N;
        InterlockedIncrement(&Dev->CntShapeAdv);
    }
}

// shape 5 sequence given up (disconnect, Windows scan, refused escan): counted, next shape
static void ChunkAbort(PNWOA_DEVICE Dev)
{
    if (!Dev->ChunkActive)
        return;
    Dev->ChunkActive = FALSE;
    Dev->ShAbort[NB_SHAPE_CHUNKS]++;
    if (Dev->Cfg.ShapeCycle)
        Dev->ShapeAdvance = TRUE;
}

// the connected (home) channel: association record, else the firmware chanspec
static UCHAR HomeChannel(PNWOA_DEVICE Dev)
{
    UCHAR ch = Dev->Conn.Channel;
    if (!ch && Dev->FwChanspec) {
        nb_u8 c8 = 0, b8 = 0;
        nb_u16 w16 = 0;
        nb_chanspec_decode((nb_u16)Dev->FwChanspec, Dev->IoType, &c8, &b8, &w16);
        ch = c8;
    }
    return ch;
}

// WLC_GET_VALID_CHANNELS once (wl_uint32_list: count in, count + channels out)
static void ReadValidChannels(PNWOA_DEVICE Dev)
{
    UCHAR b[4 + 4 * NB_SCAN_CHLIST_MAX];
    if (Dev->ValidChSt == 1)
        return;
    RtlZeroMemory(b, sizeof(b));
    nb_st32(b, NB_SCAN_CHLIST_MAX);
    NTSTATUS st = NwoaIoctl(Dev, NB_C_GET_VALID_CHANNELS, b, sizeof(b), sizeof(b), NULL, NULL);
    ULONG n = NT_SUCCESS(st) ? nb_ld32(b) : 0;
    if (!NT_SUCCESS(st) || n > NB_SCAN_CHLIST_MAX) {
        Dev->ValidChSt = NT_SUCCESS(st) ? (LONG)STATUS_BUFFER_OVERFLOW : (LONG)st;
        Dev->NValidCh = 0;
        return;
    }
    for (ULONG i = 0; i < n; i++)
        Dev->ValidCh[i] = nb_ld32(b + 4 + 4 * i);
    Dev->NValidCh = n;
    Dev->ValidChSt = 1;
}

// s258q: the escan of a test-scan shape (NB_SHAPE_*).  Returns the chanspecs put in Cl; a shape that
// cannot be built (no home channel / no list) falls back to base (*Shape = 0, counted).
static ULONG ShapeRequest(PNWOA_DEVICE Dev, UCHAR* Shape, nb_escan_req* Rq, nb_u16* Cl)
{
    UCHAR home = HomeChannel(Dev);
    switch (*Shape) {
    case 1: {                                                     // foreign: one non-home channel
        ReadValidChannels(Dev);
        UCHAR f = nb_shape_foreign_ch(home, Dev->NValidCh ? Dev->ValidCh : NULL, Dev->NValidCh);
        if (!f)
            break;
        Dev->ShapeFch = f;
        Cl[0] = nb_chanspec20(f, Dev->IoType);
        return 1;
    }
    case 2:                                                       // homelong: home channel, long active dwell
        if (!home)
            break;
        Cl[0] = nb_chanspec20(home, Dev->IoType);
        Rq->active_ms = (nb_s32)Dev->Cfg.ShapeLongMs;
        return 1;
    case 3:                                                       // wild: brcmf_escan_prep wildcard form
        Rq->ssid = NULL;
        Rq->ssid_len = 0;
        Rq->n_list_ssids = 1;
        Rq->list_ssid = NULL;
        Rq->list_ssid_len = 0;
        Rq->scan_type = NB_SCANTYPE_ACTIVE;
        Dev->ScanDirected = 0;
        return 0;
    case 4:                                                       // passive, no SSID, full list
        Rq->ssid = NULL;
        Rq->ssid_len = 0;
        Rq->scan_type = NB_SCANTYPE_PASSIVE;
        Dev->ScanDirected = 0;
        return 0;
    case 5: {                                                     // chunks of ShapeChunkN channels
        ULONG nper = Dev->Cfg.ShapeChunkN;
        if (!Dev->ChunkActive) {
            ReadValidChannels(Dev);
            Dev->ChunkNCh = nb_shape_all_channels(Dev->ChunkCh, NB_SCAN_CHLIST_MAX,
                                                  Dev->NValidCh ? Dev->ValidCh : NULL, Dev->NValidCh, Dev->IoType);
            if (!Dev->ChunkNCh)
                break;
            Dev->ChunkTotal = (UCHAR)((Dev->ChunkNCh + nper - 1) / nper);
            Dev->ChunkIdx = 0;
            Dev->ChunkStuckAt = 0xFF;
            Dev->ChunkStartMs = NowMs();
            Dev->ChunkActive = TRUE;
        }
        ULONG first = (ULONG)Dev->ChunkIdx * nper;
        ULONG k = first < Dev->ChunkNCh ? min(nper, Dev->ChunkNCh - first) : 0;
        for (ULONG i = 0; i < k; i++)
            Cl[i] = Dev->ChunkCh[first + i];
        return k;
    }
    default:
        return 0;
    }
    if (*Shape < NB_SHAPE_N)
        Dev->ShFallback[*Shape]++;
    *Shape = 0;
    return 0;
}

// one sample of the data counters per diag tick (2 s), for "frames in the last 10 s"
static void SampleTick(PNWOA_DEVICE Dev)
{
    KIRQL irql;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    ULONG i = Dev->SampNext % 8;
    Dev->Samp[i].Ms = NowMs();
    Dev->Samp[i].Rx = Dev->CntRx;
    Dev->Samp[i].Tx = Dev->CntTx;
    Dev->Samp[i].TxDone = Dev->CntTxStatus;
    Dev->SampNext++;
    KeReleaseSpinLock(&Dev->StateLock, irql);
}

// caller holds StateLock: the newest sample at least 10 s old (else the oldest one)
static void Window10(PNWOA_DEVICE Dev, NWOA_DISCDIAG* D)
{
    ULONG now = NowMs(), n = Dev->SampNext < 8 ? Dev->SampNext : 8;
    int best = -1, oldest = -1;
    for (ULONG k = 0; k < n; k++) {
        ULONG i = (Dev->SampNext - 1 - k) % 8;           // newest first
        oldest = (int)i;
        if (now - Dev->Samp[i].Ms >= 10000) { best = (int)i; break; }
    }
    if (best < 0) best = oldest;
    if (best < 0) { D->Rx10 = D->Tx10 = D->TxDone10 = -1; D->SampleSpanMs = 0; return; }
    D->Rx10 = Dev->CntRx - Dev->Samp[best].Rx;
    D->Tx10 = Dev->CntTx - Dev->Samp[best].Tx;
    D->TxDone10 = Dev->CntTxStatus - Dev->Samp[best].TxDone;
    D->SampleSpanMs = now - Dev->Samp[best].Ms;
}

static ULONG RsnMaskOf(const UCHAR* Body, ULONG Len, ULONG* Group)
{
    ULONG m = 0;
    *Group = 0;
    if (!Body || Len <= 12)
        return 0;
    const nb_u8* rsn = nb_ie_find(Body + 12, Len - 12, 48, NULL);
    nb_u32 g = 0, pw = 0, akm = 0, mask = 0;
    nb_u16 caps = 0;
    if (rsn && nb_rsn_pairwise_mask(rsn, &mask) == NB_OK)
        m = mask;
    if (rsn && nb_rsn_parse(rsn, &g, &pw, &akm, &caps) == NB_OK)
        *Group = g;
    return m;
}

// per connect / roam command: the host's settings and every candidate (bits only)
static void ConnDiagRecord(PNWOA_DEVICE Dev, UINT16 Id, const WDI_CONNECT_PARAMETERS_CONTAINER& Cp,
                           const ArrayOfElements<WDI_CONNECT_BSS_ENTRY_CONTAINER>& List)
{
    NWOA_CONNDIAG d;
    KIRQL irql;
    RtlZeroMemory(&d, sizeof(d));
    d.Ms = NowMs();
    d.Id = Id;
    const WDI_CONNECTION_SETTINGS_CONTAINER& cs = Cp.ConnectionSettings;
    d.RoamRequest = cs.RoamRequest ? 1 : 0;
    d.Hidden = cs.HiddenNetwork ? 1 : 0;
    d.Mfp = cs.MFPEnabled ? 1 : 0;
    d.BssTrans = cs.BSSTransitionSupported ? 1 : 0;
    d.RoamNeededReason = cs.RoamNeededReason;
    d.RoamTrigger = (ULONG)cs.RoamTrigger;
    d.Auth0 = Cp.AuthenticationAlgorithms.ElementCount ? (ULONG)Cp.AuthenticationAlgorithms.pElements[0] : 0xFFFFFFFFu;
    d.NUc = (UCHAR)min(Cp.UnicastCipherAlgorithms.ElementCount, 255u);
    d.Uc0 = Cp.UnicastCipherAlgorithms.ElementCount ? (ULONG)Cp.UnicastCipherAlgorithms.pElements[0] : 0xFFFFFFFFu;
    d.Mc0 = Cp.MulticastCipherAlgorithms.ElementCount ? (ULONG)Cp.MulticastCipherAlgorithms.pElements[0] : 0xFFFFFFFFu;
    for (UINT32 i = 0; i < Cp.UnicastCipherAlgorithms.ElementCount; i++)
        d.UcMask |= CipherBit((ULONG)Cp.UnicastCipherAlgorithms.pElements[i]);
    for (UINT32 i = 0; i < Cp.MulticastCipherAlgorithms.ElementCount; i++)
        d.McMask |= CipherBit((ULONG)Cp.MulticastCipherAlgorithms.pElements[i]);
    d.NCand = (UCHAR)min(List.ElementCount, 255u);
    NWOA_BSS* cached = (NWOA_BSS*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NWOA_BSS), NWOA_TAG);
    for (UINT32 i = 0; i < List.ElementCount && i < NWOA_CANDMAX; i++) {
        const WDI_CONNECT_BSS_ENTRY_CONTAINER& be = List.pElements[i];
        USHORT b = (USHORT)(((ULONG)be.ChannelInfo.ChannelNumber & 0xFF) << 8);
        if (Dev->Connected && RtlEqualMemory(be.BSSID.Address, Dev->CurBssid, 6)) b |= 1;
        if (Dev->HaveLastConn && RtlEqualMemory(be.BSSID.Address, Dev->LastConnBssid, 6)) b |= 2;
        if (be.ChannelInfo.BandId == WDI_BAND_ID_5000) b |= 8;
        const WDI_BYTE_BLOB* fr = be.Optional.ProbeResponseFrame_IsPresent ? &be.ProbeResponseFrame :
                                  be.Optional.BeaconFrame_IsPresent ? &be.BeaconFrame : NULL;
        if (fr) b |= 16;
        BOOLEAN inCache = cached && NwoaScanCacheFind(Dev, be.BSSID.Address, cached);
        if (inCache) { b |= 4; d.CandRssi[i] = cached->B.rssi; }
        if (i == 0) {
            if (fr && fr->ElementCount > 12)
                d.ApPwMask = RsnMaskOf(fr->pElements, fr->ElementCount, &d.ApGroup);
            else if (inCache && cached->B.ies_len) {
                nb_u32 g = 0, pw = 0, akm = 0, mask = 0;
                nb_u16 caps = 0;
                const nb_u8* rsn = nb_ie_find(cached->B.ies, cached->B.ies_len, 48, NULL);
                if (rsn && nb_rsn_pairwise_mask(rsn, &mask) == NB_OK) d.ApPwMask = mask;
                if (rsn && nb_rsn_parse(rsn, &g, &pw, &akm, &caps) == NB_OK) d.ApGroup = g;
            }
        }
        d.Cand[i] = b;
    }
    if (cached)
        ExFreePoolWithTag(cached, NWOA_TAG);
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    Dev->ConnDiag[Dev->ConnDiagNext % NWOA_CONNDIAG_N] = d;
    Dev->ConnDiagNext++;
    KeReleaseSpinLock(&Dev->StateLock, irql);
}

static void KeyDiagRecord(PNWOA_DEVICE Dev, const WDI_SET_ADD_CIPHER_KEYS_PARAMETERS* a,
                          const WDI_SET_DELETE_CIPHER_KEYS_PARAMETERS* d, NTSTATUS St)
{
    NWOA_KEYDIAG k;
    KIRQL irql;
    RtlZeroMemory(&k, sizeof(k));
    k.Ms = NowMs();
    k.Accepted = (UCHAR)min(Dev->NumKeys, 255u);
    k.Status = (ULONG)St;
    if (a) {
        k.Add = 1;
        k.Elements = (UCHAR)min(a->SetCipherKey.ElementCount, 255u);
        for (UINT32 i = 0; i < a->SetCipherKey.ElementCount; i++) {
            const WDI_CIPHER_KEY_TYPE_INFO_CONTAINER& t = a->SetCipherKey.pElements[i].CipherKeyTypeInfo;
            if (i == 0) { k.Type = (UCHAR)t.KeyType; k.Cipher = (ULONG)t.CipherAlgorithm; }
            k.CipherMask |= CipherBit((ULONG)t.CipherAlgorithm);
        }
    } else if (d) {
        k.Elements = (UCHAR)min(d->CipherKeyInfo.ElementCount, 255u);
        for (UINT32 i = 0; i < d->CipherKeyInfo.ElementCount; i++) {
            const WDI_CIPHER_KEY_TYPE_INFO_CONTAINER& t = d->CipherKeyInfo.pElements[i].CipherKeyTypeInfo;
            if (i == 0) { k.Type = (UCHAR)t.KeyType; k.Cipher = (ULONG)t.CipherAlgorithm; }
            k.CipherMask |= CipherBit((ULONG)t.CipherAlgorithm);
        }
    }
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    Dev->KeyDiag[Dev->KeyDiagNext % NWOA_KEYDIAG_N] = k;
    Dev->KeyDiagNext++;
    KeReleaseSpinLock(&Dev->StateLock, irql);
}

// ------------------------------------------------ disconnect evidence
// caller holds StateLock: copy the last commands (oldest first) for "what came right before"
void NwoaCmdSnapshot(PNWOA_DEVICE Dev, ULONG Why)
{
    ULONG next = Dev->CmdLogNext;
    ULONG n = next < NWOA_CMDSNAP ? next : NWOA_CMDSNAP;
    for (ULONG i = 0; i < n; i++)
        Dev->CmdSnap[i] = Dev->CmdLog[(next - n + i) % NWOA_CMDLOG];
    Dev->CmdSnapN = n;
    Dev->CmdSnapWhy = Why;
    Dev->CmdSnapMs = (ULONG)(KeQueryInterruptTime() / 10000);
}

static void CmdRecord(PNWOA_DEVICE Dev, UINT16 Id, NTSTATUS St, ULONG Info)
{
    KIRQL irql;
    int slot = -1, freeSlot = -1;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    NWOA_CMDREC* r = &Dev->CmdLog[Dev->CmdLogNext % NWOA_CMDLOG];
    r->Ms = (ULONG)(KeQueryInterruptTime() / 10000);
    r->Id = Id;
    r->Conn = Dev->Connected ? 1 : 0;
    r->Pad = 0;
    r->Status = (ULONG)St;
    r->Info = Info;
    Dev->CmdLogNext++;
    for (int i = 0; i < NWOA_CMDHIST; i++) {
        if (Dev->CmdHist[i].Count && Dev->CmdHist[i].Id == Id) { slot = i; break; }
        if (!Dev->CmdHist[i].Count && freeSlot < 0) freeSlot = i;
    }
    if (slot < 0)
        slot = freeSlot;
    if (slot >= 0) {
        Dev->CmdHist[slot].Id = Id;
        Dev->CmdHist[slot].Count++;
    }
    KeReleaseSpinLock(&Dev->StateLock, irql);
}

// every NB_C_DISASSOC the driver sends goes through here (Src = DIS_*)
void NwoaHostDisassoc(PNWOA_DEVICE Dev, ULONG Src)
{
    KIRQL irql;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    Dev->HostDis[Dev->HostDisNext % 8].Ms = (ULONG)(KeQueryInterruptTime() / 10000);
    Dev->HostDis[Dev->HostDisNext % 8].Src = Src;
    Dev->HostDisNext++;
    Dev->LastHostDisassoc = KeQueryInterruptTime();
    NwoaCmdSnapshot(Dev, Src);
    KeReleaseSpinLock(&Dev->StateLock, irql);
    (void)NwoaSetInt(Dev, NB_C_DISASSOC, 0);
}

// Info bits of a connect / roam command (layout in nwoa.h); no SSID or BSSID values are kept
static ULONG ConnectInfo(PNWOA_DEVICE Dev, const WDI_CONNECT_PARAMETERS_CONTAINER& Cp,
                         const ArrayOfElements<WDI_CONNECT_BSS_ENTRY_CONTAINER>& List)
{
    ULONG info = 0;
    const WDI_CONNECTION_SETTINGS_CONTAINER& cs = Cp.ConnectionSettings;
    if (cs.RoamRequest) info |= 1;
    info |= ((ULONG)cs.RoamTrigger & 0xF) << 4;
    info |= ((ULONG)cs.RoamNeededReason & 0xFF) << 8;
    info |= (min(List.ElementCount, 255u)) << 24;
    if (List.ElementCount >= 1) {
        const WDI_CONNECT_BSS_ENTRY_CONTAINER& be = List.pElements[0];
        if (Dev->Connected && RtlEqualMemory(be.BSSID.Address, Dev->CurBssid, 6))
            info |= 1u << 16;
        if (be.ChannelInfo.BandId == WDI_BAND_ID_5000)
            info |= 1u << 17;
        if (Dev->HaveLastConn && RtlEqualMemory(be.BSSID.Address, Dev->LastConnBssid, 6))
            info |= 1u << 19;                                  // s258l: same AP as the last association
        NWOA_BSS* cached = (NWOA_BSS*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NWOA_BSS), NWOA_TAG);
        if (cached) {
            if (NwoaScanCacheFind(Dev, be.BSSID.Address, cached))
                info |= 1u << 18;
            ExFreePoolWithTag(cached, NWOA_TAG);
        }
    }
    return info;
}

static int CipherFromWdi(ULONG c)
{
    switch (c) {
    case WDI_CIPHER_ALGO_CCMP: return NB_CIPHER_CCMP;
    case WDI_CIPHER_ALGO_TKIP: return NB_CIPHER_TKIP;
    case WDI_CIPHER_ALGO_NONE: return NB_CIPHER_NONE;
    default: return -1;
    }
}

// ------------------------------------------------------- capabilities
NTSTATUS NwoaWifiSetCapabilities(PNWOA_DEVICE Dev)
{
    NTSTATUS st;
    WIFI_DEVICE_CAPABILITIES dc;
    WIFI_DEVICE_CAPABILITIES_INIT(&dc);
    dc.HardwareRadioState = TRUE;
    dc.SoftwareRadioState = TRUE;
    RtlStringCchCopyA((PCHAR)dc.FirmwareVersion, sizeof(dc.FirmwareVersion),
                      Dev->FwVersion[0] ? Dev->FwVersion : "bcm4378b1");
    dc.ActionFramesSupported = FALSE;
    dc.NumRxStreams = 2;
    dc.NumTxStreams = 2;
    dc.Support_eCSA = FALSE;
    dc.MACAddressRandomization = FALSE;
    dc.BluetoothCoexistenceSupport = WDI_BLUETOOTH_COEXISTENCE_UNKNOWN;
    dc.SupportsNonWdiOidRequests = FALSE;
    dc.FastTransitionSupported = FALSE;
    dc.MU_MIMOSupported = FALSE;
    dc.BSSTransitionSupported = FALSE;
    dc.SAEAuthenticationSupported = FALSE;
    dc.MBOSupported = FALSE;
    dc.BeaconReportsImplemented = FALSE;
    st = WifiDeviceSetDeviceCapabilities(Dev->Device, &dc);
    NwoaDiagDword(Dev, L"NwoaCapDevice", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;

    static const DOT11_AUTH_CIPHER_PAIR ucast[] = {
        { DOT11_AUTH_ALGO_80211_OPEN, DOT11_CIPHER_ALGO_NONE },
        { DOT11_AUTH_ALGO_RSNA_PSK, DOT11_CIPHER_ALGO_CCMP },
    };
    static const DOT11_AUTH_CIPHER_PAIR mcast[] = {
        { DOT11_AUTH_ALGO_80211_OPEN, DOT11_CIPHER_ALGO_NONE },
        { DOT11_AUTH_ALGO_RSNA_PSK, DOT11_CIPHER_ALGO_CCMP },
    };
    static const DOT11_AUTH_CIPHER_PAIR mgmt[] = {
        { DOT11_AUTH_ALGO_80211_OPEN, DOT11_CIPHER_ALGO_NONE },
    };
    WIFI_STATION_CAPABILITIES sc;
    WIFI_STATION_CAPABILITIES_INIT(&sc);
    sc.ScanSSIDListSize = 1;
    sc.DesiredSSIDListSize = 1;
    sc.PrivacyExemptionListSize = 1;
    sc.KeyMappingTableSize = 4;
    sc.DefaultKeyTableSize = 4;
    sc.WEPKeyValueMaxLength = 0;
    sc.MaxNumPerSTA = 4;
    sc.SupportedQOSFlags = 0;
    sc.HostFIPSModeImplemented = 0;
    sc.MFPCapable = 0;
    sc.AutoPowerSaveMode = FALSE;
    sc.BSSListCachemanagement = FALSE;
    sc.ConnectBSSSelectionOverride = FALSE;
    sc.MaxNetworkOffloadListSize = 0;
    sc.NumSupportedUnicastAlgorithms = ARRAYSIZE(ucast);
    sc.UnicastAlgorithmsList = const_cast<PDOT11_AUTH_CIPHER_PAIR>(ucast);
    sc.NumSupportedMulticastDataAlgorithms = ARRAYSIZE(mcast);
    sc.MulticastDataAlgorithmsList = const_cast<PDOT11_AUTH_CIPHER_PAIR>(mcast);
    sc.NumSupportedMulticastMgmtAlgorithms = ARRAYSIZE(mgmt);
    sc.MulticastMgmtAlgorithmsList = const_cast<PDOT11_AUTH_CIPHER_PAIR>(mgmt);
    st = WifiDeviceSetStationCapabilities(Dev->Device, &sc);
    NwoaDiagDword(Dev, L"NwoaCapStation", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;

    static WDI_PHY_TYPE phy24[] = { WDI_PHY_TYPE_HRDSSS, WDI_PHY_TYPE_ERP, WDI_PHY_TYPE_HT };
    static WDI_PHY_TYPE phy5[] = { WDI_PHY_TYPE_OFDM, WDI_PHY_TYPE_HT, WDI_PHY_TYPE_VHT };
    static WDI_CHANNEL_MAPPING_ENTRY ch24[13];
    static WDI_CHANNEL_MAPPING_ENTRY ch5[25];
    static const UCHAR c5[25] = { 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128,
                                  132, 136, 140, 144, 149, 153, 157, 161, 165 };
    static UINT32 widths[] = { 20 };
    static UINT32 widths24[] = { 20, 40 };                          // s258m LinkInfoReal: BCM4378 HT40 / VHT80
    static UINT32 widths5[] = { 20, 40, 80 };
    BOOLEAN realW = Dev->Cfg.LinkInfoReal != 0;
    for (UCHAR i = 0; i < 13; i++) {
        ch24[i].ChannelNumber = i + 1;
        ch24[i].ChannelCenterFrequency = nb_chan_freq(2, (nb_u8)(i + 1));
    }
    for (int i = 0; i < 25; i++) {
        ch5[i].ChannelNumber = c5[i];
        ch5[i].ChannelCenterFrequency = nb_chan_freq(5, c5[i]);
    }
    WIFI_BAND_INFO bi[2];
    RtlZeroMemory(bi, sizeof(bi));
    bi[0].BandID = WDI_BAND_ID_2400;
    bi[0].BandState = TRUE;
    bi[0].NumValidPhyTypes = ARRAYSIZE(phy24);
    bi[0].ValidPhyTypeList = phy24;
    bi[0].NumValidChannelTypes = ARRAYSIZE(ch24);
    bi[0].ValidChannelTypes = ch24;
    bi[0].NumChannelWidths = realW ? ARRAYSIZE(widths24) : 1;
    bi[0].ChannelWidthList = realW ? widths24 : widths;
    bi[1].BandID = WDI_BAND_ID_5000;
    bi[1].BandState = TRUE;
    bi[1].NumValidPhyTypes = ARRAYSIZE(phy5);
    bi[1].ValidPhyTypeList = phy5;
    bi[1].NumValidChannelTypes = ARRAYSIZE(ch5);
    bi[1].ValidChannelTypes = ch5;
    bi[1].NumChannelWidths = realW ? ARRAYSIZE(widths5) : 1;
    bi[1].ChannelWidthList = realW ? widths5 : widths;
    WIFI_BAND_CAPABILITIES bc;
    RtlZeroMemory(&bc, sizeof(bc));
    bc.Size = sizeof(bc);
    bc.NumBands = 2;
    bc.BandInfoList = bi;
    st = WifiDeviceSetBandCapabilities(Dev->Device, &bc);
    NwoaDiagDword(Dev, L"NwoaCapBand", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;

    static WIFI_PHY_INFO pi[5];
    static const WDI_PHY_TYPE pt[5] = { WDI_PHY_TYPE_HRDSSS, WDI_PHY_TYPE_ERP, WDI_PHY_TYPE_OFDM,
                                        WDI_PHY_TYPE_HT, WDI_PHY_TYPE_VHT };
    static const USHORT rates[] = { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };  // 500 kb/s units
    RtlZeroMemory(pi, sizeof(pi));
    for (int k = 0; k < 5; k++) {
        ULONG n = 0;
        pi[k].PhyType = pt[k];
        for (int r = 0; r < ARRAYSIZE(rates); r++) {
            BOOLEAN cck = rates[r] == 2 || rates[r] == 4 || rates[r] == 11 || rates[r] == 22;
            if (pt[k] == WDI_PHY_TYPE_HRDSSS && !cck) continue;
            if (pt[k] == WDI_PHY_TYPE_OFDM && cck) continue;
            pi[k].DataRateList[n].DataRateFlag = WDI_DATA_RATE_RX_RATE | WDI_DATA_RATE_TX_RATE;
            pi[k].DataRateList[n].DataRateValue = rates[r];
            n++;
        }
        pi[k].NumberDataRateEntries = n;
    }
    WIFI_PHY_CAPABILITIES pc;
    RtlZeroMemory(&pc, sizeof(pc));
    pc.Size = sizeof(pc);
    pc.NumPhyTypes = 5;
    pc.PhyInfoList = pi;
    st = WifiDeviceSetPhyCapabilities(Dev->Device, &pc);
    NwoaDiagDword(Dev, L"NwoaCapPhy", (ULONG)st);
    return st;
}

// ------------------------------------------------- firmware configuration
static NTSTATUS LoadBlob(PNWOA_DEVICE Dev, PCSTR Var, const UCHAR* Blob, ULONG Len, PCWSTR DiagName)
{
    UCHAR* chunk;
    NTSTATUS st = STATUS_SUCCESS;
    ULONG n = nb_dload_count(Len), i;
    if (!Blob || !Len) {
        NwoaDiagDword(Dev, DiagName, 0xFFFFFFFF);
        return STATUS_SUCCESS;
    }
    chunk = (UCHAR*)ExAllocatePool2(POOL_FLAG_NON_PAGED, NB_DLOAD_HDR_LEN + NB_DLOAD_MAX_LEN, NWOA_TAG);
    if (!chunk)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (i = 0; i < n; i++) {
        ULONG cl = nb_dload_chunk(Blob, Len, i, chunk, NB_DLOAD_HDR_LEN + NB_DLOAD_MAX_LEN);
        st = NwoaSetVar(Dev, Var, chunk, cl);
        if (!NT_SUCCESS(st))
            break;
    }
    ExFreePoolWithTag(chunk, NWOA_TAG);
    NwoaDiagDword(Dev, DiagName, NT_SUCCESS(st) ? n : (0x80000000u | i));
    return st;
}

NTSTATUS NwoaFirmwareConfigure(PNWOA_DEVICE Dev)
{
    NTSTATUS st;
    ULONG v = 0;
    UCHAR b[256];

    NwoaSetStage(Dev, "fw_config_get_version", STATUS_PENDING);
    st = NwoaGetInt(Dev, NB_C_GET_VERSION, &v);
    NwoaDiagDword(Dev, L"NwoaIoType", v);
    if (!NT_SUCCESS(st)) {
        // first round trip failed: rings, doorbell or the DMA path (S257 verdict IOCTL_NO_REPLY)
        NwoaSetStage(Dev, "fail_first_ioctl", st);
        return st;
    }
    Dev->IoType = (v == NB_IO_TYPE_D11N) ? NB_IO_TYPE_D11N : NB_IO_TYPE_D11AC;
    st = NwoaGetVar(Dev, "cur_etheraddr", b, 6);
    if (NT_SUCCESS(st)) {
        RtlCopyMemory(Dev->Mac, b, 6);
        Dev->MacValid = TRUE;
        NwoaDiagDword(Dev, L"NwoaMacMatchesBoard",
                      Dev->BoardInfo.have_mac ? RtlEqualMemory(b, Dev->BoardInfo.mac, 6) : 2);
    }
    RtlZeroMemory(b, sizeof(b));
    if (NT_SUCCESS(NwoaGetVar(Dev, "ver", b, sizeof(b) - 1))) {
        RtlStringCchCopyA(Dev->FwVersion, sizeof(Dev->FwVersion), (PCSTR)b);
        for (char* p = Dev->FwVersion; *p; p++)
            if (*p == '\n' || *p == '\r') *p = ' ';
        NwoaDiagSz(Dev, L"NwoaFwVersion", Dev->FwVersion);
    }
    NwoaSetStage(Dev, "fw_config_blobs", STATUS_PENDING);
    st = LoadBlob(Dev, "clmload", Dev->Clm, Dev->ClmLen, L"NwoaClmChunks");
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_clmload", st); return st; }
    if (NT_SUCCESS(NwoaGetVar(Dev, "clmload_status", b, 4)))
        NwoaDiagDword(Dev, L"NwoaClmStatus2", nb_ld32(b));
    st = LoadBlob(Dev, "txcapload", Dev->Txcap, Dev->TxcapLen, L"NwoaTxcapChunks");
    if (!NT_SUCCESS(st))
        NwoaDiagDword(Dev, L"NwoaTxcapError", (ULONG)st);           // not fatal
    st = LoadBlob(Dev, "calload", Dev->BoardInfo.cal, Dev->BoardInfo.cal_len, L"NwoaCalChunks");
    if (!NT_SUCCESS(st))
        NwoaDiagDword(Dev, L"NwoaCalError", (ULONG)st);             // not fatal

    Dev->InitIovSt = 0;                                          // s258p: a new firmware run
    Dev->CandOn = 0;                                             //   (PM2 / IOV of a candidate are gone)
    Dev->ChunkActive = FALSE;                                    // s258q: no sequence across firmware runs
    Dev->ValidChSt = 0;
    NwoaSetVarInt(Dev, "mpc", (Dev->Cfg.InitIovarSet & 32) ? 1 : Dev->Cfg.Mpc);   // s258p bit 32: brcmfmac mpc 1
    if ((Dev->Cfg.InitIovarSet & 1) && NT_SUCCESS(NwoaSetVarInt(Dev, "txbf", 1)))  // brcmf_c_preinit_dcmds
        Dev->InitIovSt |= 1;
    if (Dev->Cfg.Country[0]) {
        UCHAR cc[12] = { 0 };
        cc[0] = (UCHAR)Dev->Cfg.Country[0];
        cc[1] = (UCHAR)Dev->Cfg.Country[1];
        nb_st32(cc + 4, 0xFFFFFFFF);                                 // rev -1
        cc[8] = cc[0];
        cc[9] = cc[1];
        NwoaDiagDword(Dev, L"NwoaCountrySet", (ULONG)NwoaSetVar(Dev, "country", cc, 12));
    }
    if (NT_SUCCESS(NwoaGetVar(Dev, "country", b, 12))) {
        b[3] = 0;
        NwoaDiagSz(Dev, L"NwoaCountry", (PCSTR)b);
    }
    {
        static const nb_u32 ev[] = { NB_E_SET_SSID, NB_E_AUTH, NB_E_DEAUTH, NB_E_DEAUTH_IND, NB_E_ASSOC,
                                     NB_E_REASSOC, NB_E_DISASSOC, NB_E_DISASSOC_IND, NB_E_LINK, NB_E_ROAM,
                                     NB_E_EAPOL_MSG, NB_E_IF, NB_E_ESCAN_RESULT };
        static const ULONG lens[] = { NB_EVENT_MASK_LEN, 16, 20, 24 };
        UCHAR mask[24];
        nb_u32 all[NB_E_LAST + 1 + ARRAYSIZE(ev)];
        nb_u32 nall = 0;
        for (ULONG i = 0; i < ARRAYSIZE(ev); i++)
            all[nall++] = ev[i];
        // s258n: DiagEvents=1 the 8 extra types, 2 every type but the frequent ones; counted only (events.cpp)
        if (Dev->Cfg.DiagEvents == 1)
            for (ULONG i = 0; i < NWOA_EVDIAG_N; i++)
                all[nall++] = NwoaDiagEventTypes[i];
        else if (Dev->Cfg.DiagEvents >= 2)
            for (ULONG t = 0; t <= NB_E_LAST; t++)
                if (!NwoaEventIsBase(t) && !NwoaEventIsNoisy(t))
                    all[nall++] = t;
        st = STATUS_UNSUCCESSFUL;
        for (int i = 0; i < ARRAYSIZE(lens) && !NT_SUCCESS(st); i++) {
            nb_event_mask(mask, lens[i], all, nall);
            st = NwoaSetVar(Dev, "event_msgs", mask, lens[i]);
        }
        NwoaDiagDword(Dev, L"NwoaEventMask", (ULONG)st);
    }
    {
        UCHAR jp[8] = { 4, 2, 8, 1, 1, 2, 0, 0 };                    // bwfm_init join_pref: RSSI delta (5G +8), RSSI
        NwoaSetVar(Dev, "join_pref", jp, sizeof(jp));
    }
    {
        // the firmware's own roaming settings as it boots (recorded, not secret), then roam_off:
        // in WDI the host decides roams (OID_WDI_TASK_ROAM); a firmware roam would move the link
        // to another BSSID without any indication to Windows (brcmfmac "roamoff" does the same)
        static const ULONG NB_C_GET_ROAM_TRIGGER = 54, NB_C_GET_ROAM_DELTA = 56, NB_C_GET_ROAM_SCAN_PERIOD = 58;
        UCHAR rb[8];
        ULONG rv = 0;
        RtlZeroMemory(rb, sizeof(rb));
        st = NwoaGetVar(Dev, "roam_off", rb, 4);
        NwoaDiagDword(Dev, L"NwoaFwRoamOffWas", NT_SUCCESS(st) ? nb_ld32(rb) : 0xFFFFFFFFu);
        RtlZeroMemory(rb, sizeof(rb));
        st = NwoaGetVar(Dev, "bcn_timeout", rb, 4);
        NwoaDiagDword(Dev, L"NwoaFwBcnTimeout", NT_SUCCESS(st) ? nb_ld32(rb) : 0xFFFFFFFFu);
        RtlZeroMemory(rb, sizeof(rb));
        st = NwoaIoctl(Dev, NB_C_GET_ROAM_TRIGGER, rb, 8, 8, NULL, NULL);
        NwoaDiagDword(Dev, L"NwoaFwRoamTrigger", NT_SUCCESS(st) ? nb_ld32(rb) : 0xFFFFFFFFu);
        st = NwoaGetInt(Dev, NB_C_GET_ROAM_DELTA, &rv);
        NwoaDiagDword(Dev, L"NwoaFwRoamDelta", NT_SUCCESS(st) ? rv : 0xFFFFFFFFu);
        st = NwoaGetInt(Dev, NB_C_GET_ROAM_SCAN_PERIOD, &rv);
        NwoaDiagDword(Dev, L"NwoaFwRoamScanPeriod", NT_SUCCESS(st) ? rv : 0xFFFFFFFFu);
        NwoaDiagDword(Dev, L"NwoaFwRoamOffSet", (ULONG)NwoaSetVarInt(Dev, "roam_off", Dev->Cfg.FwRoamOff ? 1 : 0));
    }
    NwoaSetInt(Dev, NB_C_SET_SCAN_CHANNEL_TIME, 40);
    NwoaSetInt(Dev, NB_C_SET_SCAN_UNASSOC_TIME, 40);
    NwoaSetInt(Dev, NB_C_SET_SCAN_PASSIVE_TIME, 120);
    NwoaSetInt(Dev, NB_C_SET_PM, Dev->Cfg.PowerSave);
    NwoaSetStage(Dev, "fw_config_up", STATUS_PENDING);
    st = NwoaSetInt(Dev, NB_C_UP, 0);
    NwoaDiagDword(Dev, L"NwoaUpStatus", (ULONG)st);
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_up", st); return st; }
    NwoaDiagDword(Dev, L"NwoaFwPmAfterUp", (ULONG)ReadPm(Dev));   // s258n: PM was set before UP
    NwoaSetInt(Dev, NB_C_SET_INFRA, 1);
    NwoaSetInt(Dev, NB_C_SET_AP, 0);
    if (Dev->Cfg.InitIovarSet & 2) {
        // s258p: brcmf_configure_arp_nd_offload(ifp, true) (ARP_OL_AGENT | ARP_OL_PEER_AUTO_REPLY)
        if (NT_SUCCESS(NwoaSetVarInt(Dev, "arp_ol", 9)) && NT_SUCCESS(NwoaSetVarInt(Dev, "arpoe", 1)) &&
            NT_SUCCESS(NwoaSetVarInt(Dev, "ndoe", 1)))
            Dev->InitIovSt |= 2;
    } else {
        NwoaSetVarInt(Dev, "arp_ol", 0);
        NwoaSetVarInt(Dev, "arpoe", 0);
        NwoaSetVarInt(Dev, "ndoe", 0);
    }
    NwoaSetVarInt(Dev, "toe", 0);
    if ((Dev->Cfg.InitIovarSet & 4) && NT_SUCCESS(NwoaSetInt(Dev, NB_C_SET_FAKEFRAG, 1)))   // brcmf_config_dongle
        Dev->InitIovSt |= 4;
    if ((Dev->Cfg.InitIovarSet & 8) && NT_SUCCESS(NwoaSetVarInt(Dev, "bcn_timeout", 4)))    // brcmf_dongle_roam, roamoff
        Dev->InitIovSt |= 8;
    if ((Dev->Cfg.InitIovarSet & 16) && NT_SUCCESS(NwoaSetVarInt(Dev, "obss_coex", 0xFFFFFFFFu)))   // BRCMF_OBSS_COEX_AUTO
        Dev->InitIovSt |= 16;
    if (Dev->Cfg.InitIovarSet & 32)
        Dev->InitIovSt |= 32;
    NwoaDiagDword(Dev, L"NwoaInitIovSt", Dev->InitIovSt);
    NwoaDiagDword(Dev, L"NwoaSupWpa0", (ULONG)NwoaSetVarInt(Dev, "sup_wpa", 0));
    RtlZeroMemory(b, 8);
    if (NT_SUCCESS(NwoaGetVar(Dev, "scan_ver", b, 8)))
        Dev->ScanVer = nb_ld16(b + 4);
    else
        Dev->ScanVer = 0;                                           // S257: -23 -> escan v1
    NwoaDiagDword(Dev, L"NwoaScanVer", (ULONG)Dev->ScanVer);
    Dev->RadioOn = TRUE;
    return STATUS_SUCCESS;
}

// ------------------------------------------------------------ jobs
static void JobScanDone(PNWOA_DEVICE Dev);
static void SfIdleEnd(PNWOA_DEVICE Dev);

// Firmware escan.  Self = TRUE: s258o test scan (TestConnScanSec), directed to the connected SSID as
// the scan Windows asks for 180 s into a connection (s258n scan0: trigger 3, directed); no WDI task.
static void ScanStartFw(PNWOA_DEVICE Dev, BOOLEAN Self)
{
    UCHAR p[8 + 80 + 2 * NB_SCAN_CHLIST_MAX + 8 + 40];
    nb_u16 cl[NB_SCAN_CHLIST_MAX];
    ULONG n, nch = 0;
    ULONG deadlineS = 12;
    NTSTATUS st;
    nb_escan_req rq;
    BOOLEAN seqNext = Self && Dev->ChunkActive;                   // s258q: next escan of a shape 5 sequence
    Dev->ScanWhileConn = FALSE;
    Dev->ScanCand = 0xFF;
    Dev->ScanBits = 0;
    Dev->ScanNCh = 0;
    Dev->ScanShape = 0xFF;
    Dev->SelfScan = Self;
    if (Self) {
        Dev->ScanSsidLen = min(Dev->Conn.SsidLen, 32ul);
        RtlCopyMemory(Dev->ScanSsid, Dev->Conn.Ssid, Dev->ScanSsidLen);
        Dev->ScanDirected = Dev->ScanSsidLen ? 1 : 0;
        Dev->ScanTrigger = 0xE;                                   // NwoaScanDiag t14 = test scan
        InterlockedIncrement(&Dev->CntSelfScan);
    }
    RtlZeroMemory(&rq, sizeof(rq));
    rq.nprobes = rq.active_ms = rq.passive_ms = rq.home_ms = -1;
    rq.ssid = Dev->ScanSsidLen ? Dev->ScanSsid : NULL;
    rq.ssid_len = Dev->ScanSsidLen;
    // a directed scan (hidden network profile) must probe; otherwise ScanPassive decides
    rq.scan_type = Dev->ScanSsidLen ? NB_SCANTYPE_ACTIVE : (Dev->Cfg.ScanPassive ? NB_SCANTYPE_PASSIVE : NB_SCANTYPE_ACTIVE);
    if (Dev->Connected) {
        KIRQL irql;
        NWOA_CSCAN c;
        SfIdleEnd(Dev);                                           // a new scan: the old watch ends
        CandService(Dev);                                         // s258p: settings of this scan's candidate
        ULONG bits = CandBits(Dev);
        Dev->ScanCand = Dev->Cfg.CandidateCycle ? (UCHAR)Dev->CandCur : 0xFF;
        Dev->ScanBits = (UCHAR)bits;
        if (bits & (NB_CAND_CH1 | NB_CAND_CH2)) {
            UCHAR ch = HomeChannel(Dev);
            if (bits & NB_CAND_CH1)
                ReadValidChannels(Dev);
            nch = nb_scan_chanlist(cl, NB_SCAN_CHLIST_MAX, (bits & NB_CAND_CH2) ? 2 : 1, ch,
                                   Dev->NValidCh ? Dev->ValidCh : NULL, Dev->NValidCh, Dev->IoType);
            Dev->ScanNCh = (UCHAR)nch;
        }
        if (Self) {                                               // s258q: shape of this test scan
            UCHAR shape;
            if (!seqNext)
                ShapeService(Dev);
            shape = (UCHAR)(seqNext ? (ULONG)NB_SHAPE_CHUNKS :
                            (Dev->Cfg.ShapeCycle ? Dev->ShapeCur : Dev->Cfg.TestScanShape));
            if (shape) {                                          // a shape replaces the candidate list
                nch = ShapeRequest(Dev, &shape, &rq, cl);
                Dev->ScanNCh = (UCHAR)nch;
            }
            Dev->ScanShape = shape;
            if (shape == NB_SHAPE_HOMELONG || shape == NB_SHAPE_PASSIVE)
                deadlineS = 25;
            if (!seqNext)
                Dev->Ev124NCs = 0;
        }
        if (!seqNext) {
            Dev->ScanEvBase124 = Dev->EvAllCnt[NB_E_CCA_CHAN_QUAL];
            Dev->ScanEvBase120 = Dev->EvAllCnt[NB_E_MSCH];
        }
        NwoaSnapTake(Dev, 0);                                     // s273: firmware RAM before the escan
        RtlZeroMemory(&c, sizeof(c));
        c.PmBefore = ReadPm(Dev);
        c.StartMs = NowMs();
        c.OutStart = NwoaTxOutstanding(Dev, NULL);
        c.PmAfter = -1;
        KeAcquireSpinLock(&Dev->StateLock, &irql);
        Dev->CScan[Dev->CScanNext % NWOA_CSCAN_N] = c;
        Dev->CScanNext++;
        Dev->CScanDoneMark = (ULONG)Dev->CntTxStatus;
        Dev->ConnScanStart = KeQueryInterruptTime();
        Dev->LastConnScanAny = Dev->ConnScanStart;
        KeReleaseSpinLock(&Dev->StateLock, irql);
        Dev->ScanWhileConn = TRUE;
        InterlockedIncrement(&Dev->CntConnScan);
        if (Dev->Cfg.ScanTxHold)
            InterlockedExchange(&Dev->TxHold, 1);                 // s258o test: nothing posted during the scan
    }
    NwoaScanCacheClear(Dev);
    InterlockedIncrement(&Dev->CntScans);
    rq.scan_ver = Dev->ScanVer == 2 || Dev->ScanVer == 3 ? Dev->ScanVer : 1;
    rq.sync_id = NB_ESCAN_SYNC_ID;
    rq.chs = nch ? cl : NULL;                                     // s258p ScanChanList / candidate CH1 CH2,
    rq.nch = nch;                                                 //   s258q shapes 1 2 5
    n = nb_escan_params_ex(p, sizeof(p), &rq);
    Dev->ScanStatus = -1;
    Dev->ScanDeadline = KeQueryInterruptTime() + (ULONG64)deadlineS * 10000000ull;   // 12 s (s258q shapes 2 4: 25 s)
    InterlockedExchange(&Dev->ScanRunning, 1);
    st = NwoaSetVar(Dev, "escan", p, n);
    NwoaDiagDword(Dev, L"NwoaEscanRequest", (ULONG)st);
    if (!NT_SUCCESS(st)) {
        InterlockedExchange(&Dev->ScanRunning, 0);
        if (Dev->ScanWhileConn && Dev->ScanCand < NB_CAND_N) {   // s258p: refused escan (e.g. a chanspec list)
            Dev->CandErr[Dev->ScanCand]++;
            Dev->CandAdvance = TRUE;
        }
        if (Dev->ScanShape < NB_SHAPE_N) {                        // s258q: refused shape escan
            Dev->ShErr[Dev->ScanShape]++;
            if (Dev->Cfg.ShapeCycle)
                Dev->ShapeAdvance = TRUE;
            Dev->ChunkActive = FALSE;
        }
        if (InterlockedExchange(&Dev->TxHold, 0))
            NwoaTxSlotsFreed(Dev);
        Dev->ScanWhileConn = FALSE;
        Dev->SelfScan = FALSE;
        if (!Self)
            TaskEnd(Dev, &Dev->ScanTask, NDIS_STATUS_FAILURE);
        else if (Dev->ScanStartDeferred) {
            Dev->ScanStartDeferred = FALSE;
            NwoaQueueJob(Dev, JOB_SCAN_START);
        }
    }
}

static void JobScanStart(PNWOA_DEVICE Dev)
{
    if (!Dev->ScanTask.Active)
        return;
    if (!Dev->RingsUp || Dev->Dead || !Dev->RadioOn) {
        TaskEnd(Dev, &Dev->ScanTask, NDIS_STATUS_FAILURE);
        return;
    }
    // s258o: a test scan is running; the Windows task starts when it is done (JobScanDone)
    if (Dev->ScanRunning && Dev->SelfScan) {
        Dev->ScanStartDeferred = TRUE;
        InterlockedIncrement(&Dev->CntScanDeferred);
        return;
    }
    ChunkAbort(Dev);                                              // s258q: a Windows scan in a shape 5 gap
    // s258n: hardware 2026-11-19 (s258k/l/m, 12 connections): every snapshot before the scan Windows
    // asks for 180 s into each connection had no TX in flight; after it TX statuses stopped until the
    // disassociation.  ConnectedScan=1: no firmware scan while connected; the task completes now with
    // the cached list (+ the connected BSS).  s258o default 0 (firmware scan + ScanFlowCheck).
    if (Dev->Connected && Dev->Cfg.ConnectedScan == 1) {
        InterlockedIncrement(&Dev->CntScanSkipped);
        Dev->ScanSkipped = TRUE;
        Dev->SelfScan = FALSE;
        Dev->ScanStatus = NB_E_STATUS_SUCCESS;
        JobScanDone(Dev);
        return;
    }
    Dev->ScanSkipped = FALSE;
    ScanStartFw(Dev, FALSE);
}

// ------------------------------------------------ s258o TX flowring after a connected scan
// [fact, s258n scan0] after the firmware scan made while connected the firmware stopped taking items
// from the TX flowring (read index 320 while the host wrote up to 346, doorbell on every commit); the
// control submit ring kept moving; a flowring delete + create made it move again at once.
// [hypothesis] the firmware does not resume the flowring it suspended for the scan.  So right after
// such a scan the read index is watched: taken by itself -> recorded (cause gone); not taken within
// ScanFlowProbeMs -> one more w index write + doorbell, then the proven re-create, before Windows
// sees a dead link.  The TCM copy of the w index tells whether the firmware has fetched it at all.
static void SfRecord(PNWOA_DEVICE Dev, const NWOA_SFREC* R)
{
    KIRQL irql;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    Dev->Sf[Dev->SfNext % NWOA_SF_N] = *R;
    Dev->SfNext++;
    KeReleaseSpinLock(&Dev->StateLock, irql);
    // s258p: per candidate counts (work item only), then the next candidate at the next CandService
    if (R->Cand < NB_CAND_N) {
        ULONG k = R->Cand;
        Dev->CandN[k]++;
        switch (R->Verdict) {
        case NWOA_SF_ALIVE:   Dev->CandAlive[k]++; break;
        case NWOA_SF_DBFIX:   Dev->CandStuck[k]++; Dev->CandDb[k]++; break;
        case NWOA_SF_RSMFIX:  Dev->CandStuck[k]++; Dev->CandRsm[k]++; break;
        case NWOA_SF_REFRESH: Dev->CandStuck[k]++; Dev->CandRef[k]++; break;
        case NWOA_SF_REFRESHERR:
        case NWOA_SF_STUCKREC: Dev->CandStuck[k]++; break;
        case NWOA_SF_IDLE:    Dev->CandIdle[k]++; break;
        default:              Dev->CandGone[k]++; break;
        }
        Dev->CandAdvance = TRUE;
    }
    // s258q: per shape (test scans) / "win" (Windows scans made while connected)
    if (R->Shape < NB_SHAPE_N || !R->Self) {
        ULONG k = R->Shape < NB_SHAPE_N ? R->Shape : NB_SHAPE_N;
        ULONG ms = R->ScanEndMs >= R->ScanStartMs ? R->ScanEndMs - R->ScanStartMs : 0;
        Dev->ShN[k]++;
        Dev->ShMsSum[k] += ms;
        if (ms > Dev->ShMsMax[k])
            Dev->ShMsMax[k] = ms;
        Dev->ShEv124[k] += R->Ev124;
        Dev->ShEv120[k] += R->Ev120;
        switch (R->Verdict) {
        case NWOA_SF_ALIVE:   Dev->ShAlive[k]++; break;
        case NWOA_SF_DBFIX:   Dev->ShStuck[k]++; Dev->ShDb[k]++; break;
        case NWOA_SF_REFRESH: Dev->ShStuck[k]++; Dev->ShRef[k]++; break;
        case NWOA_SF_RSMFIX:
        case NWOA_SF_REFRESHERR:
        case NWOA_SF_STUCKREC: Dev->ShStuck[k]++; break;
        case NWOA_SF_IDLE:    Dev->ShIdle[k]++; break;
        default:              Dev->ShGone[k]++; break;
        }
        if (R->Self && Dev->Cfg.ShapeCycle)
            Dev->ShapeAdvance = TRUE;
    }
}

static void SfFwView(PNWOA_DEVICE Dev, NWOA_SFREC* R)
{
    ULONG fr0;
    NwoaFwIdxView(Dev, Dev->Flow.D.idx, &R->FwW, &R->FwR);
    NwoaFwIdxView(Dev, 0, &R->FwW0, &fr0);
    R->HostW0 = NwoaHostWIdx(Dev, 0);
}

static void SfDelay10(void)
{
    LARGE_INTEGER d;
    d.QuadPart = -10 * 10000;                                     // 10 ms
    KeDelayExecutionThread(KernelMode, FALSE, &d);
}

// s270 FlowSpare probe: the main ring is stuck after the scan.  The next TX frame goes to the spare ring
// (open and idle since the connect, so it also went through the scan).  Up to 1 s for a frame to come, then
// 300 ms for the firmware to take it.  Only records; the caller goes on with doorbell + re-create.
static void SpareProbe(PNWOA_DEVICE Dev)
{
    ULONG w0, r0, w, r;
    if (!NwoaSpareIdx(Dev, &w0, &r0))
        return;
    InterlockedExchange(&Dev->SpareRoute, 1);
    int i;
    for (i = 0; i < 100; i++) {                                   // 1 s for a frame
        SfDelay10();
        if (!NwoaSpareIdx(Dev, &w, &r) || w != w0)
            break;
    }
    InterlockedExchange(&Dev->SpareRoute, 0);
    if (!NwoaSpareIdx(Dev, &w, &r))
        return;
    if (w == w0) {
        InterlockedIncrement(&Dev->CntSpareNoFrame);
        Dev->SpareLast = 3;
        return;
    }
    for (i = 0; i < 30; i++) {                                    // 300 ms for the firmware to read it
        if (r == w) {
            InterlockedIncrement(&Dev->CntSpareAlive);
            Dev->SpareLast = 1;
            return;
        }
        SfDelay10();
        if (!NwoaSpareIdx(Dev, &w, &r))
            return;
    }
    if (r == w) {
        InterlockedIncrement(&Dev->CntSpareAlive);
        Dev->SpareLast = 1;
        return;
    }
    InterlockedIncrement(&Dev->CntSpareStuck);
    Dev->SpareLast = 2;
}

// items posted after the scan were not taken: (s258p RSM: FLOW_RING_RESUME first,) doorbell once
// more, then delete + create
static void SfFix(PNWOA_DEVICE Dev, NWOA_SFREC* R)
{
    ULONG w, r;
    InterlockedIncrement(&Dev->CntSfStuck);
    NwoaSnapTake(Dev, 1);                                         // s273: stuck, before any fix
    SfFwView(Dev, R);
    if (Dev->Cfg.ScanFlowCheck == 3) {
        R->Verdict = NWOA_SF_STUCKREC;
        return;
    }
    if (Dev->Cfg.FlowSpare)                                       // s270: record only, before any fix
        SpareProbe(Dev);
    if ((R->Bits & NB_CAND_RSM) && NT_SUCCESS(NwoaFlowResume(Dev))) {
        for (int i = 0; i < 10; i++) {                            // 100 ms
            SfDelay10();
            if (!NwoaFlowIdx(Dev, &w, &r)) {
                R->Verdict = NWOA_SF_GONE;
                return;
            }
            if (r != R->R1) {
                InterlockedIncrement(&Dev->CntSfRsmFix);
                R->Verdict = NWOA_SF_RSMFIX;
                return;
            }
        }
    }
    NwoaFlowKick(Dev);
    for (int i = 0; i < 10; i++) {                                // 100 ms
        SfDelay10();
        if (!NwoaFlowIdx(Dev, &w, &r)) {
            R->Verdict = NWOA_SF_GONE;
            return;
        }
        if (r != R->R1) {
            InterlockedIncrement(&Dev->CntSfDbFix);
            R->Verdict = NWOA_SF_DBFIX;
            return;
        }
    }
    NTSTATUS st = NwoaFlowringRecreate(Dev);
    R->Fix = (LONG)st;
    if (NT_SUCCESS(st)) {
        InterlockedIncrement(&Dev->CntSfRefresh);
        R->Verdict = NWOA_SF_REFRESH;
    } else {
        InterlockedIncrement(&Dev->CntSfRefreshFail);
        R->Verdict = NWOA_SF_REFRESHERR;
    }
    Dev->StallSm.state = 0;
    NwoaTxSlotsFreed(Dev);
}

static void SfIdleEnd(PNWOA_DEVICE Dev)
{
    if (!Dev->SfArmed)
        return;
    Dev->SfArmed = FALSE;
    InterlockedIncrement(&Dev->CntSfIdle);
    Dev->SfArmRec.Verdict = NWOA_SF_IDLE;
    Dev->SfArmRec.Ms = NowMs();
    SfRecord(Dev, &Dev->SfArmRec);
}

static void ScanFlowAfter(PNWOA_DEVICE Dev, BOOLEAN Self, ULONG StartMs, ULONG EndMs)
{
    NWOA_SFREC rec;
    nb_flowwatch fw;
    ULONG w, r;
    int v = NB_FW_WAIT;
    if (!Dev->Cfg.ScanFlowCheck || !Dev->Connected || !NwoaFlowIdx(Dev, &w, &r))
        return;
    RtlZeroMemory(&rec, sizeof(rec));
    rec.Self = Self ? 1 : 0;
    rec.ScanStartMs = StartMs;
    rec.ScanEndMs = EndMs;
    rec.W0 = rec.W1 = w;
    rec.R0 = rec.R1 = r;
    rec.AdvMs = NB_FW_NONE;
    rec.FwW = rec.FwR = rec.FwW0 = rec.HostW0 = 0xFFFFFFFFu;
    rec.Cand = Dev->ScanCand;                                     // s258p
    rec.Bits = Dev->ScanBits;
    rec.NCh = Dev->ScanNCh;
    rec.Shape = Dev->ScanShape;                                   // s258q
    rec.Chunks = (UCHAR)(Dev->ScanShape == NB_SHAPE_CHUNKS ? Dev->ChunkIdx + 1 : 0);
    rec.Ev124 = (USHORT)min((ULONG)(Dev->EvAllCnt[NB_E_CCA_CHAN_QUAL] - Dev->ScanEvBase124), 65535ul);
    rec.Ev120 = (USHORT)min((ULONG)(Dev->EvAllCnt[NB_E_MSCH] - Dev->ScanEvBase120), 65535ul);
    LONG db0 = Dev->CntFlowDbell;
    if (Dev->Cfg.ScanFlowCheck == 2) {
        InterlockedIncrement(&Dev->CntSfForced);
        SfFwView(Dev, &rec);
        rec.Fix = (LONG)NwoaFlowringRecreate(Dev);
        NwoaTxSlotsFreed(Dev);
        rec.Verdict = NWOA_SF_FORCED;
        rec.Ms = NowMs();
        SfRecord(Dev, &rec);
        return;
    }
    nb_flowwatch_start(&fw, NowMs(), w, r);
    while (v == NB_FW_WAIT) {
        SfDelay10();
        if (!Dev->Connected || !NwoaFlowIdx(Dev, &w, &r)) {
            v = -1;
            break;
        }
        v = nb_flowwatch_step(&fw, NowMs(), w, r, Dev->Cfg.ScanFlowProbeMs, NWOA_SF_IDLE_MS);
    }
    rec.W1 = w;
    rec.R1 = r;
    rec.AdvMs = fw.adv_ms;
    rec.Db = (ULONG)(Dev->CntFlowDbell - db0);
    switch (v) {
    case NB_FW_ALIVE:
        InterlockedIncrement(&Dev->CntSfAlive);
        rec.Verdict = NWOA_SF_ALIVE;
        SfFwView(Dev, &rec);
        NwoaSnapTake(Dev, 1);                                     // s273: a scan without the stall (control)
        NwoaSnapReport(Dev, 2, rec.Verdict);
        break;
    case NB_FW_STUCK:
        SfFix(Dev, &rec);
        if (rec.Verdict == NWOA_SF_REFRESH) {                     // s273: 0.3 s after the re-create
            for (int i = 0; i < 30; i++)
                SfDelay10();
            NwoaSnapTake(Dev, 2);
            NwoaSnapReport(Dev, 3, rec.Verdict);
        } else
            NwoaSnapReport(Dev, 2, rec.Verdict);
        break;
    case NB_FW_IDLE:
        // nothing posted yet: the 2 s diag tick goes on watching for 30 s (SfArmTick)
        rec.Verdict = NWOA_SF_IDLE;
        Dev->SfArmRec = rec;
        Dev->SfArmRec.Deferred = 1;
        nb_flowwatch_start(&Dev->SfWatch, NowMs(), w, r);
        Dev->SfArmUntil = KeQueryInterruptTime() + 30ull * 10000000ull;
        Dev->SfArmed = TRUE;
        return;                                                   // recorded when the watch ends
    default:
        rec.Verdict = NWOA_SF_GONE;
        break;
    }
    rec.Ms = NowMs();
    SfRecord(Dev, &rec);
}

// 2 s diag tick: after an idle verdict, the first items posted must be taken by the next tick
static void SfArmTick(PNWOA_DEVICE Dev)
{
    ULONG w, r;
    if (!Dev->SfArmed)
        return;
    if (!Dev->Connected || KeQueryInterruptTime() > Dev->SfArmUntil || !NwoaFlowIdx(Dev, &w, &r)) {
        SfIdleEnd(Dev);
        return;
    }
    LONG db0 = Dev->CntFlowDbell;
    int v = nb_flowwatch_step(&Dev->SfWatch, NowMs(), w, r, 1500, 0xFFFFFFF0u);
    if (v == NB_FW_WAIT)
        return;
    Dev->SfArmed = FALSE;
    NWOA_SFREC* rec = &Dev->SfArmRec;
    rec->W1 = w;
    rec->R1 = r;
    rec->AdvMs = Dev->SfWatch.adv_ms;
    rec->Db += (ULONG)(Dev->CntFlowDbell - db0);
    if (v == NB_FW_ALIVE) {
        InterlockedIncrement(&Dev->CntSfAlive);
        rec->Verdict = NWOA_SF_ALIVE;
        SfFwView(Dev, rec);
    } else {
        SfFix(Dev, rec);
    }
    rec->Ms = NowMs();
    SfRecord(Dev, rec);
}

// s258q shape 5: 2 s tick in the gap after one escan of the sequence.  Items posted before / during the
// gap and still not taken (read index where the escan left it) -> the sequence ends here and the s258o
// check + fix runs now; else the next escan.
static void ChunkTick(PNWOA_DEVICE Dev)
{
    ULONG w, r;
    if (!Dev->ChunkActive || Dev->ScanRunning)
        return;
    if (!Dev->Connected || Dev->Dead || !Dev->RingsUp || !Dev->RadioOn || Dev->ConnTask.Active ||
        Dev->ScanTask.Active || Dev->FlowState != 2) {
        ChunkAbort(Dev);
        return;
    }
    if (KeQueryInterruptTime() < Dev->ChunkNextAt)
        return;
    if (NwoaFlowIdx(Dev, &w, &r) && r == Dev->ChunkR && w != r) {
        Dev->ChunkStuckAt = (UCHAR)(Dev->ChunkIdx - 1);           // after this escan the ring stayed put
        Dev->ShGapStuck[NB_SHAPE_CHUNKS]++;
        Dev->ChunkActive = FALSE;
        Dev->ChunkIdx--;                                          // rec.Chunks = escans made
        ScanFlowAfter(Dev, TRUE, Dev->ChunkStartMs, Dev->ChunkLastEndMs);
        return;
    }
    ScanStartFw(Dev, TRUE);
}

// TestConnScanSec: a directed firmware scan every N s of a connection (since the association or the
// last scan made while connected, Windows' or ours)
static void SelfScanTick(PNWOA_DEVICE Dev)
{
    if (!Dev->Cfg.TestConnScanSec || !Dev->Connected || Dev->ScanRunning || Dev->ScanTask.Active || Dev->ChunkActive ||
        Dev->ConnTask.Active || Dev->FlowState != 2 || Dev->Dead || !Dev->RingsUp || !Dev->RadioOn ||
        !Dev->Conn.SsidLen)
        return;
    ULONG64 now = KeQueryInterruptTime();
    ULONG64 last = Dev->ConnUpTime > Dev->LastConnScanAny ? Dev->ConnUpTime : Dev->LastConnScanAny;
    if (!last || SinceMs(now, last) < Dev->Cfg.TestConnScanSec * 1000)
        return;
    SfIdleEnd(Dev);
    ScanStartFw(Dev, TRUE);
}

// s258m: per scan completion a numbers-only record (NwoaScanDiag): is the connected BSS in this
// scan, its RSSI rank among the same-SSID entries, the strongest other one, firmware GET_RSSI.
// ScanReportConnectedOnly (H1 test) 1/2 changes what a scan made while connected reports; a
// connected BSS the scan did not see is then added from the association beacon (CachedInformation 1).
// BssAgeInfo adds WDI_TLV_BSS_ENTRY_AGE_INFO (KeQuerySystemTime of the last sighting, live).
static void JobScanDone(PNWOA_DEVICE Dev)
{
    ULONG count = 0;
    KIRQL irql;
    NWOA_SCANDIAG sd;
    BOOLEAN conn = Dev->Connected;
    BOOLEAN self = Dev->SelfScan;                                 // s258o test scan: no WDI task, no list
    UCHAR cur[6];
    RtlZeroMemory(&sd, sizeof(sd));
    RtlCopyMemory(cur, Dev->CurBssid, 6);
    NWOA_BSS* snap = (NWOA_BSS*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NWOA_BSS) * NWOA_MAX_BSS, NWOA_TAG);
    PUCHAR bodies = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)(NWOA_MAX_BSS + 1) * (NWOA_BSS_IE_MAX + 12), NWOA_TAG);
    if (snap && bodies) {
        KeAcquireSpinLock(&Dev->StateLock, &irql);
        for (int i = 0; i < NWOA_MAX_BSS; i++)
            if (Dev->Bss[i].Used)
                snap[count++] = Dev->Bss[i];
        KeReleaseSpinLock(&Dev->StateLock, irql);
    }
    Dev->LastScanBss = count;
    NwoaDiagDword(Dev, L"NwoaLastScanBss", count);
    NwoaDiagDword(Dev, L"NwoaLastScanStatus", (ULONG)Dev->ScanStatus);
    NwoaDiagDword(Dev, L"NwoaLastScanEvents", Dev->ScanEvents);

    // ---- diagnostics
    int curIdx = -1;
    sd.Ms = NowMs();
    sd.Trigger = Dev->ScanTrigger;
    sd.Directed = Dev->ScanDirected;
    sd.Conn = conn ? 1 : 0;
    sd.Entries = (UCHAR)min(count, 255u);
    sd.Status = (USHORT)Dev->ScanStatus;
    sd.Skipped = Dev->ScanSkipped ? 1 : 0;
    if (conn) {
        ULONG v = 0;
        if (NT_SUCCESS(NwoaGetInt(Dev, NB_C_GET_RSSI, &v)) && (LONG)v < 0 && (LONG)v > -120)
            sd.FwRssi = (SHORT)(LONG)v;
        for (ULONG i = 0; i < count; i++)
            if (RtlEqualMemory(snap[i].B.bssid, cur, 6)) { curIdx = (int)i; break; }
    }
    {
        int same = 0, better = 0, other = -1;
        BOOLEAN bySsid = Dev->Conn.SsidLen != 0;
        for (ULONG i = 0; i < count; i++) {
            BOOLEAN s2 = !bySsid || (snap[i].B.ssid_len == Dev->Conn.SsidLen &&
                                     RtlEqualMemory(snap[i].B.ssid, Dev->Conn.Ssid, Dev->Conn.SsidLen));
            if (!s2) continue;
            same++;
            if ((int)i == curIdx) continue;
            if (curIdx >= 0 && snap[i].B.rssi > snap[curIdx].B.rssi) better++;
            if (other < 0 || snap[i].B.rssi > snap[other].B.rssi) other = (int)i;
        }
        sd.SameSsid = (UCHAR)min(same, 255);
        if (other >= 0) { sd.OtherRssi = snap[other].B.rssi; sd.OtherBand = snap[other].B.band; }
        if (curIdx >= 0) {
            const nb_bss* b = &snap[curIdx].B;
            sd.Present = 1;
            sd.Rank = (UCHAR)min(better + 1, 255);
            sd.Rssi = b->rssi;
            sd.Channel = b->channel;
            sd.Band = b->band;
            sd.Bw = b->bw_mhz;
            sd.IeLen = (USHORT)b->ies_len;
            sd.Cap = b->capability;
        }
    }

    // ---- what to report
    ULONG mode = (conn && Dev->Cfg.ScanReportConnectedOnly <= 2) ? Dev->Cfg.ScanReportConnectedOnly : 0;
    BOOLEAN skipped = Dev->ScanSkipped;
    BOOLEAN synth = (mode != 0 || skipped) && curIdx < 0 && conn && Dev->Conn.BeaconLen >= 12 && bodies != NULL;
    ULONG nrep = 0;
    WDI_BSS_ENTRY_CONTAINER* e = (count || synth) ? new WDI_BSS_ENTRY_CONTAINER[count + 1] : NULL;
    if (e) {
        LARGE_INTEGER sys;
        ULONG64 inow = KeQueryInterruptTime();
        KeQuerySystemTime(&sys);
        for (ULONG i = 0; i < count; i++) {
            if (mode == 1 && (int)i != curIdx)
                continue;                                         // H1: only the connected BSS
            PUCHAR body = bodies + (SIZE_T)nrep * (NWOA_BSS_IE_MAX + 12);
            WDI_BSS_ENTRY_CONTAINER* x = &e[nrep];
            snap[i].B.ies = snap[i].Ies;
            ULONG bl = nb_bss_probe_body(&snap[i].B, body, NWOA_BSS_IE_MAX + 12);
            RtlCopyMemory(x->BSSID.Address, snap[i].B.bssid, 6);
            x->Optional.ProbeResponseFrame_IsPresent = 1;
            x->ProbeResponseFrame.SimpleAssign(body, bl);
            x->SignalInfo.RSSI = snap[i].B.rssi;
            x->SignalInfo.LinkQuality = LinkQuality(snap[i].B.rssi);
            x->ChannelInfo.ChannelNumber = snap[i].B.channel;
            x->ChannelInfo.BandId = WdiBand(snap[i].B.band);
            if (Dev->Cfg.BssAgeInfo) {
                ULONG64 age = inow > snap[i].SeenTime ? inow - snap[i].SeenTime : 0;
                x->Optional.EntryAgeInfo_IsPresent = 1;
                x->EntryAgeInfo.HostTimeStamp = (UINT64)sys.QuadPart - age;
                x->EntryAgeInfo.CachedInformation = skipped ? TRUE : FALSE;   // s258n: no scan = stored entries
            }
            nrep++;
        }
        if (synth) {
            // the connected BSS was not in this scan: its association beacon/probe body, last RSSI
            PUCHAR body = bodies + (SIZE_T)nrep * (NWOA_BSS_IE_MAX + 12);
            WDI_BSS_ENTRY_CONTAINER* x = &e[nrep];
            ULONG bl = min(Dev->Conn.BeaconLen, (ULONG)(NWOA_BSS_IE_MAX + 12));
            int rssi = Dev->RssiLast < 0 ? (int)Dev->RssiLast : (sd.FwRssi ? sd.FwRssi : -60);
            RtlCopyMemory(body, Dev->Conn.Beacon, bl);
            RtlCopyMemory(x->BSSID.Address, cur, 6);
            x->Optional.ProbeResponseFrame_IsPresent = 1;
            x->ProbeResponseFrame.SimpleAssign(body, bl);
            x->SignalInfo.RSSI = rssi;
            x->SignalInfo.LinkQuality = LinkQuality(rssi);
            x->ChannelInfo.ChannelNumber = Dev->Conn.Channel;
            x->ChannelInfo.BandId = WdiBand(Dev->Conn.Band);
            if (Dev->Cfg.BssAgeInfo) {
                x->Optional.EntryAgeInfo_IsPresent = 1;
                x->EntryAgeInfo.HostTimeStamp = (UINT64)sys.QuadPart;
                x->EntryAgeInfo.CachedInformation = TRUE;
            }
            nrep++;
            sd.Synth = 1;
            InterlockedIncrement(&Dev->CntScanSynth);
        }
        if (nrep && !self) {
            WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS p;
            p.Optional.DeviceDescriptor_IsPresent = 1;
            p.DeviceDescriptor.SimpleAssign(e, nrep);
            UINT8* out = NULL;
            ULONG outLen = 0;
            NDIS_STATUS ns = GenerateWdiIndicationBssEntryList(&p, 0, &Dev->Tlv, &outLen, &out);
            NwoaDiagDword(Dev, L"NwoaBssListGenerate", (ULONG)ns);
            if (ns == NDIS_STATUS_SUCCESS) {
                // with the scan task's port, TransactionId 0 (MS sample wifihal.cpp sends it the same way)
                SendIndication(Dev, WDI_INDICATION_BSS_ENTRY_LIST, 0, NDIS_STATUS_SUCCESS, &Dev->ScanTask.Hdr, out, outLen);
                FreeGenerated(out);
            }
            p.DeviceDescriptor.SimpleAssign(NULL, 0);
        }
        for (ULONG i = 0; i < nrep; i++)
            e[i].ProbeResponseFrame.SimpleAssign(NULL, 0);
        delete[] e;
    }
    sd.Reported = (UCHAR)min(nrep, 255u);
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    Dev->ScanDiag[Dev->ScanDiagNext % NWOA_SCANDIAG_N] = sd;
    Dev->ScanDiagNext++;
    Dev->LastScanDone = KeQueryInterruptTime();
    KeReleaseSpinLock(&Dev->StateLock, irql);
    if (snap) ExFreePoolWithTag(snap, NWOA_TAG);
    if (bodies) ExFreePoolWithTag(bodies, NWOA_TAG);
    Dev->ScanSkipped = FALSE;
    BOOLEAN connScan = Dev->ScanWhileConn;
    ULONG csStart = 0, csEnd = 0;
    if (Dev->ScanWhileConn) {
        // s258n: a firmware scan made while connected: TX in flight before / after, statuses during,
        // PM before / after (PmKickAfterScan=1: PM changed and set back first)
        Dev->ScanWhileConn = FALSE;
        LONG pm = (Dev->Cfg.PmKickAfterScan && Dev->Connected) ? PmKick(Dev) : ReadPm(Dev);
        ULONG out = NwoaTxOutstanding(Dev, NULL);
        KeAcquireSpinLock(&Dev->StateLock, &irql);
        NWOA_CSCAN* c = &Dev->CScan[(Dev->CScanNext - 1) % NWOA_CSCAN_N];
        c->EndMs = NowMs();
        c->OutEnd = out;
        c->DoneDuring = (ULONG)Dev->CntTxStatus - Dev->CScanDoneMark;
        c->PmAfter = pm;
        Dev->ConnScanEnd = KeQueryInterruptTime();
        csStart = c->StartMs;
        csEnd = c->EndMs;
        KeReleaseSpinLock(&Dev->StateLock, irql);
    }
    if (InterlockedExchange(&Dev->TxHold, 0))                     // s258o ScanTxHold: frames go now
        NwoaTxSlotsFreed(Dev);
    Dev->SelfScan = FALSE;
    // escan finished (SUCCESS / ABORT / timeout): the scan task is complete either way
    if (!self)
        TaskEnd(Dev, &Dev->ScanTask, (Dev->ScanStatus == NB_E_STATUS_FAIL) ? NDIS_STATUS_FAILURE : NDIS_STATUS_SUCCESS);
    if (self && Dev->ChunkActive) {                               // s258q shape 5
        if (!connScan) {
            ChunkAbort(Dev);
        } else if ((ULONG)Dev->ChunkIdx + 1 < Dev->ChunkTotal) {
            ULONG w = 0, r = 0;                                   // more escans: the gap first, no verdict yet
            (void)NwoaFlowIdx(Dev, &w, &r);
            Dev->ChunkW = w;
            Dev->ChunkR = r;
            Dev->ChunkLastEndMs = csEnd;
            Dev->ChunkIdx++;
            Dev->ChunkNextAt = KeQueryInterruptTime() + (ULONG64)Dev->Cfg.ShapeChunkGapMs * 10000ull;
            connScan = FALSE;
        } else {
            Dev->ChunkActive = FALSE;                             // last one: verdict over the whole sequence
            csStart = Dev->ChunkStartMs;
        }
    }
    if (connScan)
        ScanFlowAfter(Dev, self, csStart, csEnd);                // s258o
    CandService(Dev);                                             // s258p
    if (self && Dev->ScanStartDeferred) {
        Dev->ScanStartDeferred = FALSE;
        NwoaQueueJob(Dev, JOB_SCAN_START);
    }
}

static void IndicateDisassoc(PNWOA_DEVICE Dev, const UCHAR* Bssid, ULONG Reason)
{
    WDI_INDICATION_DISASSOCIATION_PARAMETERS p;
    RtlCopyMemory(p.DisconnectIndicationParameters.MacAddress.Address, Bssid, 6);
    p.DisconnectIndicationParameters.DisassociationWABIReason = (WDI_ASSOC_STATUS)Reason;
    p.Optional.NeedPeerStateCleanup_IsPresent = 1;
    p.NeedPeerStateCleanup = 0;
    UINT8* out = NULL;
    ULONG outLen = 0;
    if (GenerateWdiIndicationDisassociation(&p, 0, &Dev->Tlv, &outLen, &out) == NDIS_STATUS_SUCCESS) {
        Unsolicited(Dev, WDI_INDICATION_DISASSOCIATION, out, outLen);
        FreeGenerated(out);
    }
}

static void QuietDisconnect(PNWOA_DEVICE Dev, ULONG Src)
{
    Dev->PtkInstalled = FALSE;
    if (Dev->LinkUpReported)
        NwoaReportLink(Dev, FALSE);
    Dev->Connected = FALSE;
    InterlockedExchange(&Dev->ConnState, CONN_IDLE);
    NwoaHostDisassoc(Dev, Src);
    NwoaFlowringDelete(Dev);
    NwoaRxFlushHeld(Dev);
    RtlZeroMemory(Dev->CurBssid, 6);
}

static void IndicateAssocResult(PNWOA_DEVICE Dev, ULONG AssocStatus, ULONG StatusCode,
                                const UCHAR* Req, ULONG ReqLen, const UCHAR* Resp, ULONG RespLen)
{
    NWOA_CONNECT* c = &Dev->Conn;
    WDI_ASSOCIATION_RESULT_CONTAINER* r = new WDI_ASSOCIATION_RESULT_CONTAINER[1];
    WDI_PHY_TYPE phy = c->Band == 5 ? WDI_PHY_TYPE_VHT : WDI_PHY_TYPE_HT;
    if (!r)
        return;
    RtlCopyMemory(r->BSSID.Address, c->Bssid, 6);
    r->AssociationResultParameters.AssociationStatus = (WDI_ASSOC_STATUS)AssocStatus;
    r->AssociationResultParameters.StatusCode = StatusCode;
    r->AssociationResultParameters.ReAssociation = FALSE;
    r->AssociationResultParameters.AuthAlgorithm = (WDI_AUTH_ALGORITHM)c->WdiAuth;
    r->AssociationResultParameters.UnicastCipherAlgorithm = (WDI_CIPHER_ALGORITHM)c->WdiUcast;
    r->AssociationResultParameters.MulticastDataCipherAlgorithm = (WDI_CIPHER_ALGORITHM)c->WdiMcast;
    r->AssociationResultParameters.MulticastMgmtCipherAlgorithm = WDI_CIPHER_ALGO_NONE;
    r->AssociationResultParameters.FourAddressSupported = FALSE;
    r->AssociationResultParameters.PortAuthorized = FALSE;            // the OS supplicant runs the 4-way handshake
    // s258m: the association response says whether WMM was negotiated (WmmNegotiated, logged); reporting
    // it is ReportWmm=1 (default 0 = FALSE as before: the data path was only ever run without WMM)
    r->AssociationResultParameters.WMMQoSEnabled = (Dev->Cfg.ReportWmm && AssocStatus == WDI_ASSOC_STATUS_SUCCESS) ? Dev->WmmNegotiated : FALSE;
    r->AssociationResultParameters.DSInfo = WDI_DS_UNKNOWN;
    r->AssociationResultParameters.BandID = WdiBand(c->Band);
    if (ReqLen) {
        r->Optional.AssociationRequestFrame_IsPresent = 1;
        r->AssociationRequestFrame.SimpleAssign(const_cast<UCHAR*>(Req), ReqLen);
    }
    if (RespLen) {
        r->Optional.AssociationResponseFrame_IsPresent = 1;
        r->AssociationResponseFrame.SimpleAssign(const_cast<UCHAR*>(Resp), RespLen);
    }
    if (c->BeaconLen) {
        r->Optional.BeaconProbeResponse_IsPresent = 1;
        r->BeaconProbeResponse.SimpleAssign(c->Beacon, c->BeaconLen);
    }
    r->ActivePhyTypeList.SimpleAssign(&phy, 1);
    WDI_INDICATION_ASSOCIATION_RESULT_LIST p;
    p.AssociationResults.SimpleAssign(r, 1);
    UINT8* out = NULL;
    ULONG outLen = 0;
    NDIS_STATUS ns = GenerateWdiIndicationAssociationResult(&p, 0, &Dev->Tlv, &outLen, &out);
    NwoaDiagDword(Dev, L"NwoaAssocResultGenerate", (ULONG)ns);
    if (ns == NDIS_STATUS_SUCCESS) {
        Unsolicited(Dev, WDI_INDICATION_ASSOCIATION_RESULT, out, outLen);
        FreeGenerated(out);
    }
    p.AssociationResults.SimpleAssign(NULL, 0);
    r->AssociationRequestFrame.SimpleAssign(NULL, 0);
    r->AssociationResponseFrame.SimpleAssign(NULL, 0);
    r->BeaconProbeResponse.SimpleAssign(NULL, 0);
    r->ActivePhyTypeList.SimpleAssign(NULL, 0);
    delete[] r;
}

static void IndicateLinkState(PNWOA_DEVICE Dev, int Rssi)
{
    NWOA_CONNECT* c = &Dev->Conn;
    WDI_INDICATION_LINK_STATE_CHANGE_PARAMETERS p;
    WDI_LINK_INFO_CONTAINER* li = new WDI_LINK_INFO_CONTAINER[1];
    ULONG kbps = c->Band == 5 ? 433000 : 144000;
    ULONG bw = 20;
    if (Dev->Cfg.LinkInfoReal) {
        // s258m: the bandwidth the firmware associated with (chanspec) and its current rate; the old
        // fixed 20 MHz with 433 Mbps could not both be true (433 Mbps needs VHT80)
        if (Dev->LinkBwMhz) bw = Dev->LinkBwMhz;
        if (Dev->LinkRateKbps) kbps = Dev->LinkRateKbps;
    }
    Dev->LastLqInd = KeQueryInterruptTime();
    RtlCopyMemory(p.LinkStateChangeParameters.PeerMACAddress.Address, c->Bssid, 6);
    p.LinkStateChangeParameters.TxLinkSpeed = kbps;
    p.LinkStateChangeParameters.RxLinkSpeed = kbps;
    p.LinkStateChangeParameters.LinkQuality = (UINT8)LinkQuality(Rssi);
    if (li) {
        li->LinkID = 0;
        RtlCopyMemory(li->LocalLinkMACAddress.Address, Dev->Mac, 6);
        RtlCopyMemory(li->PeerLinkMACAddress.Address, c->Bssid, 6);
        li->ChannelNumber = c->Channel;
        li->BandId = WdiBand(c->Band);
        li->RSSI = Rssi;
        li->Bandwidth = bw;
        p.LinkInfo.SimpleAssign(li, 1);
    }
    UINT8* out = NULL;
    ULONG outLen = 0;
    NDIS_STATUS ns = GenerateWdiIndicationLinkStateChange(&p, 0, &Dev->Tlv, &outLen, &out);
    NwoaDiagDword(Dev, L"NwoaLinkStateGenerate", (ULONG)ns);
    if (ns == NDIS_STATUS_SUCCESS) {
        Unsolicited(Dev, WDI_INDICATION_LINK_STATE_CHANGE, out, outLen);
        FreeGenerated(out);
    }
    p.LinkInfo.SimpleAssign(NULL, 0);
    if (li)
        delete[] li;
}

static ULONG FailToWdi(PNWOA_DEVICE Dev)
{
    switch (Dev->ConnFailEvent) {
    case NB_E_AUTH: return WDI_ASSOC_STATUS_AUTH_FAILED_BY_PEER;
    case NB_E_SET_SSID:
        return Dev->ConnFailStatus == NB_E_STATUS_NO_NETWORKS || Dev->ConnFailStatus == NB_E_STATUS_TIMEOUT
               ? WDI_ASSOC_STATUS_UNREACHABLE : WDI_ASSOC_STATUS_FAILURE;
    default: return WDI_ASSOC_STATUS_FAILURE;
    }
}

static void JobConnect(PNWOA_DEVICE Dev)
{
    NWOA_CONNECT* c = &Dev->Conn;
    NTSTATUS st;
    UCHAR jp[80];
    ULONG n;
    LARGE_INTEGER t;

    if (!Dev->ConnTask.Active)
        return;
    if (Dev->Connected || Dev->ConnState != CONN_IDLE) {
        UCHAR old[6];
        BOOLEAN was = Dev->Connected;
        RtlCopyMemory(old, Dev->CurBssid, 6);
        QuietDisconnect(Dev, Dev->ConnIsRoam ? DIS_ROAM : DIS_CONNECT);
        // OID_WDI_TASK_ROAM: the adapter leaves the old AP and associates with the new one; the
        // disassociation from the old AP is one of the task's unsolicited indications
        if (Dev->ConnIsRoam && was)
            IndicateDisassoc(Dev, old, WDI_ASSOC_STATUS_DISASSOCIATED_BY_HOST);
    }
    NwoaSetStage(Dev, Dev->ConnIsRoam ? "roam_start" : "connect_start", STATUS_PENDING);
    // security (bwfm_connect): wpa_auth, wsec, auth open, mfp none, RSN IE for the request
    if (c->Rsn) {
        UCHAR ie[24];
        ULONG il = nb_rsn_build(ie, sizeof(ie), (nb_u32)c->Group, (nb_u32)c->Pairwise, (nb_u32)c->Akm, 0);
        NwoaSetVar(Dev, "wpaie", ie, il);
        NwoaSetVarInt(Dev, "wpa_auth", nb_wpa_auth_for(1, c->Akm));
        NwoaSetVarInt(Dev, "wsec", nb_wsec_for_cipher(c->Pairwise) | nb_wsec_for_cipher(c->Group));
    } else {
        NwoaSetVarInt(Dev, "wpa_auth", NB_WPA_AUTH_DISABLED);
        NwoaSetVarInt(Dev, "wsec", NB_WSEC_NONE);
    }
    NwoaSetVarInt(Dev, "auth", NB_AUTH_OPEN);
    NwoaSetVarInt(Dev, "mfp", NB_MFP_NONE);
    NwoaSetVarInt(Dev, "sup_wpa", 0);
    NwoaSetInt(Dev, NB_C_SET_INFRA, 1);

    KeClearEvent(&Dev->ConnEvent);
    Dev->PtkInstalled = FALSE;
    Dev->ConnFailEvent = Dev->ConnFailStatus = Dev->ConnFailReason = 0;
    InterlockedExchange(&Dev->ConnState, CONN_JOINING);
    n = nb_ext_join_params(jp, sizeof(jp), c->Ssid, c->SsidLen, c->Bssid, c->Chanspec);
    st = n ? NwoaSetVar(Dev, "join", jp, n) : STATUS_INVALID_PARAMETER;
    NwoaDiagDword(Dev, L"NwoaJoinIovar", (ULONG)st);
    if (!NT_SUCCESS(st)) {
        n = nb_join_params(jp, sizeof(jp), c->Ssid, c->SsidLen, c->Bssid, c->Chanspec);
        st = NwoaIoctl(Dev, NB_C_SET_SSID, jp, n, 0, NULL, NULL);
        NwoaDiagDword(Dev, L"NwoaSetSsid", (ULONG)st);
    }
    if (NT_SUCCESS(st)) {
        t.QuadPart = -15ll * 10000000;                                // 15 s
        KeWaitForSingleObject(&Dev->ConnEvent, Executive, KernelMode, FALSE, &t);
    }
    NwoaDiagDword(Dev, L"NwoaConnState", (ULONG)Dev->ConnState);
    NwoaDiagDword(Dev, L"NwoaConnFailEvent", Dev->ConnFailEvent);
    NwoaDiagDword(Dev, L"NwoaConnFailStatus", Dev->ConnFailStatus);
    NwoaDiagDword(Dev, L"NwoaConnFailReason", Dev->ConnFailReason);

    if (NT_SUCCESS(st) && Dev->ConnState == CONN_LINKED) {
        UCHAR ai[512] = { 0 };                                 // brcmfmac WL_ASSOC_INFO_MAX
        ULONG reqIe = 0, respIe = 0;
        PUCHAR req = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, 2 * 1400, NWOA_TAG);
        PUCHAR resp = req ? req + 1400 : NULL;
        ULONG reqLen = 0, respLen = 0;
        ULONG rssi = 0;
        NTSTATUS ais = NwoaGetVar(Dev, "assoc_info", ai, sizeof(ai));
        NwoaDiagDword(Dev, L"NwoaAssocInfoStatus", (ULONG)ais);
        if (NT_SUCCESS(ais))
            nb_assoc_info_parse(ai, 8, &reqIe, &respIe);
        if (req) {
            // fixed fields + IEs (the firmware only returns the IEs)
            nb_st16(req, c->Capability & 0x0431);                     // ESS, privacy, short preamble/slot
            nb_st16(req + 2, 10);                                     // listen interval
            reqIe = min(reqIe, 512u);
            if (reqIe && NT_SUCCESS(NwoaGetVar(Dev, "assoc_req_ies", req + 4, 512)))
                reqLen = 4 + reqIe;
            nb_st16(resp, c->Capability);
            nb_st16(resp + 2, 0);                                     // status success
            nb_st16(resp + 4, 0xC001);                                // AID unknown to the host
            respIe = min(respIe, 512u);
            if (respIe && NT_SUCCESS(NwoaGetVar(Dev, "assoc_resp_ies", resp + 6, 512)))
                respLen = 6 + respIe;
        }
        NwoaDiagDword(Dev, L"NwoaAssocReqIes", reqIe);
        NwoaDiagDword(Dev, L"NwoaAssocRespIes", respIe);
        {
            // s258m: WMM parameter element (00-50-F2 type 2) in the response, firmware chanspec and rate
            static const UCHAR wmm[4] = { 0x00, 0x50, 0xF2, 0x02 };
            UCHAR cb[8] = { 0 };
            ULONG rate = 0;
            Dev->WmmNegotiated = (resp && respLen > 6 && nb_ie_find(resp + 6, respLen - 6, 221, wmm)) ? TRUE : FALSE;
            Dev->LinkBwMhz = Dev->LinkRateKbps = Dev->FwChanspec = Dev->FwRate500k = 0;
            if (NT_SUCCESS(NwoaGetVar(Dev, "chanspec", cb, 4))) {
                nb_u8 ch = 0, bd = 0;
                nb_u16 w = 0;
                Dev->FwChanspec = nb_ld32(cb) & 0xFFFF;
                nb_chanspec_decode((nb_u16)Dev->FwChanspec, Dev->IoType, &ch, &bd, &w);
                if (bd == c->Band && (w == 20 || w == 40 || w == 80 || w == 160))
                    Dev->LinkBwMhz = w;
            }
            if (NT_SUCCESS(NwoaGetInt(Dev, NB_C_GET_RATE, &rate)) && rate > 0 && rate < 20000) {
                Dev->FwRate500k = rate;
                Dev->LinkRateKbps = rate * 500;
            }
        }
        st = NwoaFlowringCreate(Dev, c->Bssid);
        NwoaDiagDword(Dev, L"NwoaFlowringCreate", (ULONG)st);
        NwoaDiagDword(Dev, L"NwoaFwPmAtConnect", (ULONG)ReadPm(Dev));   // s258n
        Dev->StallSm.state = 0;
        Dev->ConnUpTime = KeQueryInterruptTime();
        Dev->FwCntBaseDone = FALSE;
        NwoaGetInt(Dev, NB_C_GET_RSSI, &rssi);
        RtlCopyMemory(Dev->CurBssid, c->Bssid, 6);
        RtlCopyMemory(Dev->LastConnBssid, c->Bssid, 6);
        Dev->HaveLastConn = TRUE;
        Dev->Connected = TRUE;
        IndicateAssocResult(Dev, WDI_ASSOC_STATUS_SUCCESS, 0, req, reqLen, resp, respLen);
        NwoaReportLink(Dev, TRUE);                             // lets NetAdapterCx create the TX/RX queues
        IndicateLinkState(Dev, (int)(LONG)rssi);
        Dev->RssiLast = Dev->RssiMin = Dev->RssiMax = (LONG)rssi;
        Dev->LqReported = LinkQuality((int)(LONG)rssi);
        InterlockedIncrement(&Dev->CntLinkStateInd);
        NwoaDiagEvents(Dev);
        if (req)
            ExFreePoolWithTag(req, NWOA_TAG);
        NwoaSetStage(Dev, NT_SUCCESS(st) ? "connected" : "connected_no_flowring", st);
        TaskEnd(Dev, &Dev->ConnTask, NDIS_STATUS_SUCCESS);
        return;
    }
    // failed: stop the firmware from retrying, report the association failure
    InterlockedExchange(&Dev->ConnState, CONN_IDLE);
    NwoaHostDisassoc(Dev, DIS_CONN_FAIL);
    IndicateAssocResult(Dev, FailToWdi(Dev), Dev->ConnFailStatus, NULL, 0, NULL, 0);
    NwoaDiagEvents(Dev);
    NwoaSetStage(Dev, "connect_failed", st);
    // hypothesis: the task itself completes successfully; the association result carries the failure
    TaskEnd(Dev, &Dev->ConnTask, NDIS_STATUS_SUCCESS);
}

static void JobDisconnect(PNWOA_DEVICE Dev)
{
    UCHAR bssid[6];
    BOOLEAN was = Dev->Connected;
    RtlCopyMemory(bssid, Dev->CurBssid, 6);
    QuietDisconnect(Dev, DIS_DISCONNECT);
    // OID_WDI_TASK_DISCONNECT lists NDIS_STATUS_WDI_INDICATION_DISASSOCIATION as its unsolicited
    // indication ("the disconnect may be triggered by a command from the OS").  H3 test: 1 = skip it.
    if (was && Dev->Cfg.NoDisassocIndOnHostDisconnect)
        InterlockedIncrement(&Dev->CntDisassocIndSkipped);
    else if (was)
        IndicateDisassoc(Dev, bssid, WDI_ASSOC_STATUS_DISASSOCIATED_BY_HOST);
    NwoaDiagEvents(Dev);
    NwoaSetStage(Dev, "disconnected", STATUS_SUCCESS);
    TaskEnd(Dev, &Dev->DiscTask, NDIS_STATUS_SUCCESS);
}

static void JobLinkLost(PNWOA_DEVICE Dev)
{
    UCHAR bssid[6];
    if (!Dev->Connected)
        return;
    RtlCopyMemory(bssid, Dev->CurBssid, 6);
    Dev->Connected = FALSE;
    Dev->PtkInstalled = FALSE;
    if (Dev->LinkUpReported)
        NwoaReportLink(Dev, FALSE);
    NwoaFlowringDelete(Dev);
    NwoaRxFlushHeld(Dev);
    InterlockedExchange(&Dev->ConnState, CONN_IDLE);         // the firmware already left the BSS
    IndicateDisassoc(Dev, bssid, Dev->LostReason);
    NwoaDiagDword(Dev, L"NwoaLostReasonWdi", Dev->LostReason);
    NwoaDiagEvents(Dev);
    NwoaSetStage(Dev, "link_lost", STATUS_SUCCESS);
}

static void JobReset(PNWOA_DEVICE Dev)
{
    if (Dev->Connected || Dev->ConnState != CONN_IDLE)
        QuietDisconnect(Dev, DIS_RESET);
    TaskEnd(Dev, &Dev->ResetTask, NDIS_STATUS_SUCCESS);
}

static void JobRadio(PNWOA_DEVICE Dev)
{
    BOOLEAN on = Dev->RadioWant != 0;
    NTSTATUS st = STATUS_SUCCESS;
    if (!on && (Dev->Connected || Dev->ConnState != CONN_IDLE))
        QuietDisconnect(Dev, DIS_RADIO_OFF);
    st = NwoaSetInt(Dev, on ? NB_C_UP : NB_C_DOWN, 0);
    if (NT_SUCCESS(st))
        Dev->RadioOn = on;
    WDI_INDICATION_RADIO_STATUS_PARAMETERS p;
    p.RadioState.HardwareState = TRUE;
    p.RadioState.SoftwareState = Dev->RadioOn;
    UINT8* out = NULL;
    ULONG outLen = 0;
    if (GenerateWdiIndicationRadioStatus(&p, 0, &Dev->Tlv, &outLen, &out) == NDIS_STATUS_SUCCESS) {
        SendIndication(Dev, WDI_INDICATION_RADIO_STATUS, 0, NDIS_STATUS_SUCCESS, &Dev->RadioTask.Hdr, out, outLen);
        FreeGenerated(out);
    }
    TaskEnd(Dev, &Dev->RadioTask, NT_SUCCESS(st) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE);
}

static void JobKeys(PNWOA_DEVICE Dev)
{
    NTSTATUS st = STATUS_SUCCESS;
    WIFIREQUEST req = Dev->KeyRequest;
    UCHAR k[NB_WSEC_KEY_LEN];
    LARGE_INTEGER t;
    for (ULONG i = 0; i < Dev->NumKeys; i++) {
        NWOA_KEY* key = &Dev->Keys[i];
        ULONG n;
        if (key->Pairwise && !key->Delete) {
            // brcmfmac waits for the 802.1X frames in flight before a pairwise key
            t.QuadPart = -10 * 10000;
            for (int w = 0; w < 100 && Dev->EapolPending > 0; w++) {
                KeDelayExecutionThread(KernelMode, FALSE, &t);
                NwoaProcess(Dev);
            }
        }
        n = nb_wsec_key(k, sizeof(k), key->Index, key->Delete ? NB_CIPHER_NONE : key->Cipher, key->Pairwise,
                        key->Delete ? NULL : key->Key, key->Delete ? 0 : key->KeyLen,
                        key->HavePeer ? key->Peer : NULL, key->HaveRsc ? key->Rsc : NULL);
        if (!n) { st = STATUS_INVALID_PARAMETER; break; }
        st = NwoaSetVar(Dev, "wsec_key", k, n);
        RtlSecureZeroMemory(k, sizeof(k));
        NwoaDiagDword(Dev, key->Pairwise ? L"NwoaKeyPairwise" : L"NwoaKeyGroup", (ULONG)st);
        if (!NT_SUCCESS(st))
            break;
        if (key->Pairwise)
            Dev->PtkInstalled = !key->Delete;
        if (!key->Delete) {
            ULONG wsec = 0;
            if (NT_SUCCESS(NwoaGetVar(Dev, "wsec", (PUCHAR)&wsec, 4)))
                NwoaSetVarInt(Dev, "wsec", (wsec & ~7u) | nb_wsec_for_cipher(key->Cipher) |
                              nb_wsec_for_cipher(Dev->Conn.Pairwise));
        }
    }
    RtlSecureZeroMemory(Dev->Keys, sizeof(Dev->Keys));
    Dev->NumKeys = 0;
    Dev->KeyRequest = NULL;
    if (req && Dev->KeyDiagNext) {
        KIRQL irql;
        KeAcquireSpinLock(&Dev->StateLock, &irql);
        Dev->KeyDiag[(Dev->KeyDiagNext - 1) % NWOA_KEYDIAG_N].Status = (ULONG)st;   // final status of this request
        KeReleaseSpinLock(&Dev->StateLock, irql);
    }
    if (req)
        M3(req, st);
}

// NDIS_STATUS_WDI_INDICATION_LINK_STATE_CHANGE: the port indicates a link quality change of more
// than 5 (1 in low latency mode); the host keeps its link metadata from it.  Before s258k only the
// value at association time was ever sent.  Polled from the 2 s diag job while connected.
static void LinkQualityTick(PNWOA_DEVICE Dev)
{
    ULONG v = 0;
    if (!Dev->Connected || !Dev->RingsUp || Dev->Dead || Dev->ConnTask.Active)
        return;
    if (!NT_SUCCESS(NwoaGetInt(Dev, NB_C_GET_RSSI, &v)))
        return;
    LONG rssi = (LONG)v;
    if (rssi >= 0 || rssi < -120)
        return;                                                // not associated / no sample
    Dev->RssiLast = rssi;
    if (rssi < Dev->RssiMin) Dev->RssiMin = rssi;
    if (rssi > Dev->RssiMax) Dev->RssiMax = rssi;
    ULONG lq = LinkQuality((int)rssi);
    if (Dev->Cfg.LinkQualityUpdates && (lq + 5 <= Dev->LqReported || lq >= Dev->LqReported + 5)) {
        IndicateLinkState(Dev, (int)rssi);
        Dev->LqReported = lq;
        InterlockedIncrement(&Dev->CntLinkStateInd);
    }
}

static void JobRoamStay(PNWOA_DEVICE Dev)
{
    // OID_WDI_TASK_ROAM: "the adapter may decide not to roam and stay on the current AP, in which
    // case it only completes the task"
    InterlockedIncrement(&Dev->CntRoamStay);
    NwoaSetStage(Dev, "roam_stayed", STATUS_SUCCESS);
    TaskEnd(Dev, &Dev->ConnTask, NDIS_STATUS_SUCCESS);
}

// ------------------------------------------------ s258n TX stall watchdog (2 s diag job)
static void StallRecord(PNWOA_DEVICE Dev, int Action, ULONG Out, ULONG64 Oldest, LONG Pm)
{
    CHAR tmp[160];
    KIRQL irql;
    NWOA_STALLREC r;
    ULONG64 now = KeQueryInterruptTime();
    RtlZeroMemory(&r, sizeof(r));
    r.Ms = NowMs();
    r.Action = (UCHAR)Action;
    r.Out = Out;
    r.OldestMs = Out ? SinceMs(now, Oldest) : 0;
    r.SinceDoneMs = SinceMs(now, Dev->LastTxDoneTime);
    r.SinceScanStartMs = SinceMs(now, Dev->ConnScanStart);
    r.SinceScanEndMs = SinceMs(now, Dev->ConnScanEnd);
    r.SinceRxMs = SinceMs(now, Dev->LastRxTime);
    r.ConnAgeMs = SinceMs(now, Dev->ConnUpTime);
    r.Pm = Pm;
    NwoaRingSnapshot(Dev, tmp, sizeof(tmp), &r.FlowW, &r.FlowR);
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    Dev->Stall[Dev->StallNext % NWOA_STALL_N] = r;
    Dev->StallNext++;
    KeReleaseSpinLock(&Dev->StateLock, irql);
}

// the data path stayed dead after the PM kick and the flowring re-create: leave the BSS and report
// the loss so Windows connects again at once (it otherwise waits 1-2.5 min, and gave up after 3)
static void StallLinkLoss(PNWOA_DEVICE Dev)
{
    UCHAR bssid[6];
    if (!Dev->Connected)
        return;
    InterlockedIncrement(&Dev->CntStallLinkLoss);
    RtlCopyMemory(bssid, Dev->CurBssid, 6);
    QuietDisconnect(Dev, DIS_TX_STALL);
    IndicateDisassoc(Dev, bssid, WDI_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST);
    NwoaDiagDword(Dev, L"NwoaLostReasonWdi", WDI_ASSOC_STATUS_ROAMING_ASSOCIATION_LOST);
    NwoaDiagEvents(Dev);
    NwoaSetStage(Dev, "tx_stall_link_lost", STATUS_SUCCESS);
}

// firmware "counters" iovar, first bytes as hex (no addresses in it): once 30 s into a connection and
// at each stall detection, to compare offline (wl_cnt layout of this firmware)
static void FwCountersHex(PNWOA_DEVICE Dev, PCWSTR Name)
{
    const ULONG n = 1024;
    PUCHAR raw = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, n, NWOA_TAG);
    PCHAR hex = (PCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, 2 * n + 32, NWOA_TAG);
    if (raw && hex) {
        NTSTATUS st = NwoaGetVar(Dev, "counters", raw, n);
        ULONG o = 0;
        RtlStringCchPrintfA(hex, 32, "st%lx@%lu ", (ULONG)st, NowMs());
        o = (ULONG)strlen(hex);
        static const char hx[] = "0123456789abcdef";
        for (ULONG i = 0; NT_SUCCESS(st) && i < n && o + 3 < 2 * n + 32; i++) {
            hex[o++] = hx[raw[i] >> 4];
            hex[o++] = hx[raw[i] & 15];
        }
        hex[o] = 0;
        NwoaDiagSz(Dev, Name, hex);
    }
    if (raw) ExFreePoolWithTag(raw, NWOA_TAG);
    if (hex) ExFreePoolWithTag(hex, NWOA_TAG);
}

static void StallTick(PNWOA_DEVICE Dev)
{
    ULONG64 now = KeQueryInterruptTime(), oldest = 0;
    LONG pm = -1;
    if (!Dev->Cfg.TxStallMs)
        return;
    if (!Dev->Connected || Dev->FlowState != 2 || Dev->ConnTask.Active || Dev->Dead || !Dev->RingsUp) {
        Dev->StallSm.state = 0;
        return;
    }
    if (!Dev->FwCntBaseDone && SinceMs(now, Dev->ConnUpTime) >= 30000) {
        Dev->FwCntBaseDone = TRUE;
        FwCountersHex(Dev, L"NwoaFwCntBase");
    }
    ULONG out = NwoaTxOutstanding(Dev, &oldest);
    int stalled = nb_tx_stalled(out, out ? SinceMs(now, oldest) : 0, SinceMs(now, Dev->LastTxDoneTime), Dev->Cfg.TxStallMs);
    int a = nb_stall_step(&Dev->StallSm, NowMs(), stalled, out, (ULONG)Dev->CntTxStatus, Dev->Cfg.TxStallRecover);
    switch (a) {
    case NB_SA_NONE:
        return;
    case NB_SA_DETECT:
        InterlockedIncrement(&Dev->CntTxStall);
        FwCountersHex(Dev, L"NwoaFwCntStall");                // before anything is changed
        if (Dev->Cfg.TxStallRecover >= 1)
            pm = PmKick(Dev);
        StallRecord(Dev, a, out, oldest, pm);
        nb_stall_mark(&Dev->StallSm, (ULONG)Dev->CntTxStatus, NowMs());
        return;
    case NB_SA_FLOW: {
        StallRecord(Dev, a, out, oldest, Dev->FwPm);
        NTSTATUS st = NwoaFlowringRecreate(Dev);
        NwoaDiagDword(Dev, L"NwoaStallFlowRecreate", (ULONG)st);
        nb_stall_mark(&Dev->StallSm, (ULONG)Dev->CntTxStatus, NowMs());   // after the flush statuses
        if (!NT_SUCCESS(st)) {
            Dev->StallSm.state = 0;
            if (Dev->Cfg.TxStallRecover >= 2) {
                StallRecord(Dev, NB_SA_LINKLOSS, 0, 0, Dev->FwPm);
                StallLinkLoss(Dev);
            } else {
                InterlockedIncrement(&Dev->CntStallGiveUp);
            }
        }
        return;
    }
    case NB_SA_OK_PM:
        InterlockedIncrement(&Dev->CntStallPmOk);
        break;
    case NB_SA_OK_FLOW:
        InterlockedIncrement(&Dev->CntStallFlowOk);
        break;
    case NB_SA_LINKLOSS:
        StallRecord(Dev, a, out, oldest, Dev->FwPm);
        StallLinkLoss(Dev);
        return;
    case NB_SA_GIVEUP:
        InterlockedIncrement(&Dev->CntStallGiveUp);
        break;
    default:
        break;
    }
    StallRecord(Dev, a, out, oldest, Dev->FwPm);
}

void NwoaEvtWork(WDFWORKITEM Work)
{
    PNWOA_DEVICE Dev = NwoaGetDevice(WdfWorkItemGetParentObject(Work));
    for (int loop = 0; loop < 16; loop++) {
        LONG j = InterlockedExchange(&Dev->Jobs, 0);
        if (!j)
            break;
        if (j & JOB_KEYS) JobKeys(Dev);
        if (j & JOB_DISCONNECT) JobDisconnect(Dev);
        if (j & JOB_RESET) JobReset(Dev);
        if (j & JOB_RADIO) JobRadio(Dev);
        if (j & JOB_LINK_LOST) JobLinkLost(Dev);
        if (j & JOB_ROAM_STAY) JobRoamStay(Dev);
        if (j & JOB_CONNECT) JobConnect(Dev);
        if (j & JOB_SCAN_START) JobScanStart(Dev);
        if (j & JOB_SCAN_DONE) JobScanDone(Dev);
        if (j & JOB_DIAG) {
            SampleTick(Dev);
            LinkQualityTick(Dev);
            NwoaMbPollTick(Dev);                               // s258n
            NwoaConsoleLiveTick(Dev);
            StallTick(Dev);
            SfArmTick(Dev);                                    // s258o
            CandService(Dev);                                  // s258p
            ChunkTick(Dev);                                    // s258q
            SelfScanTick(Dev);
            NwoaDiagCounters(Dev);
            if (Dev->Dead) NwoaDiagConsole(Dev);
        }
    }
}

// D0 exit / release: the chip loses power (S259 _PS3), so the OS is told the link is gone and
// every task still waiting for its M4 (or a key request for its M3) is finished now.
void NwoaWifiPowerDown(PNWOA_DEVICE Dev)
{
    Dev->PtkInstalled = FALSE;
    if (Dev->Connected) {
        UCHAR bssid[6];
        RtlCopyMemory(bssid, Dev->CurBssid, 6);
        Dev->Connected = FALSE;
        if (Dev->LinkUpReported)
            NwoaReportLink(Dev, FALSE);
        IndicateDisassoc(Dev, bssid, WDI_ASSOC_STATUS_DISASSOCIATED_BY_HOST);
    }
    InterlockedExchange(&Dev->ConnState, CONN_IDLE);
    InterlockedExchange(&Dev->ScanRunning, 0);
    InterlockedExchange(&Dev->TxHold, 0);                     // s258o
    Dev->SelfScan = FALSE;
    Dev->ScanStartDeferred = FALSE;
    Dev->SfArmed = FALSE;
    TaskEnd(Dev, &Dev->ScanTask, NDIS_STATUS_FAILURE);
    TaskEnd(Dev, &Dev->ConnTask, NDIS_STATUS_FAILURE);
    TaskEnd(Dev, &Dev->DiscTask, NDIS_STATUS_SUCCESS);
    TaskEnd(Dev, &Dev->ResetTask, NDIS_STATUS_SUCCESS);
    TaskEnd(Dev, &Dev->RadioTask, NDIS_STATUS_FAILURE);
    if (Dev->KeyRequest) {
        WIFIREQUEST r = Dev->KeyRequest;
        Dev->KeyRequest = NULL;
        RtlSecureZeroMemory(Dev->Keys, sizeof(Dev->Keys));
        Dev->NumKeys = 0;
        M3(r, STATUS_DEVICE_NOT_READY);
    }
}

// ------------------------------------------------ WDI_GET_STATISTICS (s258l)
// OID_WDI_GET_STATISTICS (message 34): "requests that the IHV component returns MAC and PHY layer
// statistics"; scope port, normal execution time 1 s.  Both result TLVs (WDI_TLV_MAC_STATISTICS
// per peer, WDI_TLV_PHY_STATISTICS per port) are listed as required, multiple instances allowed
// (Microsoft Learn, netcx/oid-wdi-get-statistics).  Up to s258k the driver answered
// STATUS_NOT_SUPPORTED to every one (hardware log 2026-11-19: about every 60 s, plus one off the
// 60 s grid 40-54 s after each in-connection scan, right before three of four WDI_TASK_DISCONNECTs).
// Layout follows the Microsoft WDI sample Wdi_Get_Statistics (network/wlan/WDI/PLATFORM/NDIS6):
// connected = entry 0 for the AP + entry 1 for broadcast, one PHY entry; not connected = one
// broadcast entry.  The numbers are the driver's own data-path counters (firmware counters are
// not read), so the retry / ACK / FCS fields stay 0.  The TLVs go after the request's own
// WDI_MESSAGE_HEADER; BytesWritten = header + TLVs (WiFiCx sample WifiIhvGetSupportedDeviceServices).
static NTSTATUS StatsReply(PNWOA_DEVICE Dev, PUCHAR Buf, ULONG OutLen, ULONG* Written, ULONG* Needed)
{
    static const UCHAR bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    WDI_GET_STATISTICS_PARAMETERS p;
    WDI_MAC_STATISTICS_CONTAINER mac[2];
    WDI_PHY_STATISTICS_CONTAINER phy[1];
    BOOLEAN conn = Dev->Connected;
    ULONG nmac = 0;
    UINT64 rx = (UINT64)(ULONG)Dev->CntRx, tx = (UINT64)(ULONG)Dev->CntTxStatus;
    UINT64 txFail = (UINT64)(ULONG)Dev->CntTxDrop;

    *Written = sizeof(WDI_MESSAGE_HEADER);
    *Needed = 0;
    if (conn) {
        RtlCopyMemory(mac[0].MACAddress.Address, Dev->CurBssid, 6);
        mac[0].TransmittedFrameCount = tx;
        mac[0].ReceivedFrameCount = rx;
        mac[0].DecryptSuccessCount = Dev->PtkInstalled ? rx : 0;
        nmac = 1;
    }
    RtlCopyMemory(mac[nmac].MACAddress.Address, bcast, 6);
    nmac++;
    phy[0].PhyType = (conn && Dev->Conn.Band == 5) ? WDI_PHY_TYPE_VHT : WDI_PHY_TYPE_HT;
    phy[0].TransmittedFrameCount = tx;
    phy[0].TransmittedFragmentCount = tx;
    phy[0].FailedCount = txFail;
    phy[0].ReceivedFrameCount = rx;
    phy[0].ReceivedFragmentCount = rx;
    p.PeerMACStatistics.SimpleAssign(mac, nmac);
    p.PhyStatistics.SimpleAssign(phy, 1);

    UINT8* out = NULL;
    ULONG len = 0;
    NDIS_STATUS ns = GenerateWdiGetStatisticsFromIhv(&p, 0, &Dev->Tlv, &len, &out);
    p.PeerMACStatistics.SimpleAssign(NULL, 0);
    p.PhyStatistics.SimpleAssign(NULL, 0);
    if (ns != NDIS_STATUS_SUCCESS || !out)
        return STATUS_UNSUCCESSFUL;
    NTSTATUS st = STATUS_SUCCESS;
    *Needed = sizeof(WDI_MESSAGE_HEADER) + len;
    if (!Buf || OutLen < *Needed) {
        st = STATUS_BUFFER_OVERFLOW;
    } else {
        RtlCopyMemory(Buf + sizeof(WDI_MESSAGE_HEADER), out, len);   // keep the request's header
        *Written = *Needed;
    }
    FreeGenerated(out);
    return st;
}

// -------------------------------------------------------- command entry
static BOOLEAN IsNoopSet(UINT16 id)
{
    switch (id) {
    case WDI_SET_POWER_STATE:
    case WDI_SET_PRIVACY_EXEMPTION_LIST:
    case WDI_SET_DEFAULT_KEY_ID:
    case WDI_SET_CONNECTION_QUALITY:
    case WDI_SET_RECEIVE_PACKET_FILTER:
    case WDI_SET_MULTICAST_LIST:
    case WDI_SET_ADAPTER_CONFIGURATION:
    case WDI_SET_AUTO_POWER_SAVE:
    case WDI_SET_FLUSH_BSS_ENTRY:
    case WDI_SET_END_DWELL_TIME:
    case WDI_SET_HOST_DETECT_ERROR:
    case WDI_SET_OPERATION_MODE:
        return TRUE;
    default:
        return FALSE;
    }
}

static void ParseConnect(PNWOA_DEVICE Dev, const WDI_CONNECT_PARAMETERS_CONTAINER& cp,
                         const ArrayOfElements<WDI_CONNECT_BSS_ENTRY_CONTAINER>& list, NTSTATUS* St)
{
    NWOA_CONNECT* c = &Dev->Conn;
    RtlZeroMemory(c, sizeof(*c));
    *St = STATUS_NOT_SUPPORTED;
    if (cp.SSIDList.ElementCount < 1 || list.ElementCount < 1)
        return;
    ULONG sl = min(cp.SSIDList.pElements[0].ElementCount, 32u);
    if (!sl)
        return;
    RtlCopyMemory(c->Ssid, cp.SSIDList.pElements[0].pElements, sl);
    c->SsidLen = sl;
    const WDI_CONNECT_BSS_ENTRY_CONTAINER& be = list.pElements[0];
    RtlCopyMemory(c->Bssid, be.BSSID.Address, 6);
    c->WdiAuth = cp.AuthenticationAlgorithms.ElementCount ? cp.AuthenticationAlgorithms.pElements[0] : WDI_AUTH_ALGO_80211_OPEN;
    c->WdiUcast = cp.UnicastCipherAlgorithms.ElementCount ? cp.UnicastCipherAlgorithms.pElements[0] : WDI_CIPHER_ALGO_NONE;
    c->WdiMcast = cp.MulticastCipherAlgorithms.ElementCount ? cp.MulticastCipherAlgorithms.pElements[0] : WDI_CIPHER_ALGO_NONE;
    // beacon/probe body: prefer what the OS hands back, else our scan cache
    const WDI_BYTE_BLOB* fr = be.Optional.ProbeResponseFrame_IsPresent ? &be.ProbeResponseFrame :
                              be.Optional.BeaconFrame_IsPresent ? &be.BeaconFrame : NULL;
    NWOA_BSS* cached = (NWOA_BSS*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NWOA_BSS), NWOA_TAG);
    BOOLEAN haveCache = cached && NwoaScanCacheFind(Dev, c->Bssid, cached);
    if (fr && fr->ElementCount >= 12 && fr->ElementCount <= sizeof(c->Beacon)) {
        RtlCopyMemory(c->Beacon, fr->pElements, fr->ElementCount);
        c->BeaconLen = fr->ElementCount;
    } else if (haveCache) {
        c->BeaconLen = nb_bss_probe_body(&cached->B, c->Beacon, sizeof(c->Beacon));
    }
    if (haveCache) {
        c->Chanspec = cached->B.chanspec;
        c->Channel = cached->B.channel;
        c->Band = cached->B.band;
        c->Capability = cached->B.capability;
    } else {
        c->Channel = (UCHAR)be.ChannelInfo.ChannelNumber;
        c->Band = be.ChannelInfo.BandId == WDI_BAND_ID_5000 ? 5 : 2;
        c->Capability = c->BeaconLen >= 12 ? nb_ld16(c->Beacon + 10) : 0x0401;
    }
    if (cached)
        ExFreePoolWithTag(cached, NWOA_TAG);
    if (c->WdiAuth == WDI_AUTH_ALGO_80211_OPEN && c->WdiUcast == WDI_CIPHER_ALGO_NONE) {
        c->Rsn = 0;
        *St = STATUS_SUCCESS;
        return;
    }
    if (c->WdiAuth != WDI_AUTH_ALGO_RSNA_PSK || CipherFromWdi(c->WdiUcast) != NB_CIPHER_CCMP)
        return;                                              // WPA2-PSK/CCMP only
    c->Rsn = 1;
    c->Akm = NB_AKM_PSK;
    c->Pairwise = NB_CIPHER_CCMP;
    c->Group = CipherFromWdi(c->WdiMcast) == NB_CIPHER_TKIP ? NB_CIPHER_TKIP : NB_CIPHER_CCMP;
    if (c->BeaconLen > 12) {
        const nb_u8* rsn = nb_ie_find(c->Beacon + 12, c->BeaconLen - 12, 48, NULL);
        nb_u32 g = 0, pw = 0, akm = 0;
        nb_u16 caps = 0;
        if (rsn && nb_rsn_parse(rsn, &g, &pw, &akm, &caps) == NB_OK && (g == NB_CIPHER_CCMP || g == NB_CIPHER_TKIP))
            c->Group = (int)g;
    }
    *St = STATUS_SUCCESS;
}

static void ParseKeys(PNWOA_DEVICE Dev, const WDI_SET_ADD_CIPHER_KEYS_PARAMETERS* a,
                      const WDI_SET_DELETE_CIPHER_KEYS_PARAMETERS* d, NTSTATUS* St)
{
    Dev->NumKeys = 0;
    *St = STATUS_SUCCESS;
    if (a) {
        for (UINT32 i = 0; i < a->SetCipherKey.ElementCount && Dev->NumKeys < NWOA_MAX_KEYS; i++) {
            const WDI_SET_ADD_CIPHER_KEYS_CONTAINER& e = a->SetCipherKey.pElements[i];
            NWOA_KEY* k = &Dev->Keys[Dev->NumKeys];
            RtlZeroMemory(k, sizeof(*k));
            if (e.CipherKeyTypeInfo.KeyType != WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY &&
                e.CipherKeyTypeInfo.KeyType != WDI_CIPHER_KEY_TYPE_GROUP_KEY)
                continue;                                    // IGTK/BIGTK: MFP not offered, ignored
            if (e.CipherKeyTypeInfo.CipherAlgorithm != WDI_CIPHER_ALGO_CCMP || !e.Optional.CCMPKey_IsPresent ||
                e.CCMPKey.ElementCount != 16) {
                *St = STATUS_NOT_SUPPORTED;
                return;
            }
            k->Cipher = NB_CIPHER_CCMP;
            k->Pairwise = e.CipherKeyTypeInfo.KeyType == WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY;
            k->Index = e.Optional.CipherKeyID_IsPresent ? e.CipherKeyID.CipherKeyID : 0;
            if (k->Pairwise)
                k->Index = 0;
            if (e.Optional.PeerMacAddress_IsPresent) {
                RtlCopyMemory(k->Peer, e.PeerMacAddress.Address, 6);
                k->HavePeer = TRUE;
            } else if (k->Pairwise) {
                RtlCopyMemory(k->Peer, Dev->CurBssid, 6);
                k->HavePeer = TRUE;
            }
            RtlCopyMemory(k->Key, e.CCMPKey.pElements, 16);
            k->KeyLen = 16;
            if (e.Optional.ReceiveSequenceCount_IsPresent) {
                RtlCopyMemory(k->Rsc, e.ReceiveSequenceCount.ReceiveSequenceCount, 6);
                k->HaveRsc = TRUE;
            }
            Dev->NumKeys++;
        }
    }
    if (d) {
        for (UINT32 i = 0; i < d->CipherKeyInfo.ElementCount && Dev->NumKeys < NWOA_MAX_KEYS; i++) {
            const WDI_SET_DELETE_CIPHER_KEYS_CONTAINER& e = d->CipherKeyInfo.pElements[i];
            NWOA_KEY* k = &Dev->Keys[Dev->NumKeys];
            RtlZeroMemory(k, sizeof(*k));
            if (e.CipherKeyTypeInfo.KeyType != WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY &&
                e.CipherKeyTypeInfo.KeyType != WDI_CIPHER_KEY_TYPE_GROUP_KEY)
                continue;
            k->Delete = 1;
            k->Pairwise = e.CipherKeyTypeInfo.KeyType == WDI_CIPHER_KEY_TYPE_PAIRWISE_KEY;
            k->Index = (!k->Pairwise && e.Optional.CipherKeyID_IsPresent) ? e.CipherKeyID.CipherKeyID : 0;
            if (e.Optional.PeerMacAddress_IsPresent) {
                RtlCopyMemory(k->Peer, e.PeerMacAddress.Address, 6);
                k->HavePeer = TRUE;
            }
            Dev->NumKeys++;
        }
    }
}

void NwoaEvtWifiSendCommand(WDFDEVICE Device, WIFIREQUEST Request)
{
    PNWOA_DEVICE Dev = NwoaGetDevice(Device);
    UINT inLen = 0, outLen = 0;
    PUCHAR buf = (PUCHAR)WifiRequestGetInOutBuffer(Request, &inLen, &outLen);
    UINT16 id = WifiRequestGetMessageId(Request);
    NTSTATUS st = STATUS_SUCCESS;
    ULONG info = 0;

// every command is recorded (id, connected, M3 status, info) before its M3
#define DONE(s_) do { NTSTATUS d_ = (s_); CmdRecord(Dev, id, d_, info); M3(Request, d_); } while (0)

    if (!buf || inLen < sizeof(WDI_MESSAGE_HEADER)) {
        DONE(STATUS_INVALID_PARAMETER);
        return;
    }
    PWDI_MESSAGE_HEADER hdr = (PWDI_MESSAGE_HEADER)buf;
    PUCHAR tlv = buf + sizeof(WDI_MESSAGE_HEADER);
    ULONG tlvLen = inLen - sizeof(WDI_MESSAGE_HEADER);
    if (id == WDI_TASK_SCAN || id == WDI_TASK_CONNECT || id == WDI_TASK_DISCONNECT || id == WDI_TASK_DOT11_RESET ||
        id == WDI_TASK_ROAM)
        Dev->StaPortId = hdr->PortId;

    switch (id) {
    case WDI_TASK_SCAN: {
        WDI_SCAN_PARAMETERS p;
        if (Dev->ScanTask.Active || Dev->ScanRunning) { DONE(STATUS_DEVICE_BUSY); return; }
        if (ParseWdiTaskScan(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { DONE(STATUS_INVALID_PARAMETER); return; }
        Dev->ScanSsidLen = 0;
        if (p.SSIDList.ElementCount && p.SSIDList.pElements[0].ElementCount &&
            p.SSIDList.pElements[0].ElementCount <= 32) {
            Dev->ScanSsidLen = p.SSIDList.pElements[0].ElementCount;
            RtlCopyMemory(Dev->ScanSsid, p.SSIDList.pElements[0].pElements, Dev->ScanSsidLen);
        }
        info = Dev->ScanSsidLen ? 1 : 0;                       // 1 = directed scan
        info |= ((ULONG)p.ScanModeParameters.ScanTrigger & 0xF) << 4;   // s258l: WDI_SCAN_TRIGGER
        Dev->ScanTrigger = (UCHAR)p.ScanModeParameters.ScanTrigger;     // s258m: for NwoaScanDiag
        Dev->ScanDirected = Dev->ScanSsidLen ? 1 : 0;
        CleanupParsedWdiTaskScan(&p);
        TaskBegin(&Dev->ScanTask, id, WDI_INDICATION_SCAN_COMPLETE, hdr);
        DONE(STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_SCAN_START);
        return;
    }
    case WDI_TASK_CONNECT: {
        WDI_TASK_CONNECT_PARAMETERS p;
        if (Dev->ConnTask.Active) { DONE(STATUS_DEVICE_BUSY); return; }
        if (ParseWdiTaskConnect(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { DONE(STATUS_INVALID_PARAMETER); return; }
        info = ConnectInfo(Dev, p.ConnectParameters, p.PreferredBSSEntryList);
        ConnDiagRecord(Dev, id, p.ConnectParameters, p.PreferredBSSEntryList);
        if (Dev->Connected)
            InterlockedIncrement(&Dev->CntConnWhileConn);
        ParseConnect(Dev, p.ConnectParameters, p.PreferredBSSEntryList, &st);
        CleanupParsedWdiTaskConnect(&p);
        NwoaDiagDword(Dev, L"NwoaConnectParse", (ULONG)st);
        if (!NT_SUCCESS(st)) { DONE(st); return; }
        Dev->ConnIsRoam = FALSE;
        TaskBegin(&Dev->ConnTask, id, WDI_INDICATION_CONNECT_COMPLETE, hdr);
        DONE(STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_CONNECT);
        return;
    }
    case WDI_TASK_ROAM: {
        // OID_WDI_TASK_ROAM (before s258k: STATUS_NOT_SUPPORTED).  Same parameters as a connect.
        WDI_TASK_ROAM_PARAMETERS p;
        if (Dev->ConnTask.Active) { DONE(STATUS_DEVICE_BUSY); return; }
        if (ParseWdiTaskRoam(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { DONE(STATUS_INVALID_PARAMETER); return; }
        info = ConnectInfo(Dev, p.ConnectParameters, p.PreferredBSSEntryList);
        ConnDiagRecord(Dev, id, p.ConnectParameters, p.PreferredBSSEntryList);
        if (Dev->Cfg.RoamStay && Dev->Connected && (info & (1u << 16))) {
            CleanupParsedWdiTaskRoam(&p);
            TaskBegin(&Dev->ConnTask, id, WDI_INDICATION_ROAM_COMPLETE, hdr);
            DONE(STATUS_SUCCESS);
            NwoaQueueJob(Dev, JOB_ROAM_STAY);              // already on that AP: stay
            return;
        }
        ParseConnect(Dev, p.ConnectParameters, p.PreferredBSSEntryList, &st);
        CleanupParsedWdiTaskRoam(&p);
        NwoaDiagDword(Dev, L"NwoaRoamParse", (ULONG)st);
        if (!NT_SUCCESS(st)) { DONE(st); return; }
        InterlockedIncrement(&Dev->CntRoamMove);
        Dev->ConnIsRoam = TRUE;
        TaskBegin(&Dev->ConnTask, id, WDI_INDICATION_ROAM_COMPLETE, hdr);
        DONE(STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_CONNECT);
        return;
    }
    case WDI_TASK_DISCONNECT: {
        WDI_TASK_DISCONNECT_PARAMETERS p;
        NWOA_DISCDIAG dd;
        RtlZeroMemory(&dd, sizeof(dd));
        if (ParseWdiTaskDisconnect(tlvLen, tlv, &Dev->Tlv, &p) == NDIS_STATUS_SUCCESS) {
            info = p.DisconnectParameters.Disassociation80211Reason;
            dd.Reason = p.DisconnectParameters.Disassociation80211Reason;
            if (Dev->Connected && RtlEqualMemory(p.DisconnectParameters.MacAddress.Address, Dev->CurBssid, 6)) {
                info |= 1u << 16;                              // s258l: the peer is the current AP
                dd.PeerCur = 1;
            }
            CleanupParsedWdiTaskDisconnect(&p);
        } else {
            info = 0xFFFF;
            dd.Reason = 0xFFFF;
        }
        {
            // s258m: what the link looked like when Windows decided to disconnect
            KIRQL irql;
            ULONG64 now = KeQueryInterruptTime();
            dd.Ms = (ULONG)(now / 10000);
            dd.PortId = (USHORT)hdr->PortId;
            dd.Conn = Dev->Connected ? 1 : 0;
            KeAcquireSpinLock(&Dev->StateLock, &irql);
            dd.SinceScanMs = SinceMs(now, Dev->LastScanDone);
            dd.SinceRxMs = SinceMs(now, Dev->LastRxTime);
            dd.SinceTxMs = SinceMs(now, Dev->LastTxTime);
            dd.SinceTxDoneMs = SinceMs(now, Dev->LastTxDoneTime);
            dd.SinceLqMs = SinceMs(now, Dev->LastLqInd);
            Window10(Dev, &dd);
            Dev->DiscDiag[Dev->DiscDiagNext % NWOA_DISCDIAG_N] = dd;
            Dev->DiscDiagNext++;
            KeReleaseSpinLock(&Dev->StateLock, irql);
        }
        TaskBegin(&Dev->DiscTask, id, WDI_INDICATION_DISCONNECT_COMPLETE, hdr);
        DONE(STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_DISCONNECT);
        return;
    }
    case WDI_TASK_DOT11_RESET:
        TaskBegin(&Dev->ResetTask, id, WDI_INDICATION_DOT11_RESET_COMPLETE, hdr);
        DONE(STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_RESET);
        return;
    case WDI_TASK_SET_RADIO_STATE: {
        WDI_SET_RADIO_STATE_PARAMETERS p;
        if (ParseWdiTaskSetRadioState(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { DONE(STATUS_INVALID_PARAMETER); return; }
        Dev->RadioWant = p.SoftwareRadioState;
        info = p.SoftwareRadioState;
        CleanupParsedWdiTaskSetRadioState(&p);
        TaskBegin(&Dev->RadioTask, id, WDI_INDICATION_SET_RADIO_STATE_COMPLETE, hdr);
        DONE(STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_RADIO);
        return;
    }
    case WDI_SET_ADD_CIPHER_KEYS:
    case WDI_SET_DELETE_CIPHER_KEYS: {
        if (Dev->KeyRequest) { DONE(STATUS_DEVICE_BUSY); return; }
        if (id == WDI_SET_ADD_CIPHER_KEYS) {
            WDI_SET_ADD_CIPHER_KEYS_PARAMETERS p;
            if (ParseWdiSetAddCipherKeys(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { DONE(STATUS_INVALID_PARAMETER); return; }
            ParseKeys(Dev, &p, NULL, &st);
            KeyDiagRecord(Dev, &p, NULL, st);                  // s258m: cipher numbers, never key data
            CleanupParsedWdiSetAddCipherKeys(&p);
        } else {
            WDI_SET_DELETE_CIPHER_KEYS_PARAMETERS p;
            if (ParseWdiSetDeleteCipherKeys(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { DONE(STATUS_INVALID_PARAMETER); return; }
            ParseKeys(Dev, NULL, &p, &st);
            KeyDiagRecord(Dev, NULL, &p, st);
            CleanupParsedWdiSetDeleteCipherKeys(&p);
        }
        info = Dev->NumKeys;                                   // key count only, never key data
        if (!NT_SUCCESS(st) || !Dev->NumKeys) {
            RtlSecureZeroMemory(Dev->Keys, sizeof(Dev->Keys));
            Dev->NumKeys = 0;
            DONE(st);
            return;
        }
        CmdRecord(Dev, id, STATUS_PENDING, info);
        Dev->KeyRequest = Request;                            // completed by JobKeys
        NwoaQueueJob(Dev, JOB_KEYS);
        return;
    }
    case WDI_GET_STATISTICS: {
        if (!Dev->Cfg.StatsReply) { DONE(STATUS_NOT_SUPPORTED); return; }   // StatsReply=0: as s258k
        ULONG written = sizeof(WDI_MESSAGE_HEADER), need = 0;
        st = StatsReply(Dev, buf, outLen, &written, &need);
        info = NT_SUCCESS(st) ? written : need;               // bytes, no counter values
        Dev->StatsLastBytes = info;
        Dev->StatsLastStatus = (ULONG)st;
        if (st == STATUS_BUFFER_OVERFLOW) {
            InterlockedIncrement(&Dev->CntStatsSmall);
            WifiRequestSetBytesNeeded(Request, need);
        } else if (NT_SUCCESS(st)) {
            InterlockedIncrement(&Dev->CntStatsOk);
        } else {
            InterlockedIncrement(&Dev->CntStatsFail);
        }
        CmdRecord(Dev, id, st, info);
        WifiRequestComplete(Request, st, NT_SUCCESS(st) ? written : sizeof(WDI_MESSAGE_HEADER));
        return;
    }
    default:
        DONE(IsNoopSet(id) ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED);
        return;
    }
#undef DONE
}

// ------------------------------------------------------------ adapter
NTSTATUS NwoaEvtWifiCreateAdapter(WDFDEVICE Device, NETADAPTER_INIT* AdapterInit)
{
    PNWOA_DEVICE Dev = NwoaGetDevice(Device);
    NET_ADAPTER_DATAPATH_CALLBACKS dp;
    WDF_OBJECT_ATTRIBUTES a;
    NETADAPTER adapter;
    NTSTATUS st;

    if (WifiAdapterInitGetType(AdapterInit) != WIFI_ADAPTER_EXTENSIBLE_STATION)
        return STATUS_NOT_SUPPORTED;
    NET_ADAPTER_DATAPATH_CALLBACKS_INIT(&dp, NwoaEvtCreateTxQueue, NwoaEvtCreateRxQueue);
    NetAdapterInitSetDatapathCallbacks(AdapterInit, &dp);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&a, NWOA_ADAPTER_CTX);
    st = NetAdapterCreate(AdapterInit, &a, &adapter);
    NwoaDiagDword(Dev, L"NwoaNetAdapterCreate", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;
    NwoaGetAdapterCtx(adapter)->Dev = Dev;
    st = WifiAdapterInitialize(adapter);
    NwoaDiagDword(Dev, L"NwoaWifiAdapterInit", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;
    st = NwoaAdapterConfigure(Dev, adapter);
    if (!NT_SUCCESS(st))
        return st;
    st = NetAdapterStart(adapter);
    NwoaDiagDword(Dev, L"NwoaNetAdapterStart", (ULONG)st);
    if (NT_SUCCESS(st)) {
        Dev->Adapter = adapter;
        if (Dev->Cfg.LinkUpAtStart)
            NwoaReportLink(Dev, TRUE);
    }
    return st;
}

NTSTATUS NwoaEvtWifiCreateWifiDirect(WDFDEVICE Device, WIFIDIRECT_DEVICE_INIT* Init)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Init);
    return STATUS_NOT_SUPPORTED;
}
