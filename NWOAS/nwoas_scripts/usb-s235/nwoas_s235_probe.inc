/* S235 USB-A read-corruption PROBE (observation only; no behaviour fix).
 *
 * For bulk endpoints of FL1100 devices it reconstructs, from the guest's own
 * structures, what Windows asked the controller to do and what came back:
 *   - doorbell: newly enqueued TRBs (shadow-compare of the whole transfer ring),
 *     BOT CBW parse on bulk OUT (SCSI opcode / LBA / blocks), per-TRB checks on
 *     bulk IN (64 KiB crossing, not-multiple-of-1024 mid-TD) and a hash of the
 *     destination buffer BEFORE the transfer;
 *   - ERDP write: every event Windows just consumed; for each completed bulk-IN
 *     data TRB: completion code / residual, hash of the buffer as the CPU sees
 *     it, then (after dc ivac at EL2) as DRAM holds it, and a per-4-KiB hash
 *     keyed by LBA, compared with the first time the same LBA was read.
 * Detectors (counted in "S235 ST", first occurrences logged):
 *   unchanged : DRAM content of a completed IN TRB buffer equals its content
 *               before the transfer (device data never arrived there)
 *   incoh     : CPU-cache view != DRAM view at completion
 *   diff      : same LBA, different 4 KiB content than on its first read
 *   x64k / notmps / ccbad / resid : TRB shape and completion anomalies
 * The pre-transfer hash reads are followed by dc civac (no stale clean line is
 * left behind); the completion check does dc ivac on device-written buffers. */
#ifndef NWOAS_S235_PROBE
#define NWOAS_S235_PROBE 1
#endif
/* Perturbation control (the probe itself touches the data buffers):
 *   S235_PRE_HASH=1  : hash each IN buffer at the doorbell, then dc civac it (this writes back any
 *                      dirty guest line BEFORE the device DMA -> can mask a dirty-line problem)
 *   S235_POST_IVAC=1 : dc ivac each completed IN buffer and re-hash from DRAM (discards any dirty
 *                      guest line AFTER the DMA -> can also mask it)
 * With both 0 the probe only READS the buffers at completion (CPU view), like the guest would. */
#ifndef S235_PRE_HASH
#define S235_PRE_HASH 1
#endif
#ifndef S235_POST_IVAC
#define S235_POST_IVAC 1
#endif
#if NWOAS_S235_PROBE

#define S235_SHADOW_N 65536
struct s235_sh {
    u64 key; // TRB IPA | endpoint tracker index (TRB addresses are 16-byte aligned); 0 = empty
    u64 w0, w1;
};
static struct s235_sh s235_shadow[S235_SHADOW_N];

struct s235_pend {
    u64 trb, buf, hbefore;
    u32 len, ctl, lba, tdoff, td;
    u8 op, ed; // ed=1: Event Data TRB marker (buf = event data parameter, len = TD length)
};
#define S235_PEND_N 4096
struct s235_ep {
    u32 slot, dci;
    bool used, primed, in;
    u64 ctxv, cursor;
    u32 ccs;
    struct s235_pend pend[S235_PEND_N];
    u32 ph, pt; // pop at ph, push at pt (free-running)
    u32 td_len, td_trbs, td_id;
    u8 td_op;
    u32 td_lba;
};
static struct s235_ep s235_eps[4];
static struct {
    bool fresh;
    u8 op;
    u32 lba, nblk;
} s235_cmd;
#define S235_TAB_N (4u << 20)
static u32 *s235_tab;
static bool s235_tab_tried;
static int s235_phase = -1;
static u64 s235_evdeq, s235_erstba_seen;
static u32 s235_logs, s235_psc_logs, s235_td_seq;
static struct {
    u64 dbs, tds, trbs, bytes, x64k, notmps, unchanged, incoh, raced, orphan, ccbad, resid, evs,
        xfer_evs, psc, other_ev, maxtd, cbw, read10, p_new, p_same, p_diff, p_skip, p_zero, incomplete,
        gaps, resync, pend_ovf, minbuf, maxbuf, hole, hole_bytes, tdh[6];
} s235_st = {.minbuf = ~0UL};

#define S235_LOG_CAP 600
static u64 s235_ms(void)
{
    u64 f = mrs(CNTFRQ_EL0) / 1000;
    return f ? mrs(CNTPCT_EL0) / f : 0;
}

