// fw.cpp - firmware files, NVRAM, TCM image, CR4 start, shared RAM.
// Sequence = bwfm_pci_load_microcode (OpenBSD, ISC) as run on hardware by
// nwoas_scripts/wifi-s257/wifi_up.py (PASS_FW_UP 2026-11-18 11:36).
//
// Files (all in the driver package, copied next to nwoa4378.sys):
//   atlantisb-X3.trx  atlantisb-X3.clmb  atlantisb-X3.txcb       macOS C-4378__s-B1
//   P-atlantisb_M-RASP_V-m.txt  P-atlantisb_M-RASP_V-u.txt        NVRAM, picked by OTP V=
//   nwoas-bcm4378-board.bin                                       ADT cal + MAC (tools/make_board_blob.py)
#include "precomp.h"

static const WCHAR* kFwName    = L"atlantisb-X3.trx";
static const WCHAR* kClmName   = L"atlantisb-X3.clmb";
static const WCHAR* kTxcapName = L"atlantisb-X3.txcb";
static const WCHAR* kBoardName = L"nwoas-bcm4378-board.bin";

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
    if (Dir)
        st = RtlStringCchPrintfW(path, ARRAYSIZE(path), L"%s\\%s", Dir, Name);
    else
        st = RtlStringCchCopyW(path, ARRAYSIZE(path), Name);
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
        PUCHAR b = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, n + 4, NWOA_TAG);
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
                ExFreePoolWithTag(b, NWOA_TAG);
            }
        }
    }
    ZwClose(h);
    return st;
}

// Look in: registry FirmwareDir, the driver store directory of nwoa4378.sys,
// \SystemRoot\System32\drivers\nwoa4378.
static NTSTATUS ReadFw(PNWOA_DEVICE Dev, PCWSTR Name, PUCHAR* Out, ULONG* Len, ULONG Max)
{
    NTSTATUS st = STATUS_OBJECT_NAME_NOT_FOUND;
    if (Dev->Cfg.FirmwareDir[0]) {
        st = ReadFileFrom(NULL, Dev->Cfg.FirmwareDir, Name, Out, Len, Max);
        if (NT_SUCCESS(st))
            return st;
    }
    {
        HANDLE dir = NULL;
        PDRIVER_OBJECT drv = WdfDriverWdmGetDriverObject(WdfGetDriver());
        if (NT_SUCCESS(IoGetDriverDirectory(drv, DriverDirectoryImage, 0, &dir))) {
            st = ReadFileFrom(dir, NULL, Name, Out, Len, Max);
            ZwClose(dir);
            if (NT_SUCCESS(st))
                return st;
        }
    }
    return ReadFileFrom(NULL, L"\\SystemRoot\\System32\\drivers\\nwoa4378", Name, Out, Len, Max);
}

// Firmware, NVRAM, CLM, txcap and board data are read from disk once (first PrepareHardware)
// and kept in non-paged pool: D0Entry after a sleep re-uses them without file I/O.
void NwoaFreeFiles(PNWOA_DEVICE Dev)
{
    if (Dev->FwImg) { ExFreePoolWithTag(Dev->FwImg, NWOA_TAG); Dev->FwImg = NULL; }
    if (Dev->NvTxt) { ExFreePoolWithTag(Dev->NvTxt, NWOA_TAG); Dev->NvTxt = NULL; }
    if (Dev->Clm)   { ExFreePoolWithTag(Dev->Clm, NWOA_TAG);   Dev->Clm = NULL; }
    if (Dev->Txcap) { ExFreePoolWithTag(Dev->Txcap, NWOA_TAG); Dev->Txcap = NULL; }
    if (Dev->Board) { ExFreePoolWithTag(Dev->Board, NWOA_TAG); Dev->Board = NULL; }
    RtlZeroMemory(&Dev->BoardInfo, sizeof(Dev->BoardInfo));
}

