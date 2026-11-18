// nwoa.h - device context and internal interfaces of nwoa4378.sys.
//
// Layering:
//   core/nwbc_core.c  pure logic (ring math, message encode/decode, NVRAM, escan, ...)
//   hw.cpp            BAR0/BAR2/config access, backplane window
//   chip.cpp          EROM, core reset, OTP, TCM size          (from OpenBSD bwfm)
//   fw.cpp            firmware files, NVRAM, CR4 start, shared RAM
//   msgbuf.cpp        DMA, rings, ioctl/iovar, message dispatch, flowring, poll/ISR
//   events.cpp        firmware events: scan cache, connect/link state
//   wifi.cpp          WiFiCx commands, indications, capabilities, work item
//   datapath.cpp      NetAdapter TX/RX queues
//   diag.cpp          status values under the device hardware key
#pragma once

#define NWOA_TAG                'A4WN'
#define NWOA_MAX_CORES          32
#define NWOA_TX_SLOTS           128
#define NWOA_RX_SLOTS_MAX       256
#define NWOA_SLOT_SIZE          2048
#define NWOA_MAX_BSS            64
#define NWOA_BSS_IE_MAX         1024
#define NWOA_EVQ                16
#define NWOA_RXQ                256
#define NWOA_FW_MAX             (4u * 1024 * 1024)
#define NWOA_CONSOLE_TAIL       4096

#define NWOA_LOG(fmt, ...) DbgPrintEx(DPFLTR_IHVNETWORK_ID, DPFLTR_ERROR_LEVEL, "NWOA4378: " fmt "\n", __VA_ARGS__)

// work item jobs (bitmask in Dev->Jobs)
#define JOB_SCAN_START          0x0001
#define JOB_SCAN_DONE           0x0002
#define JOB_CONNECT             0x0004
#define JOB_CONNECT_RESULT      0x0008
#define JOB_DISCONNECT          0x0010
#define JOB_RESET               0x0020
#define JOB_RADIO               0x0040
#define JOB_KEYS                0x0080
#define JOB_LINK_LOST           0x0100
#define JOB_DIAG                0x0200
#define JOB_TASK_ACK            0x0400
#define JOB_ROAM_STAY           0x0800   // WDI_TASK_ROAM to the current AP: complete it without roaming

typedef struct _NWOA_DMABUF {
    WDFCOMMONBUFFER Cb;
    PUCHAR          Va;
    ULONG64         La;
    SIZE_T          Len;
    PVOID           Contig;     // MmAllocateContiguousMemorySpecifyCache allocation (else WDF)
} NWOA_DMABUF;

typedef struct _NWOA_RING {
    nb_ringdesc     D;          // offset/items/size/slot in the control arena
    PUCHAR          Va;
    ULONG64         La;
    ULONG           WPtr;       // producer copy (H2D) / consumer copy (D2H)
    ULONG           RPtr;
} NWOA_RING;

typedef struct _NWOA_BSS {
    BOOLEAN  Used;
    nb_bss   B;                 // B.ies points into Ies
    UCHAR    Ies[NWOA_BSS_IE_MAX];
    ULONG64  SeenTime;
} NWOA_BSS;

typedef struct _NWOA_EVREC {
    ULONG Type, Status, Reason, Flags, Ms;
} NWOA_EVREC;
#define NWOA_EVLOG 32

// WDI command record (no SSID/BSSID/key values).  Info for WDI_TASK_CONNECT / WDI_TASK_ROAM:
//   bit0 RoamRequest, bits4-7 RoamTrigger, bits8-15 RoamNeededReason, bit16 first preferred BSSID is
//   the current BSSID, bit17 first preferred entry is 5 GHz, bit18 it is in the scan cache,
//   bits24-31 number of preferred entries.  WDI_TASK_DISCONNECT: 802.11 reason.  Radio: software state.
typedef struct _NWOA_CMDREC {
    ULONG  Ms;
    USHORT Id;
    UCHAR  Conn;                // driver thought it was connected when the command arrived
    UCHAR  Pad;
    ULONG  Status;              // M3 status (STATUS_PENDING for requests completed later)
    ULONG  Info;
} NWOA_CMDREC;
#define NWOA_CMDLOG  32
#define NWOA_CMDHIST 40
#define NWOA_CMDSNAP 10

