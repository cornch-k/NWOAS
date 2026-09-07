/* ==========================================================================================
 * NWOAS Stage-8 dispatcher-wait diagnostic  (READ-ONLY, one-shot, zero guest writes)
 * Insert at file scope in hv_exc.c, immediately BEFORE `void hv_exc_fiq(...)` (before line 1577).
 * One call site inside the existing `if (nwoas_do_dump) { ... }` block (after the wpc loop).
 *
 * All ntoskrnl offsets independently re-verified by capstone disassembly of
 * ntoskrnl.exe build 22621.525 (ImageBase 0x140000000). See Task-D offset table.
 * Every guest access: guest VA -> hv_translate(s1=false => full stage1+2) -> PA -> load;
 * a translation fault returns false and is skipped (hv_vm.c dprintf is a no-op => silent).
 * ========================================================================================== */

/* ---- guest read helpers (read-only; fault => false) ---- */
static bool nwoas_gr64(u64 gva, u64 *out)
{
    u64 pa = hv_translate(gva, false, false, NULL);   /* s1=false => at s12e1r (guest VA->PA) */
    if (!pa) return false;
    *out = read64(pa);
    return true;
}
static bool nwoas_gr32(u64 gva, u32 *out)
{
    u64 pa = hv_translate(gva, false, false, NULL);
    if (!pa) return false;
    *out = read32(pa);
    return true;
}
static bool nwoas_gr8(u64 gva, u8 *out)
{
    u32 w;
    if (!nwoas_gr32(gva & ~3ULL, &w)) return false;   /* aligned word read, then extract byte */
    *out = (w >> ((gva & 3) * 8)) & 0xff;
    return true;
}
static inline bool nwoas_kva(u64 va) { return (va >> 48) == 0xffffULL; }  /* TTBR1 kernel VA */

/* worded hexdump: re-translate every 8 bytes => page-boundary safe; unmapped => marked. */
static void nwoas_ghex(const char *tag, u64 gva, u32 len)
{
    for (u32 o = 0; o < len; o += 8) {
        u64 w;
        if (nwoas_gr64(gva + o, &w))
            printf("HVLOG: NW8 %s+0x%x VA=0x%lx = 0x%lx\n", tag, o, gva + o, w);
        else
            printf("HVLOG: NW8 %s+0x%x VA=0x%lx <unmapped>\n", tag, o, gva + o);
        hv_wdt_pet();
        udelay(200);   /* uart throttle @115200 */
    }
}

/* Unaligned CPER fields are packed by the UEFI/WHEA ABI.  Rebuild them from
 * byte reads so EL2 never performs an unaligned physical load. */
static bool nwoas_gu16(u64 gva, u16 *out)
{
    u8 b0, b1;
    if (!nwoas_gr8(gva, &b0) || !nwoas_gr8(gva + 1, &b1)) return false;
    *out = (u16)b0 | ((u16)b1 << 8);
    return true;
}

static bool nwoas_gu32(u64 gva, u32 *out)
{
    u8 b[4];
    for (int i = 0; i < 4; i++)
        if (!nwoas_gr8(gva + (u64)i, &b[i])) return false;
    *out = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
    return true;
}

/* D49: preserve the fatal CPER record while the guest is stopped at the
 * common bugcheck entry.  Header=0x80 and each section descriptor=0x48 per
 * the packed UEFI CPER layout.  The raw dump is bounded to 4 KiB. */
static void nwoas_whea_dump(u64 rec)
{
    u32 sig = 0, sigend = 0, severity = 0, length = 0;
    u16 sections = 0;
    if (!nwoas_kva(rec) || !nwoas_gu32(rec, &sig)) {
        printf("HVLOG: WHEA-CPER unavailable va=0x%lx\n", rec);
        return;
    }
    nwoas_gu32(rec + 6, &sigend);
    nwoas_gu16(rec + 10, &sections);
    nwoas_gu32(rec + 12, &severity);
    nwoas_gu32(rec + 20, &length);
    printf("HVLOG: WHEA-CPER va=0x%lx sig=0x%x sigend=0x%x sections=%u severity=%u length=0x%x\n",
           rec, sig, sigend, sections, severity, length);

    u16 n = sections > 8 ? 8 : sections;
    for (u16 i = 0; i < n; i++) {
        u64 d = rec + 0x80 + (u64)i * 0x48;
        u32 off = 0, len = 0, flags = 0, secsev = 0;
        nwoas_gu32(d, &off);
        nwoas_gu32(d + 4, &len);
        nwoas_gu32(d + 12, &flags);
        nwoas_gu32(d + 48, &secsev);
        printf("HVLOG: WHEA-DESC[%u] off=0x%x len=0x%x flags=0x%x severity=%u typeva=0x%lx\n",
               i, off, len, flags, secsev, d + 16);
    }

    u32 dump_len = (length >= 0x80 && length <= 0x1000) ? length : 0x400;
    nwoas_ghex("WHEA", rec, dump_len);
}