static inline u64 s235_mix(u64 h, u64 w)
{
    h ^= w;
    h *= 0x9E3779B97F4A7C15UL;
    return h ^ (h >> 29);
}

// One <=16 KiB-page span of a guest low-window buffer, translated and bounds-checked.
static bool s235_span(u64 ipa, u32 len, u64 *pa_out, u32 *n_out)
{
    if (ipa + len < ipa || ipa + len > NWOAS_FL_LOW_WINDOW)
        return false;
    u32 n = PAGE_SIZE - (u32)(ipa & (PAGE_SIZE - 1));
    if (n > len)
        n = len;
    u64 pa = nwoas_ipa_to_pa(ipa);
    if (!pa || !nwoas_in_dram(pa, n))
        return false;
    *pa_out = pa;
    *n_out = n;
    return true;
}

static u64 s235_hash(u64 ipa, u32 len)
{
    u64 h = 0x5235;
    while (len) {
        u64 pa;
        u32 n;
        if (!s235_span(ipa, len, &pa, &n))
            return 0;
        const u8 *p = (const u8 *)pa;
        u32 i = 0;
        for (; i + 8 <= n; i += 8) {
            u64 w;
            memcpy(&w, p + i, 8);
            h = s235_mix(h, w);
        }
        for (; i < n; i++)
            h = s235_mix(h, p[i]);
        ipa += n;
        len -= n;
    }
    return h | 1;
}

static u32 s235_zero_h(void)
{
    static u32 z = 0;
    if (!z) {
        u64 h = 0x5235;
        for (int i = 0; i < 512; i++)
            h = s235_mix(h, 0);
        z = (u32)((h | 1) >> 16) | 1;
    }
    return z;
}

static const char *s235_cls(u32 h, u32 ref)
{
    return h == ref ? "ref" : h == s235_zero_h() ? "zero" : "other";
}

#define S235_RC_N 96
static struct {
    u64 ipa, t0;
    u32 old, lba;
    u8 stage;
} s235_rc[S235_RC_N];
static u32 s235_rc_n;

// Live FL1100 DART (stream 1) leaf for an IOVA vs. the stage-2 PA of the same guest address.
static void s235_dart_diag(u64 iova)
{
    u32 ttbr = read32(FL_DART_BASE + DART_TTBR_OFF + 16 * FL_DART_SID);
    u32 err = read32(FL_DART_BASE + 0x40);
    u64 l1e = 0, l2e = 0, l1phys = ((u64)(ttbr & 0x7fffffff)) << 12;
    u64 l1i = (iova >> 25) & 0x7ff, l2i = (iova >> 14) & 0x7ff;
    if ((ttbr & 0x80000000) && nwoas_in_dram(l1phys + 8 * l1i, 8)) {
        l1e = *(const volatile u64 *)(l1phys + 8 * l1i);
        u64 l2phys = l1e & 0xfffffffc000UL;
        if ((l1e & 1) && nwoas_in_dram(l2phys + 8 * l2i, 8))
            l2e = *(const volatile u64 *)(l2phys + 8 * l2i);
    }
    printf("HVLOG: S235 DART iova=0x%lx s2pa=0x%lx ttbr=0x%x l1e=0x%lx l2e=0x%lx err=0x%x errlo=0x%x errhi=0x%x\n",
           iova, nwoas_ipa_to_pa(iova), ttbr, l1e, l2e, err, read32(FL_DART_BASE + 0x50),
           read32(FL_DART_BASE + 0x54));
}

static void s235_maint(u64 ipa, u32 len, bool clean);
static void s235_recheck(void)
{
    u64 now = s235_ms();
    for (u32 i = 0; i < s235_rc_n; i++) {
        if (s235_rc[i].stage >= 2 || now - s235_rc[i].t0 < (s235_rc[i].stage ? 1500 : 30))
            continue;
        u32 hc = (u32)(s235_hash(s235_rc[i].ipa, 0x1000) >> 16) | 1;
        s235_maint(s235_rc[i].ipa, 0x1000, false);
        u32 hd = (u32)(s235_hash(s235_rc[i].ipa, 0x1000) >> 16) | 1;
        printf("HVLOG: S235 RECHECK age=%lu lba=0x%x buf=0x%lx cpu=%s dram=%s\n", now - s235_rc[i].t0,
               s235_rc[i].lba, s235_rc[i].ipa, s235_cls(hc, s235_rc[i].old),
               s235_cls(hd, s235_rc[i].old));
        s235_rc[i].stage++;
    }
}

