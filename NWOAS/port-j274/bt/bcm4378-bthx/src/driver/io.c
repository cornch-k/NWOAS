/* io.c - ring polling (timer DPC), synchronous control/HCI commands for bring-up, BthX IOCTLs.
 *
 * Device -> host: completion rings 0..4 are read from the shared index block every PollIntervalMs.
 * HCI events (transfer ring 2) and ACL data (transfer ring 6) are copied into two FIFOs and handed to
 * BthMini's pending IOCTL_BTHX_READ_HCI requests in order.  During bring-up the driver consumes the
 * Command Complete/Status it waits for itself; nothing reaches BthMini before it opened the transport.
 * Host -> device: IOCTL_BTHX_WRITE_HCI copies the packet into the command (ring 1) or ACL (ring 5) ring
 * and rings the doorbell; when a ring is full the request waits in WriteQ until acknowledgements free it.
 * SPDX-License-Identifier: MIT */
#include "nwbt.h"

void NwbtTraceIo(PNWBT_DEVICE Dev, UCHAR Kind, ULONG Code, size_t In, size_t Out, NTSTATUS St, UCHAR Type, USHORT Op, USHORT Op2)
{
    NWBT_IOREC r;
    KIRQL irql;
    ULONG f = (Code >> 2) & 0xFFF;
    r.Ms = (ULONG)(KeQueryInterruptTime() / 10000) - Dev->IoT0;
    r.Func = (USHORT)f;
    r.In = (USHORT)(In > 0xFFFF ? 0xFFFF : In);
    r.Out = (USHORT)(Out > 0xFFFF ? 0xFFFF : Out);
    r.Kind = Kind;
    r.Type = Type;
    r.Op = Op;
    r.Op2 = Op2;
    r.St = (ULONG)St;
    KeAcquireSpinLock(&Dev->Lock, &irql);
    if (Kind == 'A' || Kind == 'P' || Kind == 'F') {
        ULONG k = (f >= 0x100 && f <= 0x104) ? f - 0x100 : 5;
        if (Kind == 'P' && k == 5) k = 6;
        if (Kind == 'F') k = 7;
        Dev->CntIoctl[k]++;
    }
    if (Dev->IoFirstN < NWBT_IOLOG_FIRST)
        Dev->IoFirst[Dev->IoFirstN++] = r;
    else
        Dev->IoLast[Dev->IoLastN++ % NWBT_IOLOG_LAST] = r;
    KeReleaseSpinLock(&Dev->Lock, irql);
}

void NwbtRingDoorbell(PNWBT_DEVICE Dev, UCHAR Db, USHORT Val)
{
    NwbtB0W(Dev, NBT_BAR0_DOORBELL, nbt_doorbell_value(Db, Val));
}

/* caller holds Dev->Lock */
static int PutLocked(PNWBT_DEVICE Dev, nbt_xring* R, const UCHAR* Data, ULONG Len)
{
    USHORT db = 0;
    int id;
    if (!R->enabled || Dev->Dead)
        return NBT_ERR_RING;
    id = nbt_xring_put(R, Dev->Blk, Dev->BlkPa, Dev->Blk + Dev->L.state_off, Data, Len, &db);
    if (id < 0)
        return id;
    if (Dev->Cfg.PokeAwake)
        NwbtB0W(Dev, NBT_BAR0_SLEEP_CONTROL, NBT_SLEEP_AWAKE);
    KeMemoryBarrier();                                    /* entry + index visible before the doorbell */
    if (!(R->flags & NBT_RF_SYNC))
        NwbtRingDoorbell(Dev, R->doorbell, db);
    return id;
}

NTSTATUS NwbtControlSync(PNWBT_DEVICE Dev, const UCHAR Msg[NBT_CONTROL_MSG_SIZE])
{
    KIRQL irql;
    int id;
    ULONG waited = 0;
    LARGE_INTEGER t;
    KeAcquireSpinLock(&Dev->Lock, &irql);
    id = PutLocked(Dev, &Dev->L.x[NBT_XR_CONTROL], Msg, NBT_CONTROL_MSG_SIZE);
    KeReleaseSpinLock(&Dev->Lock, irql);
    if (id < 0)
        return STATUS_DEVICE_BUSY;
    for (;;) {
        BOOLEAN busy;
        t.QuadPart = -10LL * 10000;                       /* 10 ms slices: the timer DPC does the work */
        KeWaitForSingleObject(&Dev->CtlEvent, Executive, KernelMode, FALSE, &t);
        KeAcquireSpinLock(&Dev->Lock, &irql);
        busy = (BOOLEAN)nbt_xring_busy(&Dev->L.x[NBT_XR_CONTROL], id);
        KeReleaseSpinLock(&Dev->Lock, irql);
        if (!busy)
            return STATUS_SUCCESS;
        waited += 10;
        if (waited >= Dev->Cfg.StepTimeoutMs || Dev->Dead)
            return STATUS_IO_TIMEOUT;
    }
}

