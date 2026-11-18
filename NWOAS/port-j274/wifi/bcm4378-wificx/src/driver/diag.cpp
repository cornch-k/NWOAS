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
    c->PollIntervalMs = QueryUlong(key, L"PollIntervalMs", 10);
    if (c->PollIntervalMs < 2 || c->PollIntervalMs > 1000)
        c->PollIntervalMs = 10;
    c->ScanPassive   = QueryUlong(key, L"ScanPassive", 1);   // S257 PASS_SCAN used passive escan v1
    c->Mpc           = QueryUlong(key, L"Mpc", 0);
    c->PowerSave     = QueryUlong(key, L"PowerSave", 0);
    c->RxFrameFormat = QueryUlong(key, L"RxFrameFormat", 0);
    c->UseInterrupt  = QueryUlong(key, L"UseInterrupt", 0);   // S259: poll first, interrupt second
    c->ClearNoSnoop  = QueryUlong(key, L"ClearNoSnoop", 1);
    c->DmaMode       = QueryUlong(key, L"DmaMode", 0);
    c->VerifyFirmware = QueryUlong(key, L"VerifyFirmware", 1);
    c->LinkUpAtStart = QueryUlong(key, L"LinkUpAtStart", 0);
    c->StopAfter     = QueryUlong(key, L"StopAfter", 0);
    c->SettleMs      = QueryUlong(key, L"SettleMs", 0);
    c->AccessDelayUs = QueryUlong(key, L"AccessDelayUs", 0);
    if (c->AccessDelayUs > 5000)
        c->AccessDelayUs = 5000;
    c->PostWriteRead = QueryUlong(key, L"PostWriteRead", 0);
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
    NwoaDiagDword(Dev, L"NwoaCntTx80211", (ULONG)Dev->CntTx80211);
    NwoaDiagDword(Dev, L"NwoaCntIoctl", (ULONG)Dev->CntIoctl);
    NwoaDiagDword(Dev, L"NwoaCntIoctlTimeout", (ULONG)Dev->CntIoctlTimeout);
    NwoaDiagDword(Dev, L"NwoaCntUnknownMsg", (ULONG)Dev->CntUnknown);
    NwoaDiagDword(Dev, L"NwoaCntMbData", (ULONG)Dev->CntMbData);
    NwoaDiagDword(Dev, L"NwoaCntScans", (ULONG)Dev->CntScans);
    NwoaDiagDword(Dev, L"NwoaDead", (ULONG)Dev->Dead);
    NwoaDiagDword(Dev, L"NwoaConnected", Dev->Connected);
    NwoaDiagDword(Dev, L"NwoaFlowState", (ULONG)Dev->FlowState);
    NwoaDiagEvents(Dev);
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
