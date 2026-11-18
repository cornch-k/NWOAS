/* diag.c - settings and status values in the device hardware key
 *   HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\NWOA5F69\0\Device Parameters\Nwbt*
 * read by the status job (nwoas_scripts/bt-s261/jobs/B2-status.cmd) with "reg query".
 * PASSIVE_LEVEL only.  The BD address is never written here.
 * SPDX-License-Identifier: MIT */
#include "nwbt.h"

#ifndef PLUGPLAY_REGISTRY_KEY_DEVICE
#define PLUGPLAY_REGISTRY_KEY_DEVICE 1
#endif

static NTSTATUS OpenHwKey(PNWBT_DEVICE Dev, ACCESS_MASK Access, WDFKEY* Key)
{
    return WdfDeviceOpenRegistryKey(Dev->Device, PLUGPLAY_REGISTRY_KEY_DEVICE, Access, WDF_NO_OBJECT_ATTRIBUTES, Key);
}

static ULONG QueryUlong(WDFKEY Key, PCWSTR Name, ULONG Default, ULONG Max)
{
    UNICODE_STRING name;
    ULONG v = Default;
    RtlInitUnicodeString(&name, Name);
    if (!Key || !NT_SUCCESS(WdfRegistryQueryULong(Key, &name, &v)) || v > Max)
        v = Default;
    return v;
}

void NwbtReadConfig(PNWBT_DEVICE Dev)
{
    WDFKEY key = NULL;
    NWBT_CONFIG* c = &Dev->Cfg;
    UNICODE_STRING name, val;
    if (!NT_SUCCESS(OpenHwKey(Dev, KEY_READ, &key)))
        key = NULL;
    c->StopAfter      = QueryUlong(key, L"StopAfter", 0, 8);
    c->DoFlr          = QueryUlong(key, L"DoFlr", 1, 1);
    c->PollIntervalMs = QueryUlong(key, L"PollIntervalMs", 2, 100);
    if (!c->PollIntervalMs)
        c->PollIntervalMs = 2;
    c->FixQuirks      = QueryUlong(key, L"FixQuirks", NBT_FIX_MWS | NBT_FIX_LE_CODED | NBT_FIX_EXTADV_PHY, 7);
    c->SetBdaddr      = QueryUlong(key, L"SetBdaddr", 1, 1);
    c->SendCal        = QueryUlong(key, L"SendCal", 1, 1);
    c->SendPtb        = QueryUlong(key, L"SendPtb", 1, 1);
    c->PokeAwake      = QueryUlong(key, L"PokeAwake", 1, 1);
    c->MsiToRam       = QueryUlong(key, L"MsiToRam", 0, 1);
    c->BootTimeoutMs  = QueryUlong(key, L"BootTimeoutMs", 5000, 60000);
    c->StepTimeoutMs  = QueryUlong(key, L"StepTimeoutMs", 1000, 60000);
    c->HciTimeoutMs   = QueryUlong(key, L"HciTimeoutMs", 2000, 60000);
    c->CreatePdo      = QueryUlong(key, L"CreatePdo", 1, 1);
    c->AutoRestart    = QueryUlong(key, L"AutoRestart", 1, 1);
    c->ScoSupport     = QueryUlong(key, L"ScoSupport", ScoSupportHCIBypass, 2);
    c->MaxScoChannels = QueryUlong(key, L"MaxScoChannels", 1, 8);
    c->FirmwareDir[0] = 0;
    if (key) {
        RtlInitUnicodeString(&name, L"FirmwareDir");
        val.Buffer = c->FirmwareDir;
        val.Length = 0;
        val.MaximumLength = (USHORT)((ARRAYSIZE(c->FirmwareDir) - 1) * sizeof(WCHAR));
        if (NT_SUCCESS(WdfRegistryQueryUnicodeString(key, &name, NULL, &val)))
            c->FirmwareDir[val.Length / sizeof(WCHAR)] = 0;
        else
            c->FirmwareDir[0] = 0;
        WdfRegistryClose(key);
    }
}

static void Assign(PNWBT_DEVICE Dev, PCWSTR Name, ULONG Type, ULONG Len, PVOID Data)
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

