/* boot.c - bring-up and teardown of the Bluetooth function (fn1).
 *
 * Sequence (facts from the Linux hci_bcm4377 probe, our own code): function reset -> bus master ->
 * vendor config windows (0x80/0x70/0x74/0x78/0x84, 0x88 bit 16) -> OTP (BAR0+0x4120, stepping 's' and
 * antenna vendor 'V') -> bootstage 0 / RTI 0 -> firmware image in host RAM, address/size in BAR2,
 * BAR0+0x140 doorbell -> bootstage 2 -> RTI 1 -> RTI window + context address -> RTI 2 (control ring
 * live) -> completion rings 1..4 and transfer rings 1..6 through control messages -> vendor HCI:
 * Taurus calibration (0xfd97), PTB (0xfe0d), BD address (0xfc01 when the chip reports its default).
 * No interrupt: every wait polls a register (boot) or the shared ring indices (timer, io.c).
 * SPDX-License-Identifier: MIT */
#include "nwbt.h"

static void SleepMs(ULONG ms)
{
    LARGE_INTEGER t;
    t.QuadPart = -(LONGLONG)ms * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &t);
}

static ULONG NowMs(void) { return (ULONG)(KeQueryInterruptTime() / 10000); }

static BOOLEAN StopHere(PNWBT_DEVICE Dev, ULONG Level, PCSTR Stage)
{
    if (Dev->Cfg.StopAfter != Level)
        return FALSE;
    NwbtSetStage(Dev, Stage, STATUS_SUCCESS);
    return TRUE;
}

static NTSTATUS Fail(PNWBT_DEVICE Dev, PCSTR Stage, NTSTATUS St)
{
    NwbtSetStage(Dev, Stage, St);
    return St;
}

static void FreeFirmware(PNWBT_DEVICE Dev)
{
    if (Dev->FwImg) { ExFreePoolWithTag(Dev->FwImg, NWBT_TAG); Dev->FwImg = NULL; }
    if (Dev->PtbImg) { ExFreePoolWithTag(Dev->PtbImg, NWBT_TAG); Dev->PtbImg = NULL; }
    Dev->FwLen = Dev->PtbLen = 0;
    Dev->FwName[0] = Dev->PtbName[0] = 0;
}

void NwbtFreeFiles(PNWBT_DEVICE Dev)
{
    FreeFirmware(Dev);
    if (Dev->Cal) { ExFreePoolWithTag(Dev->Cal, NWBT_TAG); Dev->Cal = NULL; }
    Dev->CalLen = Dev->CalPlainLen = 0;
    Dev->CalPlainLoaded = FALSE;
}

static NTSTATUS InitConfig(PNWBT_DEVICE Dev)
{
    static const ULONG regs[][2] = {
        { NBT_CFG_BAR0_WINDOW1, NBT_4378_WINDOW1 }, { NBT_CFG_BAR0_WINDOW2, NBT_4378_WINDOW2 },
        { NBT_CFG_BAR0_CORE2_WINDOW1, NBT_4378_CORE2_WINDOW1 }, { NBT_CFG_BAR0_CORE2_WINDOW2, NBT_4378_CORE2_WINDOW2 },
        { NBT_CFG_BAR2_WINDOW, NBT_BAR2_WINDOW_DEFAULT } };
    ULONG i, bad = 0, ctrl;
    NTSTATUS st;
    for (i = 0; i < ARRAYSIZE(regs); i++) {
        st = NwbtCfgWrite(Dev, regs[i][0], regs[i][1]);
        if (!NT_SUCCESS(st) || NwbtCfgRead(Dev, regs[i][0]) != regs[i][1])
            bad |= 1u << i;
    }
    ctrl = NwbtCfgRead(Dev, NBT_CFG_SUBSYS_CTRL);
    st = NwbtCfgWrite(Dev, NBT_CFG_SUBSYS_CTRL, ctrl | NBT_SUBSYS_CTRL_SET);
    ctrl = NwbtCfgRead(Dev, NBT_CFG_SUBSYS_CTRL);
    if (!NT_SUCCESS(st) || !(ctrl & NBT_SUBSYS_CTRL_SET))
        bad |= 0x100;
    NwbtDiagDword(Dev, L"NwbtSubsysCtrl", ctrl);
    NwbtDiagDword(Dev, L"NwbtCfgBad", bad);
    return bad ? STATUS_DEVICE_HARDWARE_ERROR : STATUS_SUCCESS;
}