static void s235_maint(u64 ipa, u32 len, bool clean)
{
    while (len) {
        u64 pa;
        u32 n;
        if (!s235_span(ipa, len, &pa, &n))
            return;
        nwoas_cache_maint(pa, n, clean);
        ipa += n;
        len -= n;
    }
}

static __attribute__((unused)) struct s235_sh *s235_slot(u64 key)
{
    u32 i = (u32)((key * 0x9E3779B97F4A7C15UL) >> 48) & (S235_SHADOW_N - 1);
    for (u32 n = 0; n < S235_SHADOW_N; n++, i = (i + 1) & (S235_SHADOW_N - 1))
        if (!s235_shadow[i].key || s235_shadow[i].key == key)
            return &s235_shadow[i];
    return NULL;
}

static void s235_reset(void)
{
    memset(s235_shadow, 0, sizeof(s235_shadow));
    memset(s235_eps, 0, sizeof(s235_eps));
    s235_evdeq = 0;
    s235_cmd.fresh = false;
}

static struct s235_ep *s235_find(u32 slot, u32 dci, bool create)
{
    for (int i = 0; i < 4; i++)
        if (s235_eps[i].used && s235_eps[i].slot == slot && s235_eps[i].dci == dci)
            return &s235_eps[i];
    if (!create)
        return NULL;
    for (int i = 0; i < 4; i++)
        if (!s235_eps[i].used) {
            s235_eps[i].used = true;
            s235_eps[i].slot = slot;
            s235_eps[i].dci = dci;
            return &s235_eps[i];
        }
    return NULL;
}