// who sent NB_C_DISASSOC (NwoaHostDisassoc Src)
#define DIS_CONNECT     1       // WDI_TASK_CONNECT while connected / joining
#define DIS_ROAM        2       // WDI_TASK_ROAM to another BSS
#define DIS_DISCONNECT  3       // WDI_TASK_DISCONNECT
#define DIS_RESET       4       // WDI_TASK_DOT11_RESET
#define DIS_RADIO_OFF   5       // WDI_TASK_SET_RADIO_STATE off
#define DIS_CONN_FAIL   6       // failed connect clean-up
#define DIS_SHUTDOWN    7       // D0 exit / release

typedef struct _NWOA_LOSSREC {
    ULONG  Ms;
    USHORT Type, Reason;        // firmware event type / reason
    UCHAR  WasConn;             // Connected when the event arrived (1 = not caused by our disassoc)
    UCHAR  Pad;
    USHORT SinceHostMs;         // ms since our last NB_C_DISASSOC (65535 = none / longer)
    LONG   Rssi;                // last polled RSSI before the event
} NWOA_LOSSREC;

typedef struct _NWOA_RXITEM {
    USHORT Local;               // 1 = frame in EvRxBuf (EAPOL taken out of an E_EAPOL_MSG event)
    USHORT Slot;
    USHORT Off;
    USHORT Len;
} NWOA_RXITEM;

// a WiFiCx task waiting for its M4 indication
typedef struct _NWOA_TASK {
    BOOLEAN            Active;
    UINT16             MessageId;
    UINT16             CompleteId;
    WDI_MESSAGE_HEADER Hdr;
} NWOA_TASK;

// a property request completed later from the work item (cipher keys)
#define NWOA_MAX_KEYS 4
typedef struct _NWOA_KEY {
    int     Cipher;             // NB_CIPHER_*
    int     Pairwise;
    int     Delete;
    ULONG   Index;
    UCHAR   Peer[6];
    BOOLEAN HavePeer;
    UCHAR   Key[32];
    ULONG   KeyLen;
    UCHAR   Rsc[6];
    BOOLEAN HaveRsc;
} NWOA_KEY;

typedef struct _NWOA_CONNECT {
    UCHAR   Ssid[32];
    ULONG   SsidLen;
    UCHAR   Bssid[6];
    int     Rsn;                // 1 = RSNA-PSK, 0 = open
    int     Akm;                // NB_AKM_*
    int     Pairwise;           // NB_CIPHER_*
    int     Group;
    ULONG   WdiAuth;            // WDI_AUTH_ALGORITHM
    ULONG   WdiUcast, WdiMcast; // WDI_CIPHER_ALGORITHM
    UCHAR   Beacon[NWOA_BSS_IE_MAX + 12];
    ULONG   BeaconLen;
    USHORT  Chanspec;
    UCHAR   Channel, Band;
    USHORT  Capability;
    BOOLEAN HaveWmm;
} NWOA_CONNECT;

// connect progress, written at DISPATCH by events.cpp
#define CONN_IDLE       0
#define CONN_JOINING    1
#define CONN_LINKED     2
#define CONN_FAILED     3

