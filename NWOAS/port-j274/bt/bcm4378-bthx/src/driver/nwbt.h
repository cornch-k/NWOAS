/* nwbt.h - NWOAS BCM4378 Bluetooth transport driver (nwbt4378.sys), Windows Bluetooth extensible
 * transport (BthX): KMDF bus driver on ACPI\NWOA5F69 (S261 \_SB.NWC1.BTH0) that enumerates one child
 * PDO with compatible ID MS_BTHX_BTHMINI; the inbox BthMini.sys loads on it and talks IOCTL_BTHX_*.
 * Contract: research-20261118/bt/CONTRACT-BT.md.  Test plan: research-20261118/bt/TESTPLAN-BT.md.
 * SPDX-License-Identifier: MIT */
#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <ntstrsafe.h>
#include <bthxddi.h>

#include "../core/nwbt_core.h"

#define NWBT_TAG            'TBWN'
#define NWBT_LOG(fmt, ...)  DbgPrintEx(DPFLTR_IHVBUS_ID, DPFLTR_ERROR_LEVEL, "NWBT4378: " fmt "\n", __VA_ARGS__)

/* BT control page (S261 bt_module.py / nwoas_bt_s261.py; 32-bit accesses only) */
#define BTC_MAGIC        0x00
#define BTC_VERSION      0x04
#define BTC_STATE        0x08
#define BTC_ERROR        0x0C
#define BTC_CMD          0x10      /* ACPI only: the driver never writes it */
#define BTC_IDS          0x1C
#define BTC_CFG_OFF      0x20
#define BTC_CFG_DATA     0x24
#define BTC_CFG_ERR      0x28
#define BTC_BAR0         0x2C
#define BTC_BAR2         0x30
#define BTC_CHIP_STATE   0x34
#define BTC_DEVCTL       0x38
#define BTC_EPCMD        0x3C
#define BTC_RESET        0x40
#define BTC_RESET_COUNT  0x44
#define BTC_EPOCH        0x48
#define BTC_RID2SID      0x4C
#define BTC_WINDOW       0x54
#define BTC_DEVCAP       0x58
#define BTC_MSICAP       0x5C
#define BTC_BDADDR_LO    0x60
#define BTC_BDADDR_HI    0x64
#define BTC_CAL_LEN      0x68
#define BTC_CALBF_LEN    0x6C
#define BTC_BOARD        0x70
#define BTC_GEN          0x80      /* S261b fence generation */
#define BTC_ARM          0x84      /* write = generation (lifts the fn1 BAR fence); read = result, 1 ok */
#define BTC_ARM_OK       1
#define BTC_CAL          0x1000
#define BTC_CALBF        0x2800
#define BTC_CAL_MAX      0x1800
#define BTC_MAGIC_VALUE  0x5442574Eu  /* 'NWBT' */
#define BTC_STATE_UP     1
#define BTC_RESET_OK     1
#define BTC_RESET_NO_FLR 2
#define BT_FN1_ID        0x5F6914E4u

/* DMA: the chip reaches [0, 0xfffffe00]; the dart-apcie0 window is [1 MiB, 0xBFF00000) */
#define NWBT_DMA_LOW     0x00100000ull
#define NWBT_DMA_HIGH    0xBFF00000ull

/* StopAfter (registry, Device Parameters): bisect the bring-up.  0 = everything. */
enum {
    NWBT_STOP_CTLPAGE = 1,   /* control page checked, nothing else (no BAR access at all) */
    NWBT_STOP_CONFIG  = 2,   /* + FLR + bus master + vendor config windows (still no BAR access) */
    NWBT_STOP_OTP     = 3,   /* + first BAR access: OTP bytes (BAR0), stepping/vendor */
    NWBT_STOP_FILES   = 4,   /* + bootstage/RTI read (BAR2), firmware/PTB files, DMA memory (no doorbell) */
    NWBT_STOP_BOOT    = 5,   /* + firmware boot (bootstage 2) */
    NWBT_STOP_RTI     = 6,   /* + RTI 1 -> 2, context: control ring ready */
    NWBT_STOP_RINGS   = 7,   /* + completion/transfer rings created */
    NWBT_STOP_HCI     = 8,   /* + calibration, PTB, BD address, HCI Reset + Read Local Version */
};

