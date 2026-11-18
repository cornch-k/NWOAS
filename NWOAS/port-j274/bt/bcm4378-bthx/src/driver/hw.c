/* hw.c - resources, BT control page, fn1 config space through the page, BAR access, DMA memory, files.
 *
 * BAR rule (S259/S261 contract): Windows maps MMIO Device-nGnRnE and the hypervisor emulates every
 * access to these BARs (BYPASS tracer).  Only plain aligned 8/32-bit register accesses are used here,
 * never memcpy/memset or 64-bit pair loads on a BAR.
 * SPDX-License-Identifier: MIT */
#include "nwbt.h"

NTSTATUS NwbtMapResources(PNWBT_DEVICE Dev, WDFCMRESLIST Trans)
{
    ULONG n = WdfCmResourceListGetCount(Trans), i;
    for (i = 0; i < n; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d = WdfCmResourceListGetDescriptor(Trans, i);
        ULONGLONG len;
        PHYSICAL_ADDRESS pa;
        if (!d || (d->Type != CmResourceTypeMemory && d->Type != CmResourceTypeMemoryLarge))
            continue;
        len = RtlCmDecodeMemIoResource(d, NULL);
        pa = d->u.Memory.Start;
        NWBT_LOG("mem resource %u pa=%llx len=%llx", i, pa.QuadPart, len);
        if (len == 0x4000 && !Dev->Ctl) {               /* told apart by length (contract) */
            Dev->Ctl = (PUCHAR)MmMapIoSpaceEx(pa, (SIZE_T)len, PAGE_READWRITE | PAGE_NOCACHE);
            Dev->CtlLen = (SIZE_T)len;
            NwbtDiagDword(Dev, L"NwbtCtlPaLo", (ULONG)pa.QuadPart);
        } else if (len == 0x8000 && !Dev->Bar0) {
            Dev->Bar0 = (PUCHAR)MmMapIoSpaceEx(pa, (SIZE_T)len, PAGE_READWRITE | PAGE_NOCACHE);
            Dev->Bar0Len = (SIZE_T)len;
            NwbtDiagDword(Dev, L"NwbtBar0PaLo", (ULONG)pa.QuadPart);
        } else if (len == 0x400000 && !Dev->Bar2) {
            Dev->Bar2 = (PUCHAR)MmMapIoSpaceEx(pa, (SIZE_T)len, PAGE_READWRITE | PAGE_NOCACHE);
            Dev->Bar2Len = (SIZE_T)len;
            NwbtDiagDword(Dev, L"NwbtBar2PaLo", (ULONG)pa.QuadPart);
        }
    }
    NwbtDiagDword(Dev, L"NwbtResources", (Dev->Ctl ? 1 : 0) | (Dev->Bar0 ? 2 : 0) | (Dev->Bar2 ? 4 : 0));
    return (Dev->Ctl && Dev->Bar0 && Dev->Bar2) ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

void NwbtUnmapResources(PNWBT_DEVICE Dev)
{
    if (Dev->Ctl) { MmUnmapIoSpace(Dev->Ctl, Dev->CtlLen); Dev->Ctl = NULL; }
    if (Dev->Bar0) { MmUnmapIoSpace(Dev->Bar0, Dev->Bar0Len); Dev->Bar0 = NULL; }
    if (Dev->Bar2) { MmUnmapIoSpace(Dev->Bar2, Dev->Bar2Len); Dev->Bar2 = NULL; }
}

ULONG NwbtCtlRead(PNWBT_DEVICE Dev, ULONG Off)
{
    if (!Dev->Ctl || Off + 4 > Dev->CtlLen || (Off & 3))
        return 0xFFFFFFFF;
    return READ_REGISTER_ULONG((volatile ULONG*)(Dev->Ctl + Off));
}

void NwbtCtlWrite(PNWBT_DEVICE Dev, ULONG Off, ULONG Val)
{
    if (!Dev->Ctl || Off + 4 > Dev->CtlLen || (Off & 3) || Off == BTC_CMD)   /* CMD is ACPI only */
        return;
    WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->Ctl + Off), Val);
}