static NTSTATUS MsiToRam(PNWBT_DEVICE Dev)
{
    ULONG cap = NwbtCtlRead(Dev, BTC_MSICAP), ctl;
    NTSTATUS st;
    BOOLEAN is64;
    if (!cap || cap >= 0x100)
        return STATUS_NOT_SUPPORTED;
    if (!Dev->MsiPage) {
        Dev->MsiPage = (PUCHAR)NwbtDmaAlloc(PAGE_SIZE, &Dev->MsiPa);
        if (!Dev->MsiPage)
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    ctl = NwbtCfgRead(Dev, cap);
    is64 = (ctl & (1u << 23)) != 0;
    st = NwbtCfgWrite(Dev, cap + 4, (ULONG)Dev->MsiPa);
    if (NT_SUCCESS(st) && is64)
        st = NwbtCfgWrite(Dev, cap + 8, 0);
    if (NT_SUCCESS(st))
        st = NwbtCfgWrite(Dev, cap + (is64 ? 0xC : 0x8), 0x4E57);
    if (NT_SUCCESS(st))
        st = NwbtCfgWrite(Dev, cap, ctl | (1u << 16));
    NwbtDiagDword(Dev, L"NwbtMsiCtl", NwbtCfgRead(Dev, cap));
    return st;
}

static NTSTATUS ReadOtp(PNWBT_DEVICE Dev)
{
    UCHAR otp[NBT_OTP_SIZE];
    ULONG i;
    for (i = 0; i < NBT_OTP_SIZE; i++)
        otp[i] = NwbtB0R8(Dev, NBT_BAR0_OTP + i);
    if (nbt_otp_parse(otp, NBT_OTP_SIZE, Dev->Stepping, Dev->Vendor) != NBT_OK) {
        CHAR hex[2 * 16 + 1];
        for (i = 0; i < 16; i++)
            RtlStringCchPrintfA(hex + 2 * i, 3, "%02x", otp[i]);
        NwbtDiagSz(Dev, L"NwbtOtpHead", hex);           /* TLV header bytes only, for the bisect */
        return STATUS_DEVICE_DATA_ERROR;
    }
    NwbtDiagSz(Dev, L"NwbtStepping", Dev->Stepping);
    NwbtDiagSz(Dev, L"NwbtVendor", Dev->Vendor);
    return STATUS_SUCCESS;
}

static NTSTATUS LoadFiles(PNWBT_DEVICE Dev)
{
    CHAR fw[48], ptb[48];
    NTSTATUS st;
    if (nbt_fw_name(fw, sizeof(fw), Dev->Stepping, Dev->Vendor, "bin") != NBT_OK ||
        nbt_fw_name(ptb, sizeof(ptb), Dev->Stepping, Dev->Vendor, "ptb") != NBT_OK)
        return STATUS_NOT_SUPPORTED;
    if (Dev->FwImg && strcmp(fw, Dev->FwName))
        FreeFirmware(Dev);                                /* vendor changed: never mix files */
    if (!Dev->FwImg) {
        st = NwbtLoadFile(Dev, fw, &Dev->FwImg, &Dev->FwLen, 4 * 1024 * 1024);
        if (!NT_SUCCESS(st))
            return st;
        RtlStringCchCopyA(Dev->FwName, sizeof(Dev->FwName), fw);
    }
    if (!Dev->PtbImg) {
        st = NwbtLoadFile(Dev, ptb, &Dev->PtbImg, &Dev->PtbLen, 64 * 1024);
        if (!NT_SUCCESS(st))
            return st;
        RtlStringCchCopyA(Dev->PtbName, sizeof(Dev->PtbName), ptb);
    }
    NwbtDiagSz(Dev, L"NwbtFirmware", Dev->FwName);
    NwbtDiagDword(Dev, L"NwbtFirmwareLen", Dev->FwLen);
    NwbtDiagSz(Dev, L"NwbtPtb", Dev->PtbName);
    NwbtDiagDword(Dev, L"NwbtPtbLen", Dev->PtbLen);
    return STATUS_SUCCESS;
}

/* poll a BAR2 register until it equals Want; records the distinct values seen */
static BOOLEAN WaitB2(PNWBT_DEVICE Dev, ULONG Off, ULONG Want, ULONG TimeoutMs, PULONG Last, PULONG TookMs)
{
    ULONG t0 = NowMs(), v = 0xFFFFFFFF, prev = 0xFFFFFFFE;
    for (;;) {
        v = NwbtB2R(Dev, Off);
        if (v == 0xFFFFFFFF)                             /* fenced or gone: fail at once, never spin on it */
            break;
        if (v != prev && Off == NBT_BAR2_BOOTSTAGE && Dev->BootSeenN < ARRAYSIZE(Dev->BootSeen))
            Dev->BootSeen[Dev->BootSeenN++] = v;
        prev = v;
        if (v == Want)
            break;
        if (NowMs() - t0 > TimeoutMs)
            break;
        SleepMs(1);
    }
    *Last = v;
    *TookMs = NowMs() - t0;
    return v == Want;
}

static NTSTATUS BootFirmware(PNWBT_DEVICE Dev)
{
    ULONG last, took, i;
    CHAR seen[64];
    BOOLEAN ok;
    NwbtB0W(Dev, NBT_BAR0_HOST_WINDOW_LO, 0);
    NwbtB0W(Dev, NBT_BAR0_HOST_WINDOW_HI, 0);
    NwbtB0W(Dev, NBT_BAR0_HOST_WINDOW_SIZE, NBT_DMA_WINDOW);
    NwbtB2W(Dev, NBT_BAR2_FW_LO, (ULONG)Dev->FwDmaPa);
    NwbtB2W(Dev, NBT_BAR2_FW_HI, (ULONG)(Dev->FwDmaPa >> 32));
    NwbtB2W(Dev, NBT_BAR2_FW_SIZE, Dev->FwLen);
    KeMemoryBarrier();
    NwbtB0W(Dev, NBT_BAR0_FW_DOORBELL, 0);
    ok = WaitB2(Dev, NBT_BAR2_BOOTSTAGE, NBT_BOOTSTAGE_DONE, Dev->Cfg.BootTimeoutMs, &last, &took);
    Dev->LastBootstage = last;
    Dev->BootMs = took;
    seen[0] = 0;
    for (i = 0; i < Dev->BootSeenN; i++) {
        CHAR one[12];
        RtlStringCchPrintfA(one, sizeof(one), "%lx ", Dev->BootSeen[i]);
        RtlStringCchCatA(seen, sizeof(seen), one);
    }
    NwbtDiagSz(Dev, L"NwbtBootstageSeen", seen);
    NwbtDiagDword(Dev, L"NwbtBootstage", last);
    NwbtDiagDword(Dev, L"NwbtBootMs", took);
    NwbtDiagDword(Dev, L"NwbtRtiAfterBoot", NwbtB2R(Dev, NBT_BAR2_RTI_STATUS));
    return ok ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

static NTSTATUS SetupRti(PNWBT_DEVICE Dev)
{
    ULONG last, took, t2;
    ULONG64 ctx = Dev->BlkPa + Dev->L.ctx_off;
    NwbtB0W(Dev, NBT_BAR0_RTI_CONTROL, 1);
    if (!WaitB2(Dev, NBT_BAR2_RTI_STATUS, NBT_RTI_STATE1, Dev->Cfg.StepTimeoutMs, &last, &took)) {
        NwbtDiagDword(Dev, L"NwbtRti", last);
        return STATUS_IO_TIMEOUT;
    }
    NwbtB2W(Dev, NBT_BAR2_RTI_WINDOW_LO, 0);
    NwbtB2W(Dev, NBT_BAR2_RTI_WINDOW_HI, 0);
    NwbtB2W(Dev, NBT_BAR2_RTI_WINDOW_SIZE, NBT_DMA_WINDOW);
    NwbtB2W(Dev, NBT_BAR2_CONTEXT_LO, (ULONG)ctx);
    NwbtB2W(Dev, NBT_BAR2_CONTEXT_HI, (ULONG)(ctx >> 32));
    KeMemoryBarrier();
    NwbtB0W(Dev, NBT_BAR0_RTI_CONTROL, 2);
    if (!WaitB2(Dev, NBT_BAR2_RTI_STATUS, NBT_RTI_STATE2, Dev->Cfg.StepTimeoutMs, &last, &t2)) {
        NwbtDiagDword(Dev, L"NwbtRti", last);
        return STATUS_IO_TIMEOUT;
    }
    Dev->LastRti = last;
    Dev->RtiMs = took + t2;
    NwbtDiagDword(Dev, L"NwbtRti", last);
    NwbtDiagDword(Dev, L"NwbtRtiMs", Dev->RtiMs);
    return STATUS_SUCCESS;
}

static NTSTATUS CreateRings(PNWBT_DEVICE Dev)
{
    static const UCHAR cr[] = { NBT_CR_HCI_ACL_ACK, NBT_CR_HCI_ACL_EVENT, NBT_CR_SCO_ACK, NBT_CR_SCO_EVENT };
    static const UCHAR xr[] = { NBT_XR_HCI_H2D, NBT_XR_HCI_D2H, NBT_XR_SCO_H2D, NBT_XR_SCO_D2H,
                                NBT_XR_ACL_H2D, NBT_XR_ACL_D2H };
    UCHAR m[NBT_CONTROL_MSG_SIZE];
    ULONG i, done = 0;
    NTSTATUS st = STATUS_SUCCESS;
    KIRQL irql;
    for (i = 0; i < ARRAYSIZE(cr) && NT_SUCCESS(st); i++) {
        nbt_cring* c = &Dev->L.c[cr[i]];
        RtlZeroMemory(Dev->Blk + c->ring_off, (SIZE_T)c->n_entries * (NBT_ENTRY_SIZE + c->payload_size));
        nbt_msg_create_cring(m, c, Dev->BlkPa);
        st = NwbtControlSync(Dev, m);
        if (NT_SUCCESS(st)) {
            KeAcquireSpinLock(&Dev->Lock, &irql);
            c->enabled = 1;
            KeReleaseSpinLock(&Dev->Lock, irql);
            done++;
        }
    }
    for (i = 0; i < ARRAYSIZE(xr) && NT_SUCCESS(st); i++) {
        nbt_xring* r = &Dev->L.x[xr[i]];
        UCHAR* state = Dev->Blk + Dev->L.state_off;
        KeAcquireSpinLock(&Dev->Lock, &irql);
        nbt_xring_reset(r, state);
        nbt_msg_create_xring(m, r, Dev->BlkPa);
        KeReleaseSpinLock(&Dev->Lock, irql);
        st = NwbtControlSync(Dev, m);
        if (!NT_SUCCESS(st))
            break;
        KeAcquireSpinLock(&Dev->Lock, &irql);
        if (r->flags & NBT_RF_D2H_BUFS)
            nbt_xring_fill_d2h(r, Dev->Blk, Dev->BlkPa);
        if (r->flags & (NBT_RF_VIRTUAL | NBT_RF_D2H_BUFS)) {
            nbt_rs_set(state, NBT_RS_XHEAD(r->id), NBT_D2H_POSTED);
            KeMemoryBarrier();
            NwbtRingDoorbell(Dev, r->doorbell, NBT_D2H_POSTED);
        }
        r->enabled = 1;
        KeReleaseSpinLock(&Dev->Lock, irql);
        done++;
    }
    NwbtDiagDword(Dev, L"NwbtRingsCreated", done);
    return st;
}

static NTSTATUS SendBlobs(PNWBT_DEVICE Dev)
{
    UCHAR p[NBT_CAL_PARAM_LEN > NBT_PTB_PARAM_LEN ? NBT_CAL_PARAM_LEN : NBT_PTB_PARAM_LEN];
    UCHAR ret[16], status = 0;
    ULONG rl, i, n;
    NTSTATUS st = STATUS_SUCCESS;
    if (Dev->Cfg.SendCal) {
        BOOLEAN bf = !strcmp(Dev->Stepping, "b1") || !strcmp(Dev->Stepping, "b3");
        if (!bf)
            NwbtReadPlainCal(Dev);
        const UCHAR* blob = bf ? Dev->Cal : Dev->Cal + BTC_CAL_MAX;
        ULONG len = bf ? Dev->CalLen : Dev->CalPlainLen;
        n = nbt_chunks(len, NBT_CAL_CHUNK);
        NwbtDiagDword(Dev, L"NwbtCalChunks", n);
        NwbtDiagDword(Dev, L"NwbtCalBeamforming", bf);
        for (i = 0; i < n && NT_SUCCESS(st); i++) {
            nbt_cal_chunk(blob, len, i, p);
            st = NwbtHciSync(Dev, NBT_OP_BCM_CALIBRATION, p, NBT_CAL_PARAM_LEN, ret, sizeof(ret), &rl, &status);
            if (NT_SUCCESS(st) && status)
                st = STATUS_DEVICE_PROTOCOL_ERROR;
        }
        NwbtDiagDword(Dev, L"NwbtCalSent", i);
        NwbtDiagDword(Dev, L"NwbtCalStatus", status);
        if (!NT_SUCCESS(st))
            return st;
    }
    if (Dev->Cfg.SendPtb) {
        n = nbt_chunks(Dev->PtbLen, NBT_PTB_CHUNK);
        for (i = 0; i < n && NT_SUCCESS(st); i++) {
            nbt_ptb_chunk(Dev->PtbImg, Dev->PtbLen, i, p);
            st = NwbtHciSync(Dev, NBT_OP_BCM_PTB, p, NBT_PTB_PARAM_LEN, ret, sizeof(ret), &rl, &status);
            if (NT_SUCCESS(st) && status)
                st = STATUS_DEVICE_PROTOCOL_ERROR;
        }
        NwbtDiagDword(Dev, L"NwbtPtbSent", i);
        NwbtDiagDword(Dev, L"NwbtPtbStatus", status);
    }
    return st;
}

/* BdState: 1 chip address used, 2 default -> ADT address set and read back, 3 set failed,
 * 4 default and no ADT address, 5 default and SetBdaddr=0.  The address itself is never written out. */
static NTSTATUS Address(PNWBT_DEVICE Dev)
{
    UCHAR ret[16], status = 0, le[6];
    ULONG rl = 0;
    NTSTATUS st = NwbtHciSync(Dev, NBT_OP_READ_BD_ADDR, NULL, 0, ret, sizeof(ret), &rl, &status);
    if (!NT_SUCCESS(st) || status || rl < 7)
        return NT_SUCCESS(st) ? STATUS_DEVICE_PROTOCOL_ERROR : st;
    if (!nbt_bdaddr_is_default(ret + 1, NBT_CHIP_4378)) {
        Dev->BdState = 1;
        return STATUS_SUCCESS;
    }
    if (!Dev->HaveAdtBd) {
        Dev->BdState = 4;
        return STATUS_SUCCESS;
    }
    if (!Dev->Cfg.SetBdaddr) {
        Dev->BdState = 5;
        return STATUS_SUCCESS;
    }
    nbt_bdaddr_from_adt(Dev->AdtBd, le);
    st = NwbtHciSync(Dev, NBT_OP_BCM_SET_BDADDR, le, 6, ret, sizeof(ret), &rl, &status);
    if (NT_SUCCESS(st) && !status)
        st = NwbtHciSync(Dev, NBT_OP_READ_BD_ADDR, NULL, 0, ret, sizeof(ret), &rl, &status);
    Dev->BdState = (NT_SUCCESS(st) && !status && rl >= 7 && RtlCompareMemory(ret + 1, le, 6) == 6) ? 2 : 3;
    RtlSecureZeroMemory(ret, sizeof(ret));
    RtlSecureZeroMemory(le, sizeof(le));
    return STATUS_SUCCESS;
}

static NTSTATUS LocalVersion(PNWBT_DEVICE Dev)
{
    UCHAR ret[16], status = 0;
    ULONG rl = 0;
    NTSTATUS st = NwbtHciSync(Dev, NBT_OP_READ_LOCAL_VERSION, NULL, 0, ret, sizeof(ret), &rl, &status);
    if (!NT_SUCCESS(st) || status || rl < 9)
        return NT_SUCCESS(st) ? STATUS_DEVICE_PROTOCOL_ERROR : st;
    Dev->HciVersion = ret[1];
    Dev->HciRevision = ret[2] | (ret[3] << 8);
    Dev->LmpVersion = ret[4];
    Dev->Manufacturer = ret[5] | (ret[6] << 8);
    Dev->LmpSubversion = ret[7] | (ret[8] << 8);
    NwbtDiagDword(Dev, L"NwbtHciVersion", Dev->HciVersion);
    NwbtDiagDword(Dev, L"NwbtHciRevision", Dev->HciRevision);
    NwbtDiagDword(Dev, L"NwbtLmpVersion", Dev->LmpVersion);
    NwbtDiagDword(Dev, L"NwbtManufacturer", Dev->Manufacturer);
    NwbtDiagDword(Dev, L"NwbtLmpSubversion", Dev->LmpSubversion);
    return STATUS_SUCCESS;
}

NTSTATUS NwbtBringUp(PNWBT_DEVICE Dev)
{
    NTSTATUS st;
    ULONG r, bs, rti;
    KIRQL irql;

    Dev->Dead = FALSE;
    Dev->BootSeenN = 0;
    st = NwbtCtlCheck(Dev);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_control_page", st);
    /* S261b: the hypervisor keeps the fn1 BARs fenced (all-ones, writes dropped) until this arm; it re-checks
     * the fn1 BARs first.  Any later chip power cycle / Wi-Fi chip reset / quiet closes it and bumps GEN. */
    Dev->Gen = NwbtCtlRead(Dev, BTC_GEN);
    NwbtCtlWrite(Dev, BTC_ARM, Dev->Gen);
    r = NwbtCtlRead(Dev, BTC_ARM);
    NwbtDiagDword(Dev, L"NwbtArmGen", Dev->Gen);
    NwbtDiagDword(Dev, L"NwbtArmResult", r);
    if (r != BTC_ARM_OK)
        return Fail(Dev, "fail_arm", STATUS_DEVICE_NOT_READY);
    if (StopHere(Dev, NWBT_STOP_CTLPAGE, "stopafter_1_ctlpage"))
        return STATUS_SUCCESS;

    if (Dev->Cfg.DoFlr) {
        NwbtCtlWrite(Dev, BTC_RESET, 1);
        r = NwbtCtlRead(Dev, BTC_RESET);
        NwbtDiagDword(Dev, L"NwbtResetResult", r);
        if (r != BTC_RESET_OK && r != BTC_RESET_NO_FLR)
            return Fail(Dev, "fail_flr", STATUS_DEVICE_HARDWARE_ERROR);
        SleepMs(100);                                    /* Linux: >= 50 ms before any BAR access */
    }
    st = NwbtBusMaster(Dev, TRUE);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_bus_master", st);
    st = InitConfig(Dev);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_config_windows", st);
    if (Dev->Cfg.MsiToRam) {
        st = MsiToRam(Dev);
        NwbtDiagDword(Dev, L"NwbtMsiStatus", (ULONG)st);
    }
    if (StopHere(Dev, NWBT_STOP_CONFIG, "stopafter_2_config"))
        return STATUS_SUCCESS;

    st = ReadOtp(Dev);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_otp", st);
    if (StopHere(Dev, NWBT_STOP_OTP, "stopafter_3_otp"))
        return STATUS_SUCCESS;

    bs = NwbtB2R(Dev, NBT_BAR2_BOOTSTAGE);
    rti = NwbtB2R(Dev, NBT_BAR2_RTI_STATUS);
    NwbtDiagDword(Dev, L"NwbtBootstageBefore", bs);
    NwbtDiagDword(Dev, L"NwbtRtiBefore", rti);
    if (bs != 0 || rti != 0)
        return Fail(Dev, "fail_not_fresh", STATUS_DEVICE_NOT_READY);  /* needs FLR or a chip power cycle */
    st = LoadFiles(Dev);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_files", st);
    if (nbt_layout_init(&Dev->L) != NBT_OK)
        return Fail(Dev, "fail_layout", STATUS_INTERNAL_ERROR);
    Dev->BlkLen = Dev->L.size;
    Dev->Blk = (PUCHAR)NwbtDmaAlloc(Dev->BlkLen, &Dev->BlkPa);
    Dev->FwDmaLen = Dev->FwLen;
    Dev->FwDma = (PUCHAR)NwbtDmaAlloc(Dev->FwDmaLen, &Dev->FwDmaPa);
    if (!Dev->Blk || !Dev->FwDma)
        return Fail(Dev, "fail_dma_memory", STATUS_INSUFFICIENT_RESOURCES);
    RtlCopyMemory(Dev->FwDma, Dev->FwImg, Dev->FwLen);
    nbt_context_build(Dev->Blk + Dev->L.ctx_off, &Dev->L, Dev->BlkPa);
    NwbtDiagDword(Dev, L"NwbtDmaBlkPa", (ULONG)Dev->BlkPa);
    NwbtDiagDword(Dev, L"NwbtDmaBlkLen", (ULONG)Dev->BlkLen);
    NwbtDiagDword(Dev, L"NwbtDmaFwPa", (ULONG)Dev->FwDmaPa);
    if (StopHere(Dev, NWBT_STOP_FILES, "stopafter_4_files"))
        return STATUS_SUCCESS;

    st = BootFirmware(Dev);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_boot_bootstage", st);
    NwbtDmaFree(Dev->FwDma, Dev->FwDmaLen);              /* the chip copied it (Linux frees it here too) */
    Dev->FwDma = NULL;
    if (StopHere(Dev, NWBT_STOP_BOOT, "stopafter_5_boot"))
        return STATUS_SUCCESS;

    st = SetupRti(Dev);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_rti", st);
    KeAcquireSpinLock(&Dev->Lock, &irql);
    Dev->L.c[NBT_CR_CONTROL_ACK].enabled = 1;
    Dev->L.x[NBT_XR_CONTROL].enabled = 1;
    Dev->CtlRingUp = TRUE;
    Dev->Running = TRUE;
    KeReleaseSpinLock(&Dev->Lock, irql);
    WdfTimerStart(Dev->Timer, WDF_REL_TIMEOUT_IN_MS(Dev->Cfg.PollIntervalMs));
    if (StopHere(Dev, NWBT_STOP_RTI, "stopafter_6_rti"))
        return STATUS_SUCCESS;

    st = CreateRings(Dev);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_create_rings", st);
    Dev->RingsUp = TRUE;
    if (StopHere(Dev, NWBT_STOP_RINGS, "stopafter_7_rings"))
        return STATUS_SUCCESS;

    st = SendBlobs(Dev);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_calibration_ptb", st);
    st = Address(Dev);
    NwbtDiagDword(Dev, L"NwbtBdState", Dev->BdState);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_bd_address", st);
    st = LocalVersion(Dev);
    if (!NT_SUCCESS(st))
        return Fail(Dev, "fail_local_version", st);
    Dev->HciReady = TRUE;
    if (StopHere(Dev, NWBT_STOP_HCI, "stopafter_8_hci"))
        return STATUS_SUCCESS;
    NwbtSetStage(Dev, Dev->Cfg.CreatePdo ? "hci_ready" : "hci_ready_no_child", STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

void NwbtTeardown(PNWBT_DEVICE Dev)
{
    KIRQL irql;
    BOOLEAN any = Dev->BmeOn || Dev->Blk || Dev->FwDma || Dev->MsiPage;
    if (Dev->Running) {
        KeAcquireSpinLock(&Dev->Lock, &irql);
        Dev->Running = FALSE;
        KeReleaseSpinLock(&Dev->Lock, irql);
        if (Dev->Timer)
            WdfTimerStop(Dev->Timer, TRUE);
    }
    WdfIoQueuePurgeSynchronously(Dev->WriteQ);
    WdfIoQueuePurgeSynchronously(Dev->EvtQ);
    WdfIoQueuePurgeSynchronously(Dev->AclQ);
    if (any && !Dev->DmaLeaked) {
        BOOLEAN off = NT_SUCCESS(NwbtBusMaster(Dev, FALSE));
        if (!off) {
            ULONG s = NwbtCtlRead(Dev, BTC_STATE);
            off = (s == 0 || s == 2);                    /* chip off, or fn1 quiet (hypervisor did BME off) */
        }
        if (off) {
            NwbtDmaFree(Dev->Blk, Dev->BlkLen);
            NwbtDmaFree(Dev->FwDma, Dev->FwDmaLen);
            NwbtDmaFree(Dev->MsiPage, PAGE_SIZE);
        } else {
            Dev->DmaLeaked = TRUE;                       /* the chip may still write there: keep it */
            NwbtDiagDword(Dev, L"NwbtDmaLeaked", 1);
        }
        Dev->Blk = Dev->FwDma = Dev->MsiPage = NULL;
        Dev->BmeOn = FALSE;
    }
    KeAcquireSpinLock(&Dev->Lock, &irql);
    Dev->CtlRingUp = Dev->RingsUp = Dev->HciReady = FALSE;
    Dev->SyncWanted = FALSE;
    RtlZeroMemory(&Dev->L, sizeof(Dev->L));
    if (Dev->EvtFifoMem)
        nbt_fifo_init(&Dev->EvtFifo, Dev->EvtFifoMem, NWBT_FIFO_BYTES);
    if (Dev->AclFifoMem)
        nbt_fifo_init(&Dev->AclFifo, Dev->AclFifoMem, NWBT_FIFO_BYTES);
    KeReleaseSpinLock(&Dev->Lock, irql);
}
