// diag.cpp - settings and status values in the device hardware key:
//   HKLM\SYSTEM\CurrentControlSet\Enum\<instance>\Device Parameters\Nwoa*
// so the status job (nwoas_scripts/wifi-s258/jobs/W2-status.cmd) can read them
// with "reg query" without a kernel debugger.  PASSIVE_LEVEL only.
// The MAC address and key material are never written here.
#include "precomp.h"

#ifndef PLUGPLAY_REGISTRY_KEY_DEVICE
#define PLUGPLAY_REGISTRY_KEY_DEVICE 1           // wdm.h value (device hardware key)
#endif

static NTSTATUS OpenHwKey(PNWOA_DEVICE Dev, ACCESS_MASK Access, WDFKEY* Key)
{
    return WdfDeviceOpenRegistryKey(Dev->Device, PLUGPLAY_REGISTRY_KEY_DEVICE, Access,
                                    WDF_NO_OBJECT_ATTRIBUTES, Key);
}

static void QueryStr(WDFKEY Key, PCWSTR Name, PWCHAR Buf, USHORT Cch, PCWSTR Default)
{
    UNICODE_STRING name, val;
    RtlInitUnicodeString(&name, Name);
    RtlStringCchCopyW(Buf, Cch, Default);
    if (!Key)
        return;
    val.Buffer = Buf;
    val.Length = 0;
    val.MaximumLength = (USHORT)((Cch - 1) * sizeof(WCHAR));
    if (NT_SUCCESS(WdfRegistryQueryUnicodeString(Key, &name, NULL, &val)))
        Buf[val.Length / sizeof(WCHAR)] = L'\0';
    else
        RtlStringCchCopyW(Buf, Cch, Default);
}

static ULONG QueryUlong(WDFKEY Key, PCWSTR Name, ULONG Default)
{
    UNICODE_STRING name;
    ULONG v = Default;
    RtlInitUnicodeString(&name, Name);
    if (!Key || !NT_SUCCESS(WdfRegistryQueryULong(Key, &name, &v)))
        v = Default;
    return v;
}

void NwoaReadConfig(PNWOA_DEVICE Dev)
{
    WDFKEY key = NULL;
    NWOA_CONFIG* c = &Dev->Cfg;
    if (!NT_SUCCESS(OpenHwKey(Dev, KEY_READ, &key)))
        key = NULL;
    QueryStr(key, L"FirmwareDir", c->FirmwareDir, ARRAYSIZE(c->FirmwareDir), L"");
    QueryStr(key, L"Country", c->Country, ARRAYSIZE(c->Country), L"");
    c->PollIntervalMs = QueryUlong(key, L"PollIntervalMs", 2);
    if (c->PollIntervalMs < 1 || c->PollIntervalMs > 1000)
        c->PollIntervalMs = 2;
    c->PollIdleMs = QueryUlong(key, L"PollIdleMs", 50);
    if (c->PollIdleMs < c->PollIntervalMs || c->PollIdleMs > 2000)
        c->PollIdleMs = 50;
    c->ScanPassive   = QueryUlong(key, L"ScanPassive", 1);   // S257 PASS_SCAN used passive escan v1
    c->Mpc           = QueryUlong(key, L"Mpc", 0);
    c->PowerSave     = QueryUlong(key, L"PowerSave", 0);
    c->RxFrameFormat = QueryUlong(key, L"RxFrameFormat", 1)   /* 1 = 802.11 + LLC: what WiFiCx wanted on hardware 2026-11-18 */;
    c->UseInterrupt  = QueryUlong(key, L"UseInterrupt", 0);   // S259: poll first, interrupt second
    c->ClearNoSnoop  = QueryUlong(key, L"ClearNoSnoop", 1);
    c->DmaMode       = QueryUlong(key, L"DmaMode", 0);
    c->VerifyFirmware = QueryUlong(key, L"VerifyFirmware", 1);
    c->LinkUpAtStart = QueryUlong(key, L"LinkUpAtStart", 0);
    c->RxProtectedBit = QueryUlong(key, L"RxProtectedBit", 1);
    c->StopAfter     = QueryUlong(key, L"StopAfter", 0);
    c->SettleMs      = QueryUlong(key, L"SettleMs", 0);
    c->AccessDelayUs = QueryUlong(key, L"AccessDelayUs", 0);
    if (c->AccessDelayUs > 5000)
        c->AccessDelayUs = 5000;
    c->PostWriteRead = QueryUlong(key, L"PostWriteRead", 0);
    c->FwRoamOff     = QueryUlong(key, L"FwRoamOff", 1);
    c->LinkQualityUpdates = QueryUlong(key, L"LinkQualityUpdates", 1);
    c->RoamStay      = QueryUlong(key, L"RoamStay", 1);
    c->StatsReply    = QueryUlong(key, L"StatsReply", 1);
    c->ScanReportConnectedOnly = QueryUlong(key, L"ScanReportConnectedOnly", 0);   // s258m H1 test
    c->NoDisassocIndOnHostDisconnect = QueryUlong(key, L"NoDisassocIndOnHostDisconnect", 0);   // s258m H3 test
    c->BssAgeInfo    = QueryUlong(key, L"BssAgeInfo", 1);
    c->LinkInfoReal  = QueryUlong(key, L"LinkInfoReal", 1);
    c->ReportWmm     = QueryUlong(key, L"ReportWmm", 0);
    c->ConnectedScan = QueryUlong(key, L"ConnectedScan", 0);           // s258n (default 1) -> s258o 0
    c->PmKickAfterScan = QueryUlong(key, L"PmKickAfterScan", 0);
    c->TxStallMs     = QueryUlong(key, L"TxStallMs", 8000);
    if (c->TxStallMs && (c->TxStallMs < 4000 || c->TxStallMs > 600000))
        c->TxStallMs = 8000;
    c->TxStallRecover = QueryUlong(key, L"TxStallRecover", 2);
    c->MbPoll        = QueryUlong(key, L"MbPoll", 1);
    c->ConsoleLive   = QueryUlong(key, L"ConsoleLive", 1);
    c->DiagEvents    = QueryUlong(key, L"DiagEvents", 2);
    c->ScanFlowCheck = QueryUlong(key, L"ScanFlowCheck", 1);           // s258o
    if (c->ScanFlowCheck > 3)
        c->ScanFlowCheck = 1;
    c->ScanFlowProbeMs = QueryUlong(key, L"ScanFlowProbeMs", 300);
    if (c->ScanFlowProbeMs < 50 || c->ScanFlowProbeMs > 3000)
        c->ScanFlowProbeMs = 300;
    c->ScanTxHold    = QueryUlong(key, L"ScanTxHold", 0);
    c->TestConnScanSec = QueryUlong(key, L"TestConnScanSec", 0);
    if (c->TestConnScanSec && (c->TestConnScanSec < 60 || c->TestConnScanSec > 3600))
        c->TestConnScanSec = 0;
    // s258p
    c->ScanPm        = QueryUlong(key, L"ScanPm", 0);
    if (c->ScanPm != 2)
        c->ScanPm = 0;
    c->ScanChanList  = QueryUlong(key, L"ScanChanList", 0);
    if (c->ScanChanList > 2)
        c->ScanChanList = 0;
    c->FlowResume    = QueryUlong(key, L"FlowResume", 0) ? 1 : 0;
    c->InitIovarSet  = QueryUlong(key, L"InitIovarSet", 0) & 0x3F;
    c->CandidateCycle = QueryUlong(key, L"CandidateCycle", 0) ? 1 : 0;
    if (c->CandidateCycle) {
        c->ScanFlowCheck = 1;                     // the s258o fix is the safety net of every candidate
        if (!c->TestConnScanSec)
            c->TestConnScanSec = 120;
    }
    // s258q
    c->TestScanShape = QueryUlong(key, L"TestScanShape", 0);
    if (c->TestScanShape >= NB_SHAPE_N)
        c->TestScanShape = 0;
    c->ShapeCycle    = QueryUlong(key, L"ShapeCycle", 0) ? 1 : 0;
    c->ShapeChunkN   = QueryUlong(key, L"ShapeChunkN", 3);
    if (c->ShapeChunkN < 1 || c->ShapeChunkN > 16)
        c->ShapeChunkN = 3;
    c->ShapeChunkGapMs = QueryUlong(key, L"ShapeChunkGapMs", 2000);
    if (c->ShapeChunkGapMs < 1000 || c->ShapeChunkGapMs > 10000)
        c->ShapeChunkGapMs = 2000;
    c->ShapeLongMs   = QueryUlong(key, L"ShapeLongMs", 3000);
    if (c->ShapeLongMs < 100 || c->ShapeLongMs > 10000)
        c->ShapeLongMs = 3000;
    if (c->ShapeCycle) {
        c->ScanFlowCheck = 1;                     // the s258o fix stays the safety net of every shape
        c->CandidateCycle = 0;                    // one variable at a time
        if (!c->TestConnScanSec)
            c->TestConnScanSec = 120;
    }
    if (c->SettleMs > 10000)
        c->SettleMs = 10000;
    if (key)
        WdfRegistryClose(key);
}