ULONG NwbtCfgRead(PNWBT_DEVICE Dev, ULONG Off)
{
    NwbtCtlWrite(Dev, BTC_CFG_OFF, Off);
    return NwbtCtlRead(Dev, BTC_CFG_DATA);
}

NTSTATUS NwbtCfgWrite(PNWBT_DEVICE Dev, ULONG Off, ULONG Val)
{
    ULONG err;
    NwbtCtlWrite(Dev, BTC_CFG_OFF, Off);
    NwbtCtlWrite(Dev, BTC_CFG_DATA, Val);
    err = NwbtCtlRead(Dev, BTC_CFG_ERR);
    if (err) {
        NWBT_LOG("config write %x=%x refused err %u", Off, Val, err);
        return STATUS_ACCESS_DENIED;
    }
    return STATUS_SUCCESS;
}

/* contract: MAGIC, VERSION, STATE up, BARs assigned, fn1 IDs - before any BAR access */
NTSTATUS NwbtCtlCheck(PNWBT_DEVICE Dev)
{
    ULONG magic = NwbtCtlRead(Dev, BTC_MAGIC), ver = NwbtCtlRead(Dev, BTC_VERSION);
    ULONG state = NwbtCtlRead(Dev, BTC_STATE), err = NwbtCtlRead(Dev, BTC_ERROR);
    ULONG b0 = NwbtCtlRead(Dev, BTC_BAR0), b2 = NwbtCtlRead(Dev, BTC_BAR2), ids = NwbtCtlRead(Dev, BTC_IDS);
    Dev->Epoch = NwbtCtlRead(Dev, BTC_EPOCH);
    NwbtDiagDword(Dev, L"NwbtCtlMagic", magic);
    NwbtDiagDword(Dev, L"NwbtCtlState", state);
    NwbtDiagDword(Dev, L"NwbtCtlError", err);
    NwbtDiagDword(Dev, L"NwbtCtlBar0", b0);
    NwbtDiagDword(Dev, L"NwbtCtlBar2", b2);
    NwbtDiagDword(Dev, L"NwbtCtlIds", ids);
    NwbtDiagDword(Dev, L"NwbtCtlChipState", NwbtCtlRead(Dev, BTC_CHIP_STATE));
    NwbtDiagDword(Dev, L"NwbtCtlEpoch", Dev->Epoch);
    NwbtDiagDword(Dev, L"NwbtCtlWindow", NwbtCtlRead(Dev, BTC_WINDOW));
    NwbtDiagDword(Dev, L"NwbtCtlDevCap", NwbtCtlRead(Dev, BTC_DEVCAP));
    NwbtDiagDword(Dev, L"NwbtCtlMsiCap", NwbtCtlRead(Dev, BTC_MSICAP));
    NwbtDiagDword(Dev, L"NwbtCtlRid2Sid", NwbtCtlRead(Dev, BTC_RID2SID));
    if (magic != BTC_MAGIC_VALUE || ver != 1 || state != BTC_STATE_UP || !b0 || !b2 || ids != BT_FN1_ID)
        return STATUS_DEVICE_NOT_READY;
    return STATUS_SUCCESS;
}