/* ntoskrnl KASLR base from any in-image kernel PC (idle WFI); self-verified: MZ + 'PE\0\0'
 * + AddressOfEntryPoint == KiSystemStartup rva 0x9a3340. SizeOfImage 0x103d000 < 0x1200 pages. */
static u64 nwoas_find_ntos_base(u64 kpc)
{
    u64 page = kpc & ~0xfffULL;
    for (u32 i = 0; i < 0x1200; i++) {
        u64 base = page - (u64)i * 0x1000;
        u32 peoff, sig, ent;
        u8 mz0, mz1;
        if (!nwoas_gr8(base, &mz0) || mz0 != 'M') continue;
        if (!nwoas_gr8(base + 1, &mz1) || mz1 != 'Z') continue;
        if (!nwoas_gr32(base + 0x3c, &peoff) || peoff > 0x400) continue;
        if (!nwoas_gr32(base + peoff, &sig) || sig != 0x00004550) continue;   /* 'PE\0\0' */
        if (!nwoas_gr32(base + peoff + 0x28, &ent)) continue;                  /* AddressOfEntryPoint */
        if (ent == 0x9a3340) return base;                                     /* KiSystemStartup */
        hv_wdt_pet();
    }
    return 0;
}

/* ---- Task-D CONFIRMED offsets (ntoskrnl 22621.525) ---- */
#define NW_KT_TYPE      0x000  /* DISPATCHER_HEADER.Type   KeInitThread 0x247cc0 (strh w8,[x19]) */
#define NW_KT_STATE     0x17c  /* KTHREAD.State (Waiting=5) 0x2559ec / 0x2889f4                  */
#define NW_KT_WAITREAS  0x2a3  /* KTHREAD.WaitReason        0x2559f4 / 0x2889f8 (cmp #0xf WrQueue)*/
#define NW_KT_WBLIST    0x0c8  /* KTHREAD.WaitBlockList     KiCommitThreadWait 0x2887fc          */
#define NW_KT_WB0       0x138  /* KTHREAD.WaitBlock[0]      0x255894 (add x24,x19,#0x138)        */
#define NW_KT_WB0_OBJ   0x158  /* WaitBlock[0].Object = 0x138+0x20                               */
#define NW_KT_PROCESS   0x240  /* KTHREAD.Process           IoThreadToProcess 0x2474c0           */
#define NW_WB_WAITTYPE  0x010  /* KWAIT_BLOCK.WaitType      0x255a1c                              */
#define NW_WB_BLOCKST   0x011  /* KWAIT_BLOCK.BlockState    0x255a20 / 0x288a94                   */
#define NW_WB_THREAD    0x018  /* KWAIT_BLOCK.Thread        INFERRED (self-check only)            */
#define NW_WB_OBJECT    0x020  /* KWAIT_BLOCK.Object        0x255a30 / 0x288a78 (ldr [x25,#0x20]) */
#define NW_DH_SIGNAL    0x004  /* DISPATCHER_HEADER.SignalState  0x26e1e4 (str w1,[x0,#4])        */
#define NW_PRCB_CURTH   0x988  /* KPRCB.CurrentThread from KPCR(x18)  ldr x19,[x18,#0x988] x3     */
#define NW_PSISP_RVA    0xd1aa20  /* PsInitialSystemProcess export rva (export table CONFIRMED)   */

