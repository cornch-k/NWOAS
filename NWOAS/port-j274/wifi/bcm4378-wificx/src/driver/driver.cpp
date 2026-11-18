// driver.cpp - DriverEntry, device creation, PnP/power, bring-up order.
//
// Bring-up (EvtDevicePrepareHardware, PASSIVE; again in D0Entry after a D3):
//   map BAR0/BAR2 -> config access -> chip ID/EROM -> CR4 halt + chip reset + OTP
//   -> firmware/NVRAM/seed into TCM -> CR4 start -> shared RAM -> DMA memory
//   -> rings/indices/host_cap in TCM -> bus master -> host ready -> buffers
//   -> GET_VERSION, cur_etheraddr, CLM/txcap/cal, event mask, UP  (wifi.cpp)
// Every step writes NwoaStage under Device Parameters.
#include "precomp.h"

extern "C" DRIVER_INITIALIZE DriverEntry;

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG cfg;
    WDF_DRIVER_CONFIG_INIT(&cfg, NwoaEvtDeviceAdd);
    cfg.DriverPoolTag = NWOA_TAG;
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &cfg, WDF_NO_HANDLE);
}

void NwoaSetStage(PNWOA_DEVICE Dev, PCSTR Stage, NTSTATUS St)
{
    RtlStringCchCopyA(Dev->Stage, sizeof(Dev->Stage), Stage);
    Dev->LastStatus = St;
    NWOA_LOG("stage %s status %08x", Stage, St);
    NwoaDiagSz(Dev, L"NwoaStage", Stage);
    NwoaDiagDword(Dev, L"NwoaStatus", (ULONG)St);
    NwoaDiagDword(Dev, L"NwoaBpFault", (ULONG)Dev->BpFault);
    NwoaDiagDword(Dev, L"NwoaBpBadAddr", Dev->BpBadAddr);    // first refused backplane address (bit 0 = window)
    NwoaDiagDword(Dev, L"NwoaCfgErrors", Dev->CfgErrors);
}

NTSTATUS NwoaEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);
    NTSTATUS st;
    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_OBJECT_ATTRIBUTES attr;
    WDFDEVICE device;
    PNWOA_DEVICE Dev;

    st = NetDeviceInitConfig(DeviceInit);
    if (!NT_SUCCESS(st))
        return st;
    st = WifiDeviceInitConfig(DeviceInit);
    if (!NT_SUCCESS(st))
        return st;

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware = NwoaEvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware = NwoaEvtReleaseHardware;
    pnp.EvtDeviceD0Entry = NwoaEvtD0Entry;
    pnp.EvtDeviceD0Exit = NwoaEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, NWOA_DEVICE);
    st = WdfDeviceCreate(&DeviceInit, &attr, &device);
    if (!NT_SUCCESS(st))
        return st;
    Dev = NwoaGetDevice(device);
    RtlZeroMemory(Dev, sizeof(*Dev));
    Dev->Device = device;
    KeInitializeSpinLock(&Dev->RingLock);
    KeInitializeSpinLock(&Dev->TxLock);
    KeInitializeSpinLock(&Dev->StateLock);
    KeInitializeSpinLock(&Dev->WinLock);
    ExInitializeFastMutex(&Dev->IoctlMutex);
    KeInitializeEvent(&Dev->IoctlEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Dev->FlowEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Dev->ConnEvent, NotificationEvent, FALSE);
    Dev->IoType = NB_IO_TYPE_D11AC;
    Dev->RadioOn = TRUE;

    WIFI_DEVICE_CONFIG wcfg;
    WIFI_DEVICE_CONFIG_INIT(&wcfg, WDI_VERSION_LATEST, NwoaEvtWifiSendCommand, NwoaEvtWifiCreateAdapter,
                            NwoaEvtWifiCreateWifiDirect);
    st = WifiDeviceInitialize(device, &wcfg);
    if (!NT_SUCCESS(st))
        return st;
    Dev->Tlv.AllocationContext = 0;
    Dev->Tlv.PeerVersion = WifiDeviceGetOsWdiVersion(device);

    {
        WDF_WORKITEM_CONFIG wc;
        WDF_OBJECT_ATTRIBUTES wa;
        WDF_WORKITEM_CONFIG_INIT(&wc, NwoaEvtWork);
        wc.AutomaticSerialization = FALSE;
        WDF_OBJECT_ATTRIBUTES_INIT(&wa);
        wa.ParentObject = device;
        st = WdfWorkItemCreate(&wc, &wa, &Dev->Work);
        if (!NT_SUCCESS(st))
            return st;
    }
    {
        WDF_TIMER_CONFIG tc;
        WDF_OBJECT_ATTRIBUTES ta;
        NwoaReadConfig(Dev);
        WDF_TIMER_CONFIG_INIT(&tc, NwoaEvtPollTimer);         // one-shot, re-armed with an adaptive interval
        tc.UseHighResolutionTimer = WdfTrue;
        tc.AutomaticSerialization = FALSE;
        WDF_OBJECT_ATTRIBUTES_INIT(&ta);
        ta.ParentObject = device;
        st = WdfTimerCreate(&tc, &ta, &Dev->PollTimer);
        if (!NT_SUCCESS(st))
            return st;
    }
    return STATUS_SUCCESS;
}