/* BD address and the calibration blob for the chip stepping (b1/b3: beamforming blob, as Linux). */
NTSTATUS NwbtReadAdt(PNWBT_DEVICE Dev)
{
    ULONG lo = NwbtCtlRead(Dev, BTC_BDADDR_LO), hi = NwbtCtlRead(Dev, BTC_BDADDR_HI), i;
    ULONG lenbf = NwbtCtlRead(Dev, BTC_CALBF_LEN), len = NwbtCtlRead(Dev, BTC_CAL_LEN);
    CHAR board[17];
    for (i = 0; i < 4; i++) {
        ULONG w = NwbtCtlRead(Dev, BTC_BOARD + 4 * i);
        board[4 * i] = (CHAR)w; board[4 * i + 1] = (CHAR)(w >> 8);
        board[4 * i + 2] = (CHAR)(w >> 16); board[4 * i + 3] = (CHAR)(w >> 24);
    }
    board[16] = 0;
    NwbtDiagSz(Dev, L"NwbtBoard", board);
    NwbtDiagDword(Dev, L"NwbtCalLen", len);
    NwbtDiagDword(Dev, L"NwbtCalBfLen", lenbf);
    if (strcmp(board, "atlantisb"))
        return STATUS_NOT_SUPPORTED;
    for (i = 0; i < 4; i++)
        Dev->AdtBd[i] = (UCHAR)(lo >> (8 * i));
    Dev->AdtBd[4] = (UCHAR)hi;
    Dev->AdtBd[5] = (UCHAR)(hi >> 8);
    Dev->HaveAdtBd = (lo | (hi & 0xFFFF)) != 0;
    NwbtDiagDword(Dev, L"NwbtAdtBdPresent", Dev->HaveAdtBd);
    if (Dev->Cal)
        return STATUS_SUCCESS;                           /* kept from the first start */
    if (!lenbf || lenbf > BTC_CAL_MAX || !len || len > BTC_CAL_MAX)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    /* B1 stepping uses the beamforming blob; the plain one is kept as a fallback for other steppings */
    Dev->Cal = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, 2 * BTC_CAL_MAX, NWBT_TAG);
    if (!Dev->Cal)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (i = 0; i < lenbf; i += 4) {                     /* 32-bit reads only, like every control page access */
        ULONG v = NwbtCtlRead(Dev, BTC_CALBF + i), k;
        for (k = 0; k < 4 && i + k < BTC_CAL_MAX; k++)
            Dev->Cal[i + k] = (UCHAR)(v >> (8 * k));
    }
    /* the plain blob (other steppings) is read later only if needed: NwbtReadPlainCal */
    Dev->CalLen = lenbf;
    Dev->CalPlainLen = len;
    NwbtDiagDword(Dev, L"NwbtCalLoaded", lenbf);
    return STATUS_SUCCESS;
}

NTSTATUS NwbtBusMaster(PNWBT_DEVICE Dev, BOOLEAN On)
{
    NTSTATUS st = NwbtCfgWrite(Dev, 0x04, On ? 0x0406 : 0x0402);   /* MEM always on, INTx always off */
    ULONG cmd = NwbtCfgRead(Dev, 0x04) & 0x407;
    NwbtDiagDword(Dev, L"NwbtFn1Command", cmd);
    if (!NT_SUCCESS(st))
        return st;
    if (cmd != (On ? 0x406u : 0x402u))
        return STATUS_DEVICE_HARDWARE_ERROR;
    Dev->BmeOn = On;
    return STATUS_SUCCESS;
}

ULONG NwbtB0R(PNWBT_DEVICE Dev, ULONG Off) { return READ_REGISTER_ULONG((volatile ULONG*)(Dev->Bar0 + Off)); }
void NwbtB0W(PNWBT_DEVICE Dev, ULONG Off, ULONG Val) { WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->Bar0 + Off), Val); }
ULONG NwbtB2R(PNWBT_DEVICE Dev, ULONG Off) { return READ_REGISTER_ULONG((volatile ULONG*)(Dev->Bar2 + Off)); }
void NwbtB2W(PNWBT_DEVICE Dev, ULONG Off, ULONG Val) { WRITE_REGISTER_ULONG((volatile ULONG*)(Dev->Bar2 + Off), Val); }
UCHAR NwbtB0R8(PNWBT_DEVICE Dev, ULONG Off) { return READ_REGISTER_UCHAR((volatile UCHAR*)(Dev->Bar0 + Off)); }

/* non-cached contiguous RAM with guest PA inside [1 MiB, 0xBFF00000): bus address == guest PA there */
PVOID NwbtDmaAlloc(SIZE_T Len, PULONG64 Pa)
{
    PHYSICAL_ADDRESS lo, hi, bnd;
    PVOID va;
    ULONG64 p;
    lo.QuadPart = (LONGLONG)NWBT_DMA_LOW;
    hi.QuadPart = (LONGLONG)(NWBT_DMA_HIGH - 1);
    bnd.QuadPart = 0;
    va = MmAllocateContiguousMemorySpecifyCache(Len, lo, hi, bnd, MmNonCached);
    if (!va)
        return NULL;
    p = (ULONG64)MmGetPhysicalAddress(va).QuadPart;
    if (p < NWBT_DMA_LOW || p + Len > NWBT_DMA_HIGH) {
        MmFreeContiguousMemorySpecifyCache(va, Len, MmNonCached);
        return NULL;                                      /* never give the chip an address outside */
    }
    RtlZeroMemory(va, Len);
    *Pa = p;
    return va;
}