void NwbtDiagSz(PNWBT_DEVICE Dev, PCWSTR Name, PCSTR Value)
{
    WCHAR w[96];
    ULONG i = 0;
    for (; Value && Value[i] && i < ARRAYSIZE(w) - 1; i++)
        w[i] = (WCHAR)(UCHAR)Value[i];
    w[i] = 0;
    Assign(Dev, Name, REG_SZ, (i + 1) * sizeof(WCHAR), w);
}

void NwbtDiagDword(PNWBT_DEVICE Dev, PCWSTR Name, ULONG Value)
{
    Assign(Dev, Name, REG_DWORD, sizeof(ULONG), &Value);
}

void NwbtDiagCounters(PNWBT_DEVICE Dev)
{
    NwbtDiagDword(Dev, L"NwbtCntPoll", (ULONG)Dev->CntPoll);
    NwbtDiagDword(Dev, L"NwbtCntEvt", (ULONG)Dev->CntEvt);
    NwbtDiagDword(Dev, L"NwbtCntAclRx", (ULONG)Dev->CntAclRx);
    NwbtDiagDword(Dev, L"NwbtCntCmdTx", (ULONG)Dev->CntCmdTx);
    NwbtDiagDword(Dev, L"NwbtCntAclTx", (ULONG)Dev->CntAclTx);
    NwbtDiagDword(Dev, L"NwbtCntAck", (ULONG)Dev->CntAck);
    NwbtDiagDword(Dev, L"NwbtCntBadEntry", (ULONG)Dev->CntBadEntry);
    NwbtDiagDword(Dev, L"NwbtCntFix", (ULONG)Dev->CntFix);
    NwbtDiagDword(Dev, L"NwbtCntDropNoHost", (ULONG)Dev->CntDropNoHost);
    NwbtDiagDword(Dev, L"NwbtCntDropFifo", (ULONG)Dev->CntDropFifo);
    NwbtDiagDword(Dev, L"NwbtCntWriteHeld", (ULONG)Dev->CntWriteHeld);
    NwbtDiagDword(Dev, L"NwbtCntReadDone", (ULONG)Dev->CntReadDone);
    NwbtDiagDword(Dev, L"NwbtCntInitEvtDropped", (ULONG)Dev->CntInitEvtDropped);
    NwbtDiagDword(Dev, L"NwbtCntMsi", (ULONG)Dev->CntMsi);
    NwbtDiagDword(Dev, L"NwbtDead", Dev->Dead);
    NwbtDiagDword(Dev, L"NwbtDeadReason", Dev->DeadReason);
    NwbtDiagDword(Dev, L"NwbtDeadGen", Dev->DeadGen);
    NwbtDiagDword(Dev, L"NwbtLastRti", Dev->LastRti);
    NwbtDiagDword(Dev, L"NwbtHciReady", Dev->HciReady);
    NwbtDiagDword(Dev, L"NwbtBthxOpen", Dev->BthxOpen);
    NwbtDiagDword(Dev, L"NwbtEvtFifoDrops", Dev->EvtFifo.drops);
    NwbtDiagDword(Dev, L"NwbtAclFifoDrops", Dev->AclFifo.drops);
    if (Dev->Ctl)
        NwbtDiagDword(Dev, L"NwbtCtlStateNow", NwbtCtlRead(Dev, BTC_STATE));
    NwbtDiagDword(Dev, L"NwbtIoGetVersion", (ULONG)Dev->CntIoctl[0]);
    NwbtDiagDword(Dev, L"NwbtIoSetVersion", (ULONG)Dev->CntIoctl[1]);
    NwbtDiagDword(Dev, L"NwbtIoCaps", (ULONG)Dev->CntIoctl[2]);
    NwbtDiagDword(Dev, L"NwbtIoWrite", (ULONG)Dev->CntIoctl[3]);
    NwbtDiagDword(Dev, L"NwbtIoRead", (ULONG)Dev->CntIoctl[4]);
    NwbtDiagDword(Dev, L"NwbtIoOther", (ULONG)Dev->CntIoctl[5]);
    NwbtDiagDword(Dev, L"NwbtIoPdoOther", (ULONG)Dev->CntIoctl[6]);
    NwbtDiagDword(Dev, L"NwbtIoFwdFail", (ULONG)Dev->CntIoctl[7]);
    NwbtDiagIoLog(Dev);
}