static void s235_new_trb(struct s235_ep *ep, u64 trb, u64 w0, u64 w1)
{
    u32 ctl = (u32)(w1 >> 32);
    u32 type = (ctl >> 10) & 0x3f;
    if (type == 7 && ep->in && ep->td_trbs) { // Event Data TRB: ends the TD (Windows usbxhci style)
        if (ep->pt - ep->ph >= S235_PEND_N) {
            s235_st.pend_ovf++;
        } else {
            struct s235_pend *e = &ep->pend[ep->pt % S235_PEND_N];
            memset(e, 0, sizeof(*e));
            e->trb = trb;
            e->buf = w0;
            e->len = ep->td_len;
            e->op = ep->td_op;
            e->lba = ep->td_lba;
            e->td = ep->td_id;
            e->ed = 1;
            ep->pt++;
        }
        goto td_end;
    }
    if (type != 1) // Normal only
        return;
    u64 buf = w0;
    u32 len = (u32)w1 & 0x1ffff;
    bool chain = ctl & 0x10;
    if (!ep->in) {
        if (len == 31) {
            u64 pa;
            u32 n;
            if (s235_span(buf, 31, &pa, &n) && n == 31) {
                const u8 *c = (const u8 *)pa;
                if (c[0] == 0x55 && c[1] == 0x53 && c[2] == 0x42 && c[3] == 0x43) {
                    s235_st.cbw++;
                    s235_cmd.fresh = true;
                    s235_cmd.op = c[15];
                    s235_cmd.lba = ((u32)c[17] << 24) | ((u32)c[18] << 16) | ((u32)c[19] << 8) | c[20];
                    s235_cmd.nblk = ((u32)c[22] << 8) | c[23];
                    if (s235_cmd.op == 0x28) {
                        static u32 next_lba = 0, seek_logs = 0;
                        s235_st.read10++;
                        if (s235_cmd.lba != next_lba && seek_logs < 400) {
                            seek_logs++;
                            printf("HVLOG: S235 SEEK t=%lu lba=0x%x nblk=0x%x prev_end=0x%x\n", s235_ms(),
                                   s235_cmd.lba, s235_cmd.nblk, next_lba);
                        }
                        next_lba = s235_cmd.lba + s235_cmd.nblk;
                    }
                }
            }
        }
        return;
    }
    if (!ep->td_trbs) { // first TRB of a TD: bind the pending CBW (data stage), else status/other
        ep->td_id = ++s235_td_seq;
        ep->td_op = 0;
        ep->td_lba = 0;
        if (s235_cmd.fresh && len != 13) {
            ep->td_op = s235_cmd.op;
            ep->td_lba = s235_cmd.lba;
            s235_cmd.fresh = false;
        }
    }
    s235_st.trbs++;
    s235_st.bytes += len;
    if (buf < s235_st.minbuf)
        s235_st.minbuf = buf;
    if (buf + len > s235_st.maxbuf)
        s235_st.maxbuf = buf + len;
    if (len && buf < 0xC0100000UL && buf + len > 0xC0000000UL) {
        s235_st.hole++; // IN buffer inside the root-port bridge window (bus 0xC0000000..0xC00FFFFF)
        s235_st.hole_bytes += len;
    }
    if (len && (buf & 0xffff) + len > 0x10000) {
        s235_st.x64k++;
        static u32 x64k_logs = 0;
        if (x64k_logs < 8) {
            x64k_logs++;
            printf("HVLOG: S235 X64K t=%lu td=%u trb=0x%lx buf=0x%lx len=0x%x ctl=0x%x\n", s235_ms(),
                   ep->td_id, trb, buf, len, ctl);
        }
    }
    if (chain && (len & 0x3ff))
        s235_st.notmps++;
    bool sample = ep->td_op == 0x28 && (ep->td_id < 40 || (ep->td_id & 0x7ff) < 2);
    if (sample && s235_logs < S235_LOG_CAP + 400)
        printf("HVLOG: S235 TRB t=%lu td=%u i=%u lba=0x%x off=0x%x trb=0x%lx buf=0x%lx len=0x%x ctl=0x%x\n",
               s235_ms(), ep->td_id, ep->td_trbs, ep->td_lba, ep->td_len, trb, buf, len, ctl);
    if (ep->pt - ep->ph >= S235_PEND_N) {
        s235_st.pend_ovf++;
    } else {
        struct s235_pend *e = &ep->pend[ep->pt % S235_PEND_N];
        e->trb = trb;
        e->buf = buf;
        e->len = len;
        e->ctl = ctl;
        e->op = ep->td_op;
        e->lba = ep->td_lba;
        e->tdoff = ep->td_len;
        e->td = ep->td_id;
        e->ed = 0;
        e->hbefore = 0;
#if S235_PRE_HASH
        if (ep->td_op == 0x28 && len >= 512) {
            e->hbefore = s235_hash(buf, len);
            s235_maint(buf, len, true); // do not leave pre-transfer lines in the CPU cache
        }
#endif
        ep->pt++;
    }
    ep->td_len += len;
    ep->td_trbs++;
    if (!chain) {
    td_end:
        s235_st.tds++;
        if (ep->td_len > s235_st.maxtd)
            s235_st.maxtd = ep->td_len;
        u32 b = ep->td_len <= 0x1000 ? 0 : ep->td_len <= 0x10000 ? 1 : ep->td_len <= 0x20000 ? 2 :
                ep->td_len <= 0x80000 ? 3 : ep->td_len <= 0x100000 ? 4 : 5;
        s235_st.tdh[b]++;
        ep->td_len = 0;
        ep->td_trbs = 0;
    }
}

// One-shot dump of the Apple PCIe root ports' bridge windows and the FL1100 BARs (config space via
// ECAM 0x690000000; bus 0 dev 0..2 = root ports, bus 2 dev 0 = FL1100). A bus-master write whose
// address lies inside the memory window of the bridge above the device is NOT forwarded upstream.
static void s235_ecam_dump(void)
{
    enum exc_guard_t saved = exc_guard;
    exc_count = 0;
    exc_guard = GUARD_SKIP | GUARD_SILENT;
    for (u64 d = 0; d < 3; d++) {
        u64 b = 0x690000000UL + (d << 15);
        u32 id = read32(b), cmd = read32(b + 4), bus = read32(b + 0x18), mem = read32(b + 0x20);
        u32 pf = read32(b + 0x24), pfbu = read32(b + 0x28), pflu = read32(b + 0x2c);
        printf("HVLOG: S235 ECAM port%lu id=0x%x cmd=0x%x bus=0x%x membase=0x%x memlimit=0x%x "
               "pref=0x%x prefbaseup=0x%x preflimitup=0x%x faults=%d\n",
               d, id, cmd & 0xffff, bus & 0xffffff, (mem & 0xfff0) << 16,
               ((mem >> 16) & 0xfff0) << 16 | 0xfffff, pf, pfbu, pflu, exc_count);
    }
    u64 f = 0x690200000UL;
    printf("HVLOG: S235 ECAM fl1100 id=0x%x cmd=0x%x bar0=0x%x bar1=0x%x faults=%d\n", read32(f),
           read32(f + 4) & 0xffff, read32(f + 0x10), read32(f + 0x14), exc_count);
    exc_guard = saved;
}