typedef struct _NWOA_CONFIG {
    WCHAR  FirmwareDir[160];
    ULONG  PollIntervalMs;      // poll interval while traffic flows (ms, default 2)
    ULONG  PollIdleMs;          // poll interval when idle for 200 ms (default 50; 200 with a verified IRQ)
    ULONG  ScanPassive;         // 1 = passive escan (receive only)
    ULONG  Mpc;                 // firmware "mpc" (S257 used 0)
    ULONG  PowerSave;           // 0 = CAM, 2 = fast PS
    ULONG  RxFrameFormat;       // 0 = 802.3 (default), 1 = 802.11 + LLC, 2 = auto (follow TX)
    ULONG  UseInterrupt;        // 1 = use the interrupt resource if there is one
    ULONG  ClearNoSnoop;        // 1 = clear DevCtl No Snoop/Relaxed Ordering (S257 did)
    ULONG  DmaMode;             // 0 contiguous below 0xC0000000 (default), 1 WDF common buffer, 2 contiguous 4..48 GiB
    WCHAR  Country[4];
    ULONG  StopAfter;           // bisect: 0 full, 1 chip id + EROM, 2 + S257 core-up probe + config mirror,
                                //         3 + CR4 halt, 4 + chip reset/TCM/OTP, 5 + firmware up (no rings)
    ULONG  SettleMs;            // delay before the first backplane access (0 = none)
    ULONG  AccessDelayUs;       // stall before every backplane access (S257 ran through a slow UART proxy)
    ULONG  RxProtectedBit;      // 802.11 RX: 1 = Protected bit on EAPOL once the PTK is set (default), 2 = all frames, 0 = never
    ULONG  LinkUpAtStart;       // 1 = report media connected right after NetAdapterStart (MS sample style)
    ULONG  VerifyFirmware;      // TCM read-back after the upload: 0 none, 1 every 256th word + first/last 4 KiB
                                //  (default; each BAR access may be a hypervisor trap), 2 every word
    ULONG  PostWriteRead;       // 1 = read the register back after every backplane write (write completes
                                //     before the next access / window change)
    ULONG  FwRoamOff;           // firmware "roam_off": 1 = the firmware does not roam by itself (default; WDI roams from the host)
    ULONG  LinkQualityUpdates;  // 1 = poll RSSI every 2 s and send LINK_STATE_CHANGE on a 5 point change (default)
    ULONG  RoamStay;            // 1 = WDI_TASK_ROAM to the current AP completes without roaming (default)
} NWOA_CONFIG;

