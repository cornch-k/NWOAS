/* pdo.c - the child PDO BthMini binds to (inbox bth.inf: compatible ID MS_BTHX_BTHMINI).
 * Same shape as the serialhcibus sample PDO: internal, not removable, non power managed queue that
 * forwards every IOCTL_BTHX_* to the parent FDO queue (io.c).
 * SPDX-License-Identifier: MIT */
#include "nwbt.h"

/* {6AC27878-A6FA-4155-BA85-F98F491D4F33} GUID_CONTAINERID_INTERNALLY_CONNECTED_DEVICE */
static const GUID NwbtContainerInternal =
    { 0x6ac27878, 0xa6fa, 0x4155, { 0xba, 0x85, 0xf9, 0x8f, 0x49, 0x1d, 0x4f, 0x33 } };

void NwbtEvtPdoIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutLen, size_t InLen, ULONG Code)
{
    WDF_REQUEST_FORWARD_OPTIONS fo;
    NTSTATUS st = STATUS_NOT_SUPPORTED;
    WDFDEVICE parent = NwbtGetPdo(WdfIoQueueGetDevice(Queue))->Parent;
    PNWBT_DEVICE Dev = NwbtGetDevice(parent);
    NwbtTraceIo(Dev, 'P', Code, InLen, OutLen, 0, 0, 0, 0);
    switch (Code) {
    case IOCTL_BTHX_GET_VERSION:
    case IOCTL_BTHX_SET_VERSION:
    case IOCTL_BTHX_QUERY_CAPABILITIES:
    case IOCTL_BTHX_WRITE_HCI:
    case IOCTL_BTHX_READ_HCI:
        WDF_REQUEST_FORWARD_OPTIONS_INIT(&fo);
        fo.Flags = WDF_REQUEST_FORWARD_OPTION_SEND_AND_FORGET;
        st = WdfRequestForwardToParentDeviceIoQueue(Request, WdfDeviceGetDefaultQueue(parent), &fo);
        if (NT_SUCCESS(st))
            return;
        NwbtTraceIo(Dev, 'F', Code, InLen, OutLen, st, 0, 0, 0);
        break;
    default:
        NwbtTraceIo(Dev, 'C', Code, InLen, OutLen, st, 0xFF, 0, 0);   /* answered by the PDO itself */
        break;
    }
    WdfRequestComplete(Request, st);
}

NTSTATUS NwbtCreateChild(PNWBT_DEVICE Dev)
{
    PWDFDEVICE_INIT init;
    NTSTATUS st;
    WDF_OBJECT_ATTRIBUTES attr;
    WDF_IO_QUEUE_CONFIG qc;
    WDF_DEVICE_PNP_CAPABILITIES pnp;
    WDF_DEVICE_POWER_CAPABILITIES pw;
    WDFDEVICE child = NULL;
    UNICODE_STRING cont = { 0 };
    DECLARE_CONST_UNICODE_STRING(hwid, L"NWBT\\BCM4378BTH");
    DECLARE_CONST_UNICODE_STRING(compat, L"MS_BTHX_BTHMINI");
    DECLARE_CONST_UNICODE_STRING(inst, L"00");
    DECLARE_CONST_UNICODE_STRING(desc, L"NWOAS BCM4378 Bluetooth radio");
    DECLARE_CONST_UNICODE_STRING(loc, L"NWOAS BCM4378 PCIe fn1 - Bluetooth function");

    init = WdfPdoInitAllocate(Dev->Device);
    if (!init)
        return STATUS_INSUFFICIENT_RESOURCES;
    WdfDeviceInitSetDeviceType(init, FILE_DEVICE_BUS_EXTENDER);
    st = WdfPdoInitAssignDeviceID(init, &hwid);
    if (NT_SUCCESS(st))
        st = WdfPdoInitAddHardwareID(init, &hwid);
    if (NT_SUCCESS(st))
        st = WdfPdoInitAddCompatibleID(init, &compat);
    if (NT_SUCCESS(st))
        st = WdfPdoInitAssignInstanceID(init, &inst);
    if (NT_SUCCESS(st) && NT_SUCCESS(RtlStringFromGUID(&NwbtContainerInternal, &cont))) {
        st = WdfPdoInitAssignContainerID(init, &cont);
        RtlFreeUnicodeString(&cont);
    }
    if (NT_SUCCESS(st))
        st = WdfPdoInitAddDeviceText(init, &desc, &loc, 0x409);
    if (!NT_SUCCESS(st)) {
        WdfDeviceInitFree(init);
        return st;
    }
    WdfPdoInitSetDefaultLocale(init, 0x409);
    WdfPdoInitAllowForwardingRequestToParent(init);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, NWBT_PDO);
    st = WdfDeviceCreate(&init, &attr, &child);
    if (!NT_SUCCESS(st)) {
        if (init)
            WdfDeviceInitFree(init);
        return st;
    }
    NwbtGetPdo(child)->Parent = Dev->Device;

    WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnp);
    pnp.Removable = WdfFalse;
    pnp.SurpriseRemovalOK = WdfTrue;
    pnp.Address = 1;
    pnp.UINumber = 1;
    WdfDeviceSetPnpCapabilities(child, &pnp);
    WDF_DEVICE_POWER_CAPABILITIES_INIT(&pw);
    pw.DeviceD1 = WdfFalse;
    pw.DeviceD2 = WdfFalse;
    pw.WakeFromD0 = pw.WakeFromD1 = pw.WakeFromD2 = pw.WakeFromD3 = WdfFalse;
    pw.DeviceState[PowerSystemWorking] = PowerDeviceD0;
    pw.DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
    pw.DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    pw.DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    pw.DeviceState[PowerSystemHibernate] = PowerDeviceD3;
    pw.DeviceState[PowerSystemShutdown] = PowerDeviceD3;
    WdfDeviceSetPowerCapabilities(child, &pw);

    /* not power managed: BthMini queries version/capabilities before D0 (sample note) */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qc, WdfIoQueueDispatchParallel);
    qc.PowerManaged = WdfFalse;
    qc.EvtIoDeviceControl = NwbtEvtPdoIoDeviceControl;
    qc.EvtIoInternalDeviceControl = NwbtEvtPdoIoDeviceControl;
    st = WdfIoQueueCreate(child, &qc, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (NT_SUCCESS(st))
        st = WdfFdoAddStaticChild(Dev->Device, child);
    if (!NT_SUCCESS(st)) {
        WdfObjectDelete(child);
        return st;
    }
    Dev->Child = child;
    return STATUS_SUCCESS;
}