// Called from the (kernel-gated) FL1100 doorbell trap, before the write reaches the controller.
static void s235_doorbell(u32 slot, u32 dci)
{
    static bool ecam_dumped = false;
    if (!ecam_dumped) {
        ecam_dumped = true;
        s235_ecam_dump();
    }
    if (!slot || slot > 15 || dci < 2 || dci > 31 || !nwoas_fl_dcbaap)
        return;
    if (nwoas_fl_erstba != s235_erstba_seen) {
        s235_erstba_seen = nwoas_fl_erstba;
        s235_reset();
    }
    u64 dp = nwoas_ipa_to_pa(nwoas_fl_dcbaap & ~0x3fUL);
    if (!dp || !nwoas_in_dram(dp, 0x100))
        return;
    u64 ctxp = *(const volatile u64 *)(dp + 8 * slot);
    u64 cp = ctxp ? nwoas_ipa_to_pa(ctxp & ~0x3fUL) : 0;
    if (!cp || !nwoas_in_dram(cp, 32 * 32))
        return;
    // The output device context is DEVICE-written: drop any CPU copy first so the dequeue pointer
    // is the controller's current one (an earlier HV read can have left a stale line behind).
    nwoas_cache_maint(cp + (u64)dci * 32, 32, true);
    const volatile u32 *epc = (const volatile u32 *)(cp + (u64)dci * 32);
    u32 eptype = (epc[1] >> 3) & 7;
    if (eptype != 2 && eptype != 6) // bulk OUT / bulk IN only
        return;
    if ((epc[0] >> 10) & 0x1f) // streams: not a ring
        return;
    u64 start = (((u64)epc[3] << 32) | epc[2]) & ~0xfUL;
    if (!start)
        return;
    struct s235_ep *ep = s235_find(slot, dci, true);
    if (!ep)
        return;
    ep->in = eptype == 6;
    s235_st.dbs++;
    // Consumer-side tracking (what the controller itself does): Windows rings are dynamic (Link
    // TRBs are written where needed), so they cannot be walked from a stale pointer. Start at the
    // output-context TR Dequeue Pointer | DCS when it is first seen or whenever the controller has
    // rewritten it (stop / reset / Set TR Dequeue), then consume every TRB whose cycle bit matches.
    u64 ctxv = ((u64)epc[3] << 32) | epc[2];
    if (!ep->primed || ctxv != ep->ctxv) {
        if (ep->primed)
            s235_st.gaps++; // re-synchronised from the controller's dequeue pointer
        ep->primed = true;
        ep->ctxv = ctxv;
        ep->cursor = ctxv & ~0xfUL;
        ep->ccs = ctxv & 1;
        ep->ph = ep->pt; // drop pending bookkeeping; still-queued TRBs are re-read below
        ep->td_len = 0;
        ep->td_trbs = 0;
    }
    u64 pg = ~0UL, pg_pa = 0;
    for (u32 n = 0; n < 4096; n++) {
        u64 pa = 0;
        if (!nwoas_fl_low_mapped(ep->cursor, &pg, &pg_pa, &pa)) {
            s235_st.incomplete++;
            static u32 inc_logs = 0;
            if (inc_logs < 12) {
                inc_logs++;
                printf("HVLOG: S235 BADCURSOR slot=%u dci=%u cursor=0x%lx ctx=0x%lx\n", slot, dci,
                       ep->cursor, ctxv);
            }
            ep->primed = false;
            return;
        }
        u64 w0 = *(const volatile u64 *)pa, w1 = *(const volatile u64 *)(pa + 8);
        u32 ctl = (u32)(w1 >> 32);
        if ((ctl & 1) != ep->ccs)
            break; // not enqueued yet
        u32 type = (ctl >> 10) & 0x3f;
        if (type == 6) {
            ep->cursor = w0 & ~0xfUL;
            if (ctl & 2)
                ep->ccs ^= 1;
            continue;
        }
        s235_new_trb(ep, ep->cursor, w0, w1);
        ep->cursor += 16;
    }
}

