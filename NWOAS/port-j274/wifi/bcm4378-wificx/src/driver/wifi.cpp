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

    NwoaSetVarInt(Dev, "mpc", Dev->Cfg.Mpc);
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
        st = STATUS_UNSUCCESSFUL;
        for (int i = 0; i < ARRAYSIZE(lens) && !NT_SUCCESS(st); i++) {
            nb_event_mask(mask, lens[i], ev, ARRAYSIZE(ev));
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
    NwoaSetInt(Dev, NB_C_SET_INFRA, 1);
    NwoaSetInt(Dev, NB_C_SET_AP, 0);
    NwoaSetVarInt(Dev, "arp_ol", 0);
    NwoaSetVarInt(Dev, "arpoe", 0);
    NwoaSetVarInt(Dev, "ndoe", 0);
    NwoaSetVarInt(Dev, "toe", 0);
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
static void JobScanStart(PNWOA_DEVICE Dev)
{
    UCHAR p[96];
    ULONG n;
    NTSTATUS st;
    if (!Dev->ScanTask.Active)
        return;
    if (!Dev->RingsUp || Dev->Dead || !Dev->RadioOn) {
        TaskEnd(Dev, &Dev->ScanTask, NDIS_STATUS_FAILURE);
        return;
    }
    NwoaScanCacheClear(Dev);
    InterlockedIncrement(&Dev->CntScans);
    n = nb_escan_params(p, sizeof(p), Dev->ScanVer == 2 || Dev->ScanVer == 3 ? Dev->ScanVer : 1,
                        // a directed scan (hidden network profile) must probe; otherwise ScanPassive decides
                        Dev->ScanSsidLen ? NB_SCANTYPE_ACTIVE : (Dev->Cfg.ScanPassive ? NB_SCANTYPE_PASSIVE : NB_SCANTYPE_ACTIVE),
                        NB_ESCAN_SYNC_ID, Dev->ScanSsidLen ? Dev->ScanSsid : NULL, Dev->ScanSsidLen);
    Dev->ScanStatus = -1;
    Dev->ScanDeadline = KeQueryInterruptTime() + 12ull * 10000000ull;   // 12 s
    InterlockedExchange(&Dev->ScanRunning, 1);
    st = NwoaSetVar(Dev, "escan", p, n);
    NwoaDiagDword(Dev, L"NwoaEscanRequest", (ULONG)st);
    if (!NT_SUCCESS(st)) {
        InterlockedExchange(&Dev->ScanRunning, 0);
        TaskEnd(Dev, &Dev->ScanTask, NDIS_STATUS_FAILURE);
    }
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
    BOOLEAN synth = mode != 0 && curIdx < 0 && conn && Dev->Conn.BeaconLen >= 12 && bodies != NULL;
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
                x->EntryAgeInfo.CachedInformation = FALSE;
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
        if (nrep) {
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
    // escan finished (SUCCESS / ABORT / timeout): the scan task is complete either way
    TaskEnd(Dev, &Dev->ScanTask, (Dev->ScanStatus == NB_E_STATUS_FAIL) ? NDIS_STATUS_FAILURE : NDIS_STATUS_SUCCESS);
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
        if (j & JOB_DIAG) { SampleTick(Dev); LinkQualityTick(Dev); NwoaDiagCounters(Dev); if (Dev->Dead) NwoaDiagConsole(Dev); }
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