typedef struct _NWOA_DEVICE {
    WDFDEVICE       Device;
    NWOA_CONFIG     Cfg;

    // ---- resources (hw.cpp)
    PUCHAR          Bar0;   SIZE_T Bar0Len;
    PUCHAR          Bar2;   SIZE_T Bar2Len;
    PUCHAR          CtlPage; SIZE_T CtlPageLen;   // S259 NWOAS control page (ACPI\NWOA4378)
    BOOLEAN         UseCtlPage;
    ULONG           CfgErrors;
    BUS_INTERFACE_STANDARD BusIf;
    BOOLEAN         HaveBusIf;
    BOOLEAN         CfgViaMirror;
    KSPIN_LOCK      WinLock;
    ULONG           CurWindow;
    BOOLEAN         WindowValid;
    ULONG           EromBase;           // 0 until chipcommon EROMPTR was read
    volatile LONG   BpFault;            // backplane access refused (allowlist) or window not confirmed
    ULONG           BpBadAddr;
    ULONG           LastWriteBack;      // value read back after the last backplane write
    ULONG           PcieCap;            // PCIe capability offset (0 = unknown)
    BOOLEAN         HaveIntRes;
    CM_PARTIAL_RESOURCE_DESCRIPTOR IntRaw, IntTrans;

    // ---- chip (chip.cpp)
    ULONG           ChipIdReg, ChipRev;
    nb_core         Cores[NWOA_MAX_CORES];
    int             NumCores;
    ULONG           Pcie2Base, Pcie2Rev;
    BOOLEAN         PciReg64;
    ULONG           RamBase, RamSize;
    nb_otp          Otp;

    // ---- firmware (fw.cpp)
    nb_ramlayout    Ram;
    ULONG           SharedAddr;
    nb_shared       Sh;
    BOOLEAN         FwUp;
    CHAR            FwVersion[200];
    CHAR            NvramName[64];
    PUCHAR          Clm;  ULONG ClmLen;
    PUCHAR          Txcap; ULONG TxcapLen;
    PUCHAR          Board; ULONG BoardLen;
    PUCHAR          FwImg; ULONG FwImgLen;      // cached at first load, freed in ReleaseHardware
    PUCHAR          NvTxt; ULONG NvTxtLen;
    nb_board        BoardInfo;

    // ---- msgbuf (msgbuf.cpp)
    WDFDMAENABLER   DmaEnabler;
    nb_dmalayout    L;
    NWOA_DMABUF     Ctl;                // control arena (rings, indices, ioctl, events)
    NWOA_DMABUF     RxPool;             // NumRx * 2048
    NWOA_DMABUF     TxPool;             // NWOA_TX_SLOTS * 2048
    NWOA_RING       Ring[NB_NUM_COMMON_RINGS];
    NWOA_RING       Flow;               // the one TX flowring (flow ring id 2)
    ULONG           NumRx;
    KSPIN_LOCK      RingLock;           // all ring state + message dispatch
    BOOLEAN         RingsUp;
    BOOLEAN         BusMasterOn;
    volatile LONG   Dead;               // firmware trapped / device gone

    // ioctl
    FAST_MUTEX      IoctlMutex;
    KEVENT          IoctlEvent;
    USHORT          TransId;
    volatile LONG   IoctlPending;
    USHORT          IoctlTrans;
    LONG            IoctlStatus;
    ULONG           IoctlOutCap;
    UCHAR           IoRsp[NB_CTL_PKT_SIZE];
    ULONG           IoctlRetLen;
    int             IoType;
    int             ScanVer;

    // flowring
    volatile LONG   FlowState;          // 0 closed, 1 opening, 2 open, 3 closing, 4 failed
    KEVENT          FlowEvent;
    UCHAR           FlowDa[6];

    // tx slots
    KSPIN_LOCK      TxLock;
    ULONG           TxFree[NWOA_TX_SLOTS];
    ULONG           TxFreeCount;
    BOOLEAN         TxEapol[NWOA_TX_SLOTS];
    BOOLEAN         TxBusy[NWOA_TX_SLOTS];
    LONG            IoctlTimeoutsInRow;
    volatile LONG   EapolPending;

    // rx handoff to NetAdapter
    NWOA_RXITEM     RxQ[NWOA_RXQ];
    ULONG           RxQHead, RxQTail;   // under RingLock

    // interrupt + poll
    WDFINTERRUPT    Interrupt;
    WDFTIMER        PollTimer;
    BOOLEAN         IntrEnabled;
    volatile LONG   PollRunning;
    volatile LONG   PollIdle;           // 1 while the timer runs at the idle interval
    volatile LONG64 LastActivity;       // KeQueryInterruptTime of the last message / TX
    ULONG64         RingsUpTime;
    volatile LONG   IrqMode;            // 0 poll only, 1 irq on trial, 2 irq verified, 3 no irq seen -> poll, 4 storm -> poll
    volatile LONG   IsrUnclaimedRow;

    // ---- events / state (events.cpp)
    KSPIN_LOCK      StateLock;
    NWOA_BSS        Bss[NWOA_MAX_BSS];
    ULONG           ScanEvents, ScanRaw;
    volatile LONG   ScanRunning;
    LONG            ScanStatus;
    ULONG64         ScanDeadline;
    volatile LONG   ConnState;
    ULONG           ConnFailEvent, ConnFailStatus, ConnFailReason;
    KEVENT          ConnEvent;
    BOOLEAN         Connected;
    UCHAR           CurBssid[6];
    ULONG           LostReason;         // WDI_ASSOC_STATUS for unsolicited loss
    NWOA_EVREC      EvLog[NWOA_EVLOG];  // last non-scan firmware events (under RingLock)
    ULONG           EvLogNext;
    volatile LONG   CntEapolRx, CntEapolTx, CntRxHeld, CntEapolEv, CntEapolEvDrop, CntLinkUp;
#define NWOA_EVRX 8
    UCHAR           EvRxBuf[NWOA_EVRX][NWOA_SLOT_SIZE];
    ULONG           EvRxBusy;           // bit per EvRxBuf slot (under RingLock)
    ULONG           EapolEvFmt, EapolEvLen; // format/length of the last EAPOL event (no content)
    volatile LONG   RxNotifyPending;
    BOOLEAN         PtkInstalled;       // pairwise key in the firmware: RX frames are decrypted ones
    LONG            EapolRxMsg[8], EapolTxMsg[8];   // [0] other, [1..4] 4-way 1..4, [5] G1, [6] G2
    struct { ULONG Crc, Len; ULONG64 T; } EapolRecent[4];
    ULONG           EapolRecentNext;
    volatile LONG   CntEapolDup;
    volatile LONG   CntTxDropConv, CntTxDropSize, CntTxDropNotReady, CntTxDropOther;
    BOOLEAN         LinkUpReported;
    UCHAR           Mac[6];
    BOOLEAN         MacValid;
    BOOLEAN         RadioOn;

    // ---- WiFiCx (wifi.cpp)
    TLV_CONTEXT     Tlv;
    NETADAPTER      Adapter;
    NETPACKETQUEUE  TxQueue, RxQueue;
    volatile LONG   TxNotify, RxNotify;
    WDFWORKITEM     Work;
    volatile LONG   Jobs;
    NWOA_TASK       ScanTask, ConnTask, DiscTask, ResetTask, RadioTask, AckTask;
    UINT16          AckIndication;
    WDI_PORT_ID     StaPortId;          // port of the last station command (scan/connect/...)
    NWOA_CONNECT    Conn;
    UCHAR           ScanSsid[32];
    ULONG           ScanSsidLen;
    UCHAR           RadioWant;
    WIFIREQUEST     KeyRequest;
    NWOA_KEY        Keys[NWOA_MAX_KEYS];
    ULONG           NumKeys;
    BOOLEAN         FirstD0Done;
    BOOLEAN         ConnIsRoam;         // the connect job serves a WDI_TASK_ROAM

    // ---- disconnect evidence (diag.cpp writes it; under StateLock)
    NWOA_CMDREC     CmdLog[NWOA_CMDLOG];
    ULONG           CmdLogNext;
    struct { USHORT Id; LONG Count; } CmdHist[NWOA_CMDHIST];
    NWOA_CMDREC     CmdSnap[NWOA_CMDSNAP];  // last commands before the most recent link loss / our disassoc
    ULONG           CmdSnapN, CmdSnapMs, CmdSnapWhy; // why: 100 + firmware event type, or DIS_* source
    struct { ULONG Ms, Src; } HostDis[8];
    ULONG           HostDisNext;
    ULONG64         LastHostDisassoc;   // KeQueryInterruptTime, 0 = never
    NWOA_LOSSREC    Loss[8];
    ULONG           LossNext;
    LONG            RssiLast, RssiMin, RssiMax;
    ULONG           LqReported;
    volatile LONG   CntLinkStateInd, CntRoamStay, CntRoamMove, CntConnWhileConn, CntD0Entry, CntD0Exit;

    // ---- statistics
    volatile LONG   CntIsr, CntPoll, CntMsgs, CntEvents, CntRx, CntRxDrop, CntTx, CntTxStatus,
                    CntTxDrop, CntIoctl, CntIoctlTimeout, CntUnknown, CntMbData, CntScans,
                    CntRxAsEvent, CntTx80211;
    ULONG           LastScanBss;
    NTSTATUS        LastStatus;
    CHAR            Stage[48];
} NWOA_DEVICE, *PNWOA_DEVICE;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NWOA_DEVICE, NwoaGetDevice);