static void s235_complete(struct s235_ep *ep, struct s235_pend *e, u32 cc, u32 resid)
{
    s235_st.xfer_evs++;
    if (cc != 1) {
        s235_st.ccbad++;
        if (s235_logs < S235_LOG_CAP) {
            s235_logs++;
            printf("HVLOG: S235 CC t=%lu td=%u cc=%u resid=0x%x op=0x%x lba=0x%x off=0x%x buf=0x%lx len=0x%x\n",
                   s235_ms(), e->td, cc, resid, e->op, e->lba, e->tdoff, e->buf, e->len);
        }
    } else if (resid) {
        s235_st.resid++;
    }
    if (e->op != 0x28 || e->len < 512)
        return;
    for (u32 q = ep->ph + 1; q != ep->pt; q++)
        if (!ep->pend[q % S235_PEND_N].ed && ep->pend[q % S235_PEND_N].buf == e->buf) {
            s235_st.raced++; // buffer already re-queued: its content is no longer this transfer's
            return;
        }
    u64 hc = s235_hash(e->buf, e->len);
    // 4 KiB pieces keyed by LBA, CPU view (what Windows reads at this moment)
    if (!s235_tab && !s235_tab_tried) {
        s235_tab_tried = true;
        s235_tab = malloc(S235_TAB_N * sizeof(u32));
        if (s235_tab)
            memset(s235_tab, 0, S235_TAB_N * sizeof(u32));
        printf("HVLOG: S235 TAB %s\n", s235_tab ? "allocated" : "ALLOC FAILED (LBA compare off)");
    }
    if (s235_tab && !(e->buf & 0xfff) && !(e->tdoff & 0xfff) && !resid && cc == 1) {
        for (u32 off = 0; off + 0x1000 <= e->len; off += 0x1000) {
            u32 lba = e->lba + (e->tdoff + off) / 512;
            if (s235_phase < 0)
                s235_phase = lba & 7;
            if ((int)(lba & 7) != s235_phase || (lba >> 3) >= S235_TAB_N) {
                s235_st.p_skip++;
                continue;
            }
            u32 h = (u32)(s235_hash(e->buf + off, 0x1000) >> 16) | 1;
            if (h == s235_zero_h()) {
                static u32 zero_logs = 0;
                s235_st.p_zero++;
                if (zero_logs < 24) {
                    zero_logs++;
                    printf("HVLOG: S235 ZERO t=%lu td=%u lba=0x%x cmdlba=0x%x off=0x%x buf=0x%lx trblen=0x%x\n",
                           s235_ms(), e->td, lba, e->lba, e->tdoff + off, e->buf + off, e->len);
                }
            }
            u32 old = s235_tab[lba >> 3];
            if (!old) {
                s235_tab[lba >> 3] = h;
                s235_st.p_new++;
            } else if (old == h) {
                s235_st.p_same++;
            } else {
                static u32 diff_logs = 0;
                s235_st.p_diff++;
                if (diff_logs < 420) {
                    // Diagnose this piece only (after the fact): DRAM view after dc ivac, the live
                    // DART leaf for its IOVA, the DART error latch; then re-check it later.
                    u64 ipa = e->buf + off;
                    diff_logs++;
                    s235_maint(ipa, 0x1000, false);
                    u32 hd4 = (u32)(s235_hash(ipa, 0x1000) >> 16) | 1;
                    printf("HVLOG: S235 DIFF t=%lu td=%u lba=0x%x cmdlba=0x%x off=0x%x buf=0x%lx trb=0x%lx "
                           "trbbuf=0x%lx trblen=0x%x ctl=0x%x cpu=%s dram=%s\n",
                           s235_ms(), e->td, lba, e->lba, e->tdoff + off, ipa, e->trb, e->buf, e->len,
                           e->ctl, s235_cls(h, old), s235_cls(hd4, old));
                    if (diff_logs <= 24 || !(diff_logs & 15))
                        s235_dart_diag(ipa);
                    if (s235_rc_n < S235_RC_N) {
                        s235_rc[s235_rc_n].ipa = ipa;
                        s235_rc[s235_rc_n].old = old;
                        s235_rc[s235_rc_n].lba = lba;
                        s235_rc[s235_rc_n].t0 = s235_ms();
                        s235_rc[s235_rc_n].stage = 0;
                        s235_rc_n++;
                    }
                }
            }
        }
    } else if (s235_tab) {
        s235_st.p_skip += e->len >> 12;
    }
#if S235_POST_IVAC
    s235_maint(e->buf, e->len, false); // dc ivac: drop the CPU copy, next read is DRAM
    u64 hd = s235_hash(e->buf, e->len);
#else
    u64 hd = hc;
#endif
    if (hd != hc) {
        s235_st.incoh++;
        if (s235_logs < S235_LOG_CAP) {
            s235_logs++;
            printf("HVLOG: S235 INCOH t=%lu td=%u lba=0x%x off=0x%x buf=0x%lx len=0x%x before=%d\n",
                   s235_ms(), e->td, e->lba, e->tdoff, e->buf, e->len, hc == e->hbefore);
        }
    }
    if (e->hbefore && hd == e->hbefore && cc == 1 && !resid) {
        s235_st.unchanged++;
        if (s235_logs < S235_LOG_CAP) {
            s235_logs++;
            printf("HVLOG: S235 UNCHANGED t=%lu td=%u lba=0x%x off=0x%x buf=0x%lx len=0x%x ctl=0x%x\n",
                   s235_ms(), e->td, e->lba, e->tdoff, e->buf, e->len, e->ctl);
        }
    }
}