NTSTATUS NwbtHciSync(PNWBT_DEVICE Dev, USHORT Opcode, const UCHAR* Params, ULONG Plen,
                     PUCHAR Ret, ULONG RetCap, PULONG RetLen, PUCHAR Status)
{
    UCHAR pkt[3 + 255];
    ULONG n = nbt_hci_cmd(pkt, sizeof(pkt), Opcode, Params, Plen);
    uint32_t ro = 0, rl = 0;
    USHORT op;
    UCHAR stv = 0;
    KIRQL irql;
    LARGE_INTEGER t;
    NTSTATUS st;
    int id, k;
    *RetLen = 0;
    *Status = 0xFF;
    if (!n)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Dev->Lock, &irql);
    Dev->SyncOpcode = Opcode;
    Dev->SyncLen = 0;
    Dev->SyncWanted = TRUE;
    KeClearEvent(&Dev->CmdEvent);
    id = PutLocked(Dev, &Dev->L.x[NBT_XR_HCI_H2D], pkt, n);
    if (id < 0)
        Dev->SyncWanted = FALSE;
    KeReleaseSpinLock(&Dev->Lock, irql);
    if (id < 0)
        return STATUS_DEVICE_BUSY;
    t.QuadPart = -(LONGLONG)Dev->Cfg.HciTimeoutMs * 10000;
    st = KeWaitForSingleObject(&Dev->CmdEvent, Executive, KernelMode, FALSE, &t);
    KeAcquireSpinLock(&Dev->Lock, &irql);
    Dev->SyncWanted = FALSE;
    if (st == STATUS_SUCCESS && Dev->SyncLen) {
        k = nbt_hci_cmd_done(Dev->SyncEvt, Dev->SyncLen, &op, &stv, &ro, &rl);
        if (k == 1 && rl) {
            ULONG c = rl < RetCap ? rl : RetCap;
            RtlCopyMemory(Ret, Dev->SyncEvt + ro, c);
            *RetLen = c;
        }
        *Status = stv;
        st = (k > 0) ? STATUS_SUCCESS : STATUS_DEVICE_PROTOCOL_ERROR;
    } else {
        st = STATUS_IO_TIMEOUT;
    }
    RtlSecureZeroMemory(Dev->SyncEvt, sizeof(Dev->SyncEvt));    /* may hold the BD address */
    KeReleaseSpinLock(&Dev->Lock, irql);
    if (st == STATUS_IO_TIMEOUT)
        NWBT_LOG("HCI command %04x timed out", Opcode);
    return st;
}

/* ---------------------------------------------------------------- device -> host */
static void EventLocked(PNWBT_DEVICE Dev, PUCHAR P, ULONG Len)
{
    USHORT op;
    UCHAR stv;
    uint32_t ro, rl;
    Dev->CntEvt++;
    if (Dev->SyncWanted && nbt_hci_cmd_done(P, Len, &op, &stv, &ro, &rl) > 0 && op == Dev->SyncOpcode &&
        Len <= sizeof(Dev->SyncEvt)) {
        RtlCopyMemory(Dev->SyncEvt, P, Len);
        Dev->SyncLen = Len;
        Dev->SyncWanted = FALSE;
        KeSetEvent(&Dev->CmdEvent, IO_NO_INCREMENT, FALSE);
        return;
    }
    if (!Dev->BthxOpen || !Dev->HciReady) {
        Dev->CntInitEvtDropped++;
        return;
    }
    if (nbt_fix_event(P, Len, Dev->Cfg.FixQuirks))
        Dev->CntFix++;
    if (nbt_fifo_push(&Dev->EvtFifo, HciPacketEvent, P, Len) != NBT_OK)
        Dev->CntDropFifo++;
}

static void AclLocked(PNWBT_DEVICE Dev, const UCHAR* P, ULONG Len)
{
    Dev->CntAclRx++;
    if (!Dev->BthxOpen || !Dev->HciReady) {
        Dev->CntDropNoHost++;
        return;
    }
    if (nbt_fifo_push(&Dev->AclFifo, HciPacketAclData, P, Len) != NBT_OK)
        Dev->CntDropFifo++;
}