typedef struct _NWOA_ADAPTER_CTX {
    PNWOA_DEVICE Dev;
} NWOA_ADAPTER_CTX;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NWOA_ADAPTER_CTX, NwoaGetAdapterCtx);

typedef struct _NWOA_QUEUE_CTX {
    PNWOA_DEVICE Dev;
    NET_EXTENSION VaExt;
    BOOLEAN IsTx;
} NWOA_QUEUE_CTX;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(NWOA_QUEUE_CTX, NwoaGetQueueCtx);

// ---- driver.cpp
EVT_WDF_DRIVER_DEVICE_ADD                 NwoaEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE           NwoaEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE           NwoaEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY                   NwoaEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT                    NwoaEvtD0Exit;
NTSTATUS NwoaBringUp(PNWOA_DEVICE Dev);
void     NwoaShutdown(PNWOA_DEVICE Dev);
void     NwoaSetStage(PNWOA_DEVICE Dev, PCSTR Stage, NTSTATUS St);

// ---- diag.cpp (PASSIVE)
void NwoaReadConfig(PNWOA_DEVICE Dev);
void NwoaDiagSz(PNWOA_DEVICE Dev, PCWSTR Name, PCSTR Value);
void NwoaDiagDword(PNWOA_DEVICE Dev, PCWSTR Name, ULONG Value);
void NwoaDiagCounters(PNWOA_DEVICE Dev);
void NwoaDiagConsole(PNWOA_DEVICE Dev);
void NwoaDiagEvents(PNWOA_DEVICE Dev);
void NwoaDiagDisconnect(PNWOA_DEVICE Dev);
void NwoaHostDisassoc(PNWOA_DEVICE Dev, ULONG Src);    // NB_C_DISASSOC + who sent it
void NwoaCmdSnapshot(PNWOA_DEVICE Dev, ULONG Why);   // caller holds StateLock