void NwbtDmaFree(PVOID Va, SIZE_T Len)
{
    if (Va)
        MmFreeContiguousMemorySpecifyCache(Va, Len, MmNonCached);
}

static NTSTATUS ReadFileFrom(HANDLE Root, PCWSTR Dir, PCWSTR Name, PUCHAR* Out, ULONG* Len, ULONG Max)
{
    WCHAR path[260];
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK io;
    HANDLE h = NULL;
    FILE_STANDARD_INFORMATION fsi;
    NTSTATUS st;
    *Out = NULL;
    *Len = 0;
    st = Dir ? RtlStringCchPrintfW(path, ARRAYSIZE(path), L"%s\\%s", Dir, Name)
             : RtlStringCchCopyW(path, ARRAYSIZE(path), Name);
    if (!NT_SUCCESS(st))
        return st;
    RtlInitUnicodeString(&us, path);
    InitializeObjectAttributes(&oa, &us, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Root, NULL);
    st = ZwCreateFile(&h, GENERIC_READ | SYNCHRONIZE, &oa, &io, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ,
                      FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(st))
        return st;
    st = ZwQueryInformationFile(h, &io, &fsi, sizeof(fsi), FileStandardInformation);
    if (NT_SUCCESS(st) && (fsi.EndOfFile.QuadPart <= 0 || fsi.EndOfFile.QuadPart > Max))
        st = STATUS_FILE_TOO_LARGE;
    if (NT_SUCCESS(st)) {
        ULONG n = (ULONG)fsi.EndOfFile.QuadPart;
        PUCHAR b = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, n, NWBT_TAG);
        if (!b) {
            st = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            LARGE_INTEGER off;
            off.QuadPart = 0;
            st = ZwReadFile(h, NULL, NULL, NULL, &io, b, n, &off, NULL);
            if (NT_SUCCESS(st) && io.Information != n)
                st = STATUS_END_OF_FILE;
            if (NT_SUCCESS(st)) {
                *Out = b;
                *Len = n;
            } else {
                ExFreePoolWithTag(b, NWBT_TAG);
            }
        }
    }
    ZwClose(h);
    return st;
}

/* registry FirmwareDir, then the driver store directory of nwbt4378.sys */
NTSTATUS NwbtLoadFile(PNWBT_DEVICE Dev, PCSTR Name, PUCHAR* Out, ULONG* Len, ULONG Max)
{
    WCHAR w[64];
    ULONG i;
    NTSTATUS st = STATUS_OBJECT_NAME_NOT_FOUND;
    for (i = 0; Name[i] && i < ARRAYSIZE(w) - 1; i++)
        w[i] = (WCHAR)(UCHAR)Name[i];
    w[i] = 0;
    if (Dev->Cfg.FirmwareDir[0]) {
        st = ReadFileFrom(NULL, Dev->Cfg.FirmwareDir, w, Out, Len, Max);
        if (NT_SUCCESS(st))
            return st;
    }
    {
        HANDLE dir = NULL;
        PDRIVER_OBJECT drv = WdfDriverWdmGetDriverObject(WdfGetDriver());
        if (NT_SUCCESS(IoGetDriverDirectory(drv, DriverDirectoryImage, 0, &dir))) {
            st = ReadFileFrom(dir, NULL, w, Out, Len, Max);
            ZwClose(dir);
        }
    }
    return st;
}

void NwbtReadPlainCal(PNWBT_DEVICE Dev)
{
    ULONG i;
    if (!Dev->Cal || Dev->CalPlainLoaded)
        return;
    for (i = 0; i < Dev->CalPlainLen && i < BTC_CAL_MAX; i += 4) {
        ULONG v = NwbtCtlRead(Dev, BTC_CAL + i), k;
        for (k = 0; k < 4 && i + k < BTC_CAL_MAX; k++)
            Dev->Cal[BTC_CAL_MAX + i + k] = (UCHAR)(v >> (8 * k));
    }
    Dev->CalPlainLoaded = TRUE;
}