static void D2hLocked(PNWBT_DEVICE Dev, UCHAR Xr, const nbt_cring* C, const nbt_centry* E)
{
    nbt_xring* r = &Dev->L.x[Xr];
    uint32_t off = 0, len = 0;
    USHORT db;
    if (r->enabled && nbt_d2h_payload(r, C, E, &off, &len) == NBT_OK && off + len <= Dev->BlkLen) {
        if (Xr == NBT_XR_HCI_D2H)
            EventLocked(Dev, Dev->Blk + off, len);
        else if (Xr == NBT_XR_ACL_D2H)
            AclLocked(Dev, Dev->Blk + off, len);
    } else {
        Dev->CntBadEntry++;
    }
    db = nbt_xring_advance(r, Dev->Blk + Dev->L.state_off);   /* give the slot back, as Linux does */
    KeMemoryBarrier();
    NwbtRingDoorbell(Dev, r->doorbell, db);
}

static void PollCringLocked(PNWBT_DEVICE Dev, UCHAR Cr)
{
    nbt_cring* c = &Dev->L.c[Cr];
    PUCHAR st = Dev->Blk + Dev->L.state_off;
    USHORT tail, head, guard = 0;
    if (!c->enabled)
        return;
    tail = nbt_rs_get(st, NBT_RS_CTAIL(Cr));
    head = *(volatile USHORT*)(st + NBT_RS_CHEAD(Cr));
    if (head >= c->n_entries || tail >= c->n_entries) {
        Dev->Dead = TRUE;
        Dev->DeadReason = 3;                              /* ring index out of range */
        return;
    }
    while (tail != head && guard++ < c->n_entries) {
        nbt_centry e;
        KeMemoryBarrier();                                /* read the entry only after the index */
        if (nbt_cring_entry(c, Dev->Blk, tail, &e) == NBT_OK) {
            switch (e.xring) {
            case NBT_XR_CONTROL:
                if (nbt_xring_ack(&Dev->L.x[NBT_XR_CONTROL], e.msgid) >= 0)
                    KeSetEvent(&Dev->CtlEvent, IO_NO_INCREMENT, FALSE);
                else
                    Dev->CntBadEntry++;
                break;
            case NBT_XR_HCI_H2D:
            case NBT_XR_ACL_H2D:
            case NBT_XR_SCO_H2D:
                if (nbt_xring_ack(&Dev->L.x[e.xring], e.msgid) >= 0)
                    Dev->CntAck++;
                else
                    Dev->CntBadEntry++;
                break;
            case NBT_XR_HCI_D2H:
            case NBT_XR_ACL_D2H:
            case NBT_XR_SCO_D2H:
                D2hLocked(Dev, (UCHAR)e.xring, c, &e);
                break;
            default:
                Dev->CntBadEntry++;
            }
        } else {
            Dev->CntBadEntry++;
        }
        tail = (USHORT)((tail + 1) % c->n_entries);
        nbt_rs_set(st, NBT_RS_CTAIL(Cr), tail);
        head = *(volatile USHORT*)(st + NBT_RS_CHEAD(Cr));
        if (head >= c->n_entries) {
            Dev->Dead = TRUE;
            Dev->DeadReason = 3;
            break;
        }
    }
}