// ---- hw.cpp
NTSTATUS NwoaMapResources(PNWOA_DEVICE Dev, WDFCMRESLIST Raw, WDFCMRESLIST Trans);
void     NwoaUnmapResources(PNWOA_DEVICE Dev);
NTSTATUS NwoaConfigInit(PNWOA_DEVICE Dev);
ULONG    NwoaCtlRead(PNWOA_DEVICE Dev, ULONG Off);
ULONG    NwoaCfgRead32(PNWOA_DEVICE Dev, ULONG Off);
void     NwoaCfgWrite32(PNWOA_DEVICE Dev, ULONG Off, ULONG Val);
ULONG    NwoaBpRead32(PNWOA_DEVICE Dev, ULONG Addr);
void     NwoaBpWrite32(PNWOA_DEVICE Dev, ULONG Addr, ULONG Val);
USHORT   NwoaBpRead16(PNWOA_DEVICE Dev, ULONG Addr);
void     NwoaWindowInvalidate(PNWOA_DEVICE Dev);
BOOLEAN  NwoaSelectPcie2(PNWOA_DEVICE Dev);
ULONG    NwoaRegRead(PNWOA_DEVICE Dev, ULONG Off);          // BAR0 + off (window = PCIe2 core)
void     NwoaRegWrite(PNWOA_DEVICE Dev, ULONG Off, ULONG Val);
ULONG    NwoaTcmRead32(PNWOA_DEVICE Dev, ULONG Off);
void     NwoaTcmWrite32(PNWOA_DEVICE Dev, ULONG Off, ULONG Val);
NTSTATUS NwoaTcmWriteBuf(PNWOA_DEVICE Dev, ULONG Off, const UCHAR* Buf, ULONG Len);
NTSTATUS NwoaTcmReadBuf(PNWOA_DEVICE Dev, ULONG Off, PUCHAR Buf, ULONG Len);
void     NwoaBusMaster(PNWOA_DEVICE Dev, BOOLEAN On);
void     NwoaFixDevCtl(PNWOA_DEVICE Dev);

// ---- chip.cpp
NTSTATUS NwoaChipAttach(PNWOA_DEVICE Dev);
const nb_core* NwoaCore(PNWOA_DEVICE Dev, USHORT Id, int Idx = 0);
void     NwoaChipPassive(PNWOA_DEVICE Dev);
void     NwoaChipActive(PNWOA_DEVICE Dev, ULONG ResetVector);
NTSTATUS NwoaChipResetAndOtp(PNWOA_DEVICE Dev);
NTSTATUS NwoaChipProbeLikeS257(PNWOA_DEVICE Dev);
NTSTATUS NwoaChipHalt(PNWOA_DEVICE Dev);

// ---- fw.cpp (PASSIVE)
NTSTATUS NwoaLoadFiles(PNWOA_DEVICE Dev, PUCHAR* Fw, ULONG* FwLen, PUCHAR* Nv, ULONG* NvLen);
NTSTATUS NwoaFirmwareBoot(PNWOA_DEVICE Dev);
void     NwoaFreeFiles(PNWOA_DEVICE Dev);
NTSTATUS NwoaReadConsoleTail(PNWOA_DEVICE Dev, PCHAR Out, ULONG Cap);