NTSTATUS NwoaLoadFiles(PNWOA_DEVICE Dev, PUCHAR* Fw, ULONG* FwLen, PUCHAR* Nv, ULONG* NvLen)
{
    NTSTATUS st;
    char name[64];
    WCHAR wname[64];

    if (Dev->FwImg && Dev->NvTxt && Dev->Clm && Dev->Txcap) {
        *Fw = Dev->FwImg; *FwLen = Dev->FwImgLen;
        *Nv = Dev->NvTxt; *NvLen = Dev->NvTxtLen;
        NwoaDiagDword(Dev, L"NwoaFilesCached", 1);
        return STATUS_SUCCESS;
    }
    NwoaFreeFiles(Dev);
    st = ReadFw(Dev, kFwName, Fw, FwLen, NWOA_FW_MAX);
    if (NT_SUCCESS(st)) { Dev->FwImg = *Fw; Dev->FwImgLen = *FwLen; }
    NwoaDiagDword(Dev, L"NwoaFwFileStatus", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;
    NwoaDiagDword(Dev, L"NwoaFwFileLen", *FwLen);
    if (nb_nvram_name(name, sizeof(name), "atlantisb", &Dev->Otp) != NB_OK)
        return STATUS_DEVICE_DATA_ERROR;
    if (strcmp(Dev->Otp.module, "RASP") != 0 || (strcmp(Dev->Otp.vendor, "m") && strcmp(Dev->Otp.vendor, "u")))
        return STATUS_NOT_SUPPORTED;                       // only the two J274 NVRAM files are shipped
    RtlStringCchCopyA(Dev->NvramName, sizeof(Dev->NvramName), name);
    NwoaDiagSz(Dev, L"NwoaNvramFile", name);
    for (int i = 0; i < 64; i++) {
        wname[i] = (WCHAR)(UCHAR)name[i];
        if (!name[i])
            break;
    }
    st = ReadFw(Dev, wname, Nv, NvLen, 64 * 1024);
    if (NT_SUCCESS(st)) { Dev->NvTxt = *Nv; Dev->NvTxtLen = *NvLen; }
    NwoaDiagDword(Dev, L"NwoaNvramFileStatus", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;
    st = ReadFw(Dev, kClmName, &Dev->Clm, &Dev->ClmLen, 512 * 1024);
    NwoaDiagDword(Dev, L"NwoaClmStatus", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;
    st = ReadFw(Dev, kTxcapName, &Dev->Txcap, &Dev->TxcapLen, 64 * 1024);
    NwoaDiagDword(Dev, L"NwoaTxcapStatus", (ULONG)st);
    if (!NT_SUCCESS(st))
        return st;
    if (Dev->UseCtlPage) {
        // S259 contract 3: ADT values from the control page (MAC 0x60/0x64, CAL_LEN 0x68,
        // antenna 0x6C, board 0x70..0x7C, calibration bytes at 0x1000)
        char board[17], ant[5];
        UCHAR mac[6];
        ULONG lo = NwoaCtlRead(Dev, 0x60), hi = NwoaCtlRead(Dev, 0x64), cl = NwoaCtlRead(Dev, 0x68);
        nb_st32(ant, NwoaCtlRead(Dev, 0x6C));
        ant[4] = 0;
        for (ULONG i = 0; i < 4; i++)
            nb_st32(board + 4 * i, NwoaCtlRead(Dev, 0x70 + 4 * i));
        board[16] = 0;
        NwoaDiagSz(Dev, L"NwoaCtlBoard", board);
        NwoaDiagSz(Dev, L"NwoaCtlAntenna", ant);
        NwoaDiagDword(Dev, L"NwoaCtlCalLen", cl);
        if (strcmp(board, "atlantisb") != 0 || strcmp(ant, "X3") != 0)
            return STATUS_NOT_SUPPORTED;                   // the package only has atlantisb-X3 files
        nb_st32(mac, lo);
        mac[4] = (UCHAR)hi;
        mac[5] = (UCHAR)(hi >> 8);
        BOOLEAN haveMac = (lo | (hi & 0xFFFF)) != 0;
        if (cl > 4096)
            cl = 0;
        PUCHAR cal = cl ? (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, nb_align(cl, 4), NWOA_TAG) : NULL;
        PUCHAR blob = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, NB_BOARD_HDR + nb_align(cl, 4), NWOA_TAG);
        if (blob && (cal || !cl)) {
            for (ULONG i = 0; i < cl; i += 4)
                nb_st32(cal + i, NwoaCtlRead(Dev, 0x1000 + i));
            ULONG n = nb_board_build(blob, NB_BOARD_HDR + nb_align(cl, 4), haveMac ? mac : NULL, cal, cl);
            if (n && nb_board_parse(blob, n, &Dev->BoardInfo) == NB_OK) {
                Dev->Board = blob;
                Dev->BoardLen = n;
                blob = NULL;
                NwoaDiagDword(Dev, L"NwoaBoardSource", 2);   // control page
            }
        }
        if (cal) ExFreePoolWithTag(cal, NWOA_TAG);
        if (blob) ExFreePoolWithTag(blob, NWOA_TAG);
        RtlSecureZeroMemory(mac, sizeof(mac));
    }
    if (!Dev->Board) {
        st = ReadFw(Dev, kBoardName, &Dev->Board, &Dev->BoardLen, 128 * 1024);
        NwoaDiagDword(Dev, L"NwoaBoardStatus", (ULONG)st);
        if (NT_SUCCESS(st) && nb_board_parse(Dev->Board, Dev->BoardLen, &Dev->BoardInfo) != NB_OK) {
            NwoaDiagSz(Dev, L"NwoaBoard", "bad board file (crc/format)");
            RtlZeroMemory(&Dev->BoardInfo, sizeof(Dev->BoardInfo));
        } else if (NT_SUCCESS(st)) {
            NwoaDiagDword(Dev, L"NwoaBoardSource", 1);       // package file
        }
    }
    NwoaDiagDword(Dev, L"NwoaCalLen", Dev->BoardInfo.cal_len);
    NwoaDiagDword(Dev, L"NwoaBoardMac", Dev->BoardInfo.have_mac);    // presence only, never the value
    return STATUS_SUCCESS;                                 // board file optional (no cal, firmware MAC)
}

static void FreeBuf(PUCHAR b) { if (b) ExFreePoolWithTag(b, NWOA_TAG); }

NTSTATUS NwoaFirmwareBoot(PNWOA_DEVICE Dev)
{
    PUCHAR fw = NULL, nvtxt = NULL, nv = NULL;
    ULONG fwlen = 0, nvtxtlen = 0, nvlen = 0, entries = 0;
    NTSTATUS st;
    ULONG ramsize;
    nb_ramlayout* L = &Dev->Ram;

    Dev->FwUp = FALSE;
    st = NwoaLoadFiles(Dev, &fw, &fwlen, &nvtxt, &nvtxtlen);
    if (!NT_SUCCESS(st))
        goto out;
    nv = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, nvtxtlen + 64, NWOA_TAG);
    if (!nv) { st = STATUS_INSUFFICIENT_RESOURCES; goto out; }
    if (nb_nvram_convert(nvtxt, nvtxtlen, Dev->BoardInfo.have_mac ? Dev->BoardInfo.mac : NULL,
                         nv, nvtxtlen + 64, &nvlen, &entries) != NB_OK) {
        st = STATUS_DEVICE_DATA_ERROR;
        goto out;
    }
    NwoaDiagDword(Dev, L"NwoaNvramBlobLen", nvlen);
    ramsize = nb_fw_ramsize_override(fw, fwlen);
    if (ramsize)
        Dev->RamSize = ramsize;
    if (nb_ram_layout(Dev->RamBase, Dev->RamSize, fwlen, nvlen, L) != NB_OK || L->top > Dev->Bar2Len) {
        st = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto out;
    }
    // image into TCM, then read back and compare
    st = NwoaTcmWriteBuf(Dev, L->rambase, fw, fwlen);
    if (!NT_SUCCESS(st))
        goto out;
    for (ULONG i = 0; Dev->Cfg.VerifyFirmware && i + 4 <= fwlen; i += 4) {
        if (Dev->Cfg.VerifyFirmware == 1 && i >= 4096 && i + 4096 < fwlen && (i & 1023))
            continue;                                      // sparse: 1 word in 256 + both ends
        if (NwoaTcmRead32(Dev, L->rambase + i) != nb_ld32(fw + i)) {
            NwoaDiagDword(Dev, L"NwoaFwMismatchAt", i);
            st = STATUS_DEVICE_HARDWARE_ERROR;
            goto out;
        }
    }
    {
        UCHAR seed[NB_SEED_LEN];
        UCHAR footer[8];
        if (!NT_SUCCESS(BCryptGenRandom(NULL, seed, sizeof(seed), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            LARGE_INTEGER pc = KeQueryPerformanceCounter(NULL);
            ULONG s = pc.LowPart ^ 0x5257u;
            for (ULONG i = 0; i < sizeof(seed); i++) {
                s = s * 1664525u + 1013904223u;          // fallback only (BCrypt failed)
                seed[i] = (UCHAR)(s >> 24);
            }
        }
        NwoaTcmWrite32(Dev, L->shared_ptr, 0);
        NwoaTcmWriteBuf(Dev, L->nvram, nv, nvlen);
        nb_st32(footer, NB_SEED_LEN);
        nb_st32(footer + 4, NB_SEED_MAGIC);
        NwoaTcmWriteBuf(Dev, L->footer, footer, 8);
        NwoaTcmWriteBuf(Dev, L->seed, seed, sizeof(seed));
        RtlSecureZeroMemory(seed, sizeof(seed));
    }
    for (ULONG i = 0; i + 4 <= nvlen; i += 4) {
        if (NwoaTcmRead32(Dev, L->nvram + i) != nb_ld32(nv + i)) {
            st = STATUS_DEVICE_HARDWARE_ERROR;
            goto out;
        }
    }
    {
        ULONG before = NwoaTcmRead32(Dev, L->shared_ptr), shared = before;
        LARGE_INTEGER t;
        NwoaChipActive(Dev, nb_ld32(fw));
        NwoaSetStage(Dev, "cr4_started", STATUS_SUCCESS);
        t.QuadPart = -50 * 10000;                          // 50 ms
        for (int i = 0; i < 100; i++) {                    // 5 s (brcmfmac FW_UP_TIMEOUT)
            KeDelayExecutionThread(KernelMode, FALSE, &t);
            shared = NwoaTcmRead32(Dev, L->shared_ptr);
            if (shared != before)
                break;
        }
        NwoaDiagDword(Dev, L"NwoaSharedAddr", shared);
        if (shared == before) { st = STATUS_IO_TIMEOUT; goto out; }
        if (shared < L->rambase || shared >= L->top) { st = STATUS_DEVICE_DATA_ERROR; goto out; }
        Dev->SharedAddr = shared;
    }
    {
        nb_u32 w[NB_SH_WORDS], ri[NB_RI_WORDS];
        for (ULONG i = 0; i < NB_SH_WORDS; i++)
            w[i] = NwoaTcmRead32(Dev, Dev->SharedAddr + 4 * i);
        NwoaDiagDword(Dev, L"NwoaSharedFlags", w[0]);
        if (Dev->Cfg.RomDump && KeGetCurrentIrql() == PASSIVE_LEVEL && !(L->rambase & 15) &&
            L->rambase <= 0x400000 && L->rambase <= Dev->Bar2Len) {       // s276
            ULONG part = L->rambase / 4;
            PULONG rb = (PULONG)ExAllocatePool2(POOL_FLAG_PAGED, part, NWOA_TAG);
            for (ULONG q = 0; rb && q < 4; q++) {
                WCHAR nm[16];
                for (ULONG i2 = 0; i2 < part / 4; i2++)
                    rb[i2] = READ_REGISTER_ULONG((volatile ULONG*)(Dev->Bar2 + q * part + 4 * i2));
                RtlStringCchPrintfW(nm, ARRAYSIZE(nm), L"NwoaRom%lu", q);
                NwoaDiagBin(Dev, nm, (const UCHAR*)rb, part);
            }
            if (rb)
                ExFreePoolWithTag(rb, NWOA_TAG);
        }
        ULONG ria = w[NB_SH_RING_INFO / 4];
        if (ria < L->rambase || ria + sizeof(ri) > L->top) { st = STATUS_DEVICE_DATA_ERROR; goto out; }
        for (ULONG i = 0; i < NB_RI_WORDS; i++)
            ri[i] = NwoaTcmRead32(Dev, ria + 4 * i);
        int rc = nb_shared_parse(w, ri, L->rambase, L->top, &Dev->Sh);
        NwoaDiagDword(Dev, L"NwoaSharedParse", (ULONG)rc);
        if (rc != NB_OK) { st = STATUS_DEVICE_DATA_ERROR; goto out; }
        NwoaDiagDword(Dev, L"NwoaMaxSub", Dev->Sh.max_sub);
        NwoaDiagDword(Dev, L"NwoaMaxCompl", Dev->Sh.max_compl);
        NwoaDiagDword(Dev, L"NwoaIdxSz", Dev->Sh.idx_sz);
    }
    Dev->FwUp = TRUE;
    st = STATUS_SUCCESS;
out:
    UNREFERENCED_PARAMETER(fw);                            // fw / nvtxt are the cached copies
    UNREFERENCED_PARAMETER(nvtxt);
    FreeBuf(nv);
    return st;
}

// Last bytes of the firmware console ring (for the status job).
NTSTATUS NwoaReadConsoleTail(PNWOA_DEVICE Dev, PCHAR Out, ULONG Cap)
{
    ULONG con = Dev->Sh.console, buf, size, widx, n;
    if (!Dev->FwUp || !con || Cap < 2)
        return STATUS_INVALID_DEVICE_STATE;
    buf = NwoaTcmRead32(Dev, con + NB_CON_BUFADDR);
    size = NwoaTcmRead32(Dev, con + NB_CON_BUFSIZE);
    widx = NwoaTcmRead32(Dev, con + NB_CON_WRITEIDX);
    if (buf < Dev->Ram.rambase || buf + size > Dev->Ram.top || !size || size > 0x10000 || widx >= size)
        return STATUS_DEVICE_DATA_ERROR;
    n = min(Cap - 1, size);
    // the n bytes that end at widx (the ring wraps)
    ULONG start = (widx + size - n) % size, o = 0;
    while (o < n) {
        ULONG chunk = min(n - o, size - start);
        NwoaTcmReadBuf(Dev, buf + start, (PUCHAR)Out + o, chunk);
        o += chunk;
        start = 0;
    }
    for (ULONG i = 0; i < n; i++)
        if ((UCHAR)Out[i] < 0x20 && Out[i] != '\n')
            Out[i] = (Out[i] == 0 || Out[i] == '\r') ? ' ' : '?';
        else if ((UCHAR)Out[i] >= 0x7F)
            Out[i] = '?';
    Out[n] = 0;
    return STATUS_SUCCESS;
}

// s258n: new firmware console text since the last 2 s diag tick, whole lines only, masked
// (nb_mask_text: MAC / 12-hex tokens, the SSID of the last connect), kept as a rolling tail
// of NWOA_CONLIVE characters for NwoaConsoleLive.  At most 2048 new bytes per tick.
static void ConAppendLine(PNWOA_DEVICE Dev, const CHAR* L, ULONG Len)
{
    CHAR m[600];
    KIRQL irql;
    ULONG n = nb_mask_text(L, Len, Dev->Conn.Ssid, Dev->Conn.SsidLen, m, sizeof(m));
    while (n && (m[n - 1] == '\n' || m[n - 1] == ' '))
        n--;
    if (!n)
        return;
    if (n > NWOA_CONLIVE - 1)
        n = NWOA_CONLIVE - 1;
    KeAcquireSpinLock(&Dev->StateLock, &irql);
    if (Dev->ConLen + n + 1 > NWOA_CONLIVE) {
        ULONG drop = Dev->ConLen + n + 1 - NWOA_CONLIVE;
        while (drop < Dev->ConLen && Dev->ConBuf[drop - 1] != '\n')
            drop++;                                           // whole lines only
        if (drop > Dev->ConLen)
            drop = Dev->ConLen;
        RtlMoveMemory(Dev->ConBuf, Dev->ConBuf + drop, Dev->ConLen - drop);
        Dev->ConLen -= drop;
    }
    RtlCopyMemory(Dev->ConBuf + Dev->ConLen, m, n);
    Dev->ConLen += n;
    Dev->ConBuf[Dev->ConLen++] = '\n';
    Dev->ConBuf[Dev->ConLen] = 0;
    Dev->ConDirty = TRUE;
    KeReleaseSpinLock(&Dev->StateLock, irql);
}

void NwoaConsoleLiveTick(PNWOA_DEVICE Dev)
{
    ULONG con = Dev->Sh.console, buf, size, widx, n, start;
    if (!Dev->Cfg.ConsoleLive || !Dev->FwUp || !con)
        return;
    buf = NwoaTcmRead32(Dev, con + NB_CON_BUFADDR);
    size = NwoaTcmRead32(Dev, con + NB_CON_BUFSIZE);
    widx = NwoaTcmRead32(Dev, con + NB_CON_WRITEIDX);
    if (buf < Dev->Ram.rambase || buf + size > Dev->Ram.top || !size || size > 0x10000 || widx >= size)
        return;
    if (!Dev->ConInit || Dev->ConIdx >= size) {
        Dev->ConIdx = widx;                                   // start now: the boot text is in NwoaConsoleTail
        Dev->ConInit = TRUE;
        return;
    }
    if (widx == Dev->ConIdx)
        return;
    n = (widx + size - Dev->ConIdx) % size;
    start = Dev->ConIdx;
    if (n > 2048) {
        Dev->ConSkipped += n - 2048;
        start = (widx + size - 2048) % size;
        n = 2048;
    }
    PUCHAR raw = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, n, NWOA_TAG);
    if (!raw)
        return;
    for (ULONG o = 0, s = start; o < n;) {
        ULONG chunk = min(n - o, size - s);
        NwoaTcmReadBuf(Dev, buf + s, raw + o, chunk);
        o += chunk;
        s = 0;
    }
    Dev->ConIdx = widx;
    Dev->ConBytes += n;
    for (ULONG i = 0; i < n; i++) {
        CHAR c = (CHAR)raw[i];
        if (c == 0)
            continue;
        Dev->ConLine[Dev->ConLineLen++] = c;
        if (c == '\n' || Dev->ConLineLen >= sizeof(Dev->ConLine) - 1) {
            ConAppendLine(Dev, Dev->ConLine, Dev->ConLineLen);
            Dev->ConLineLen = 0;
        }
    }
    ExFreePoolWithTag(raw, NWOA_TAG);
}

// ------------------------------------------------------------ s273 FwSnap
// [observation] After a scan that leaves the home channel the firmware stops reading the TX flowrings and a
// delete + create makes it read again; no message, no console text.  So its own state is compared: the whole
// firmware RAM before the escan (0), at the verdict before any fix (1) and after the re-create (2).
//   NwoaSnap<k>   text summary: verdict, read ms, counts of differing words, ring base hits
//   NwoaSnap<k>A  REG_BINARY, records of 3 u32 {offset from rambase, word in 0, word in 1} for the words that
//                 are equal in 0 and 2 and differ in 1 ("changed by the scan, restored by the re-create")
//                 (have2 = no snapshot 2, e.g. a scan without the stall: every word that differs between 0 and 1)
//   NwoaSnap<k>H<i>  REG_BINARY, ring base hit i: u32 offset, then NWOA_SNAP_WIN bytes of snapshot 0, 1, 2
//                 starting NWOA_SNAP_WIN / 2 before the hit
//   NwoaSnap<k>C<t>  REG_BINARY, the "counters" iovar at moment t
//   NwoaSnap<k>W<j>  REG_BINARY (s274), for an A record with no other A record within 0x40 bytes below the top
//                 64 KiB (single state words, not tables or the stack): u32 offset of the window, then
//                 NWOA_SNAP_WW bytes of snapshot 0, 1, 2 starting NWOA_SNAP_WW / 2 before the word
// The connected BSSID (0xEB), the own MAC (0xEA) and the network name (0xE5) are overwritten in every copy
// before anything is compared.
#define NWOA_SNAP_WIN   1024u
#define NWOA_SNAP_AMAX  4000u
#define NWOA_SNAP_HITS  6u
#define NWOA_SNAP_WW    768u
#define NWOA_SNAP_WMAX  14u

static ULONG SnapRamLen(PNWOA_DEVICE Dev)
{
    ULONG base = Dev->Ram.rambase, len = Dev->Ram.ramsize & ~3u;
    if (!len || len > 0x200000 || (base & 3) || (ULONGLONG)base + len > Dev->Bar2Len)
        return 0;
    return len;
}

static void SnapMask(PUCHAR B, ULONG Len, const UCHAR* M, ULONG N, UCHAR Fill)
{
    ULONG nz = 0;
    for (ULONG i = 0; i < N; i++)
        nz |= M[i];
    if (N < 4 || Len < N || !nz)
        return;
    for (ULONG i = 0; i + N <= Len; i++)
        if (B[i] == M[0] && RtlEqualMemory(B + i, M, N)) {
            RtlFillMemory(B + i, N, Fill);
            i += N - 1;
        }
}

void NwoaSnapFree(PNWOA_DEVICE Dev)
{
    for (ULONG i = 0; i < 3; i++)
        if (Dev->Snap[i]) {
            ExFreePoolWithTag(Dev->Snap[i], NWOA_TAG);
            Dev->Snap[i] = NULL;
        }
    Dev->SnapT0 = FALSE;
}

void NwoaSnapTake(PNWOA_DEVICE Dev, ULONG Which)
{
    ULONG len = SnapRamLen(Dev);
    WCHAR name[24];
    UCHAR cnt[1024];
    if (!Dev->Cfg.FwSnap || Which > 2 || Dev->SnapN >= NWOA_SNAP_SCANS || !len || !Dev->FwUp ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    if (Which && !Dev->SnapT0)
        return;
    if (Dev->SnapLen != len)
        NwoaSnapFree(Dev);
    if (!Dev->Snap[Which])
        Dev->Snap[Which] = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, len, NWOA_TAG);
    if (!Dev->Snap[Which])
        return;
    Dev->SnapLen = len;
    ULONG64 t0 = KeQueryInterruptTime();
    PULONG d = (PULONG)Dev->Snap[Which];
    volatile ULONG* s = (volatile ULONG*)(Dev->Bar2 + Dev->Ram.rambase);
    for (ULONG i = 0; i < len / 4; i++)
        d[i] = READ_REGISTER_ULONG(s + i);
    Dev->SnapMs[Which] = (ULONG)((KeQueryInterruptTime() - t0) / 10000);
    {   // s275: the firmware's flowring structs ("txfl<n>", 0xE0 bytes): own MAC at +0x26, flowring da at +0x2c
        UCHAR pat[12];
        const UCHAR* sn = Dev->Snap[Which];
        RtlCopyMemory(pat, Dev->Mac, 6);
        RtlCopyMemory(pat + 6, Dev->FlowDa, 6);
        Dev->SnapRingN[Which] = 0;
        for (ULONG i = 0x26; i + 0xE0 <= len && Dev->SnapRingN[Which] < 4; i += 2)
            if (sn[i] == pat[0] && RtlEqualMemory(sn + i, pat, 12) && sn[i - 0x25] == 't' && sn[i - 0x24] == 'x')
                Dev->SnapRing[Which][Dev->SnapRingN[Which]++] = i - 0x26;
    }
    if (!Dev->SnapRomDone) {                                          // s275: is the ROM below rambase readable here?
        static const ULONG at[3] = { 0, 0x4ad80, 0x75bec };            // reset vectors, two call targets of RAM code
        ULONG rom[3 * 64];
        for (ULONG q = 0; q < 3; q++)
            for (ULONG i = 0; i < 64; i++)
                rom[q * 64 + i] = NwoaTcmRead32(Dev, at[q] + 4 * i);
        NwoaDiagBin(Dev, L"NwoaRomProbe", (const UCHAR*)rom, sizeof(rom));
        Dev->SnapRomDone = TRUE;
    }
    SnapMask(Dev->Snap[Which], len, Dev->CurBssid, 6, 0xEB);          // s274: EB = BSSID, EA = own MAC, E5 = name
    SnapMask(Dev->Snap[Which], len, Dev->Mac, 6, 0xEA);
    SnapMask(Dev->Snap[Which], len, Dev->Conn.Ssid, min(Dev->Conn.SsidLen, 32ul), 0xE5);   // network name (4+ bytes)
    if (!Which)
        Dev->SnapT0 = TRUE;
    RtlZeroMemory(cnt, sizeof(cnt));
    if (NT_SUCCESS(NwoaGetVar(Dev, "counters", cnt, sizeof(cnt)))) {
        RtlStringCchPrintfW(name, ARRAYSIZE(name), L"NwoaSnap%luC%lu", Dev->SnapN, Which);
        NwoaDiagBin(Dev, name, cnt, sizeof(cnt));
    }
}

void NwoaSnapReport(PNWOA_DEVICE Dev, ULONG Have, ULONG Verdict)
{
    ULONG len = Dev->SnapLen, k = Dev->SnapN, nw = len / 4;
    ULONG n01 = 0, n12 = 0, n02 = 0, nA = 0, nhit = 0, hit[NWOA_SNAP_HITS] = { 0 };
    WCHAR name[24];
    CHAR txt[400], one[24];
    if (!Dev->Cfg.FwSnap || !Dev->SnapT0 || k >= NWOA_SNAP_SCANS || (Have != 2 && Have != 3) ||
        !Dev->Snap[0] || !Dev->Snap[1] || (Have == 3 && !Dev->Snap[2]) || KeGetCurrentIrql() != PASSIVE_LEVEL) {
        Dev->SnapT0 = FALSE;
        return;
    }
    const ULONG* a = (const ULONG*)Dev->Snap[0];
    const ULONG* b = (const ULONG*)Dev->Snap[1];
    const ULONG* c = (const ULONG*)(Have == 3 ? Dev->Snap[2] : Dev->Snap[1]);
    PULONG rec = (PULONG)ExAllocatePool2(POOL_FLAG_PAGED, NWOA_SNAP_AMAX * 12, NWOA_TAG);
    PUCHAR win = (PUCHAR)ExAllocatePool2(POOL_FLAG_PAGED, 4 + 3 * NWOA_SNAP_WIN, NWOA_TAG);
    ULONG lo = (ULONG)Dev->Flow.La, hi = (ULONG)(Dev->Flow.La >> 32);
    for (ULONG i = 0; i < nw; i++) {
        if (a[i] != b[i]) {
            n01++;
            if (Have == 2 || a[i] == c[i]) {                  // no re-create (alive: control): every changed word
                if (rec && nA < NWOA_SNAP_AMAX) {
                    rec[3 * nA] = i * 4; rec[3 * nA + 1] = a[i]; rec[3 * nA + 2] = b[i];
                }
                nA++;
            }
        }
        if (b[i] != c[i]) n12++;
        if (a[i] != c[i]) n02++;
        // the flowring's host base (64 bit, low word first) as the firmware keeps it
        if (lo && i + 1 < nw && b[i] == lo && b[i + 1] == hi && nhit < NWOA_SNAP_HITS)
            hit[nhit++] = i * 4;
    }
    RtlStringCchPrintfA(txt, sizeof(txt), "v%lu have%lu len%lx ms%lu/%lu/%lu d01=%lu d12=%lu d02=%lu A=%lu hits%lu",
        Verdict, Have, len, Dev->SnapMs[0], Dev->SnapMs[1], Have == 3 ? Dev->SnapMs[2] : 0, n01, n12, n02, nA, nhit);
    for (ULONG h = 0; h < nhit; h++) {
        RtlStringCchPrintfA(one, sizeof(one), " %lx", hit[h]);
        RtlStringCchCatA(txt, sizeof(txt), one);
    }
    RtlStringCchPrintfW(name, ARRAYSIZE(name), L"NwoaSnap%lu", k);
    NwoaDiagSz(Dev, name, txt);
    if (rec) {
        RtlStringCchPrintfW(name, ARRAYSIZE(name), L"NwoaSnap%luA", k);
        NwoaDiagBin(Dev, name, (const UCHAR*)rec, min(nA, NWOA_SNAP_AMAX) * 12);
    }
    for (ULONG h = 0; win && h < nhit; h++) {
        ULONG st = hit[h] > NWOA_SNAP_WIN / 2 ? hit[h] - NWOA_SNAP_WIN / 2 : 0;
        if (st + NWOA_SNAP_WIN > len)
            st = len - NWOA_SNAP_WIN;
        RtlCopyMemory(win, &st, 4);
        RtlCopyMemory(win + 4, Dev->Snap[0] + st, NWOA_SNAP_WIN);
        RtlCopyMemory(win + 4 + NWOA_SNAP_WIN, Dev->Snap[1] + st, NWOA_SNAP_WIN);
        RtlCopyMemory(win + 4 + 2 * NWOA_SNAP_WIN, (const UCHAR*)c + st, NWOA_SNAP_WIN);
        RtlStringCchPrintfW(name, ARRAYSIZE(name), L"NwoaSnap%luH%lu", k, h);
        NwoaDiagBin(Dev, name, win, 4 + 3 * NWOA_SNAP_WIN);
    }
    if (rec && win && Have == 3 && len > 0x10000 + NWOA_SNAP_WW) {          // s274: windows around single state words
        ULONG n = min(nA, NWOA_SNAP_AMAX), nwin = 0;
        for (ULONG i = 0; i < n && nwin < NWOA_SNAP_WMAX; i++) {
            ULONG o = rec[3 * i];
            // s275: only the two state words seen in every stalled scan of s273 (3 -> 0x10003, 0x618 -> 0)
            if (!((rec[3 * i + 1] == 3 && rec[3 * i + 2] == 0x10003) || (rec[3 * i + 1] == 0x618 && rec[3 * i + 2] == 0)))
                continue;
            ULONG st = o > NWOA_SNAP_WW / 2 ? (o - NWOA_SNAP_WW / 2) & ~3u : 0;
            if (st + NWOA_SNAP_WW > len)
                st = len - NWOA_SNAP_WW;
            RtlCopyMemory(win, &st, 4);
            RtlCopyMemory(win + 4, Dev->Snap[0] + st, NWOA_SNAP_WW);
            RtlCopyMemory(win + 4 + NWOA_SNAP_WW, Dev->Snap[1] + st, NWOA_SNAP_WW);
            RtlCopyMemory(win + 4 + 2 * NWOA_SNAP_WW, Dev->Snap[2] + st, NWOA_SNAP_WW);
            RtlStringCchPrintfW(name, ARRAYSIZE(name), L"NwoaSnap%luW%lu", k, nwin);
            NwoaDiagBin(Dev, name, win, 4 + 3 * NWOA_SNAP_WW);
            nwin++;
        }
    }
    // s275 NwoaSnap<k>R<i>: flowring struct i of snapshot 1 and its open/close history (pointer at +0x78), in
    // every snapshot: u32 ring offset, then per snapshot u32 history offset + 0xE0 ring bytes + 0x140 history bytes
    for (ULONG r = 0; win && r < Dev->SnapRingN[1]; r++) {
        ULONG ro = Dev->SnapRing[1][r], pos = 4;
        RtlCopyMemory(win, &ro, 4);
        for (ULONG t = 0; t < 3; t++) {
            const UCHAR* sn = t == 0 ? Dev->Snap[0] : t == 1 ? Dev->Snap[1] : (const UCHAR*)c;
            ULONG hp, ho = 0xFFFFFFFFu;
            RtlCopyMemory(&hp, sn + ro + 0x78, 4);
            if (hp >= Dev->Ram.rambase && hp - Dev->Ram.rambase + 0x140 <= len)
                ho = hp - Dev->Ram.rambase;
            RtlCopyMemory(win + pos, &ho, 4);
            RtlCopyMemory(win + pos + 4, sn + ro, 0xE0);
            if (ho != 0xFFFFFFFFu)
                RtlCopyMemory(win + pos + 4 + 0xE0, sn + ho, 0x140);
            else
                RtlZeroMemory(win + pos + 4 + 0xE0, 0x140);
            pos += 4 + 0xE0 + 0x140;
        }
        RtlStringCchPrintfW(name, ARRAYSIZE(name), L"NwoaSnap%luR%lu", k, r);
        NwoaDiagBin(Dev, name, win, pos);
    }
    if (rec) ExFreePoolWithTag(rec, NWOA_TAG);
    if (win) ExFreePoolWithTag(win, NWOA_TAG);
    Dev->SnapT0 = FALSE;
    Dev->SnapN = k + 1;
    if (Dev->SnapN >= NWOA_SNAP_SCANS)
        NwoaSnapFree(Dev);
}