/* BthX read context: ULONG DataLen, UCHAR Type, Data[] (packed, bthxddi.h) */
static BOOLEAN DeliverOne(PNWBT_DEVICE Dev, WDFQUEUE Q, nbt_fifo* F)
{
    WDFREQUEST req = NULL;
    PBTHX_HCI_READ_WRITE_CONTEXT ctx = NULL;
    size_t cap = 0;
    NTSTATUS st = STATUS_SUCCESS;
    uint32_t len = 0;
    ULONG need = 0;
    UCHAR type = 0;
    KIRQL irql;
    BOOLEAN done = FALSE;
    USHORT op = 0, op2 = 0;
    KeAcquireSpinLock(&Dev->Lock, &irql);
    if (F->count && NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Q, &req))) {
        st = WdfRequestRetrieveOutputBuffer(req, FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data), (PVOID*)&ctx, &cap);
        if (NT_SUCCESS(st) && nbt_fifo_peek(F, &type, &len) != NBT_OK)
            st = STATUS_INTERNAL_ERROR;
        if (NT_SUCCESS(st)) {
            need = (ULONG)FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data) + len;
            if (cap >= need) {
                nbt_fifo_pop(F, ctx->Data, (uint32_t)(cap - FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data)), &type, &len);
                ctx->DataLen = len;
                ctx->Type = type;
                op = len ? ctx->Data[0] : 0;
                if (len >= 5 && ctx->Data[0] == NBT_HCI_EVT_CMD_COMPLETE)
                    op2 = (USHORT)(ctx->Data[3] | (ctx->Data[4] << 8));
                else if (len >= 6 && ctx->Data[0] == NBT_HCI_EVT_CMD_STATUS)
                    op2 = (USHORT)(ctx->Data[4] | (ctx->Data[5] << 8));
                else if (len >= 2 && type == HciPacketAclData)
                    op = (USHORT)(ctx->Data[0] | ((ctx->Data[1] & 0x0F) << 8));    /* ACL handle */
            } else {
                nbt_fifo_drop(F);                          /* can never fit this reader: drop, report */
                Dev->CntDropFifo++;
                st = STATUS_BUFFER_TOO_SMALL;
            }
        }
        done = TRUE;
    }
    KeReleaseSpinLock(&Dev->Lock, irql);
    if (done) {
        if (NT_SUCCESS(st))
            Dev->CntReadDone++;
        NwbtTraceIo(Dev, 'R', IOCTL_BTHX_READ_HCI, sizeof(ULONG), cap, st, type, op, op2);
        WdfRequestCompleteWithInformation(req, st, NT_SUCCESS(st) ? need : 0);
    }
    return done;
}

static void Deliver(PNWBT_DEVICE Dev)
{
    ULONG n = 0;
    while (n++ < 64 && (DeliverOne(Dev, Dev->EvtQ, &Dev->EvtFifo) | DeliverOne(Dev, Dev->AclQ, &Dev->AclFifo)))
        ;
}

static NTSTATUS WriteOne(PNWBT_DEVICE Dev, WDFREQUEST Req, BOOLEAN* Full)
{
    PBTHX_HCI_READ_WRITE_CONTEXT ctx = NULL;
    size_t inlen = 0;
    PULONG out = NULL;
    size_t outlen = 0;
    NTSTATUS st;
    nbt_xring* r;
    KIRQL irql;
    int id;
    *Full = FALSE;
    st = WdfRequestRetrieveInputBuffer(Req, FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data), (PVOID*)&ctx, &inlen);
    if (!NT_SUCCESS(st))
        return st;
    st = WdfRequestRetrieveOutputBuffer(Req, sizeof(ULONG), (PVOID*)&out, &outlen);
    if (!NT_SUCCESS(st))
        return st;
    if (ctx->DataLen > inlen - FIELD_OFFSET(BTHX_HCI_READ_WRITE_CONTEXT, Data))
        return STATUS_INVALID_PARAMETER;
    if (ctx->Type == HciPacketCommand)
        r = &Dev->L.x[NBT_XR_HCI_H2D];
    else if (ctx->Type == HciPacketAclData)
        r = &Dev->L.x[NBT_XR_ACL_H2D];
    else
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Dev->Lock, &irql);
    if (!Dev->HciReady || !Dev->Running)
        id = NBT_ERR_RING;
    else
        id = PutLocked(Dev, r, ctx->Data, ctx->DataLen);
    if (id >= 0) {
        if (ctx->Type == HciPacketCommand)
            Dev->CntCmdTx++;
        else
            Dev->CntAclTx++;
    }
    KeReleaseSpinLock(&Dev->Lock, irql);
    NwbtTraceIo(Dev, id == NBT_ERR_FULL ? 'H' : 'W', IOCTL_BTHX_WRITE_HCI, inlen, outlen,
                id >= 0 ? STATUS_SUCCESS : (NTSTATUS)(0xC0000000u | (ULONG)(-id)), ctx->Type,
                (USHORT)(ctx->DataLen >= 2 ? (ctx->Data[0] | (ctx->Data[1] << 8)) : 0), (USHORT)ctx->DataLen);
    if (id == NBT_ERR_FULL) {
        *Full = TRUE;
        return STATUS_PENDING;
    }
    if (id < 0)
        return id == NBT_ERR_TOOBIG ? STATUS_INVALID_BUFFER_SIZE : STATUS_DEVICE_NOT_READY;
    *out = ctx->DataLen;
    WdfRequestCompleteWithInformation(Req, STATUS_SUCCESS, sizeof(ULONG));
    return STATUS_SUCCESS;
}