/* ---- One-shot common bugcheck probe (D45) ----
 * Windows 11 ARM64 ntoskrnl.exe build 22621.525 routes KeBugCheck,
 * KeBugCheckEx, and direct internal callers to RVA 0x256d40.  D44 proved the
 * exported KeBugCheckEx veneer can be bypassed.  The common entry starts with
 * PACIBSP; replacing it with HVC keeps x0..x5 (BugCheckCode + parameters and
 * internal flag) intact.  On the trap we restore PACIBSP and re-execute it,
 * preserving the original bugcheck path exactly.
 */
#define NW_KEBUGCHECKEX_RVA 0x256d40
#define NW_KEBUGCHECKEX_WORD 0xd503237fU
#define NW_BUGCHECK_HVC_IMM 0x124U
#define NW_BUGCHECK_HVC_WORD (0xd4000002U | (NW_BUGCHECK_HVC_IMM << 5))

static u64 nwoas_bugcheck_va;
static u64 nwoas_bugcheck_pa;
static u32 nwoas_bugcheck_state; /* 0=searching, 1=armed, 2=fired, 3=incompatible */
static u32 nwoas_bugcheck_searches;

static void nwoas_bugcheck_sync_icache(u64 pa)
{
    dc_cvau_range((void *)(pa & ~0x3fULL), 64);
    sysop("dsb ish");
    /* KeBugCheckEx may run on any guest CPU.  Broadcast the invalidate. */
    sysop("ic ialluis");
    sysop("dsb ish");
    sysop("isb");
}

static void nwoas_bugcheck_arm(struct exc_info *ctx)
{
    if (nwoas_bugcheck_state != 0 || !nwoas_kva(ctx->elr))
        return;

    /* A handful of early high VAs can belong to boot drivers.  Retry a small
     * bounded number of FIQ samples until one lies inside ntoskrnl. */
    if (nwoas_bugcheck_searches++ >= 16) {
        nwoas_bugcheck_state = 3;
        printf("HVLOG: BUGPROBE incompatible: ntos base not found after 16 kernel samples\n");
        return;
    }

    u64 ntos = nwoas_find_ntos_base(ctx->elr);
    if (!ntos)
        return;

    u64 va = ntos + NW_KEBUGCHECKEX_RVA;
    /* We write through EL2's physical alias, so the guest's read-only text
     * permission must not make address discovery fail. */
    u64 pa = hv_translate(va, false, false, NULL);
    if (!pa) {
        nwoas_bugcheck_state = 3;
        printf("HVLOG: BUGPROBE incompatible: common bugcheck translation failed ntos=0x%lx va=0x%lx\n",
               ntos, va);
        return;
    }

    u32 old = read32(pa);
    if (old != NW_KEBUGCHECKEX_WORD) {
        nwoas_bugcheck_state = 3;
        printf("HVLOG: BUGPROBE incompatible: ntos=0x%lx va=0x%lx pa=0x%lx word=0x%x expected=0x%x\n",
               ntos, va, pa, old, NW_KEBUGCHECKEX_WORD);
        return;
    }

    write32(pa, NW_BUGCHECK_HVC_WORD);
    nwoas_bugcheck_sync_icache(pa);
    nwoas_bugcheck_va = va;
    nwoas_bugcheck_pa = pa;
    nwoas_bugcheck_state = 1;
    printf("HVLOG: BUGPROBE armed ntos=0x%lx va=0x%lx pa=0x%lx hvc=0x%x\n",
           ntos, va, pa, NW_BUGCHECK_HVC_WORD);
}

