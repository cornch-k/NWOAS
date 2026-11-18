/* driver.c - DriverEntry, FDO creation, PnP/power callbacks.
 *
 * Model (Windows Bluetooth extensible transport, the documented way for a non-USB HCI transport and the
 * shape of Microsoft's bluetooth/serialhcibus sample): this driver is the function driver of
 * ACPI\NWOA5F69 and a bus driver for one child PDO "NWBT\BCM4378BTH" with compatible ID
 * MS_BTHX_BTHMINI.  The inbox bth.inf installs BthMini.sys (and BthPort above it) on that PDO; BthMini
 * sends IOCTL_BTHX_GET_VERSION / SET_VERSION / QUERY_CAPABILITIES / WRITE_HCI / READ_HCI, which the PDO
 * forwards to this FDO's queue (io.c).
 *
 * Order: PrepareHardware (map, control page, ADT data) -> D0Entry (NwbtBringUp: FLR, config, OTP,
 * firmware, RTI, rings, HCI setup; StopAfter can stop at any step) -> SelfManagedIoInit (child PDO once
 * HCI is up) ... D0Exit (NwbtTeardown: poll stop, bus master off, DMA memory freed).
 * SPDX-License-Identifier: MIT */
#include "nwbt.h"

DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DEVICE_PREPARE_HARDWARE NwbtEvtPrepareHardware;
static EVT_WDF_DEVICE_RELEASE_HARDWARE NwbtEvtReleaseHardware;
static EVT_WDF_DEVICE_D0_ENTRY NwbtEvtD0Entry;
static EVT_WDF_DEVICE_D0_EXIT NwbtEvtD0Exit;
static EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT NwbtEvtSelfManagedIoInit;

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG cfg;
    WDF_DRIVER_CONFIG_INIT(&cfg, NwbtEvtDeviceAdd);
    cfg.DriverPoolTag = NWBT_TAG;
    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &cfg, WDF_NO_HANDLE);
}

void NwbtSetStage(PNWBT_DEVICE Dev, PCSTR Stage, NTSTATUS St)
{
    RtlStringCchCopyA(Dev->Stage, sizeof(Dev->Stage), Stage);
    Dev->LastStatus = St;
    NWBT_LOG("stage %s status %08x", Stage, St);
    NwbtDiagSz(Dev, L"NwbtStage", Stage);
    NwbtDiagDword(Dev, L"NwbtStatus", (ULONG)St);
}

static NTSTATUS ManualQueue(WDFDEVICE Device, WDFQUEUE* Q)
{
    WDF_IO_QUEUE_CONFIG qc;
    WDF_IO_QUEUE_CONFIG_INIT(&qc, WdfIoQueueDispatchManual);
    qc.PowerManaged = WdfFalse;
    return WdfIoQueueCreate(Device, &qc, WDF_NO_OBJECT_ATTRIBUTES, Q);
}