typedef struct _NWBT_CONFIG {
    ULONG StopAfter;
    ULONG DoFlr;             /* 1: function level reset of fn1 first (needed for any restart) */
    ULONG PollIntervalMs;    /* ring poll period */
    ULONG FixQuirks;         /* NBT_FIX_* mask applied to events going to BthMini */
    ULONG SetBdaddr;         /* 1: program the ADT address when the chip reports its default one */
    ULONG SendCal, SendPtb;
    ULONG PokeAwake;         /* write SLEEP_CONTROL=AWAKE before every enqueue (Linux does it) */
    ULONG MsiToRam;          /* 1: MSI enabled with a host RAM page as address (diagnostic) */
    ULONG BootTimeoutMs, StepTimeoutMs, HciTimeoutMs;
    ULONG CreatePdo;         /* 0: bring HCI up but do not hand it to BthMini */
    ULONG AutoRestart;
    ULONG ScoSupport, MaxScoChannels;   /* BTHX_CAPABILITIES: documented required values 2 (HCIBypass), 1 */       /* firmware died (RTI != 2): restart the device, at most 3 times per boot */
    WCHAR FirmwareDir[128];
} NWBT_CONFIG;

/* IOCTL trace (registry NwbtIoLog / NwbtIoLogLast): the first 48 records after start stay, then a ring of 16.
 * kind: P = arrived at the child PDO, A = arrived at the FDO, C = completed by the FDO, W = written to a ring,
 * R = read completed with a packet, H = held (ring full), F = forward to the parent failed. */
typedef struct _NWBT_IOREC {
    ULONG Ms;
    USHORT Func;          /* (IoControlCode >> 2) & 0xFFF; BthX = 0x100..0x104 */
    USHORT In, Out;
    UCHAR Kind, Type;
    USHORT Op;            /* HCI opcode (W command), event code (R event: code | CC opcode << 8 in Op2) */
    USHORT Op2;
    ULONG St;
} NWBT_IOREC;
#define NWBT_IOLOG_FIRST 48
#define NWBT_IOLOG_LAST  16

typedef struct _NWBT_DEVICE {
    WDFDEVICE Device;
    NWBT_CONFIG Cfg;
    PUCHAR Bar0, Bar2, Ctl;
    SIZE_T Bar0Len, Bar2Len, CtlLen;
    ULONG Epoch;

    CHAR Stepping[16], Vendor[16], FwName[48], PtbName[48];
    PUCHAR FwImg, PtbImg, Cal;
    ULONG FwLen, PtbLen, CalLen, CalPlainLen;   /* Cal = bf blob, Cal + BTC_CAL_MAX = plain blob */
    UCHAR AdtBd[6];
    BOOLEAN HaveAdtBd, CalPlainLoaded;

    PUCHAR Blk;              /* one DMA block: context, ring indices, rings, payload slots */
    ULONG64 BlkPa;
    SIZE_T BlkLen;
    PUCHAR FwDma;
    ULONG64 FwDmaPa;
    SIZE_T FwDmaLen;
    PUCHAR MsiPage;
    ULONG64 MsiPa;
    BOOLEAN DmaLeaked;       /* bus master could not be confirmed off: memory deliberately kept */
    nbt_layout L;

    KSPIN_LOCK Lock;         /* rings, FIFOs, sync state */
    BOOLEAN BmeOn, CtlRingUp, RingsUp, HciReady, Dead, Running, BthxOpen;
    WDFTIMER Timer;
    WDFWORKITEM DiagWork, RecoverWork;
    LONG DiagQueued, RecoverQueued;
    ULONG TicksSinceHealth, TicksSinceDiag;

    KEVENT CtlEvent, CmdEvent;
    BOOLEAN SyncWanted;
    USHORT SyncOpcode;
    UCHAR SyncEvt[260];
    ULONG SyncLen;

    WDFQUEUE EvtQ, AclQ, WriteQ;
    nbt_fifo EvtFifo, AclFifo;
    PUCHAR EvtFifoMem, AclFifoMem;
    WDFDEVICE Child;

    LONG64 CntPoll, CntEvt, CntAclRx, CntCmdTx, CntAclTx, CntAck, CntBadEntry, CntFix, CntDropNoHost,
           CntDropFifo, CntWriteHeld, CntReadDone, CntInitEvtDropped, CntMsi;
    ULONG BootSeen[8], BootSeenN, LastBootstage, LastRti, BootMs, RtiMs;
    ULONG HciVersion, HciRevision, LmpVersion, LmpSubversion, Manufacturer, BdState;
    CHAR Stage[48];
    NTSTATUS LastStatus;
    NWBT_IOREC IoFirst[NWBT_IOLOG_FIRST], IoLast[NWBT_IOLOG_LAST];
    ULONG IoFirstN, IoLastN, IoT0, BthxVersionSet;
    ULONG DeadReason, DeadGen;   /* 1 GEN changed, 2 RTI != 2 (all-ones = fenced), 3 ring index */
    ULONG Gen;            /* fence generation this start armed; any change = chip went away: dead */
    LONG CntIoctl[8];     /* GET_VERSION, SET_VERSION, CAPS, WRITE, READ, other, PDO other, forward failed */
} NWBT_DEVICE, *PNWBT_DEVICE;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NWBT_DEVICE, NwbtGetDevice)