static void Assign(PNWOA_DEVICE Dev, PCWSTR Name, ULONG Type, ULONG Len, PVOID Data)
{
    WDFKEY key;
    UNICODE_STRING name;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || !Dev->Device)
        return;
    if (!NT_SUCCESS(OpenHwKey(Dev, KEY_SET_VALUE, &key)))
        return;
    RtlInitUnicodeString(&name, Name);
    (void)WdfRegistryAssignValue(key, &name, Type, Len, Data);
    WdfRegistryClose(key);
}

void NwoaDiagSz(PNWOA_DEVICE Dev, PCWSTR Name, PCSTR Value)
{
    ULONG i = 0;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    PWCHAR w = (PWCHAR)ExAllocatePool2(POOL_FLAG_PAGED, (NWOA_CONSOLE_TAIL + 1) * sizeof(WCHAR), NWOA_TAG);
    if (!w)
        return;
    for (; Value && Value[i] && i < NWOA_CONSOLE_TAIL; i++)
        w[i] = (WCHAR)(UCHAR)Value[i];
    w[i] = 0;
    Assign(Dev, Name, REG_SZ, (i + 1) * sizeof(WCHAR), w);
    ExFreePoolWithTag(w, NWOA_TAG);
}

void NwoaDiagDword(PNWOA_DEVICE Dev, PCWSTR Name, ULONG Value)
{
    Assign(Dev, Name, REG_DWORD, sizeof(ULONG), &Value);
}

void NwoaDiagCounters(PNWOA_DEVICE Dev)
{
    NwoaDiagDword(Dev, L"NwoaCntIsr", (ULONG)Dev->CntIsr);
    NwoaDiagDword(Dev, L"NwoaCntPoll", (ULONG)Dev->CntPoll);
    NwoaDiagDword(Dev, L"NwoaCntMsgs", (ULONG)Dev->CntMsgs);
    NwoaDiagDword(Dev, L"NwoaCntEvents", (ULONG)Dev->CntEvents);
    NwoaDiagDword(Dev, L"NwoaCntRx", (ULONG)Dev->CntRx);
    NwoaDiagDword(Dev, L"NwoaCntRxDrop", (ULONG)Dev->CntRxDrop);
    NwoaDiagDword(Dev, L"NwoaCntRxEventFrames", (ULONG)Dev->CntRxAsEvent);
    NwoaDiagDword(Dev, L"NwoaCntTx", (ULONG)Dev->CntTx);
    NwoaDiagDword(Dev, L"NwoaCntTxStatus", (ULONG)Dev->CntTxStatus);
    NwoaDiagDword(Dev, L"NwoaCntTxDrop", (ULONG)Dev->CntTxDrop);
    NwoaDiagDword(Dev, L"NwoaTxDropConvert", (ULONG)Dev->CntTxDropConv);     // 802.11 -> 802.3 failed
    NwoaDiagDword(Dev, L"NwoaTxDropSize", (ULONG)Dev->CntTxDropSize);        // > 2048 bytes / empty
    NwoaDiagDword(Dev, L"NwoaTxDropNotReady", (ULONG)Dev->CntTxDropNotReady); // no flowring (not connected)
    NwoaDiagDword(Dev, L"NwoaTxDropOther", (ULONG)Dev->CntTxDropOther);
    NwoaDiagDword(Dev, L"NwoaPtkInstalled", Dev->PtkInstalled);
    NwoaDiagDword(Dev, L"NwoaCntEapolDup", (ULONG)Dev->CntEapolDup);
    {
        CHAR b[200];
        RtlStringCchPrintfA(b, sizeof(b), "rx p1=%ld p2=%ld p3=%ld p4=%ld g1=%ld g2=%ld other=%ld; tx p1=%ld p2=%ld p3=%ld p4=%ld g1=%ld g2=%ld other=%ld",
            Dev->EapolRxMsg[1], Dev->EapolRxMsg[2], Dev->EapolRxMsg[3], Dev->EapolRxMsg[4], Dev->EapolRxMsg[5], Dev->EapolRxMsg[6], Dev->EapolRxMsg[0],
            Dev->EapolTxMsg[1], Dev->EapolTxMsg[2], Dev->EapolTxMsg[3], Dev->EapolTxMsg[4], Dev->EapolTxMsg[5], Dev->EapolTxMsg[6], Dev->EapolTxMsg[0]);
        NwoaDiagSz(Dev, L"NwoaEapolMsgs", b);              // message types only (Key Information flags)
    }
    NwoaDiagDword(Dev, L"NwoaCntTx80211", (ULONG)Dev->CntTx80211);
    NwoaDiagDword(Dev, L"NwoaCntIoctl", (ULONG)Dev->CntIoctl);
    NwoaDiagDword(Dev, L"NwoaCntIoctlTimeout", (ULONG)Dev->CntIoctlTimeout);
    NwoaDiagDword(Dev, L"NwoaCntUnknownMsg", (ULONG)Dev->CntUnknown);
    NwoaDiagDword(Dev, L"NwoaCntMbData", (ULONG)Dev->CntMbData);
    NwoaDiagDword(Dev, L"NwoaCntScans", (ULONG)Dev->CntScans);
    NwoaDiagDword(Dev, L"NwoaDead", (ULONG)Dev->Dead);
    NwoaDiagDword(Dev, L"NwoaConnected", Dev->Connected);
    NwoaDiagDword(Dev, L"NwoaFlowState", (ULONG)Dev->FlowState);
    NwoaDiagDword(Dev, L"NwoaIrqMode", (ULONG)Dev->IrqMode);
    NwoaDiagDword(Dev, L"NwoaPollIdle", (ULONG)Dev->PollIdle);
    NwoaDiagEvents(Dev);
    NwoaDiagDisconnect(Dev);
}

