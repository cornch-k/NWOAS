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
//   bit19 it is the BSSID of the last association (s258l), bits24-31 number of preferred entries.
//   WDI_TASK_DISCONNECT: bits0-15 802.11 reason, bit16 the peer is the current AP (s258l).
//   WDI_TASK_SCAN: bit0 directed, bits4-7 WDI_SCAN_TRIGGER (s258l).  Radio: software state.
//   WDI_GET_STATISTICS: bytes written (or needed) for the answer (s258l).
//   s258m: more per-command detail in NwoaScanDiag / NwoaDiscDiag / NwoaConnDiag / NwoaKeyDiag (diag.cpp).
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

// s258m diagnostic records (numbers and bits only: no SSID, BSSID, MAC or key values)
typedef struct _NWOA_SCANDIAG {
    ULONG Ms;                   // scan completion
    UCHAR Trigger, Conn, Present, Synth;   // WDI_SCAN_TRIGGER, connected, connected BSS found, added from beacon
    UCHAR Entries, Reported, SameSsid, Rank;   // BSS found, indicated, same-SSID entries, RSSI rank of the connected BSS (1 = best)
    SHORT Rssi, OtherRssi, FwRssi;   // connected BSS / strongest other same-SSID entry / firmware GET_RSSI (0 = none)
    UCHAR Channel, Band, OtherBand, Directed;
    UCHAR Skipped, Pad2[3];     // s258n: no firmware scan (ConnectedScan=1), cached list reported
    USHORT IeLen, Cap;          // connected BSS IE bytes, capability field
    USHORT Bw;                  // connected BSS bandwidth from its chanspec (MHz)
    USHORT Status;              // escan status
} NWOA_SCANDIAG;
#define NWOA_SCANDIAG_N 6

typedef struct _NWOA_DISCDIAG {
    ULONG Ms;
    USHORT Reason, PortId;      // full Disassociation80211Reason, WDI port id
    UCHAR PeerCur, Conn, Pad[2];
    ULONG SinceScanMs, SinceRxMs, SinceTxMs, SinceTxDoneMs, SinceLqMs;   // 0xFFFFFFFF = never
    LONG Rx10, Tx10, TxDone10;  // frames in the previous ~10 s (from the 2 s samples)
    ULONG SampleSpanMs;         // time the Rx10/Tx10 window really covers
} NWOA_DISCDIAG;
#define NWOA_DISCDIAG_N 4

#define NWOA_CANDMAX 8
typedef struct _NWOA_CONNDIAG {
    ULONG Ms;
    USHORT Id;                  // 6 connect / 100 roam (WDI_TASK_ROAM)
    UCHAR RoamRequest, Hidden, Mfp, BssTrans, NCand, NUc;
    ULONG RoamNeededReason, RoamTrigger;
    ULONG Auth0, Uc0, Mc0;      // first auth / unicast / multicast algorithm the host listed
    ULONG UcMask, McMask;       // bit n = WDI cipher n (n < 32) of every listed algorithm
    ULONG ApPwMask, ApGroup;    // RSN IE of the first candidate (from its frame or our cache): pairwise suite mask, group suite
    USHORT Cand[NWOA_CANDMAX];  // per candidate: bit0 current, 1 last association, 2 in scan cache, 3 5 GHz,
                                //   4 frame given by the host, bits8-15 channel
    SHORT CandRssi[NWOA_CANDMAX];   // RSSI from our scan cache (0 = not cached)
} NWOA_CONNDIAG;
#define NWOA_CONNDIAG_N 4

typedef struct _NWOA_KEYDIAG {
    ULONG Ms;
    UCHAR Add, Type, Elements, Accepted;  // 1 add / 0 delete, WDI_CIPHER_KEY_TYPE of the first element, count, accepted count
    ULONG Cipher;               // WDI_CIPHER_ALGORITHM of the first element (add)
    ULONG CipherMask;           // bit n = cipher n of every element
    ULONG Status;
} NWOA_KEYDIAG;
#define NWOA_KEYDIAG_N 8

