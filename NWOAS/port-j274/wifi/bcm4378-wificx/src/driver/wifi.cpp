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
    bi[0].NumChannelWidths = 1;
    bi[0].ChannelWidthList = widths;
    bi[1].BandID = WDI_BAND_ID_5000;
    bi[1].BandState = TRUE;
    bi[1].NumValidPhyTypes = ARRAYSIZE(phy5);
    bi[1].ValidPhyTypeList = phy5;
    bi[1].NumValidChannelTypes = ARRAYSIZE(ch5);
    bi[1].ValidChannelTypes = ch5;
    bi[1].NumChannelWidths = 1;
    bi[1].ChannelWidthList = widths;
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
                        Dev->Cfg.ScanPassive || !Dev->ScanSsidLen ? (Dev->Cfg.ScanPassive ? NB_SCANTYPE_PASSIVE : NB_SCANTYPE_ACTIVE) : NB_SCANTYPE_ACTIVE,
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

static void JobScanDone(PNWOA_DEVICE Dev)
{
    ULONG count = 0;
    KIRQL irql;
    NWOA_BSS* snap = (NWOA_BSS*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NWOA_BSS) * NWOA_MAX_BSS, NWOA_TAG);
    PUCHAR bodies = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)NWOA_MAX_BSS * (NWOA_BSS_IE_MAX + 12), NWOA_TAG);
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
    if (count) {
        WDI_BSS_ENTRY_CONTAINER* e = new WDI_BSS_ENTRY_CONTAINER[count];
        if (e) {
            for (ULONG i = 0; i < count; i++) {
                PUCHAR body = bodies + (SIZE_T)i * (NWOA_BSS_IE_MAX + 12);
                snap[i].B.ies = snap[i].Ies;
                ULONG bl = nb_bss_probe_body(&snap[i].B, body, NWOA_BSS_IE_MAX + 12);
                RtlCopyMemory(e[i].BSSID.Address, snap[i].B.bssid, 6);
                e[i].Optional.ProbeResponseFrame_IsPresent = 1;
                e[i].ProbeResponseFrame.SimpleAssign(body, bl);
                e[i].SignalInfo.RSSI = snap[i].B.rssi;
                e[i].SignalInfo.LinkQuality = LinkQuality(snap[i].B.rssi);
                e[i].ChannelInfo.ChannelNumber = snap[i].B.channel;
                e[i].ChannelInfo.BandId = WdiBand(snap[i].B.band);
            }
            WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS p;
            p.Optional.DeviceDescriptor_IsPresent = 1;
            p.DeviceDescriptor.SimpleAssign(e, count);
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
            for (ULONG i = 0; i < count; i++)
                e[i].ProbeResponseFrame.SimpleAssign(NULL, 0);
            delete[] e;
        }
    }
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

static void QuietDisconnect(PNWOA_DEVICE Dev)
{
    if (Dev->LinkUpReported)
        NwoaReportLink(Dev, FALSE);
    Dev->Connected = FALSE;
    InterlockedExchange(&Dev->ConnState, CONN_IDLE);
    (void)NwoaSetInt(Dev, NB_C_DISASSOC, 0);
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
    r->AssociationResultParameters.WMMQoSEnabled = FALSE;             // data path is 802.3 without QoS fields
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
        li->Bandwidth = 20;
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
    if (Dev->Connected || Dev->ConnState != CONN_IDLE)
        QuietDisconnect(Dev);
    NwoaSetStage(Dev, "connect_start", STATUS_PENDING);
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
        st = NwoaFlowringCreate(Dev, c->Bssid);
        NwoaDiagDword(Dev, L"NwoaFlowringCreate", (ULONG)st);
        NwoaGetInt(Dev, NB_C_GET_RSSI, &rssi);
        RtlCopyMemory(Dev->CurBssid, c->Bssid, 6);
        Dev->Connected = TRUE;
        IndicateAssocResult(Dev, WDI_ASSOC_STATUS_SUCCESS, 0, req, reqLen, resp, respLen);
        NwoaReportLink(Dev, TRUE);                             // lets NetAdapterCx create the TX/RX queues
        IndicateLinkState(Dev, (int)(LONG)rssi);
        NwoaDiagEvents(Dev);
        if (req)
            ExFreePoolWithTag(req, NWOA_TAG);
        NwoaSetStage(Dev, NT_SUCCESS(st) ? "connected" : "connected_no_flowring", st);
        TaskEnd(Dev, &Dev->ConnTask, NDIS_STATUS_SUCCESS);
        return;
    }
    // failed: stop the firmware from retrying, report the association failure
    InterlockedExchange(&Dev->ConnState, CONN_IDLE);
    (void)NwoaSetInt(Dev, NB_C_DISASSOC, 0);
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
    QuietDisconnect(Dev);
    if (was)
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
    if (Dev->LinkUpReported)
        NwoaReportLink(Dev, FALSE);
    NwoaFlowringDelete(Dev);
    NwoaRxFlushHeld(Dev);
    IndicateDisassoc(Dev, bssid, Dev->LostReason);
    NwoaDiagDword(Dev, L"NwoaLostReasonWdi", Dev->LostReason);
    NwoaDiagEvents(Dev);
    NwoaSetStage(Dev, "link_lost", STATUS_SUCCESS);
}