static void PumpWrites(PNWBT_DEVICE Dev)
{
    WDFREQUEST req;
    ULONG n = 0;
    while (n++ < 64 && NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Dev->WriteQ, &req))) {
        BOOLEAN full;
        NTSTATUS st = WriteOne(Dev, req, &full);
        if (full) {
            if (!NT_SUCCESS(WdfRequestRequeue(req)))
                WdfRequestComplete(req, STATUS_DEVICE_BUSY);
            break;
        }
        if (!NT_SUCCESS(st))
            WdfRequestComplete(req, st);
    }
}

void NwbtEvtTimer(WDFTIMER Timer)
{
    PNWBT_DEVICE Dev = NwbtGetDevice(WdfTimerGetParentObject(Timer));
    ULONG period = Dev->Cfg.PollIntervalMs ? Dev->Cfg.PollIntervalMs : 2;
    KeAcquireSpinLockAtDpcLevel(&Dev->Lock);
    if (Dev->Running && Dev->Blk && !Dev->Dead) {
        static const UCHAR order[] = { NBT_CR_CONTROL_ACK, NBT_CR_HCI_ACL_EVENT, NBT_CR_HCI_ACL_ACK,
                                       NBT_CR_SCO_ACK, NBT_CR_SCO_EVENT };
        ULONG i;
        Dev->CntPoll++;
        for (i = 0; i < ARRAYSIZE(order); i++)
            PollCringLocked(Dev, order[i]);
        if (Dev->MsiPage && *(volatile ULONG*)Dev->MsiPage) {
            Dev->CntMsi++;
            *(volatile ULONG*)Dev->MsiPage = 0;
        }
        Dev->TicksSinceHealth += period;
        if (Dev->TicksSinceHealth >= 1000) {              /* once a second: chip powered? firmware alive? */
            ULONG gen = NwbtCtlRead(Dev, BTC_GEN);       /* control page first: GEN changes on every   */
            Dev->TicksSinceHealth = 0;                    /* power cycle / chip reset / quiet (fence)   */
            if (gen != Dev->Gen) {
                Dev->Dead = TRUE;
                Dev->DeadReason = 1;                      /* fence generation changed */
                Dev->DeadGen = gen;
                Dev->LastRti = 0xFFFFFFFF;
            } else {
                ULONG rti = NwbtB2R(Dev, NBT_BAR2_RTI_STATUS);
                Dev->LastRti = rti;
                if (rti != NBT_RTI_STATE2) {              /* all-ones = fenced: also dead */
                    Dev->Dead = TRUE;
                    Dev->DeadReason = 2;
                }
            }
        }
    }
    KeReleaseSpinLockFromDpcLevel(&Dev->Lock);
    if (Dev->Dead && Dev->Running && Dev->Cfg.AutoRestart &&
        InterlockedCompareExchange(&Dev->RecoverQueued, 1, 0) == 0)
        WdfWorkItemEnqueue(Dev->RecoverWork);
    if (Dev->Running) {
        PumpWrites(Dev);
        Deliver(Dev);
    }
    Dev->TicksSinceDiag += period;
    if (Dev->TicksSinceDiag >= 2000 && InterlockedCompareExchange(&Dev->DiagQueued, 1, 0) == 0) {
        Dev->TicksSinceDiag = 0;
        WdfWorkItemEnqueue(Dev->DiagWork);
    }
}