NTSTATUS NwbtEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS st;
    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_OBJECT_ATTRIBUTES attr;
    WDF_IO_QUEUE_CONFIG qc;
    WDF_WORKITEM_CONFIG wc;
    WDFDEVICE device;
    PNWBT_DEVICE Dev;
    UNREFERENCED_PARAMETER(Driver);

    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware = NwbtEvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware = NwbtEvtReleaseHardware;
    pnp.EvtDeviceD0Entry = NwbtEvtD0Entry;
    pnp.EvtDeviceD0Exit = NwbtEvtD0Exit;
    pnp.EvtDeviceSelfManagedIoInit = NwbtEvtSelfManagedIoInit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnp);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, NWBT_DEVICE);
    st = WdfDeviceCreate(&DeviceInit, &attr, &device);
    if (!NT_SUCCESS(st))
        return st;
    Dev = NwbtGetDevice(device);
    RtlZeroMemory(Dev, sizeof(*Dev));
    Dev->Device = device;
    KeInitializeSpinLock(&Dev->Lock);
    KeInitializeEvent(&Dev->CtlEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Dev->CmdEvent, NotificationEvent, FALSE);

    /* BthX requests (forwarded by the child PDO): not power managed, BthMini asks for the version and
     * capabilities before its device is in D0 (serialhcibus sample note) */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qc, WdfIoQueueDispatchParallel);
    qc.PowerManaged = WdfFalse;
    qc.EvtIoDeviceControl = NwbtEvtIoDeviceControl;
    qc.EvtIoInternalDeviceControl = NwbtEvtIoDeviceControl;
    st = WdfIoQueueCreate(device, &qc, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (NT_SUCCESS(st))
        st = ManualQueue(device, &Dev->EvtQ);
    if (NT_SUCCESS(st))
        st = ManualQueue(device, &Dev->AclQ);
    if (NT_SUCCESS(st))
        st = ManualQueue(device, &Dev->WriteQ);
    if (!NT_SUCCESS(st))
        return st;

    WDF_WORKITEM_CONFIG_INIT(&wc, NwbtEvtDiagWork);
    WDF_OBJECT_ATTRIBUTES_INIT(&attr);
    attr.ParentObject = device;
    st = WdfWorkItemCreate(&wc, &attr, &Dev->DiagWork);
    if (!NT_SUCCESS(st))
        return st;
    WDF_WORKITEM_CONFIG_INIT(&wc, NwbtEvtRecoverWork);
    WDF_OBJECT_ATTRIBUTES_INIT(&attr);
    attr.ParentObject = device;
    st = WdfWorkItemCreate(&wc, &attr, &Dev->RecoverWork);
    if (!NT_SUCCESS(st))
        return st;

    {
        WDF_DEVICE_STATE ds;
        WDF_DEVICE_STATE_INIT(&ds);
        ds.NotDisableable = WdfFalse;
        WdfDeviceSetDeviceState(device, &ds);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS NwbtEvtPrepareHardware(WDFDEVICE Device, WDFCMRESLIST Raw, WDFCMRESLIST Trans)
{
    PNWBT_DEVICE Dev = NwbtGetDevice(Device);
    NTSTATUS st;
    UNREFERENCED_PARAMETER(Raw);
    NwbtReadConfig(Dev);
    NwbtDiagDword(Dev, L"NwbtStopAfterUsed", Dev->Cfg.StopAfter);
    NwbtDiagDword(Dev, L"NwbtCapsScoSupport", Dev->Cfg.ScoSupport);
    NwbtDiagDword(Dev, L"NwbtCapsMaxScoChannels", Dev->Cfg.MaxScoChannels);
    Dev->IoFirstN = Dev->IoLastN = 0;                    /* IOCTL trace starts with every hardware start */
    RtlZeroMemory(Dev->CntIoctl, sizeof(Dev->CntIoctl));
    Dev->IoT0 = (ULONG)(KeQueryInterruptTime() / 10000);
    st = NwbtCreateTimer(Dev);                           /* period = PollIntervalMs (registry) */
    if (!NT_SUCCESS(st))
        return st;
    if (!Dev->EvtFifoMem)                                /* freed in ReleaseHardware (stop/start safe) */
        Dev->EvtFifoMem = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, NWBT_FIFO_BYTES, NWBT_TAG);
    if (!Dev->AclFifoMem)
        Dev->AclFifoMem = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, NWBT_FIFO_BYTES, NWBT_TAG);
    if (!Dev->EvtFifoMem || !Dev->AclFifoMem)
        return STATUS_INSUFFICIENT_RESOURCES;
    nbt_fifo_init(&Dev->EvtFifo, Dev->EvtFifoMem, NWBT_FIFO_BYTES);
    nbt_fifo_init(&Dev->AclFifo, Dev->AclFifoMem, NWBT_FIFO_BYTES);
    st = NwbtMapResources(Dev, Trans);
    if (!NT_SUCCESS(st)) {
        NwbtSetStage(Dev, "fail_resources", st);
        return st;
    }
    st = NwbtCtlCheck(Dev);
    if (!NT_SUCCESS(st)) {
        NwbtSetStage(Dev, "fail_control_page", st);
        return st;
    }
    st = NwbtReadAdt(Dev);
    if (!NT_SUCCESS(st)) {
        NwbtSetStage(Dev, "fail_adt_data", st);
        return st;
    }
    NwbtSetStage(Dev, "prepared", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

static NTSTATUS NwbtEvtReleaseHardware(WDFDEVICE Device, WDFCMRESLIST Trans)
{
    PNWBT_DEVICE Dev = NwbtGetDevice(Device);
    UNREFERENCED_PARAMETER(Trans);
    NwbtTeardown(Dev);                                   /* no-op when D0Exit already ran */
    NwbtUnmapResources(Dev);
    NwbtFreeFiles(Dev);
    {
        KIRQL irql;
        KeAcquireSpinLock(&Dev->Lock, &irql);            /* timer stopped by the teardown above */
        RtlZeroMemory(&Dev->EvtFifo, sizeof(Dev->EvtFifo));
        RtlZeroMemory(&Dev->AclFifo, sizeof(Dev->AclFifo));
        KeReleaseSpinLock(&Dev->Lock, irql);
    }
    if (Dev->EvtFifoMem) { ExFreePoolWithTag(Dev->EvtFifoMem, NWBT_TAG); Dev->EvtFifoMem = NULL; }
    if (Dev->AclFifoMem) { ExFreePoolWithTag(Dev->AclFifoMem, NWBT_TAG); Dev->AclFifoMem = NULL; }
    return STATUS_SUCCESS;
}

NTSTATUS NwbtCreateTimer(PNWBT_DEVICE Dev)
{
    WDF_TIMER_CONFIG tc;
    WDF_OBJECT_ATTRIBUTES attr;
    if (Dev->Timer)
        return STATUS_SUCCESS;
    WDF_TIMER_CONFIG_INIT_PERIODIC(&tc, NwbtEvtTimer, Dev->Cfg.PollIntervalMs);
    tc.UseHighResolutionTimer = WdfTrue;
    WDF_OBJECT_ATTRIBUTES_INIT(&attr);
    attr.ParentObject = Dev->Device;
    return WdfTimerCreate(&tc, &attr, &Dev->Timer);
}

static NTSTATUS NwbtEvtD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Previous)
{
    PNWBT_DEVICE Dev = NwbtGetDevice(Device);
    NTSTATUS st;
    UNREFERENCED_PARAMETER(Previous);
    WdfIoQueueStart(Dev->EvtQ);
    WdfIoQueueStart(Dev->AclQ);
    WdfIoQueueStart(Dev->WriteQ);
    st = NwbtBringUp(Dev);
    NwbtDiagCounters(Dev);
    if (!NT_SUCCESS(st))
        NwbtTeardown(Dev);                               /* D0Exit is not called after a failed D0Entry */
    return st;
}

static NTSTATUS NwbtEvtD0Exit(WDFDEVICE Device, WDF_POWER_DEVICE_STATE Target)
{
    PNWBT_DEVICE Dev = NwbtGetDevice(Device);
    UNREFERENCED_PARAMETER(Target);
    NwbtTeardown(Dev);
    NwbtDiagCounters(Dev);
    return STATUS_SUCCESS;
}

static NTSTATUS NwbtEvtSelfManagedIoInit(WDFDEVICE Device)
{
    PNWBT_DEVICE Dev = NwbtGetDevice(Device);
    NTSTATUS st;
    if (!Dev->HciReady || !Dev->Cfg.CreatePdo || Dev->Child)
        return STATUS_SUCCESS;
    st = NwbtCreateChild(Dev);
    NwbtDiagDword(Dev, L"NwbtChildStatus", (ULONG)st);
    if (NT_SUCCESS(st))
        NwbtSetStage(Dev, "ready_bthmini_child", STATUS_SUCCESS);
    else
        NwbtSetStage(Dev, "fail_child_pdo", st);
    return STATUS_SUCCESS;                               /* the HCI stays up for the status job */
}