// s258n: one TX stall watchdog record / one scan made while connected
typedef struct _NWOA_STALLREC {
    ULONG Ms;
    UCHAR Action, Pad[3];       // NB_SA_*
    ULONG Out, OldestMs, SinceDoneMs, SinceScanStartMs, SinceScanEndMs;   // 0xFFFFFFFF = never
    ULONG SinceRxMs, ConnAgeMs;  // last RX frame / time since the association
    ULONG FlowW, FlowR;         // host write pointer / firmware read index of the TX flowring
    LONG  Pm;                   // firmware PM after the action (-1 = not read)
} NWOA_STALLREC;
#define NWOA_STALL_N 8
typedef struct _NWOA_CSCAN {
    ULONG StartMs, EndMs;
    ULONG OutStart, OutEnd;     // TX outstanding at escan start / completion
    ULONG DoneDuring;           // TX statuses during the scan
    LONG  PmBefore, PmAfter;    // firmware PM (-1 = not read)
} NWOA_CSCAN;
#define NWOA_CSCAN_N 4
// s258o: what the TX flowring did after one firmware scan made while connected
typedef struct _NWOA_SFREC {
    ULONG Ms;                   // verdict time
    ULONG ScanStartMs, ScanEndMs;
    UCHAR Verdict, Self, Deferred, Cand;  // NB_SF_*; driver-started test scan; found by the 2 s tick;
                                          //   s258p candidate index during the scan (0xFF = no CandidateCycle)
    ULONG W0, R0;               // flowring host w / firmware r at scan end
    ULONG W1, R1;               // at the verdict
    ULONG AdvMs;                // ms from scan end to the first read index advance (0xFFFFFFFF = none)
    ULONG Db;                   // flowring doorbells rung between scan end and the verdict
    ULONG FwW, FwR;             // TCM copy of the flowring w / r index at the verdict (0xFFFFFFFF = not read)
    ULONG FwW0, HostW0;         // TCM copy / host value of the control submit ring w index (same moment)
    LONG  Fix;                  // NTSTATUS of the flowring re-create (0 = none)
    UCHAR Bits, NCh;            // s258p: NB_CAND_* in force during the scan, chanspecs in the escan (0 = full list)
    UCHAR Shape, Chunks;        // s258q: test-scan shape (0xFF = a Windows scan), escans in the sequence (shape 5)
    USHORT Ev124, Ev120;        // s258q: firmware events 124 (CCA_CHAN_QUAL) / 120 (MSCH) from scan start to verdict
} NWOA_SFREC;
#define NWOA_SF_N 8
#define NWOA_SF_ALIVE      1    // the firmware took the items posted after the scan by itself
#define NWOA_SF_DBFIX      2    // not taken; taken after one more w index write + doorbell
#define NWOA_SF_REFRESH    3    // not taken after that either; flowring deleted + created
#define NWOA_SF_REFRESHERR 4    // re-create failed (the 8 s watchdog stays)
#define NWOA_SF_STUCKREC   5    // not taken, ScanFlowCheck=3: recorded only
#define NWOA_SF_IDLE       6    // nothing posted to judge (scan end + 1 s, then 30 s of 2 s ticks)
#define NWOA_SF_FORCED     7    // ScanFlowCheck=2: re-created without looking
#define NWOA_SF_GONE       8    // link or flowring gone while watching
#define NWOA_SF_RSMFIX     9    // s258p: not taken; taken after FLOW_RING_RESUME (NB_CAND_RSM / FlowResume=1)
#define NWOA_SF_IDLE_MS    1000
#define NWOA_CONLIVE 3000
#define NWOA_EVDIAG_N 8