/* ---------------------------------------------------------------- BthX IOCTLs (forwarded by the PDO) */
void NwbtEvtIoDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutLen, size_t InLen, ULONG Code)
{
    PNWBT_DEVICE Dev = NwbtGetDevice(WdfIoQueueGetDevice(Queue));
    NTSTATUS st = STATUS_INVALID_DEVICE_REQUEST;
    PVOID buf = NULL;
    size_t len = 0;
    NwbtTraceIo(Dev, 'A', Code, InLen, OutLen, WdfRequestGetRequestorMode(Request), 0, 0, 0);
    if (WdfRequestGetRequestorMode(Request) != KernelMode) {
        NwbtTraceIo(Dev, 'C', Code, InLen, OutLen, STATUS_ACCESS_DENIED, 0, 0, 0);
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);    /* METHOD_NEITHER: kernel callers only */
        return;
    }
    switch (Code) {
    case IOCTL_BTHX_GET_VERSION:
        st = WdfRequestRetrieveOutputBuffer(Request, sizeof(BTHX_VERSION), &buf, &len);
        if (NT_SUCCESS(st)) {
            ((PBTHX_VERSION)buf)->Version = BTHX_DDI_VERSION_1;
            NwbtTraceIo(Dev, 'C', Code, InLen, OutLen, STATUS_SUCCESS, 0, BTHX_DDI_VERSION_1, 0);
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(BTHX_VERSION));
            return;
        }
        break;
    case IOCTL_BTHX_SET_VERSION:
        st = WdfRequestRetrieveInputBuffer(Request, sizeof(BTHX_VERSION), &buf, &len);
        if (NT_SUCCESS(st)) {
            Dev->BthxVersionSet = ((PBTHX_VERSION)buf)->Version;
            NwbtDiagDword(Dev, L"NwbtBthxVersionSet", Dev->BthxVersionSet);
            st = STATUS_SUCCESS;
        }
        break;
    case IOCTL_BTHX_QUERY_CAPABILITIES:
        st = WdfRequestRetrieveOutputBuffer(Request, sizeof(BTHX_CAPABILITIES), &buf, &len);
        if (NT_SUCCESS(st)) {
            PBTHX_CAPABILITIES c = (PBTHX_CAPABILITIES)buf;
            RtlZeroMemory(c, sizeof(*c));
            c->MaxAclTransferInSize = NWBT_MAX_ACL_IN;
            /* bthxddi.h docs (_BTHX_CAPABILITIES): ScoSupport "must be set to ScoSupportHCIBypass",
             * MaxScoChannels "must be set to 1" (serialhcibus sample: the same).  s261e had None / 0:
             * BthMini failed its start with STATUS_DEVICE_CONFIGURATION_ERROR before any HCI write. */
            c->ScoSupport = (BTHX_SCO_SUPPORT)Dev->Cfg.ScoSupport;
            c->MaxScoChannels = Dev->Cfg.MaxScoChannels;
            c->IsDeviceIdleCapable = FALSE;
            c->IsDeviceWakeCapable = FALSE;
            Dev->BthxOpen = TRUE;                      /* from here on events/ACL are kept for BthMini */
            NwbtTraceIo(Dev, 'C', Code, InLen, OutLen, STATUS_SUCCESS, (UCHAR)c->ScoSupport,
                        (USHORT)c->MaxAclTransferInSize, (USHORT)c->MaxScoChannels);
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(BTHX_CAPABILITIES));
            return;
        }
        break;
    case IOCTL_BTHX_WRITE_HCI: {
        BOOLEAN full = FALSE;
        Dev->BthxOpen = TRUE;
        /* keep order: while earlier writes wait, new ones queue behind them */
        {
            WDFREQUEST peek = NULL;
            if (NT_SUCCESS(WdfIoQueueFindRequest(Dev->WriteQ, NULL, NULL, NULL, &peek))) {
                WdfObjectDereference(peek);
                full = TRUE;
            }
        }
        if (!full) {
            st = WriteOne(Dev, Request, &full);
            if (st == STATUS_SUCCESS)
                return;
        }
        if (full) {
            Dev->CntWriteHeld++;
            st = WdfRequestForwardToIoQueue(Request, Dev->WriteQ);
            if (NT_SUCCESS(st))
                return;
        }
        break;
    }
    case IOCTL_BTHX_READ_HCI: {
        PULONG type = NULL;
        Dev->BthxOpen = TRUE;
        st = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), (PVOID*)&type, &len);
        if (!NT_SUCCESS(st))
            break;
        if (*type == HciPacketEvent)
            st = WdfRequestForwardToIoQueue(Request, Dev->EvtQ);
        else if (*type == HciPacketAclData)
            st = WdfRequestForwardToIoQueue(Request, Dev->AclQ);
        else
            st = STATUS_INVALID_PARAMETER;
        if (NT_SUCCESS(st)) {
            if (Dev->Running)
                Deliver(Dev);
            return;
        }
        break;
    }
    default:
        st = STATUS_NOT_SUPPORTED;
        break;
    }
    NwbtTraceIo(Dev, 'C', Code, InLen, OutLen, st, 0, 0, 0);
    WdfRequestComplete(Request, st);
}

void NwbtFailPending(PNWBT_DEVICE Dev, NTSTATUS St)
{
    WDFREQUEST req;
    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Dev->WriteQ, &req)))
        WdfRequestComplete(req, St);
}