// ---- msgbuf.cpp
NTSTATUS NwoaDmaInit(PNWOA_DEVICE Dev);
void     NwoaDmaFree(PNWOA_DEVICE Dev);
NTSTATUS NwoaRingsInit(PNWOA_DEVICE Dev);
void     NwoaRingsStop(PNWOA_DEVICE Dev);
void     NwoaProcess(PNWOA_DEVICE Dev);                    // drains D2H rings (any IRQL <= DISPATCH)
NTSTATUS NwoaIoctl(PNWOA_DEVICE Dev, ULONG Cmd, PUCHAR Buf, ULONG Len, ULONG OutCap, ULONG* RetLen, LONG* FwStatus);
NTSTATUS NwoaSetInt(PNWOA_DEVICE Dev, ULONG Cmd, ULONG Val);
NTSTATUS NwoaGetInt(PNWOA_DEVICE Dev, ULONG Cmd, ULONG* Val);
NTSTATUS NwoaSetVar(PNWOA_DEVICE Dev, PCSTR Name, const void* Data, ULONG Len);
NTSTATUS NwoaSetVarInt(PNWOA_DEVICE Dev, PCSTR Name, ULONG Val);
NTSTATUS NwoaGetVar(PNWOA_DEVICE Dev, PCSTR Name, PUCHAR Out, ULONG Len);
NTSTATUS NwoaFlowringCreate(PNWOA_DEVICE Dev, const UCHAR* Da);
void     NwoaFlowringDelete(PNWOA_DEVICE Dev);
NTSTATUS NwoaTxFrame(PNWOA_DEVICE Dev, const UCHAR* Eth, ULONG Len);   // copy, post, ring
void     NwoaRxRepost(PNWOA_DEVICE Dev, USHORT Slot);                   // caller holds RingLock
void     NwoaRxFlushHeld(PNWOA_DEVICE Dev);
void     NwoaRxItemDone(PNWOA_DEVICE Dev, const NWOA_RXITEM* It);   // caller holds RingLock
BOOLEAN  NwoaEapolRxDup(PNWOA_DEVICE Dev, const UCHAR* Eth, ULONG Len); // caller holds RingLock
NTSTATUS NwoaInterruptCreate(PNWOA_DEVICE Dev);
void     NwoaIntrEnable(PNWOA_DEVICE Dev, BOOLEAN On);
EVT_WDF_TIMER NwoaEvtPollTimer;

// ---- events.cpp (DISPATCH, RingLock held)
void NwoaHandleEvent(PNWOA_DEVICE Dev, const UCHAR* Frame, ULONG Len);
void NwoaScanCacheClear(PNWOA_DEVICE Dev);
BOOLEAN NwoaScanCacheFind(PNWOA_DEVICE Dev, const UCHAR* Bssid, NWOA_BSS* Out);

// ---- wifi.cpp
EVT_WIFI_DEVICE_SEND_COMMAND            NwoaEvtWifiSendCommand;
EVT_WIFI_DEVICE_CREATE_ADAPTER          NwoaEvtWifiCreateAdapter;
EVT_WIFI_DEVICE_CREATE_WIFIDIRECTDEVICE NwoaEvtWifiCreateWifiDirect;
EVT_WDF_WORKITEM                        NwoaEvtWork;
NTSTATUS NwoaWifiSetCapabilities(PNWOA_DEVICE Dev);
NTSTATUS NwoaFirmwareConfigure(PNWOA_DEVICE Dev);
void     NwoaWifiPowerDown(PNWOA_DEVICE Dev);
void     NwoaQueueJob(PNWOA_DEVICE Dev, LONG Job);

// ---- datapath.cpp
EVT_NET_ADAPTER_CREATE_TXQUEUE NwoaEvtCreateTxQueue;
EVT_NET_ADAPTER_CREATE_RXQUEUE NwoaEvtCreateRxQueue;
NTSTATUS NwoaAdapterConfigure(PNWOA_DEVICE Dev, NETADAPTER Adapter);
void     NwoaRxDeliverNotify(PNWOA_DEVICE Dev);
void     NwoaTxSlotsFreed(PNWOA_DEVICE Dev);
void     NwoaReportLink(PNWOA_DEVICE Dev, BOOLEAN Up);