static bool nwoas_bugcheck_handle(struct exc_info *ctx)
{
    if (nwoas_bugcheck_state != 1)
        return false;

    u64 trap_va = 0;
    u64 cur_pa = hv_translate(ctx->elr, false, false, NULL);
    u64 prev_pa = hv_translate(ctx->elr - 4, false, false, NULL);
    if (cur_pa && read32(cur_pa) == NW_BUGCHECK_HVC_WORD)
        trap_va = ctx->elr;
    else if (prev_pa && read32(prev_pa) == NW_BUGCHECK_HVC_WORD)
        trap_va = ctx->elr - 4;

    if (trap_va != nwoas_bugcheck_va) {
        printf("HVLOG: BUGPROBE unexpected HVC124 elr=0x%lx trap=0x%lx expected=0x%lx\n",
               ctx->elr, trap_va, nwoas_bugcheck_va);
        return false;
    }

    write32(nwoas_bugcheck_pa, NW_KEBUGCHECKEX_WORD);
    nwoas_bugcheck_sync_icache(nwoas_bugcheck_pa);
    nwoas_bugcheck_state = 2;

    printf("HVLOG: BUGCHECK code=0x%lx p1=0x%lx p2=0x%lx p3=0x%lx p4=0x%lx "
           "x5=0x%lx cpu=%lu elr=0x%lx lr=0x%lx\n",
           ctx->regs[0], ctx->regs[1], ctx->regs[2], ctx->regs[3], ctx->regs[4],
           ctx->regs[5], ctx->cpu_id, ctx->elr, ctx->regs[30]);
    printf("HVLOG: BUGCHECK-SERR esr1=0x%lx far1=0x%lx disr=0x%lx isr=0x%lx "
           "hcr2=0x%lx l2sts=0x%lx l2adr=0x%lx l2inf=0x%lx\n",
           mrs(ESR_EL1), mrs(FAR_EL1), mrs(DISR_EL1), mrs(ISR_EL1), mrs(HCR_EL2),
           mrs(SYS_IMP_APL_L2C_ERR_STS), mrs(SYS_IMP_APL_L2C_ERR_ADR),
           mrs(SYS_IMP_APL_L2C_ERR_INF));
#ifdef ENABLE_VGIC_MODULE
    printf("HVLOG: BUGCHECK-VGIC ich_hcr=0x%lx vmcr=0x%lx misr=0x%lx eisr=0x%lx elrsr=0x%lx\n",
           mrs(ICH_HCR_EL2), mrs(ICH_VMCR_EL2), mrs(ICH_MISR_EL2),
           mrs(ICH_EISR_EL2), mrs(ICH_ELRSR_EL2));
    for (int i = 0; i < 8; i++)
        printf("HVLOG: BUGCHECK-VGIC lr%d=0x%lx\n", i, hv_vgic3_read_lr(i));
#endif
    if (ctx->regs[0] == 0x124 && ctx->regs[2])
        nwoas_whea_dump(ctx->regs[2]);

    /* Re-execute the restored PACIBSP.  HVC normally reports trap_va+4. */
    ctx->elr = trap_va;
    return true;
}

static const char *nw_objtype(u8 t)
{
    switch (t & 0x7f) {
    case 0: return "Event-Notif";  case 1: return "Event-Sync";  case 2: return "Mutant";
    case 3: return "Process";      case 4: return "Queue";       case 5: return "Semaphore";
    case 6: return "Thread";       case 7: return "Gate";
    case 8: return "Timer-Notif";  case 9: return "Timer-Sync";  default: return "?";
    }
}
static const char *nw_state(u8 s)
{
    static const char *const n[] = {"Init", "Ready", "Running", "Standby", "Terminated",
                                    "Waiting", "Transition", "DeferredReady", "GateWait",
                                    "WaitingUnsafe"};
    return (s < 10) ? n[s] : "?";
}
static const char *nw_reason(u8 r)
{
    static const char *const n[] = {
        "Executive", "FreePage", "PageIn", "PoolAllocation", "DelayExecution", "Suspended",
        "UserRequest", "WrExecutive", "WrFreePage", "WrPageIn", "WrPoolAllocation",
        "WrDelayExecution", "WrSuspended", "WrUserRequest", "WrEventPair", "WrQueue",
        "WrLpcReceive", "WrLpcReply", "WrVirtualMemory", "WrPageOut", "WrRendezvous",
        "WrKeyedEvent", "WrTerminated", "WrProcessInSwap", "WrCpuRateControl", "WrCalloutStack",
        "WrKernel", "WrResource", "WrPushLock", "WrMutex", "WrQuantumEnd", "WrDispatchInt",
        "WrPreempted", "WrYieldExecution", "WrFastMutex", "WrGuardedMutex", "WrRundown",
        "WrAlertByThreadId", "WrDeferredPreempt", "WrPhysicalFault", "WrIoRing", "WrMdlCache"};
    return (r < (u8)ARRAY_SIZE(n)) ? n[r] : "?";
}