static void s235_event(u64 w0, u64 w1)
{
    u32 ctl = (u32)(w1 >> 32);
    u32 type = (ctl >> 10) & 0x3f;
    s235_st.evs++;
    if (type == 34) {
        s235_st.psc++;
        if (s235_psc_logs < 48) {
            s235_psc_logs++;
            printf("HVLOG: S235 PSC t=%lu port=%u cc=%u\n", s235_ms(), (u32)(w0 >> 24) & 0xff,
                   (u32)(w1 >> 24) & 0xff);
        }
        return;
    }
    if (type != 32) {
        s235_st.other_ev++;
        return;
    }
    bool ed = ctl & 4; // Event Data: w0 is the Event Data TRB parameter, length field is EDTLA
    u64 ptr = ed ? w0 : (w0 & ~0xfUL);
    u32 resid = (u32)w1 & 0xffffff, cc = ((u32)w1 >> 24) & 0xff;
    struct s235_ep *ep = s235_find(ctl >> 24, (ctl >> 16) & 0x1f, false);
    if (!ep || !ep->in)
        return;
    u32 q;
    for (q = ep->ph; q != ep->pt; q++) {
        struct s235_pend *m = &ep->pend[q % S235_PEND_N];
        // ED events complete TDs in ring order; the parameter is not unique per TD, so take the
        // oldest marker whose parameter AND length agree (a TD queued before tracking began, or a
        // genuinely short TD, is skipped/handled below).
        if (ed ? (m->ed && m->buf == ptr && (cc != 1 || m->len == resid)) : (!m->ed && m->trb == ptr))
            break;
    }
    if (ed && q == ep->pt) { // no exact match: oldest marker with this parameter (short/failed TD)
        for (q = ep->ph; q != ep->pt; q++)
            if (ep->pend[q % S235_PEND_N].ed && ep->pend[q % S235_PEND_N].buf == ptr)
                break;
        if (q != ep->pt && ep->pend[q % S235_PEND_N].op != 0x28)
            q = ep->pt; // only report mismatches against READ(10) data TDs
    }
    if (ed && q != ep->pt) {
        struct s235_pend *m = &ep->pend[q % S235_PEND_N];
        u32 short_by = m->len - resid; // resid holds EDTLA here
        if (cc != 1 || short_by) {
            if (cc != 1)
                s235_st.ccbad++;
            else
                s235_st.resid++;
            if (m->op == 0x28 && s235_logs < S235_LOG_CAP) {
                s235_logs++;
                printf("HVLOG: S235 TDBAD t=%lu td=%u cc=%u tdlen=0x%x edtla=0x%x lba=0x%x\n", s235_ms(),
                       m->td, cc, m->len, resid, m->lba);
            }
            ep->ph = q + 1; // drop the whole TD unchecked
            return;
        }
        while (ep->ph != q) {
            if (!ep->pend[ep->ph % S235_PEND_N].ed)
                s235_complete(ep, &ep->pend[ep->ph % S235_PEND_N], 1, 0);
            ep->ph++;
        }
        ep->ph++;
        return;
    }
    if (q == ep->pt) {
        s235_st.orphan++;
        return;
    }
    u32 td = ep->pend[q % S235_PEND_N].td;
    while (ep->ph != q) {
        s235_complete(ep, &ep->pend[ep->ph % S235_PEND_N], 1, 0);
        ep->ph++;
    }
    s235_complete(ep, &ep->pend[ep->ph % S235_PEND_N], cc, resid);
    ep->ph++;
    if (cc != 1) // the rest of a failed/short TD is skipped by the controller
        while (ep->ph != ep->pt && ep->pend[ep->ph % S235_PEND_N].td == td)
            ep->ph++;
}