typedef struct _NWBT_PDO {
    WDFDEVICE Parent;
} NWBT_PDO, *PNWBT_PDO;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NWBT_PDO, NwbtGetPdo)

#define NWBT_FIFO_BYTES   (64 * 1024)
#define NWBT_MAX_ACL_IN   1021     /* BthX MaxAclTransferInSize: fits the 1032-byte d2h slots */

/* driver.c */
EVT_WDF_DRIVER_DEVICE_ADD NwbtEvtDeviceAdd;
void NwbtSetStage(PNWBT_DEVICE Dev, PCSTR Stage, NTSTATUS St);
/* hw.c */
NTSTATUS NwbtMapResources(PNWBT_DEVICE Dev, WDFCMRESLIST Trans);
void NwbtUnmapResources(PNWBT_DEVICE Dev);
ULONG NwbtCtlRead(PNWBT_DEVICE Dev, ULONG Off);
void NwbtCtlWrite(PNWBT_DEVICE Dev, ULONG Off, ULONG Val);
ULONG NwbtCfgRead(PNWBT_DEVICE Dev, ULONG Off);
NTSTATUS NwbtCfgWrite(PNWBT_DEVICE Dev, ULONG Off, ULONG Val);
NTSTATUS NwbtCtlCheck(PNWBT_DEVICE Dev);
NTSTATUS NwbtReadAdt(PNWBT_DEVICE Dev);
void NwbtReadPlainCal(PNWBT_DEVICE Dev);
NTSTATUS NwbtBusMaster(PNWBT_DEVICE Dev, BOOLEAN On);
ULONG NwbtB0R(PNWBT_DEVICE Dev, ULONG Off);
void NwbtB0W(PNWBT_DEVICE Dev, ULONG Off, ULONG Val);
ULONG NwbtB2R(PNWBT_DEVICE Dev, ULONG Off);
void NwbtB2W(PNWBT_DEVICE Dev, ULONG Off, ULONG Val);
UCHAR NwbtB0R8(PNWBT_DEVICE Dev, ULONG Off);
PVOID NwbtDmaAlloc(SIZE_T Len, PULONG64 Pa);
void NwbtDmaFree(PVOID Va, SIZE_T Len);
NTSTATUS NwbtLoadFile(PNWBT_DEVICE Dev, PCSTR Name, PUCHAR* Out, ULONG* Len, ULONG Max);
/* boot.c */
NTSTATUS NwbtBringUp(PNWBT_DEVICE Dev);
void NwbtTeardown(PNWBT_DEVICE Dev);
void NwbtFreeFiles(PNWBT_DEVICE Dev);
/* io.c */
EVT_WDF_TIMER NwbtEvtTimer;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL NwbtEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL NwbtEvtPdoIoDeviceControl;
NTSTATUS NwbtControlSync(PNWBT_DEVICE Dev, const UCHAR Msg[NBT_CONTROL_MSG_SIZE]);
NTSTATUS NwbtHciSync(PNWBT_DEVICE Dev, USHORT Opcode, const UCHAR* Params, ULONG Plen,
                     PUCHAR Ret, ULONG RetCap, PULONG RetLen, PUCHAR Status);
void NwbtRingDoorbell(PNWBT_DEVICE Dev, UCHAR Db, USHORT Val);
void NwbtFailPending(PNWBT_DEVICE Dev, NTSTATUS St);
void NwbtTraceIo(PNWBT_DEVICE Dev, UCHAR Kind, ULONG Code, size_t In, size_t Out, NTSTATUS St, UCHAR Type, USHORT Op, USHORT Op2);
/* pdo.c */
NTSTATUS NwbtCreateChild(PNWBT_DEVICE Dev);
/* diag.c */
void NwbtReadConfig(PNWBT_DEVICE Dev);
void NwbtDiagSz(PNWBT_DEVICE Dev, PCWSTR Name, PCSTR Value);
void NwbtDiagDword(PNWBT_DEVICE Dev, PCWSTR Name, ULONG Value);
void NwbtDiagCounters(PNWBT_DEVICE Dev);
void NwbtDiagIoLog(PNWBT_DEVICE Dev);
EVT_WDF_WORKITEM NwbtEvtDiagWork;
EVT_WDF_WORKITEM NwbtEvtRecoverWork;
NTSTATUS NwbtCreateTimer(PNWBT_DEVICE Dev);