static void StartPoll(PNWOA_DEVICE Dev)
{
    Dev->LastActivity = (LONG64)KeQueryInterruptTime();
    Dev->RingsUpTime = KeQueryInterruptTime();
    InterlockedExchange(&Dev->PollIdle, 0);
    InterlockedExchange(&Dev->PollRunning, 1);
    WdfTimerStart(Dev->PollTimer, WDF_REL_TIMEOUT_IN_MS(Dev->Cfg.PollIntervalMs));
}

NTSTATUS NwoaBringUp(PNWOA_DEVICE Dev)
{
    NTSTATUS st;
    ULONG stop = Dev->Cfg.StopAfter;
    InterlockedExchange(&Dev->Dead, 0);
    NwoaDiagDword(Dev, L"NwoaStopAfterUsed", stop);
    if (Dev->Cfg.SettleMs) {
        LARGE_INTEGER t;
        t.QuadPart = -(LONGLONG)Dev->Cfg.SettleMs * 10000;
        KeDelayExecutionThread(KernelMode, FALSE, &t);
    }
    NwoaSetStage(Dev, "chip_attach", STATUS_PENDING);
    st = NwoaChipAttach(Dev);                                // chip id + EROM only (S257 order)
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_chip_attach", st); return st; }
    if (stop == 1) { NwoaSetStage(Dev, "stopped_after_1_chipid_erom", STATUS_SUCCESS); return STATUS_DEVICE_NOT_READY; }
    NwoaSetStage(Dev, "chip_probe", STATUS_PENDING);
    st = NwoaChipProbeLikeS257(Dev);                         // every wrapper read + config mirror
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_chip_probe", st); return st; }
    if (stop == 2) { NwoaSetStage(Dev, "stopped_after_2_core_probe", STATUS_SUCCESS); return STATUS_DEVICE_NOT_READY; }
    NwoaSetStage(Dev, "chip_halt", STATUS_PENDING);
    st = NwoaChipHalt(Dev);                                  // CR4 halt, 802.11 cores disabled
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_chip_halt", st); return st; }
    if (stop == 3) { NwoaSetStage(Dev, "stopped_after_3_cr4_halt", STATUS_SUCCESS); return STATUS_DEVICE_NOT_READY; }
    NwoaSetStage(Dev, "chip_reset_otp", STATUS_PENDING);
    st = NwoaChipResetAndOtp(Dev);
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_chip_reset_otp", st); return st; }
    if (stop == 4) { NwoaSetStage(Dev, "stopped_after_4_reset_otp", STATUS_SUCCESS); return STATUS_DEVICE_NOT_READY; }
    NwoaSetStage(Dev, "fw_load", STATUS_PENDING);
    st = NwoaFirmwareBoot(Dev);
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_fw_boot", st); NwoaDiagConsole(Dev); return st; }
    NwoaSetStage(Dev, "fw_up", STATUS_SUCCESS);
    NwoaDiagConsole(Dev);
    if (stop == 5) { NwoaSetStage(Dev, "stopped_after_5_fw_up", STATUS_SUCCESS); return STATUS_DEVICE_NOT_READY; }
    st = NwoaDmaInit(Dev);
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_dma", st); return st; }
    st = NwoaRingsInit(Dev);
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_rings", st); return st; }
    NwoaSetStage(Dev, "rings_up", STATUS_SUCCESS);
    StartPoll(Dev);
    InterlockedExchange(&Dev->IrqMode, Dev->Interrupt ? 1 : 0);
    Dev->IsrUnclaimedRow = 0;
    NwoaIntrEnable(Dev, TRUE);
    st = NwoaFirmwareConfigure(Dev);
    NwoaDiagCounters(Dev);
    NwoaDiagConsole(Dev);
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, Dev->Stage[0] == 'f' ? Dev->Stage : "fail_fw_config", st); return st; }
    NwoaSetStage(Dev, "ready", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

void NwoaShutdown(PNWOA_DEVICE Dev)
{
    InterlockedExchange(&Dev->PollRunning, 0);
    if (Dev->RingsUp && !Dev->Dead) {
        Dev->Connected = FALSE;
        NwoaHostDisassoc(Dev, DIS_SHUTDOWN);
        NwoaFlowringDelete(Dev);
        (void)NwoaSetInt(Dev, NB_C_DOWN, 1);
    }
    if (Dev->PollTimer) {
        WdfTimerStop(Dev->PollTimer, TRUE);
        WdfTimerStop(Dev->PollTimer, TRUE);   // a callback running during the first stop may have re-armed it
    }
    NwoaRingsStop(Dev);                       // mailbox mask off, bus master off (before freeing DMA)
    if (Dev->Work)
        WdfWorkItemFlush(Dev->Work);          // jobs now fail fast (RingsUp FALSE); none may touch DMA after this
    NwoaWifiPowerDown(Dev);
    // No CR4 halt here: wifi_up.py never touched the cores while the firmware ran (it powered
    // the port off), and S259 _PS3 cuts the power.  Bus master off (above) already stops DMA.
    NwoaDmaFree(Dev);
    Dev->FwUp = FALSE;                        // files stay cached (freed in ReleaseHardware)
}

NTSTATUS NwoaEvtPrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw, WDFCMRESLIST Trans)
{
    PNWOA_DEVICE Dev = NwoaGetDevice(Device);
    NTSTATUS st;
    NwoaReadConfig(Dev);
    NwoaSetStage(Dev, "prepare", STATUS_PENDING);
    NwoaDiagSz(Dev, L"NwoaDriverBuild", "nwoa4378 1.0.1.2 (wifi-s258k: disconnect evidence log, roam task, link quality updates, fw roam_off)");
    st = NwoaMapResources(Dev, Raw, Trans);
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_resources", st); return st; }
    st = NwoaConfigInit(Dev);
    if (!NT_SUCCESS(st)) { NwoaSetStage(Dev, "fail_config_space", st); return st; }
    NwoaInterruptCreate(Dev);
    st = NwoaBringUp(Dev);
    if (!NT_SUCCESS(st)) {
        NwoaShutdown(Dev);
        return st;
    }
    st = NwoaWifiSetCapabilities(Dev);
    if (!NT_SUCCESS(st))
        NwoaSetStage(Dev, "fail_wifi_caps", st);
    return st;
}