static void JobReset(PNWOA_DEVICE Dev)
{
    if (Dev->Connected || Dev->ConnState != CONN_IDLE)
        QuietDisconnect(Dev);
    TaskEnd(Dev, &Dev->ResetTask, NDIS_STATUS_SUCCESS);
}

static void JobRadio(PNWOA_DEVICE Dev)
{
    BOOLEAN on = Dev->RadioWant != 0;
    NTSTATUS st = STATUS_SUCCESS;
    if (!on && (Dev->Connected || Dev->ConnState != CONN_IDLE))
        QuietDisconnect(Dev);
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
    if (req)
        M3(req, st);
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
        if (j & JOB_CONNECT) JobConnect(Dev);
        if (j & JOB_SCAN_START) JobScanStart(Dev);
        if (j & JOB_SCAN_DONE) JobScanDone(Dev);
        if (j & JOB_DIAG) { NwoaDiagCounters(Dev); if (Dev->Dead) NwoaDiagConsole(Dev); }
    }
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

static void ParseConnect(PNWOA_DEVICE Dev, const WDI_TASK_CONNECT_PARAMETERS& p, NTSTATUS* St)
{
    NWOA_CONNECT* c = &Dev->Conn;
    RtlZeroMemory(c, sizeof(*c));
    *St = STATUS_NOT_SUPPORTED;
    const WDI_CONNECT_PARAMETERS_CONTAINER& cp = p.ConnectParameters;
    if (cp.SSIDList.ElementCount < 1 || p.PreferredBSSEntryList.ElementCount < 1)
        return;
    ULONG sl = min(cp.SSIDList.pElements[0].ElementCount, 32u);
    if (!sl)
        return;
    RtlCopyMemory(c->Ssid, cp.SSIDList.pElements[0].pElements, sl);
    c->SsidLen = sl;
    const WDI_CONNECT_BSS_ENTRY_CONTAINER& be = p.PreferredBSSEntryList.pElements[0];
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

    if (!buf || inLen < sizeof(WDI_MESSAGE_HEADER)) {
        M3(Request, STATUS_INVALID_PARAMETER);
        return;
    }
    PWDI_MESSAGE_HEADER hdr = (PWDI_MESSAGE_HEADER)buf;
    PUCHAR tlv = buf + sizeof(WDI_MESSAGE_HEADER);
    ULONG tlvLen = inLen - sizeof(WDI_MESSAGE_HEADER);
    if (id == WDI_TASK_SCAN || id == WDI_TASK_CONNECT || id == WDI_TASK_DISCONNECT || id == WDI_TASK_DOT11_RESET)
        Dev->StaPortId = hdr->PortId;

    switch (id) {
    case WDI_TASK_SCAN: {
        WDI_SCAN_PARAMETERS p;
        if (Dev->ScanTask.Active || Dev->ScanRunning) { M3(Request, STATUS_DEVICE_BUSY); return; }
        if (ParseWdiTaskScan(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { M3(Request, STATUS_INVALID_PARAMETER); return; }
        Dev->ScanSsidLen = 0;
        if (p.SSIDList.ElementCount && p.SSIDList.pElements[0].ElementCount &&
            p.SSIDList.pElements[0].ElementCount <= 32) {
            Dev->ScanSsidLen = p.SSIDList.pElements[0].ElementCount;
            RtlCopyMemory(Dev->ScanSsid, p.SSIDList.pElements[0].pElements, Dev->ScanSsidLen);
        }
        CleanupParsedWdiTaskScan(&p);
        TaskBegin(&Dev->ScanTask, id, WDI_INDICATION_SCAN_COMPLETE, hdr);
        M3(Request, STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_SCAN_START);
        return;
    }
    case WDI_TASK_CONNECT: {
        WDI_TASK_CONNECT_PARAMETERS p;
        if (Dev->ConnTask.Active) { M3(Request, STATUS_DEVICE_BUSY); return; }
        if (ParseWdiTaskConnect(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { M3(Request, STATUS_INVALID_PARAMETER); return; }
        ParseConnect(Dev, p, &st);
        CleanupParsedWdiTaskConnect(&p);
        NwoaDiagDword(Dev, L"NwoaConnectParse", (ULONG)st);
        if (!NT_SUCCESS(st)) { M3(Request, st); return; }
        TaskBegin(&Dev->ConnTask, id, WDI_INDICATION_CONNECT_COMPLETE, hdr);
        M3(Request, STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_CONNECT);
        return;
    }
    case WDI_TASK_DISCONNECT:
        TaskBegin(&Dev->DiscTask, id, WDI_INDICATION_DISCONNECT_COMPLETE, hdr);
        M3(Request, STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_DISCONNECT);
        return;
    case WDI_TASK_DOT11_RESET:
        TaskBegin(&Dev->ResetTask, id, WDI_INDICATION_DOT11_RESET_COMPLETE, hdr);
        M3(Request, STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_RESET);
        return;
    case WDI_TASK_SET_RADIO_STATE: {
        WDI_SET_RADIO_STATE_PARAMETERS p;
        if (ParseWdiTaskSetRadioState(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { M3(Request, STATUS_INVALID_PARAMETER); return; }
        Dev->RadioWant = p.SoftwareRadioState;
        CleanupParsedWdiTaskSetRadioState(&p);
        TaskBegin(&Dev->RadioTask, id, WDI_INDICATION_SET_RADIO_STATE_COMPLETE, hdr);
        M3(Request, STATUS_SUCCESS);
        NwoaQueueJob(Dev, JOB_RADIO);
        return;
    }
    case WDI_SET_ADD_CIPHER_KEYS:
    case WDI_SET_DELETE_CIPHER_KEYS: {
        if (Dev->KeyRequest) { M3(Request, STATUS_DEVICE_BUSY); return; }
        if (id == WDI_SET_ADD_CIPHER_KEYS) {
            WDI_SET_ADD_CIPHER_KEYS_PARAMETERS p;
            if (ParseWdiSetAddCipherKeys(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { M3(Request, STATUS_INVALID_PARAMETER); return; }
            ParseKeys(Dev, &p, NULL, &st);
            CleanupParsedWdiSetAddCipherKeys(&p);
        } else {
            WDI_SET_DELETE_CIPHER_KEYS_PARAMETERS p;
            if (ParseWdiSetDeleteCipherKeys(tlvLen, tlv, &Dev->Tlv, &p) != NDIS_STATUS_SUCCESS) { M3(Request, STATUS_INVALID_PARAMETER); return; }
            ParseKeys(Dev, NULL, &p, &st);
            CleanupParsedWdiSetDeleteCipherKeys(&p);
        }
        if (!NT_SUCCESS(st) || !Dev->NumKeys) {
            RtlSecureZeroMemory(Dev->Keys, sizeof(Dev->Keys));
            Dev->NumKeys = 0;
            M3(Request, st);
            return;
        }
        Dev->KeyRequest = Request;                            // completed by JobKeys
        NwoaQueueJob(Dev, JOB_KEYS);
        return;
    }
    default:
        M3(Request, IsNoopSet(id) ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED);
        return;
    }
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