/* Resolve KTHREAD base from a thread-list node using CONFIRMED anchors only:
 * Type==6 at +0x00 AND Process==sys_eproc at +0x240. Yields the KTHREAD.ThreadListEntry offset. */
static u64 nw_kt_from_node(u64 node, u64 sys_eproc, u32 *tle_off)
{
    for (u32 d = 0x40; d <= 0xc00; d += 8) {
        u64 b = node - d, proc;
        u8 ty;
        if (!nwoas_gr8(b + NW_KT_TYPE, &ty) || ty != 6) continue;
        if (!nwoas_gr64(b + NW_KT_PROCESS, &proc)) continue;
        if (proc == sys_eproc) {
            if (tle_off) *tle_off = d;
            return b;
        }
    }
    return 0;
}

/* Discover KPROCESS.ThreadListHead inside EPROCESS by circular-list invariant + node->KTHREAD. */
static u64 nw_find_threadlist(u64 eproc, u32 *out_off, u32 *out_tle)
{
    for (u32 o = 0x20; o <= 0x180; o += 8) {
        u64 f, bl, blf, fbl;
        if (!nwoas_gr64(eproc + o, &f) || !nwoas_gr64(eproc + o + 8, &bl)) continue;
        if (!nwoas_kva(f) || !nwoas_kva(bl) || f == eproc + o) continue;  /* unmapped / empty */
        if (!nwoas_gr64(bl, &blf) || blf != eproc + o) continue;          /* Blink->Flink == head */
        if (!nwoas_gr64(f + 8, &fbl) || fbl != eproc + o) continue;       /* Flink->Blink == head */
        u32 tle = 0;
        if (nw_kt_from_node(f, eproc, &tle)) {
            *out_off = o;
            *out_tle = tle;
            return eproc + o;
        }
    }
    return 0;
}

/* Decode + print one KTHREAD's wait state; raw=1 => raw window dumps (offline fallback). */
static void nw_report_thread(u64 t, int raw)
{
    u8 state = 0, reason = 0, otype = 0, wtype = 0, bstate = 0;
    u32 sig = 0;
    u64 wbl = 0, objA = 0, objB = 0, wbthr = 0;

    nwoas_gr8(t + NW_KT_STATE, &state);
    nwoas_gr8(t + NW_KT_WAITREAS, &reason);
    nwoas_gr64(t + NW_KT_WBLIST, &wbl);      /* primary KWAIT_BLOCK ptr */
    nwoas_gr64(t + NW_KT_WB0_OBJ, &objB);    /* embedded WaitBlock[0].Object (cross-check) */
    if (nwoas_kva(wbl)) {
        nwoas_gr64(wbl + NW_WB_OBJECT, &objA);
        nwoas_gr8(wbl + NW_WB_WAITTYPE, &wtype);
        nwoas_gr8(wbl + NW_WB_BLOCKST, &bstate);
        nwoas_gr64(wbl + NW_WB_THREAD, &wbthr);
    }
    u64 obj = nwoas_kva(objA) ? objA : objB;
    if (nwoas_kva(obj)) {
        nwoas_gr8(obj, &otype);
        nwoas_gr32(obj + NW_DH_SIGNAL, &sig);
    }

    printf("HVLOG: NW8   t=0x%lx state=%d(%s) reason=%d(%s)\n",
           t, state, nw_state(state), reason, nw_reason(reason));
    printf("HVLOG: NW8   wbl=0x%lx objA=0x%lx objB=0x%lx match=%d wb.Thread=0x%lx eq_t=%d\n",
           wbl, objA, objB, (objA == objB), wbthr, (wbthr == t));
    printf("HVLOG: NW8   ==> OBJECT=0x%lx TYPE=0x%x(%s) Signal=%d WaitType=%d BlockState=%d\n",
           obj, otype, nw_objtype(otype), (int)sig, wtype, bstate);

    if (raw) {
        nwoas_ghex("KT", t, 0x60);            /* header/Type/Size/Signal region */
        nwoas_ghex("KTst", t + 0x170, 0x18);  /* State/WaitIrql/WaitMode */
        nwoas_ghex("KTpr", t + 0x240, 0x10);  /* Process */
        nwoas_ghex("KTwr", t + 0x2a0, 0x10);  /* WaitReason */
        nwoas_ghex("KTwb0", t + NW_KT_WB0, 0x30); /* embedded WaitBlock[0] */
        if (nwoas_kva(wbl)) nwoas_ghex("WBL", wbl, 0x30);  /* primary KWAIT_BLOCK */
        if (nwoas_kva(obj)) nwoas_ghex("OBJ", obj, 0x18);  /* DISPATCHER_HEADER */
    }
}