NTSTATUS NwoaEvtReleaseHardware(WDFDEVICE Device, WDFCMRESLIST Trans)
{
    UNREFERENCED_PARAMETER(Trans);
    PNWOA_DEVICE Dev = NwoaGetDevice(Device);
    NwoaShutdown(Dev);
    NwoaFreeFiles(Dev);
    NwoaUnmapResources(Dev);
    Dev->Interrupt = NULL;                    // created in PrepareHardware, deleted by the framework
    Dev->HaveIntRes = FALSE;
    NwoaSetStage(Dev, "released", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

NTSTATUS NwoaEvtD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Previous)
{
    PNWOA_DEVICE Dev = NwoaGetDevice(Device);
    UNREFERENCED_PARAMETER(Previous);
    InterlockedIncrement(&Dev->CntD0Entry);
    if (!Dev->FirstD0Done) {
        Dev->FirstD0Done = TRUE;              // PrepareHardware already brought the chip up
        return STATUS_SUCCESS;
    }
    if (Dev->FwUp)
        return STATUS_SUCCESS;
    // back from D3: full bring-up again (hypothesis: the chip lost its firmware)
    NTSTATUS st = NwoaBringUp(Dev);
    if (!NT_SUCCESS(st))
        NwoaShutdown(Dev);
    return st;
}

NTSTATUS NwoaEvtD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Target)
{
    PNWOA_DEVICE Dev = NwoaGetDevice(Device);
    InterlockedIncrement(&Dev->CntD0Exit);
    NwoaDiagCounters(Dev);
    if (Target != WdfPowerDeviceD3Final)
        NwoaShutdown(Dev);
    NwoaSetStage(Dev, "d0_exit", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}