// who sent NB_C_DISASSOC (NwoaHostDisassoc Src)
#define DIS_CONNECT     1       // WDI_TASK_CONNECT while connected / joining
#define DIS_ROAM        2       // WDI_TASK_ROAM to another BSS
#define DIS_DISCONNECT  3       // WDI_TASK_DISCONNECT
#define DIS_RESET       4       // WDI_TASK_DOT11_RESET
#define DIS_RADIO_OFF   5       // WDI_TASK_SET_RADIO_STATE off
#define DIS_CONN_FAIL   6       // failed connect clean-up
#define DIS_SHUTDOWN    7       // D0 exit / release
#define DIS_TX_STALL    8       // s258n: TX stall watchdog gave up, link loss reported to Windows

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
    ULONG  StatsReply;          // 1 = answer WDI_GET_STATISTICS (34) with MAC+PHY statistics TLVs (default, s258l);
                                //     0 = STATUS_NOT_SUPPORTED as up to s258k
    // ---- s258m (each 0 = s258l behaviour unless noted)
    ULONG  ScanReportConnectedOnly; // H1 test.  Scans made while connected: 0 report every BSS (default),
                                //     1 report only the connected BSS, 2 every BSS and the connected one too.
                                //     With 1/2 a connected BSS the scan did not find is added from the
                                //     association beacon (age info "cached").
    ULONG  NoDisassocIndOnHostDisconnect; // H3 test.  1 = no DISASSOCIATION indication after a
                                //     WDI_TASK_DISCONNECT (the spec says send one; default 0 sends it)
    ULONG  BssAgeInfo;          // 1 = BSS entries carry WDI_TLV_BSS_ENTRY_AGE_INFO (default); 0 = as s258l
    ULONG  LinkInfoReal;        // 1 = link Bandwidth / speed from the firmware chanspec and rate, channel
                                //     width capability 20/40(/80) (default); 0 = 20 MHz, 433/144 Mbps as s258l
    ULONG  ReportWmm;           // 1 = WMMQoSEnabled from the association response; 0 = FALSE as s258l (default)
    // ---- s258n (data path stall after the scan made while connected; 0 = s258m behaviour unless noted)
    ULONG  ConnectedScan;       // 1 = no firmware scan while connected: the task completes at once with the
                                //     cached list + the connected BSS (s258n default); 0 = firmware escan
                                //     (s258o default, with ScanFlowCheck)
    ULONG  PmKickAfterScan;     // 1 = SET_PM 1 then PowerSave after a firmware scan made while connected (default 0)
    ULONG  TxStallMs;           // TX status stall threshold in ms (default 8000; 0 = detector off)
    ULONG  TxStallRecover;      // 0 record only, 1 + PM kick and flowring re-create, 2 + link loss to Windows (default)
    ULONG  MbPoll;              // 1 = read MAILBOXINT and the D2H mailbox word every 2 s, record only (default)
    ULONG  ConsoleLive;         // 1 = copy new firmware console lines every 2 s, masked (default)
    ULONG  DiagEvents;          // 1 = enable and count 8 extra firmware events, 2 = every event type except the
                                //     frequent 15/30/44/52/58 (default), all counted per type; 0 = s258m mask
    // ---- s258o (firmware scans while connected again: ConnectedScan default 0)
    ULONG  ScanFlowCheck;       // after a firmware scan made while connected, watch the TX flowring read index:
                                //     0 off (s258n), 1 = if posted items are not taken within ScanFlowProbeMs:
                                //     doorbell again, then flowring re-create (default); 2 = always re-create;
                                //     3 = watch and record only (no action; the 8 s watchdog stays)
    ULONG  ScanFlowProbeMs;     // 1: time posted items may wait at the firmware after the scan (default 300)
    ULONG  ScanTxHold;          // 1 = no TX_POST while a connected firmware scan runs (frames wait in the OS ring)
    ULONG  TestConnScanSec;     // test: driver-started directed scan (connected SSID) every N s while connected
                                //     (0 = off, default; 60..3600)
    // ---- s258p (Linux brcmfmac differences, each 0 = s258o behaviour)
    ULONG  ScanPm;              // 2 = SET_PM 2 + pm2_sleep_ret 2000 once associated (brcmfmac default power save)
    ULONG  ScanChanList;        // scans made while connected: 0 no list (firmware full list), 1 valid channels of
                                //     the current band, 2 the current channel only
    ULONG  FlowResume;          // 1 = flowring not taken after a connected scan: FLOW_RING_RESUME before the doorbell
    ULONG  InitIovarSet;        // bitmask, firmware init as brcmfmac: 1 txbf 1, 2 arp_ol 9 + arpoe 1 + ndoe 1,
                                //     4 FAKEFRAG 1, 8 bcn_timeout 4, 16 obss_coex -1, 32 mpc 1
    ULONG  CandidateCycle;      // 1 = rotate the nb_cand_bits() candidates, one per connected scan; forces
                                //     ScanFlowCheck 1 and TestConnScanSec 120 when it is 0
    // ---- s258q (shape of the driver test scan, TestConnScanSec; Windows scans unchanged)
    ULONG  TestScanShape;       // NB_SHAPE_* 0..5 (0 = s258p test scan, default)
    ULONG  ShapeCycle;          // 1 = rotate shapes 0..5, one per test-scan verdict; forces ScanFlowCheck 1,
                                //     CandidateCycle 0 and TestConnScanSec 120 when it is 0
    ULONG  ShapeChunkN;         // shape 5: channels per escan (1..16, default 3)
    ULONG  ShapeChunkGapMs;     // shape 5: gap after each escan before the next (1000..10000, default 2000;
                                //     the 2 s diag tick starts the next one)
    ULONG  ShapeLongMs;         // shape 2: active dwell on the home channel (100..10000, default 3000)
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
    // ---- WDI_GET_STATISTICS (s258l)
    volatile LONG   CntStatsOk, CntStatsFail, CntStatsSmall;
    ULONG           StatsLastBytes;     // bytes written for the last answer (header + TLVs)
    ULONG           StatsLastStatus;
    UCHAR           LastConnBssid[6];   // BSSID of the last successful association (memory only, never logged)
    BOOLEAN         HaveLastConn;
    // ---- s258m diagnostics (under StateLock; written to the registry by the 2 s diag job)
    UCHAR           ScanTrigger, ScanDirected;
    ULONG64         LastScanDone, LastRxTime, LastTxTime, LastTxDoneTime, LastLqInd;   // KeQueryInterruptTime, 0 = never
    struct { ULONG Ms; LONG Rx, Tx, TxDone; } Samp[8];  // counters every diag tick
    ULONG           SampNext;
    NWOA_SCANDIAG   ScanDiag[NWOA_SCANDIAG_N];  ULONG ScanDiagNext;
    NWOA_DISCDIAG   DiscDiag[NWOA_DISCDIAG_N];  ULONG DiscDiagNext;
    NWOA_CONNDIAG   ConnDiag[NWOA_CONNDIAG_N];  ULONG ConnDiagNext;
    NWOA_KEYDIAG    KeyDiag[NWOA_KEYDIAG_N];    ULONG KeyDiagNext;
    ULONG           LinkBwMhz, LinkRateKbps;    // from the firmware at association (0 = unknown)
    ULONG           FwChanspec, FwRate500k;     // raw values read (diagnostics)
    BOOLEAN         WmmNegotiated;              // WMM parameter element in the association response
    volatile LONG   CntScanSynth, CntDisassocIndSkipped;
    // ---- s258n
    ULONG64         TxPostTime[NWOA_TX_SLOTS];  // KeQueryInterruptTime of the TX_POST (under TxLock)
    ULONG           TxMaxOut;                   // most TX slots in flight at once since rings up
    ULONG           RxPend[NWOA_RX_SLOTS_MAX / 32];   // RX buffers whose repost failed (retried, RingLock)
    ULONG           CtlPend;                    // bit 0-7 ioctl response, 8-15 event buffer whose post failed
    volatile LONG   CntRxRepostFail, CntRxRepostRetry, CntCtlPostFail, CntCtlPostRetry;
    volatile LONG   CntTxStatusNz, CntGenStatus, CntRingStatus;
    LONG            TxStatusLast, GenStatusLast, RingStatusLast;
    ULONG           RingStatusRing;
    ULONG           MbIntLast, DtohMbLast;
    volatile LONG   CntDtohMbNz, CntMbIntNz;
    LONG            FwPm;                       // firmware PM read back (-1 = never read)
    BOOLEAN         ScanSkipped;                // the current scan task was answered from the cache
    BOOLEAN         ScanWhileConn;              // the running firmware escan started while connected
    ULONG64         ConnScanStart, ConnScanEnd; // last firmware scan made while connected (0 = none)
    NWOA_CSCAN      CScan[NWOA_CSCAN_N];  ULONG CScanNext;
    ULONG           CScanDoneMark;
    volatile LONG   CntScanSkipped, CntPmKick;
    nb_stall        StallSm;                    // watchdog state (work item only)
    NWOA_STALLREC   Stall[NWOA_STALL_N];  ULONG StallNext;
    volatile LONG   CntTxStall, CntStallPmOk, CntStallFlowOk, CntStallLinkLoss, CntStallGiveUp;
    ULONG           ConIdx;                     // firmware console ring read position
    BOOLEAN         ConInit;
    CHAR            ConLine[512];  ULONG ConLineLen;   // partial line not masked yet
    CHAR            ConBuf[NWOA_CONLIVE + 1];  ULONG ConLen;
    ULONG           ConBytes, ConSkipped;
    BOOLEAN         ConDirty;
    LONG            EvDiagCnt[NWOA_EVDIAG_N];
    ULONG           EvDiagLastMs[NWOA_EVDIAG_N];
    LONG            EvAllCnt[NB_E_LAST + 1];    // DiagEvents=2: every counted-only event type
    ULONG           EvAllMs[NB_E_LAST + 1];
    ULONG64         ConnUpTime;                 // KeQueryInterruptTime of the last successful association
    BOOLEAN         FwCntBaseDone;
    // ---- s258o
    volatile LONG   CntDbell, CntFlowDbell;     // H2D doorbells rung (all rings / TX flowring commits)
    volatile LONG   CntConnScan, CntSelfScan;   // firmware scans started while connected / by the test timer
    volatile LONG   CntSfAlive, CntSfStuck, CntSfIdle, CntSfDbFix, CntSfRefresh, CntSfRefreshFail, CntSfForced;
    volatile LONG   CntTxHeld, CntScanDeferred;
    volatile LONG   TxHold;                     // ScanTxHold: TX_POST refused (busy) while 1
    BOOLEAN         SelfScan;                   // the running escan was started by TestConnScanSec
    BOOLEAN         ScanStartDeferred;          // a Windows scan task waited for a test scan
    BOOLEAN         SfArmed;                    // 2 s tick still watches the flowring after an idle verdict
    ULONG64         SfArmUntil;
    nb_flowwatch    SfWatch;                    // that watch (work item only)
    NWOA_SFREC      SfArmRec;                   // its record so far
    ULONG64         LastConnScanAny;            // last firmware scan start while connected (Windows or test)
    NWOA_SFREC      Sf[NWOA_SF_N];  ULONG SfNext;
    // ---- s258p
    ULONG           CandCur;                    // CandidateCycle: index of the active candidate
    ULONG           CandOn;                     // NB_CAND_* bits applied to the firmware now (PM2 / IOV)
    BOOLEAN         CandAdvance;                // a verdict was recorded: next candidate at the next service
    UCHAR           ScanCand, ScanBits, ScanNCh;   // candidate / bits / list size of the running escan
    volatile LONG   CntCandAdv;
    LONG            CandN[NB_CAND_N], CandAlive[NB_CAND_N], CandStuck[NB_CAND_N], CandIdle[NB_CAND_N],
                    CandGone[NB_CAND_N], CandDb[NB_CAND_N], CandRsm[NB_CAND_N], CandRef[NB_CAND_N],
                    CandErr[NB_CAND_N];   // escan refused (no verdict)
    ULONG           CandPmSt;                   // status bits of the last PM2 apply / revert
    ULONG           IovOld[7], IovOldOk, IovSetOk, IovBackOk;   // IOV candidate: old values, which read / set / put back
    ULONG           InitIovSt;                  // InitIovarSet: bit per iovar set successfully
    ULONG           ValidCh[NB_SCAN_CHLIST_MAX], NValidCh;  LONG ValidChSt;   // WLC_GET_VALID_CHANNELS: 0 not read, 1 read, else NTSTATUS
    volatile LONG   CntRsmSent, CntRsmCmplt, CntSfRsmFix;
    LONG            RsmLastStatus;  ULONG RsmLastRing;
    // ---- s258q
    ULONG           ShapeCur;                   // ShapeCycle: index of the active shape
    BOOLEAN         ShapeAdvance;               // a test-scan verdict was recorded: next shape at the next scan
    UCHAR           ScanShape;                  // shape of the running escan (0xFF = Windows scan / not connected)
    UCHAR           ShapeFch;                   // shape 1: foreign channel of the last one
    volatile LONG   CntShapeAdv;
    // per shape, index NB_SHAPE_N = Windows scans made while connected
    LONG            ShN[NB_SHAPE_N + 1], ShAlive[NB_SHAPE_N + 1], ShStuck[NB_SHAPE_N + 1], ShIdle[NB_SHAPE_N + 1],
                    ShGone[NB_SHAPE_N + 1], ShErr[NB_SHAPE_N + 1], ShDb[NB_SHAPE_N + 1], ShRef[NB_SHAPE_N + 1],
                    ShAbort[NB_SHAPE_N + 1], ShGapStuck[NB_SHAPE_N + 1], ShFallback[NB_SHAPE_N + 1];
    ULONG           ShMsSum[NB_SHAPE_N + 1], ShMsMax[NB_SHAPE_N + 1], ShEv124[NB_SHAPE_N + 1], ShEv120[NB_SHAPE_N + 1];
    // shape 5 sequence (work item only)
    BOOLEAN         ChunkActive;
    UCHAR           ChunkIdx, ChunkTotal, ChunkStuckAt; // next escan / escans in all / gap after which posted
                                                        //   items were not taken (0xFF none)
    USHORT          ChunkCh[NB_SCAN_CHLIST_MAX];  ULONG ChunkNCh;
    ULONG64         ChunkNextAt;
    ULONG           ChunkW, ChunkR, ChunkStartMs, ChunkLastEndMs;
    LONG            ScanEvBase124, ScanEvBase120;   // EvAllCnt at the start of the scan (sequence)
    // event 124 (WLC_E_CCA_CHAN_QUAL), written at DISPATCH by the event handler
    volatile LONG   CntEv124, CntEv124Scan, CntEv124Bad;
    LONG            Ev124Id[4];                 // id 0 CCA, 1 noise, 2 noise LTE, other
    nb_cca_ev       Ev124Last;
    ULONG           Ev124LastMs, Ev124LastDl;
    UCHAR           Ev124First[24];  ULONG Ev124FirstLen, Ev124FirstDl, Ev124FirstMs;
    USHORT          Ev124Cs[32];  ULONG Ev124NCs;  // distinct chanspecs during the last test scan (sequence)

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
void NwoaDiagS258n(PNWOA_DEVICE Dev, PCHAR B, SIZE_T Cap);
void NwoaDiagS258o(PNWOA_DEVICE Dev, PCHAR B, SIZE_T Cap);
void NwoaDiagS258p(PNWOA_DEVICE Dev, PCHAR B, SIZE_T Cap);
void NwoaDiagS258q(PNWOA_DEVICE Dev, PCHAR B, SIZE_T Cap);
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
void     NwoaConsoleLiveTick(PNWOA_DEVICE Dev);                        // s258n

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
NTSTATUS NwoaFlowResume(PNWOA_DEVICE Dev);                  // s258p: FLOW_RING_RESUME for the TX flowring
void     NwoaFlowringDelete(PNWOA_DEVICE Dev);
NTSTATUS NwoaTxFrame(PNWOA_DEVICE Dev, const UCHAR* Eth, ULONG Len);   // copy, post, ring
void     NwoaRxRepost(PNWOA_DEVICE Dev, USHORT Slot);                   // caller holds RingLock
void     NwoaRxFlushHeld(PNWOA_DEVICE Dev);
// s258n (msgbuf.cpp)
ULONG    NwoaTxOutstanding(PNWOA_DEVICE Dev, ULONG64* OldestPost);    // TX slots in flight (TxLock)
void     NwoaRingSnapshot(PNWOA_DEVICE Dev, PCHAR B, SIZE_T Cap, ULONG* FlowW, ULONG* FlowR);
void     NwoaPoolSnapshot(PNWOA_DEVICE Dev, PCHAR B, SIZE_T Cap);
NTSTATUS NwoaFlowringRecreate(PNWOA_DEVICE Dev);
void     NwoaMbPollTick(PNWOA_DEVICE Dev);
// s258o (msgbuf.cpp)
BOOLEAN  NwoaFlowIdx(PNWOA_DEVICE Dev, ULONG* W, ULONG* R);         // FALSE = no flowring
void     NwoaFlowKick(PNWOA_DEVICE Dev);                             // rewrite the flowring w index + doorbell
void     NwoaFwIdxView(PNWOA_DEVICE Dev, ULONG Slot, ULONG* FwW, ULONG* FwR);   // TCM copies (0xFFFFFFFF = n/a)
ULONG    NwoaHostWIdx(PNWOA_DEVICE Dev, ULONG Slot);
void     NwoaRxItemDone(PNWOA_DEVICE Dev, const NWOA_RXITEM* It);   // caller holds RingLock
BOOLEAN  NwoaEapolRxDup(PNWOA_DEVICE Dev, const UCHAR* Eth, ULONG Len); // caller holds RingLock
NTSTATUS NwoaInterruptCreate(PNWOA_DEVICE Dev);
void     NwoaIntrEnable(PNWOA_DEVICE Dev, BOOLEAN On);
EVT_WDF_TIMER NwoaEvtPollTimer;

// ---- events.cpp (DISPATCH, RingLock held)
void NwoaHandleEvent(PNWOA_DEVICE Dev, const UCHAR* Frame, ULONG Len);
extern const ULONG NwoaDiagEventTypes[NWOA_EVDIAG_N];                  // s258n extra events (counted only)
BOOLEAN NwoaEventIsBase(ULONG Type);                                    // handled by NwoaHandleEvent itself
BOOLEAN NwoaEventIsNoisy(ULONG Type);                                   // never enabled for counting
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