/* Main entry: called once from the wedge one-shot dump block. READ-ONLY. */
static void nwoas_stage8_walk(struct exc_info *ctx)
{
    u64 x18 = ctx->regs[18];         /* Windows kernel x18 = KPCR (page-aligned)      */
    u64 tpidr = mrs(TPIDR_EL1);      /* guest TPIDR_EL1 = KPCR (Task-D correction)    */
    u64 ntos = nwoas_find_ntos_base(ctx->elr);

    printf("HVLOG: NW8 ===== stage8 dispatcher-wait dump =====\n");
    printf("HVLOG: NW8 ntos=0x%lx elr=0x%lx x18=0x%lx tpidr_el1=0x%lx kpcrX18=0x%lx kpcrTP=0x%lx\n",
           ntos, ctx->elr, x18, tpidr, x18 & ~0xfffULL, tpidr & ~0xfffULL);

    /* idle CurrentThread (context only; the blocked thread is scheduled out) */
    u64 cur = 0;
    nwoas_gr64((x18 & ~0xfffULL) + NW_PRCB_CURTH, &cur);
    if (nwoas_kva(cur)) {
        u8 st = 0;
        nwoas_gr8(cur + NW_KT_STATE, &st);
        printf("HVLOG: NW8 idle CurrentThread=0x%lx state=%d(%s)\n", cur, st, nw_state(st));
    }

    if (!ntos) {
        printf("HVLOG: NW8 FAIL: ntos base not found; raw KPCR:\n");
        nwoas_ghex("KPCR", x18 & ~0xfffULL, 0x40);
        return;
    }

    u64 sysproc = 0;
    if (!nwoas_gr64(ntos + NW_PSISP_RVA, &sysproc) || !nwoas_kva(sysproc)) {
        printf("HVLOG: NW8 FAIL: PsInitialSystemProcess deref=0x%lx\n", sysproc);
        return;
    }

    u32 tlh_off = 0, tle_off = 0;
    u64 tlh = nw_find_threadlist(sysproc, &tlh_off, &tle_off);
    printf("HVLOG: NW8 SystemEPROC=0x%lx ThreadListHead=0x%lx (KPROC.off=0x%x KT.tle_off=0x%x)\n",
           sysproc, tlh, tlh_off, tle_off);
    nwoas_ghex("EPROC", sysproc, 0x80);
    if (!tlh) {
        printf("HVLOG: NW8 FAIL: ThreadListHead not discovered; EPROC raw above\n");
        return;
    }

    u64 node = 0;
    nwoas_gr64(tlh, &node);
    int n = 0, waiting = 0, rawbudget = 6;
    while (nwoas_kva(node) && node != tlh && n < 200) {
        u64 kt = node - tle_off;
        u8 ty = 0, st = 0;
        nwoas_gr8(kt + NW_KT_TYPE, &ty);
        if (ty == 6) {
            nwoas_gr8(kt + NW_KT_STATE, &st);
            if (st == 5) {   /* Waiting */
                waiting++;
                int raw = (rawbudget > 0);
                if (raw) rawbudget--;
                printf("HVLOG: NW8 [%d] node=0x%lx kt=0x%lx WAITING\n", n, node, kt);
                nw_report_thread(kt, raw);
            }
        } else {
            printf("HVLOG: NW8 [%d] node=0x%lx kt=0x%lx bad-type=0x%x\n", n, node, kt, ty);
            if (rawbudget > 0) { rawbudget--; nwoas_ghex("NODE", node - 0x40, 0x80); }
        }
        u64 next = 0;
        nwoas_gr64(node, &next);   /* Flink */
        node = next;
        n++;
        hv_wdt_pet();
    }
    printf("HVLOG: NW8 ===== done: %d threads walked, %d waiting =====\n", n, waiting);
}