/* one record: <ms>:<kind><func> i<in> o<out> s<status> t<type> <op>/<op2>;  (no packet payload, no address) */
static void IoLogValue(PNWBT_DEVICE Dev, PCWSTR Name, const NWBT_IOREC* R, ULONG N)
{
    const ULONG cch = 64 * 64;
    PWCHAR w = (PWCHAR)ExAllocatePool2(POOL_FLAG_PAGED, cch * sizeof(WCHAR), NWBT_TAG);
    ULONG i;
    if (!w)
        return;
    w[0] = 0;
    for (i = 0; i < N; i++) {
        WCHAR one[64];
        RtlStringCchPrintfW(one, ARRAYSIZE(one), L"%lu:%c%03x i%u o%u s%lx t%u %x/%x; ", R[i].Ms, (WCHAR)R[i].Kind,
                            R[i].Func, R[i].In, R[i].Out, R[i].St, R[i].Type, R[i].Op, R[i].Op2);
        RtlStringCchCatW(w, cch, one);
    }
    Assign(Dev, Name, REG_SZ, (ULONG)((wcslen(w) + 1) * sizeof(WCHAR)), w);
    ExFreePoolWithTag(w, NWBT_TAG);
}

void NwbtDiagIoLog(PNWBT_DEVICE Dev)
{
    NWBT_IOREC first[NWBT_IOLOG_FIRST], last[NWBT_IOLOG_LAST];
    ULONG nf, nl, i;
    KIRQL irql;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    KeAcquireSpinLock(&Dev->Lock, &irql);
    nf = Dev->IoFirstN;
    RtlCopyMemory(first, Dev->IoFirst, sizeof(first));
    nl = Dev->IoLastN;
    for (i = 0; i < NWBT_IOLOG_LAST && i < nl; i++)              /* oldest first */
        last[i] = Dev->IoLast[(nl > NWBT_IOLOG_LAST ? nl - NWBT_IOLOG_LAST + i : i) % NWBT_IOLOG_LAST];
    KeReleaseSpinLock(&Dev->Lock, irql);
    IoLogValue(Dev, L"NwbtIoLog", first, nf);
    IoLogValue(Dev, L"NwbtIoLogLast", last, nl < NWBT_IOLOG_LAST ? nl : NWBT_IOLOG_LAST);
    NwbtDiagDword(Dev, L"NwbtIoLogTotal", nf + nl);
}

void NwbtEvtDiagWork(WDFWORKITEM Item)
{
    PNWBT_DEVICE Dev = NwbtGetDevice(WdfWorkItemGetParentObject(Item));
    NwbtDiagCounters(Dev);
    InterlockedExchange(&Dev->DiagQueued, 0);
}

/* The firmware stopped answering (RTI status left 2, or a ring index went out of range): ask the
 * framework to restart the whole device (ReleaseHardware -> PrepareHardware -> D0Entry: FLR + boot).
 * At most 3 times per Windows boot: NwbtAutoRestarts + NwbtAutoRestartAt (seconds since boot). */
void NwbtEvtRecoverWork(WDFWORKITEM Item)
{
    PNWBT_DEVICE Dev = NwbtGetDevice(WdfWorkItemGetParentObject(Item));
    WDFKEY key = NULL;
    ULONG n = 0, at = 0, now = (ULONG)(KeQueryInterruptTime() / 10000000ull);
    NwbtDiagCounters(Dev);
    if (NT_SUCCESS(OpenHwKey(Dev, KEY_READ, &key))) {
        n = QueryUlong(key, L"NwbtAutoRestarts", 0, 0xFFFF);
        at = QueryUlong(key, L"NwbtAutoRestartAt", 0, 0xFFFFFFFF);
        WdfRegistryClose(key);
    }
    if (at > now)
        n = 0;                                           /* a new Windows boot */
    NwbtDiagDword(Dev, L"NwbtAutoRestarts", n + 1);
    NwbtDiagDword(Dev, L"NwbtAutoRestartAt", now);
    if (n >= 3) {
        NwbtSetStage(Dev, "dead_no_more_restarts", STATUS_DEVICE_HARDWARE_ERROR);
        return;                                          /* RecoverQueued stays 1: no further tries */
    }
    NwbtSetStage(Dev, "dead_restarting", STATUS_DEVICE_HARDWARE_ERROR);
    WdfDeviceSetFailed(Dev->Device, WdfDeviceFailedAttemptRestart);
}
