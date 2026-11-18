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
    NwoaDiagDword(Dev, L"NwoaNowMs", (ULONG)(KeQueryInterruptTime() / 10000));
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