static void CmdStr(PCHAR B, SIZE_T Cap, const NWOA_CMDREC* R, ULONG N)
{
    B[0] = 0;
    for (ULONG i = 0; i < N; i++) {
        CHAR one[64];
        RtlStringCchPrintfA(one, sizeof(one), "%u/%u/%lx/%lx@%lu ", R[i].Id, R[i].Conn, R[i].Status, R[i].Info, R[i].Ms);
        RtlStringCchCatA(B, Cap, one);
    }
}

// s258m records, oldest first (layouts in nwoa.h; numbers and bits only).
//  NwoaScanDiag  t<trigger> d<directed> c<connected> n<found>/<reported> p<connected BSS present> s<added from beacon>
//                r<its RSSI> k<its RSSI rank among same-SSID, 1 best> ss<same-SSID entries> o<strongest other RSSI>/<band>
//                ch<channel> b<band> bw<MHz> ie<IE bytes> cap<capability hex> fw<firmware RSSI> st<escan status>@ms
//  NwoaDiscDiag  r<802.11 reason> port<id> cur<peer is current AP> c<connected> scan/rx/tx/txd/lq<ms since last scan
//                done / RX frame / TX submit / TX completion / link state indication> rx10/tx10/txd10<frames>/span<ms>@ms
//  NwoaConnDiag  id<6 connect, 100 roam> rr<RoamRequest> rnr<RoamNeededReason> rt<RoamTrigger> h<hidden> mfp bt<BSS transition>
//                au<first auth> uc<n>:<first>/<mask> mc<first>/<mask> ap<AP RSN pairwise mask>/<group suite>
//                [<candidate bits hex>/<cached RSSI> ...]@ms   candidate bits: 1 current, 2 last assoc, 4 cached,
//                8 5 GHz, 10 frame from host, bits 8-15 channel
//  NwoaKeyDiag   a<1 add, 0 delete> ty<key type> n<elements>/<accepted> ci<first cipher>/<mask> st<status>@ms
static void NwoaDiagS258m(PNWOA_DEVICE Dev, PCHAR b, SIZE_T cap)
{
    KIRQL irql;
    NWOA_SCANDIAG sd[NWOA_SCANDIAG_N];
    NWOA_DISCDIAG dd[NWOA_DISCDIAG_N];
    NWOA_CONNDIAG cd[NWOA_CONNDIAG_N];
    NWOA_KEYDIAG kd[NWOA_KEYDIAG_N];
    ULONG ns, nd, nc, nk;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    RtlCopyMemory(sd, Dev->ScanDiag, sizeof(sd)); ns = Dev->ScanDiagNext;
    RtlCopyMemory(dd, Dev->DiscDiag, sizeof(dd)); nd = Dev->DiscDiagNext;
    RtlCopyMemory(cd, Dev->ConnDiag, sizeof(cd)); nc = Dev->ConnDiagNext;
    RtlCopyMemory(kd, Dev->KeyDiag, sizeof(kd));  nk = Dev->KeyDiagNext;
    KeReleaseSpinLock(&Dev->StateLock, irql);
    CHAR one[256];

    b[0] = 0;
    for (ULONG k = ns > NWOA_SCANDIAG_N ? ns - NWOA_SCANDIAG_N : 0; k < ns; k++) {
        const NWOA_SCANDIAG* r = &sd[k % NWOA_SCANDIAG_N];
        RtlStringCchPrintfA(one, sizeof(one), "t%u d%u c%u n%u/%u p%u s%u r%d k%u ss%u o%d/%u ch%u b%u bw%u ie%u cap%x fw%d st%u sk%u@%lu ",
            r->Trigger, r->Directed, r->Conn, r->Entries, r->Reported, r->Present, r->Synth, r->Rssi, r->Rank, r->SameSsid,
            r->OtherRssi, r->OtherBand, r->Channel, r->Band, r->Bw, r->IeLen, r->Cap, r->FwRssi, r->Status, r->Skipped, r->Ms);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaScanDiag", b);
    NwoaDiagDword(Dev, L"NwoaScanDiagCount", ns);

    b[0] = 0;
    for (ULONG k = nd > NWOA_DISCDIAG_N ? nd - NWOA_DISCDIAG_N : 0; k < nd; k++) {
        const NWOA_DISCDIAG* r = &dd[k % NWOA_DISCDIAG_N];
        RtlStringCchPrintfA(one, sizeof(one), "r%u port%u cur%u c%u scan%lu rx%lu tx%lu txd%lu lq%lu rx10=%ld tx10=%ld txd10=%ld span%lu@%lu ",
            r->Reason, r->PortId, r->PeerCur, r->Conn, r->SinceScanMs, r->SinceRxMs, r->SinceTxMs, r->SinceTxDoneMs,
            r->SinceLqMs, r->Rx10, r->Tx10, r->TxDone10, r->SampleSpanMs, r->Ms);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaDiscDiag", b);

    b[0] = 0;
    for (ULONG k = nc > NWOA_CONNDIAG_N ? nc - NWOA_CONNDIAG_N : 0; k < nc; k++) {
        const NWOA_CONNDIAG* r = &cd[k % NWOA_CONNDIAG_N];
        RtlStringCchPrintfA(one, sizeof(one), "id%u rr%u rnr%lu rt%lu h%u mfp%u bt%u au%lu uc%u:%lu/%lx mc%lu/%lx ap%lx/%lu [",
            r->Id, r->RoamRequest, r->RoamNeededReason, r->RoamTrigger, r->Hidden, r->Mfp, r->BssTrans, r->Auth0,
            r->NUc, r->Uc0, r->UcMask, r->Mc0, r->McMask, r->ApPwMask, r->ApGroup);
        RtlStringCchCatA(b, cap, one);
        for (ULONG i = 0; i < r->NCand && i < NWOA_CANDMAX; i++) {
            RtlStringCchPrintfA(one, sizeof(one), "%x/%d ", r->Cand[i], r->CandRssi[i]);
            RtlStringCchCatA(b, cap, one);
        }
        RtlStringCchPrintfA(one, sizeof(one), "n%u]@%lu ", r->NCand, r->Ms);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaConnDiag", b);

    b[0] = 0;
    for (ULONG k = nk > NWOA_KEYDIAG_N ? nk - NWOA_KEYDIAG_N : 0; k < nk; k++) {
        const NWOA_KEYDIAG* r = &kd[k % NWOA_KEYDIAG_N];
        RtlStringCchPrintfA(one, sizeof(one), "a%u ty%u n%u/%u ci%lu/%lx st%lx@%lu ",
            r->Add, r->Type, r->Elements, r->Accepted, r->Cipher, r->CipherMask, r->Status, r->Ms);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaKeyDiag", b);

    RtlStringCchPrintfA(b, cap, "ScanReportConnectedOnly=%lu NoDisassocIndOnHostDisconnect=%lu BssAgeInfo=%lu LinkInfoReal=%lu ReportWmm=%lu",
        Dev->Cfg.ScanReportConnectedOnly, Dev->Cfg.NoDisassocIndOnHostDisconnect, Dev->Cfg.BssAgeInfo,
        Dev->Cfg.LinkInfoReal, Dev->Cfg.ReportWmm);
    NwoaDiagSz(Dev, L"NwoaCfgS258m", b);
    NwoaDiagDword(Dev, L"NwoaLinkBwMhz", Dev->LinkBwMhz);
    NwoaDiagDword(Dev, L"NwoaLinkRateKbps", Dev->LinkRateKbps);
    NwoaDiagDword(Dev, L"NwoaFwChanspec", Dev->FwChanspec);    // firmware chanspec at association (channel + width bits)
    NwoaDiagDword(Dev, L"NwoaFwRate500k", Dev->FwRate500k);
    NwoaDiagDword(Dev, L"NwoaWmmNegotiated", Dev->WmmNegotiated);
    NwoaDiagDword(Dev, L"NwoaCntScanSynth", (ULONG)Dev->CntScanSynth);
    NwoaDiagDword(Dev, L"NwoaCntDisassocIndSkipped", (ULONG)Dev->CntDisassocIndSkipped);
}

// Who disconnects: Windows commands (id/connected/M3 status/info@ms, oldest first), a count per
// command id, every NB_C_DISASSOC we sent (source@ms), every firmware link loss event
// (type/reason/connected/ms since our disassoc/rssi@ms) and the commands right before the last one.
void NwoaDiagDisconnect(PNWOA_DEVICE Dev)
{
    KIRQL irql;
    const SIZE_T cap = 2048;
    PCHAR b = (PCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, cap, NWOA_TAG);
    NWOA_CMDREC* rec = (NWOA_CMDREC*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NWOA_CMDREC) * NWOA_CMDLOG, NWOA_TAG);
    if (!b || !rec) {
        if (b) ExFreePoolWithTag(b, NWOA_TAG);
        if (rec) ExFreePoolWithTag(rec, NWOA_TAG);
        return;
    }
    ULONG n, next;
    // command log
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    next = Dev->CmdLogNext;
    n = next < NWOA_CMDLOG ? next : NWOA_CMDLOG;
    for (ULONG i = 0; i < n; i++)
        rec[i] = Dev->CmdLog[(next - n + i) % NWOA_CMDLOG];
    KeReleaseSpinLock(&Dev->StateLock, irql);
    CmdStr(b, cap, rec, n);
    NwoaDiagSz(Dev, L"NwoaCmdLog", b);
    NwoaDiagDword(Dev, L"NwoaCmdLogCount", next);
    // snapshot before the last loss / host disassoc
    ULONG why, ms;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    n = Dev->CmdSnapN;
    why = Dev->CmdSnapWhy;
    ms = Dev->CmdSnapMs;
    for (ULONG i = 0; i < n; i++)
        rec[i] = Dev->CmdSnap[i];
    KeReleaseSpinLock(&Dev->StateLock, irql);
    RtlStringCchPrintfA(b, cap, "why=%lu@%lu: ", why, ms);
    CmdStr(b + strlen(b), cap - strlen(b), rec, n);
    NwoaDiagSz(Dev, L"NwoaCmdBeforeLoss", b);
    // histogram
    b[0] = 0;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    for (int i = 0; i < NWOA_CMDHIST; i++) {
        if (!Dev->CmdHist[i].Count)
            continue;
        CHAR one[24];
        RtlStringCchPrintfA(one, sizeof(one), "%u=%ld ", Dev->CmdHist[i].Id, Dev->CmdHist[i].Count);
        RtlStringCchCatA(b, cap, one);
    }
    KeReleaseSpinLock(&Dev->StateLock, irql);
    NwoaDiagSz(Dev, L"NwoaCmdHist", b);
    // our NB_C_DISASSOC
    b[0] = 0;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    next = Dev->HostDisNext;
    n = next < 8 ? next : 8;
    for (ULONG i = 0; i < n; i++) {
        CHAR one[32];
        RtlStringCchPrintfA(one, sizeof(one), "%lu@%lu ", Dev->HostDis[(next - n + i) % 8].Src, Dev->HostDis[(next - n + i) % 8].Ms);
        RtlStringCchCatA(b, cap, one);
    }
    KeReleaseSpinLock(&Dev->StateLock, irql);
    NwoaDiagSz(Dev, L"NwoaHostDisassocLog", b);
    // firmware link loss events
    b[0] = 0;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    next = Dev->LossNext;
    n = next < 8 ? next : 8;
    for (ULONG i = 0; i < n; i++) {
        const NWOA_LOSSREC* r = &Dev->Loss[(next - n + i) % 8];
        CHAR one[64];
        RtlStringCchPrintfA(one, sizeof(one), "%u/%u/%u/%u/%ld@%lu ", r->Type, r->Reason, r->WasConn, r->SinceHostMs, r->Rssi, r->Ms);
        RtlStringCchCatA(b, cap, one);
    }
    KeReleaseSpinLock(&Dev->StateLock, irql);
    NwoaDiagSz(Dev, L"NwoaLossLog", b);
    NwoaDiagDword(Dev, L"NwoaRssiLast", (ULONG)Dev->RssiLast);
    NwoaDiagDword(Dev, L"NwoaRssiMin", (ULONG)Dev->RssiMin);
    NwoaDiagDword(Dev, L"NwoaRssiMax", (ULONG)Dev->RssiMax);
    NwoaDiagDword(Dev, L"NwoaLqReported", Dev->LqReported);
    NwoaDiagDword(Dev, L"NwoaCntLinkStateInd", (ULONG)Dev->CntLinkStateInd);
    NwoaDiagDword(Dev, L"NwoaCntRoamStay", (ULONG)Dev->CntRoamStay);
    NwoaDiagDword(Dev, L"NwoaCntRoamMove", (ULONG)Dev->CntRoamMove);
    NwoaDiagDword(Dev, L"NwoaCntConnWhileConn", (ULONG)Dev->CntConnWhileConn);
    NwoaDiagDword(Dev, L"NwoaCntD0Entry", (ULONG)Dev->CntD0Entry);
    NwoaDiagDword(Dev, L"NwoaCntD0Exit", (ULONG)Dev->CntD0Exit);
    NwoaDiagDword(Dev, L"NwoaCntStatsOk", (ULONG)Dev->CntStatsOk);
    NwoaDiagDword(Dev, L"NwoaCntStatsFail", (ULONG)Dev->CntStatsFail);
    NwoaDiagDword(Dev, L"NwoaCntStatsSmall", (ULONG)Dev->CntStatsSmall);
    NwoaDiagDword(Dev, L"NwoaStatsLastBytes", Dev->StatsLastBytes);
    NwoaDiagDword(Dev, L"NwoaStatsLastStatus", Dev->StatsLastStatus);
    NwoaDiagDword(Dev, L"NwoaNowMs", (ULONG)(KeQueryInterruptTime() / 10000));
    NwoaDiagS258m(Dev, b, cap);
    NwoaDiagS258n(Dev, b, cap);
    NwoaDiagS258o(Dev, b, cap);
    NwoaDiagS258p(Dev, b, cap);
    NwoaDiagS258q(Dev, b, cap);
    ExFreePoolWithTag(rec, NWOA_TAG);
    ExFreePoolWithTag(b, NWOA_TAG);
}

// "type/status/reason/flags@ms" of the last non-scan firmware events, oldest first (no frame data)
void NwoaDiagEvents(PNWOA_DEVICE Dev)
{
    NWOA_EVREC copy[NWOA_EVLOG];
    ULONG next, n;
    KIRQL irql;
    PCHAR b = (PCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, 2048, NWOA_TAG);
    if (!b)
        return;
    KeAcquireSpinLock(&Dev->RingLock, &irql);
    RtlCopyMemory(copy, Dev->EvLog, sizeof(copy));
    next = Dev->EvLogNext;
    KeReleaseSpinLock(&Dev->RingLock, irql);
    n = next < NWOA_EVLOG ? next : NWOA_EVLOG;
    b[0] = 0;
    for (ULONG i = 0; i < n; i++) {
        NWOA_EVREC* r = &copy[(next - n + i) % NWOA_EVLOG];
        CHAR one[64];
        RtlStringCchPrintfA(one, sizeof(one), "%lu/%lu/%lu/%lx@%lu ", r->Type, r->Status, r->Reason, r->Flags, r->Ms);
        RtlStringCchCatA(b, 2048, one);
    }
    NwoaDiagSz(Dev, L"NwoaEventLog", b);
    NwoaDiagDword(Dev, L"NwoaEventLogCount", next);
    NwoaDiagDword(Dev, L"NwoaCntEapolRx", (ULONG)Dev->CntEapolRx);
    NwoaDiagDword(Dev, L"NwoaCntEapolTx", (ULONG)Dev->CntEapolTx);
    NwoaDiagDword(Dev, L"NwoaCntRxHeld", (ULONG)Dev->CntRxHeld);
    NwoaDiagDword(Dev, L"NwoaLinkUpReported", Dev->LinkUpReported);
    NwoaDiagDword(Dev, L"NwoaCntLinkUp", (ULONG)Dev->CntLinkUp);
    NwoaDiagDword(Dev, L"NwoaCntEapolEv", (ULONG)Dev->CntEapolEv);
    NwoaDiagDword(Dev, L"NwoaCntEapolEvDrop", (ULONG)Dev->CntEapolEvDrop);
    NwoaDiagDword(Dev, L"NwoaEapolEvFmt", Dev->EapolEvFmt);    // 1 Ethernet, 2 bare 802.1X, 0 not EAPOL
    NwoaDiagDword(Dev, L"NwoaEapolEvLen", Dev->EapolEvLen);
    ExFreePoolWithTag(b, NWOA_TAG);
}

void NwoaDiagConsole(PNWOA_DEVICE Dev)
{
    PCHAR b = (PCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, NWOA_CONSOLE_TAIL + 1, NWOA_TAG);
    if (!b)
        return;
    if (NT_SUCCESS(NwoaReadConsoleTail(Dev, b, 1500)))
        NwoaDiagSz(Dev, L"NwoaConsoleTail", b);
    ExFreePoolWithTag(b, NWOA_TAG);
}

// s258n records (numbers only; the console text is masked by nb_mask_text before it is kept)
//  NwoaCfgS258n   the switches in force
//  NwoaPool       tx out/free/max, rx buffers at the firmware / in the OS queue / repost pending, repost
//                 failures and retries, ctrl buffer post failures, TX status non-zero count/last,
//                 GEN_STATUS and RING_STATUS count/last
//  NwoaRingIdx    per ring host pointer / firmware index (H2D: w/fw r, D2H: fw w/r)
//  NwoaStallLog   a<NB_SA action> out<n> old<ms> d<ms since status> ss<ms since connected scan start>/<end>
//                 fl<flow w>/<fw r> pm<firmware PM>@ms
//  NwoaConnScan   s<start> e<end> o<out at start>/<at end> d<statuses during> pm<before>/<after>
//  NwoaMb         mailbox int / D2H word last value and non-zero counts
//  NwoaEvDiag     <event>:<count>@<last ms>
void NwoaDiagS258n(PNWOA_DEVICE Dev, PCHAR b, SIZE_T cap)
{
    KIRQL irql;
    CHAR one[200];
    NWOA_STALLREC st[NWOA_STALL_N];
    NWOA_CSCAN cs[NWOA_CSCAN_N];
    ULONG ns, nc;
    RtlStringCchPrintfA(b, cap, "ConnectedScan=%lu PmKickAfterScan=%lu TxStallMs=%lu TxStallRecover=%lu MbPoll=%lu ConsoleLive=%lu DiagEvents=%lu",
        Dev->Cfg.ConnectedScan, Dev->Cfg.PmKickAfterScan, Dev->Cfg.TxStallMs, Dev->Cfg.TxStallRecover,
        Dev->Cfg.MbPoll, Dev->Cfg.ConsoleLive, Dev->Cfg.DiagEvents);
    NwoaDiagSz(Dev, L"NwoaCfgS258n", b);
    NwoaPoolSnapshot(Dev, b, cap);
    NwoaDiagSz(Dev, L"NwoaPool", b);
    NwoaRingSnapshot(Dev, b, cap, NULL, NULL);
    NwoaDiagSz(Dev, L"NwoaRingIdx", b);

    KeAcquireSpinLock(&Dev->StateLock, &irql);
    RtlCopyMemory(st, Dev->Stall, sizeof(st)); ns = Dev->StallNext;
    RtlCopyMemory(cs, Dev->CScan, sizeof(cs)); nc = Dev->CScanNext;
    KeReleaseSpinLock(&Dev->StateLock, irql);
    b[0] = 0;
    for (ULONG k = ns > NWOA_STALL_N ? ns - NWOA_STALL_N : 0; k < ns; k++) {
        const NWOA_STALLREC* r = &st[k % NWOA_STALL_N];
        RtlStringCchPrintfA(one, sizeof(one), "a%u out%lu old%lu d%lu rx%lu age%lu ss%lu/%lu fl%lu/%lu pm%ld@%lu ",
            r->Action, r->Out, r->OldestMs, r->SinceDoneMs, r->SinceRxMs, r->ConnAgeMs, r->SinceScanStartMs,
            r->SinceScanEndMs, r->FlowW, r->FlowR, r->Pm, r->Ms);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaStallLog", b);
    b[0] = 0;
    for (ULONG k = nc > NWOA_CSCAN_N ? nc - NWOA_CSCAN_N : 0; k < nc; k++) {
        const NWOA_CSCAN* r = &cs[k % NWOA_CSCAN_N];
        RtlStringCchPrintfA(one, sizeof(one), "s%lu e%lu o%lu/%lu d%lu pm%ld/%ld ",
            r->StartMs, r->EndMs, r->OutStart, r->OutEnd, r->DoneDuring, r->PmBefore, r->PmAfter);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaConnScan", b);
    NwoaDiagDword(Dev, L"NwoaCntTxStall", (ULONG)Dev->CntTxStall);
    NwoaDiagDword(Dev, L"NwoaCntStallPmOk", (ULONG)Dev->CntStallPmOk);
    NwoaDiagDword(Dev, L"NwoaCntStallFlowOk", (ULONG)Dev->CntStallFlowOk);
    NwoaDiagDword(Dev, L"NwoaCntStallLinkLoss", (ULONG)Dev->CntStallLinkLoss);
    NwoaDiagDword(Dev, L"NwoaCntStallGiveUp", (ULONG)Dev->CntStallGiveUp);
    NwoaDiagDword(Dev, L"NwoaCntScanSkipped", (ULONG)Dev->CntScanSkipped);
    NwoaDiagDword(Dev, L"NwoaCntPmKick", (ULONG)Dev->CntPmKick);
    NwoaDiagDword(Dev, L"NwoaFwPm", (ULONG)Dev->FwPm);
    RtlStringCchPrintfA(b, cap, "int=%lx intnz=%ld dtoh=%lx dtohnz=%ld", Dev->MbIntLast, Dev->CntMbIntNz,
        Dev->DtohMbLast, Dev->CntDtohMbNz);
    NwoaDiagSz(Dev, L"NwoaMb", b);
    b[0] = 0;
    for (ULONG i = 0; i < NWOA_EVDIAG_N; i++) {
        RtlStringCchPrintfA(one, sizeof(one), "%lu:%ld@%lu ", NwoaDiagEventTypes[i], Dev->EvDiagCnt[i], Dev->EvDiagLastMs[i]);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaEvDiag", b);
    b[0] = 0;
    for (ULONG t = 0; t <= NB_E_LAST; t++) {
        if (!Dev->EvAllCnt[t])
            continue;
        RtlStringCchPrintfA(one, sizeof(one), "%lu:%ld@%lu ", t, Dev->EvAllCnt[t], Dev->EvAllMs[t]);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaEvAll", b);
    PCHAR cb = Dev->Cfg.ConsoleLive && Dev->ConDirty ?
               (PCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, NWOA_CONLIVE + 1, NWOA_TAG) : NULL;
    if (Dev->Cfg.ConsoleLive) {
        if (cb) {
            KeAcquireSpinLock(&Dev->StateLock, &irql);
            Dev->ConDirty = FALSE;
            RtlCopyMemory(cb, Dev->ConBuf, Dev->ConLen);
            cb[Dev->ConLen] = 0;
            KeReleaseSpinLock(&Dev->StateLock, irql);
            NwoaDiagSz(Dev, L"NwoaConsoleLive", cb);
            ExFreePoolWithTag(cb, NWOA_TAG);
        }
        NwoaDiagDword(Dev, L"NwoaConsoleBytes", Dev->ConBytes);
        NwoaDiagDword(Dev, L"NwoaConsoleSkipped", Dev->ConSkipped);
    }
}

// s258o records (numbers only)
//  NwoaCfgS258o    switches in force
//  NwoaScanFlow    verdict counts after firmware scans made while connected (see NWOA_SF_* in nwoa.h),
//                  scans started while connected / by the test timer, Windows scans that waited, frames held
//  NwoaScanFlowLog v<verdict>[t=test scan][d=found by the 2 s tick] s<scan start> e<scan end>
//                  w<host w at end>/<at verdict> r<fw r at end>/<at verdict> adv<ms to first take>
//                  db<flowring doorbells meanwhile> fw<TCM w>/<TCM r of the flowring> cs<TCM w>/<host w of
//                  the control submit ring> fix<re-create NTSTATUS>@<verdict ms>
//  NwoaFwIdxNow    the same TCM / host index pair now (every 2 s): does the TCM copy follow the host?
//  NwoaTcmIdxPtr   ring_info index array pointers h2d w / h2d r / d2h w / d2h r (chip TCM offsets)
void NwoaDiagS258o(PNWOA_DEVICE Dev, PCHAR b, SIZE_T cap)
{
    KIRQL irql;
    CHAR one[200];
    NWOA_SFREC sf[NWOA_SF_N];
    ULONG n;
    RtlStringCchPrintfA(b, cap, "ConnectedScan=%lu ScanFlowCheck=%lu ScanFlowProbeMs=%lu ScanTxHold=%lu TestConnScanSec=%lu",
        Dev->Cfg.ConnectedScan, Dev->Cfg.ScanFlowCheck, Dev->Cfg.ScanFlowProbeMs, Dev->Cfg.ScanTxHold,
        Dev->Cfg.TestConnScanSec);
    NwoaDiagSz(Dev, L"NwoaCfgS258o", b);
    RtlStringCchPrintfA(b, cap, "conn=%ld test=%ld alive=%ld stuck=%ld dbfix=%ld refresh=%ld refreshfail=%ld idle=%ld forced=%ld "
        "deferred=%ld held=%ld armed=%u",
        Dev->CntConnScan, Dev->CntSelfScan, Dev->CntSfAlive, Dev->CntSfStuck, Dev->CntSfDbFix, Dev->CntSfRefresh,
        Dev->CntSfRefreshFail, Dev->CntSfIdle, Dev->CntSfForced, Dev->CntScanDeferred, Dev->CntTxHeld,
        Dev->SfArmed ? 1u : 0u);
    NwoaDiagSz(Dev, L"NwoaScanFlow", b);
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    RtlCopyMemory(sf, Dev->Sf, sizeof(sf)); n = Dev->SfNext;
    KeReleaseSpinLock(&Dev->StateLock, irql);
    b[0] = 0;
    for (ULONG k = n > NWOA_SF_N ? n - NWOA_SF_N : 0; k < n; k++) {
        const NWOA_SFREC* r = &sf[k % NWOA_SF_N];
        // s258p: k<candidate> (255 = no CandidateCycle) b<NB_CAND_* bits> n<chanspecs in the escan> first
        // s258q: q<shape> (255 = Windows scan) c<escans in the sequence> x<events 124>/<events 120>
        RtlStringCchPrintfA(one, sizeof(one), "v%u%s%s k%u b%x n%u q%u c%u x%u/%u s%lu e%lu w%lu/%lu r%lu/%lu adv%lu db%lu fw%lx/%lx cs%lx/%lx fix%lx@%lu ",
            r->Verdict, r->Self ? "t" : "", r->Deferred ? "d" : "", r->Cand, r->Bits, r->NCh, r->Shape, r->Chunks,
            r->Ev124, r->Ev120, r->ScanStartMs,
            r->ScanEndMs, r->W0, r->W1, r->R0, r->R1, r->AdvMs, r->Db, r->FwW, r->FwR, r->FwW0, r->HostW0,
            (ULONG)r->Fix, r->Ms);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaScanFlowLog", b);
    {
        ULONG fw, fr, cw, cr, w, r;
        NwoaFwIdxView(Dev, 0, &cw, &cr);
        NwoaFwIdxView(Dev, Dev->Flow.D.idx, &fw, &fr);
        if (!NwoaFlowIdx(Dev, &w, &r))
            w = r = 0xFFFFFFFFu;
        RtlStringCchPrintfA(b, cap, "cs host%lx tcm%lx/%lx fl host%lx/%lx tcm%lx/%lx",
            NwoaHostWIdx(Dev, 0), cw, cr, w, r, fw, fr);
        NwoaDiagSz(Dev, L"NwoaFwIdxNow", b);
        RtlStringCchPrintfA(b, cap, "hw%lx hr%lx dw%lx dr%lx", Dev->Sh.tcm_idx[0], Dev->Sh.tcm_idx[1],
            Dev->Sh.tcm_idx[2], Dev->Sh.tcm_idx[3]);
        NwoaDiagSz(Dev, L"NwoaTcmIdxPtr", b);
    }
    NwoaDiagDword(Dev, L"NwoaCntDbell", (ULONG)Dev->CntDbell);
    NwoaDiagDword(Dev, L"NwoaCntFlowDbell", (ULONG)Dev->CntFlowDbell);
}

// s258p records (numbers only)
//  NwoaCfgS258p    switches in force (after CandidateCycle forced ScanFlowCheck / TestConnScanSec)
//  NwoaCand        cur=<active candidate> name adv=<rotations> on=<bits applied now> pm=<PM2 apply status>
//                  iov old/set/back=<bit per iovar: read old / set / put back> vch=<valid channels>/<status>
//                  rsm sent/cmplt/fix last=<status>@<ring> initiov=<InitIovarSet bits that were set>
//  NwoaCandStats   per candidate: k<i> <name> n<connected scans judged> a<alive: taken by itself>
//                  s<stuck: not taken> i<idle> g<gone> db<freed by doorbell> rs<freed by resume>
//                  e<escan refused> rf<freed by re-create>, separated by " | "
void NwoaDiagS258p(PNWOA_DEVICE Dev, PCHAR b, SIZE_T cap)
{
    CHAR one[96];
    RtlStringCchPrintfA(b, cap, "ScanPm=%lu ScanChanList=%lu FlowResume=%lu InitIovarSet=%lu CandidateCycle=%lu "
        "ScanFlowCheck=%lu TestConnScanSec=%lu PowerSave=%lu Mpc=%lu",
        Dev->Cfg.ScanPm, Dev->Cfg.ScanChanList, Dev->Cfg.FlowResume, Dev->Cfg.InitIovarSet, Dev->Cfg.CandidateCycle,
        Dev->Cfg.ScanFlowCheck, Dev->Cfg.TestConnScanSec, Dev->Cfg.PowerSave, Dev->Cfg.Mpc);
    NwoaDiagSz(Dev, L"NwoaCfgS258p", b);
    RtlStringCchPrintfA(b, cap, "cur=%lu %s adv=%ld on=%lx pm=%lx iov old%lx set%lx back%lx vch=%lu/%ld "
        "rsm sent%ld cmplt%ld fix%ld last%ld@%lu initiov=%lx",
        Dev->CandCur, nb_cand_name(Dev->CandCur), Dev->CntCandAdv, Dev->CandOn, Dev->CandPmSt,
        Dev->IovOldOk, Dev->IovSetOk, Dev->IovBackOk, Dev->NValidCh, Dev->ValidChSt,
        Dev->CntRsmSent, Dev->CntRsmCmplt, Dev->CntSfRsmFix, Dev->RsmLastStatus, Dev->RsmLastRing, Dev->InitIovSt);
    NwoaDiagSz(Dev, L"NwoaCand", b);
    b[0] = 0;
    for (ULONG k = 0; k < NB_CAND_N; k++) {
        RtlStringCchPrintfA(one, sizeof(one), "%sk%lu %s n%ld a%ld s%ld i%ld g%ld e%ld db%ld rs%ld rf%ld",
            k ? " | " : "", k, nb_cand_name(k), Dev->CandN[k], Dev->CandAlive[k], Dev->CandStuck[k],
            Dev->CandIdle[k], Dev->CandGone[k], Dev->CandErr[k], Dev->CandDb[k], Dev->CandRsm[k], Dev->CandRef[k]);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaCandStats", b);
}

// s258q records (numbers only)
//  NwoaCfgS258q    switches in force (after ShapeCycle forced ScanFlowCheck / CandidateCycle / TestConnScanSec)
//  NwoaShape       cur=<active shape> name adv=<rotations> fch=<foreign channel of the last shape 1>
//                  seq act/idx/total/nch stuckat=<gap index, 255 none> vch=<valid channels>/<status>
//  NwoaShapeStats  per shape (q6 win = Windows scans made while connected): n<judged> a<alive> s<stuck>
//                  i<idle> g<gone> e<escan refused> db<freed by doorbell> rf<freed by re-create> ab<sequence
//                  aborted> gs<shape 5: items not taken in a gap> fb<fell back to base> ms<mean>/<max scan ms>
//                  x<mean events 124>/<mean events 120> per judged scan
//  NwoaEv124       events 124 (WLC_E_CCA_CHAN_QUAL): n<all> scan<while a scan ran> bad<too short> id<0/1/2/other>
//                  last st/id/cs/len dur/cong/ts or nf@ms dl<data length>, ch<distinct chanspecs in the last test
//                  scan>:<first ones>, first<data length>@ms=<first 24 data bytes, own MAC / AP BSSID masked xx>
void NwoaDiagS258q(PNWOA_DEVICE Dev, PCHAR b, SIZE_T cap)
{
    CHAR one[160];
    RtlStringCchPrintfA(b, cap, "TestScanShape=%lu ShapeCycle=%lu ShapeChunkN=%lu ShapeChunkGapMs=%lu ShapeLongMs=%lu "
        "ScanFlowCheck=%lu CandidateCycle=%lu TestConnScanSec=%lu DiagEvents=%lu",
        Dev->Cfg.TestScanShape, Dev->Cfg.ShapeCycle, Dev->Cfg.ShapeChunkN, Dev->Cfg.ShapeChunkGapMs, Dev->Cfg.ShapeLongMs,
        Dev->Cfg.ScanFlowCheck, Dev->Cfg.CandidateCycle, Dev->Cfg.TestConnScanSec, Dev->Cfg.DiagEvents);
    NwoaDiagSz(Dev, L"NwoaCfgS258q", b);
    RtlStringCchPrintfA(b, cap, "cur=%lu %s adv=%ld fch=%u seq act%u idx%u/%u nch%lu stuckat=%u vch=%lu/%ld",
        Dev->ShapeCur, nb_shape_name(Dev->ShapeCur), Dev->CntShapeAdv, Dev->ShapeFch, Dev->ChunkActive ? 1u : 0u,
        Dev->ChunkIdx, Dev->ChunkTotal, Dev->ChunkNCh, Dev->ChunkStuckAt, Dev->NValidCh, Dev->ValidChSt);
    NwoaDiagSz(Dev, L"NwoaShape", b);
    b[0] = 0;
    for (ULONG k = 0; k <= NB_SHAPE_N; k++) {
        LONG n = Dev->ShN[k];
        RtlStringCchPrintfA(one, sizeof(one), "%sq%lu %s n%ld a%ld s%ld i%ld g%ld e%ld db%ld rf%ld ab%ld gs%ld fb%ld ms%lu/%lu x%lu/%lu",
            k ? " | " : "", k, nb_shape_name(k), n, Dev->ShAlive[k], Dev->ShStuck[k], Dev->ShIdle[k], Dev->ShGone[k],
            Dev->ShErr[k], Dev->ShDb[k], Dev->ShRef[k], Dev->ShAbort[k], Dev->ShGapStuck[k], Dev->ShFallback[k],
            n > 0 ? Dev->ShMsSum[k] / (ULONG)n : 0ul, Dev->ShMsMax[k],
            n > 0 ? Dev->ShEv124[k] / (ULONG)n : 0ul, n > 0 ? Dev->ShEv120[k] / (ULONG)n : 0ul);
        RtlStringCchCatA(b, cap, one);
    }
    NwoaDiagSz(Dev, L"NwoaShapeStats", b);
    {
        const nb_cca_ev* e = &Dev->Ev124Last;
        UCHAR raw[24];
        ULONG rl = min(Dev->Ev124FirstLen, (ULONG)sizeof(raw));
        BOOLEAN mask[24];
        RtlCopyMemory(raw, Dev->Ev124First, rl);
        RtlZeroMemory(mask, sizeof(mask));
        for (ULONG i = 0; i + 6 <= rl; i++)          // never a MAC address in the record
            if (RtlEqualMemory(raw + i, Dev->Mac, 6) || RtlEqualMemory(raw + i, Dev->CurBssid, 6))
                for (ULONG j = 0; j < 6; j++)
                    mask[i + j] = TRUE;
        RtlStringCchPrintfA(b, cap, "n%ld scan%ld bad%ld id%ld/%ld/%ld/%ld last st%u id%u cs%x len%u ",
            Dev->CntEv124, Dev->CntEv124Scan, Dev->CntEv124Bad, Dev->Ev124Id[0], Dev->Ev124Id[1], Dev->Ev124Id[2],
            Dev->Ev124Id[3], e->status, e->id, e->chanspec, e->len);
        if (e->has_cca)
            RtlStringCchPrintfA(one, sizeof(one), "dur%lu cong%lu ts%lu", (ULONG)e->duration, (ULONG)e->congest,
                (ULONG)e->timestamp);
        else if (e->has_noise)
            RtlStringCchPrintfA(one, sizeof(one), "nf%ld", (LONG)e->noise);
        else
            RtlStringCchPrintfA(one, sizeof(one), "nodata");
        RtlStringCchCatA(b, cap, one);
        RtlStringCchPrintfA(one, sizeof(one), "@%lu dl%lu ch%lu:", Dev->Ev124LastMs, Dev->Ev124LastDl, Dev->Ev124NCs);
        RtlStringCchCatA(b, cap, one);
        for (ULONG i = 0; i < Dev->Ev124NCs && i < 12; i++) {
            RtlStringCchPrintfA(one, sizeof(one), "%s%x", i ? "," : "", Dev->Ev124Cs[i]);
            RtlStringCchCatA(b, cap, one);
        }
        RtlStringCchPrintfA(one, sizeof(one), " first%lu@%lu=", Dev->Ev124FirstDl, Dev->Ev124FirstMs);
        RtlStringCchCatA(b, cap, one);
        for (ULONG i = 0; i < rl; i++) {
            if (mask[i])
                RtlStringCchCopyA(one, sizeof(one), "xx");
            else
                RtlStringCchPrintfA(one, sizeof(one), "%02x", raw[i]);
            RtlStringCchCatA(b, cap, one);
        }
    }
    NwoaDiagSz(Dev, L"NwoaEv124", b);
}