// Called when the Windows kernel writes ERDP (low half or 64-bit): everything between the previous
// and the new dequeue pointer has just been consumed by usbxhci.
static void s235_erdp(u64 newptr)
{
    newptr &= 0xfffffff0UL;
    if (!nwoas_fl_erstba || !newptr)
        return;
    if (nwoas_fl_erstba != s235_erstba_seen) {
        s235_erstba_seen = nwoas_fl_erstba;
        s235_reset();
    }
    if (!s235_evdeq) {
        s235_evdeq = newptr;
        return;
    }
    u64 ep = nwoas_ipa_to_pa(nwoas_fl_erstba & ~0x3fUL);
    u32 nseg = read32(0x6c0002028UL) & 0xff;
    if (nseg > 8)
        nseg = 8;
    if (!ep || !nseg || !nwoas_in_dram(ep, 16 * nseg)) {
        s235_evdeq = newptr;
        return;
    }
    u64 base[8];
    u32 size[8];
    for (u32 i = 0; i < nseg; i++) {
        base[i] = *(const volatile u64 *)(ep + 16 * i) & ~0x3fUL;
        size[i] = *(const volatile u32 *)(ep + 16 * i + 8) & 0xffff;
    }
    u64 cur = s235_evdeq, pg = ~0UL, pg_pa = 0;
    for (u32 it = 0; cur != newptr && it < 4096; it++) {
        u64 pa = 0;
        u32 s;
        for (s = 0; s < nseg; s++)
            if (cur >= base[s] && cur < base[s] + (u64)size[s] * 16)
                break;
        if (s == nseg || !nwoas_fl_low_mapped(cur, &pg, &pg_pa, &pa))
            break;
        s235_event(*(const volatile u64 *)pa, *(const volatile u64 *)(pa + 8));
        cur += 16;
        if (cur == base[s] + (u64)size[s] * 16)
            cur = base[(s + 1) % nseg];
    }
    if (cur != newptr)
        s235_st.resync++;
    s235_evdeq = newptr;
    s235_recheck();
}

static void s235_report(void)
{
    printf("HVLOG: S235 CFG pre_hash=%d post_ivac=%d\n", S235_PRE_HASH, S235_POST_IVAC);
    printf("HVLOG: S235 ST t=%lu dbs=%lu cbw=%lu read10=%lu tds=%lu trbs=%lu bytes=%lu maxtd=0x%lx "
           "tdh=%lu/%lu/%lu/%lu/%lu/%lu x64k=%lu notmps=%lu buf=0x%lx-0x%lx hole_trbs=%lu hole_bytes=%lu\n",
           s235_ms(), s235_st.dbs, s235_st.cbw, s235_st.read10, s235_st.tds, s235_st.trbs,
           s235_st.bytes, s235_st.maxtd, s235_st.tdh[0], s235_st.tdh[1], s235_st.tdh[2],
           s235_st.tdh[3], s235_st.tdh[4], s235_st.tdh[5], s235_st.x64k, s235_st.notmps,
           s235_st.minbuf, s235_st.maxbuf, s235_st.hole, s235_st.hole_bytes);
    printf("HVLOG: S235 EV t=%lu evs=%lu xfer=%lu psc=%lu other=%lu ccbad=%lu resid=%lu orphan=%lu "
           "raced=%lu unchanged=%lu incoh=%lu pnew=%lu psame=%lu pdiff=%lu pskip=%lu pzero=%lu incomplete=%lu "
           "gaps=%lu resync=%lu pendovf=%lu\n",
           s235_ms(), s235_st.evs, s235_st.xfer_evs, s235_st.psc, s235_st.other_ev, s235_st.ccbad,
           s235_st.resid, s235_st.orphan, s235_st.raced, s235_st.unchanged, s235_st.incoh,
           s235_st.p_new, s235_st.p_same, s235_st.p_diff, s235_st.p_skip, s235_st.p_zero, s235_st.incomplete,
           s235_st.gaps, s235_st.resync, s235_st.pend_ovf);
}
#endif /* NWOAS_S235_PROBE */
