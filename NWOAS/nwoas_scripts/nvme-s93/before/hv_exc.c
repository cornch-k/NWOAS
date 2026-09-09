/* SPDX-License-Identifier: MIT */

#include "hv.h"
#include "assert.h"
#include "cpu_regs.h"
#include "exception.h"
#include "smp.h"
#include "string.h"
#include "memory.h"
#include "uart.h"
#include "uartproxy.h"
#include "hv_vgic.h"
#include "aic.h"
#include "aic_regs.h"
#include "adt.h"

// NWOAS: quiet mode. Silences the heavy per-SMC / periodic diagnostic dumps (guest PC-ring,
// PSCI-SMC verbose, UEFI/bmgfw UART re-dumps, VGATE timer-gate trace) that flooded the shared
// 115200 serial -- ~90% of the log was this hv self-diagnostics, saturating the UART TX so the
// guest's cores spun waiting for TX space -> boot wedge, and made every boot crawl. Kept:
// NWOAS-PMCC (capped 600, used for kernel-phase detection) + [usb-bridge] (low volume). Set 0
// to restore the full diagnostic firehose.
#define NWOAS_QUIET 1

#define TIME_ACCOUNTING
//
// m1n1_windows change: when the vGIC is running in the guest - timer interrupts by virtue of coming from the generic timer are
// still going to come as FIQs to EL2 - we'll need to divert those to the guest as *IRQs* (to prevent Windows from crashing as it treats FIQs as
// errors).
//
extern bool vgic_inited;
extern spinlock_t bhl;

#define _SYSREG_ISS(_1, _2, op0, op1, CRn, CRm, op2)                                               \
    (((op0) << ESR_ISS_MSR_OP0_SHIFT) | ((op1) << ESR_ISS_MSR_OP1_SHIFT) |                         \
     ((CRn) << ESR_ISS_MSR_CRn_SHIFT) | ((CRm) << ESR_ISS_MSR_CRm_SHIFT) |                         \
     ((op2) << ESR_ISS_MSR_OP2_SHIFT))
#define SYSREG_ISS(...) _SYSREG_ISS(__VA_ARGS__)

#define PERCPU(x) pcpu[mrs(TPIDR_EL2)].x
#define PERCPU_N(x, y) pcpu[x].y

struct hv_pcpu_data {
    u32 ipi_queued;
    u32 ipi_pending;
    u32 pmc_pending;
    u64 pmc_irq_mode;
    u64 exc_entry_pmcr0_cnt;
#ifdef ENABLE_VGIC_MODULE
    virq_queue_t irq_queue;
    virq_queue_t sgi_queue;
    virq_queue_t timer_queue;
#endif
} ALIGNED(64);

struct hv_pcpu_data pcpu[MAX_CPUS];

void hv_exit_guest(void) __attribute__((noreturn));

static u64 stolen_time = 0;
static u64 exc_entry_time;
static int num_cpus;

extern u64 hv_cpus_in_guest;
extern int hv_pinned_cpu;
extern int hv_want_cpu;

static bool time_stealing = true;

void init_vgic_irq_queues(void) {
#ifdef ENABLE_VGIC_MODULE
    int node = adt_path_offset(adt, "/cpus");
    num_cpus = adt_get_child_count(adt, node);
    for (int i = 0; i < MAX_CPUS; i++) {
        virq_queue_init(&PERCPU_N(i, irq_queue));
        virq_queue_init(&PERCPU_N(i, sgi_queue));
        virq_queue_init(&PERCPU_N(i, timer_queue));
    }
#endif
}

static void _hv_exc_proxy(struct exc_info *ctx, uartproxy_boot_reason_t reason, u32 type,
                          void *extra)
{
    int from_el = FIELD_GET(SPSR_M, ctx->spsr) >> 2;

    hv_wdt_breadcrumb('P');

    /*
     * Get all the CPUs into the HV before running the proxy, to make sure they all exit to
     * the guest with a consistent time offset.
     */
    if (time_stealing)
        hv_rendezvous();

    u64 entry_time = mrs(CNTPCT_EL0);

    ctx->elr_phys = hv_translate(ctx->elr, false, false, NULL);
    ctx->far_phys = hv_translate(ctx->far, false, false, NULL);
    ctx->sp_phys = hv_translate(from_el == 0 ? ctx->sp[0] : ctx->sp[1], false, false, NULL);
    ctx->extra = extra;

    struct uartproxy_msg_start start = {
        .reason = reason,
        .code = type,
        .info = ctx,
    };

    hv_wdt_suspend();
    int ret = uartproxy_run(&start);
    hv_wdt_resume();

    switch (ret) {
        case EXC_RET_HANDLED:
            hv_wdt_breadcrumb('p');
            if (time_stealing) {
                u64 lost = mrs(CNTPCT_EL0) - entry_time;
                stolen_time += lost;
            }
            break;
        case EXC_EXIT_GUEST:
            hv_rendezvous();
            spin_unlock(&bhl);
            hv_exit_guest(); // does not return
        default:
            printf("Guest exception not handled, rebooting.\n");
            print_regs(ctx->regs, 0);
            flush_and_reboot(); // does not return
    }
}

static void hv_maybe_switch_cpu(struct exc_info *ctx, uartproxy_boot_reason_t reason, u32 type,
                                void *extra)
{
    while (hv_want_cpu != -1) {
        if (hv_want_cpu == smp_id()) {
            hv_want_cpu = -1;
            _hv_exc_proxy(ctx, reason, type, extra);
        } else {
            // Unlock the HV so the target CPU can get into the proxy
            spin_unlock(&bhl);
            while (hv_want_cpu != -1)
                sysop("dmb sy");
            spin_lock(&bhl);
        }
    }
}

void hv_exc_proxy(struct exc_info *ctx, uartproxy_boot_reason_t reason, u32 type, void *extra)
{
    /*
     * Wait while another CPU is pinned or being switched to.
     * If a CPU switch is requested, handle it before actually handling the
     * exception. We still tell the host the real reason code, though.
     */
    while ((hv_pinned_cpu != -1 && hv_pinned_cpu != smp_id()) || hv_want_cpu != -1) {
        if (hv_want_cpu == smp_id()) {
            hv_want_cpu = -1;
            _hv_exc_proxy(ctx, reason, type, extra);
        } else {
            // Unlock the HV so the target CPU can get into the proxy
            spin_unlock(&bhl);
            while ((hv_pinned_cpu != -1 && hv_pinned_cpu != smp_id()) || hv_want_cpu != -1)
                sysop("dmb sy");
            spin_lock(&bhl);
        }
    }

    /* Handle the actual exception */
    _hv_exc_proxy(ctx, reason, type, extra);

    /*
     * If as part of handling this exception we want to switch CPUs, handle it without returning
     * to the guest.
     */
    hv_maybe_switch_cpu(ctx, reason, type, extra);
}

void hv_set_time_stealing(bool enabled, bool reset)
{
    time_stealing = enabled;
    if (reset)
        stolen_time = 0;
}

void hv_add_time(s64 time)
{
    stolen_time -= (u64)time;
}

#ifdef ENABLE_VGIC_MODULE
// NWOAS stage-8 fix: true if virtual INTID `vintid` is already outstanding — pending
// or active in a List Register, or queued in the per-CPU timer_queue. hv_update_fiq()
// runs on every return to the guest and re-tests the level-asserted timer condition
// (CNTx_CTL.ISTATUS stays set until the guest reprograms CNTx_CVAL). Without this
// guard it delivers duplicate timer PPIs without bound, filling all 8 LRs and the
// 32-deep timer_queue; the maintenance handler can then only rotate them (one freed by
// EOI, one immediately refilled) forever -> the maintenance-interrupt storm that ends
// in a stack-overflow data abort. Capping each timer PPI to one outstanding lets the
// guest reach its timer ISR, reprogram CVAL, clear ISTATUS and stop the re-delivery.
// timer_queue is per-CPU and only this CPU writes it, so a lockless scan is safe here.
// (Exposed via hv_vgic.h so hv_vm.c's generated-PSCE path can share the "at most one 698 in flight"
// dedup this bridge uses, avoiding a duplicate vINTID in two List Registers.)
bool hv_vgic_virq_outstanding(u32 vintid)
{
    for (int lr = 0; lr < 8; lr++) {
        u64 v = hv_vgic3_read_lr(lr);
        if ((v & (ICH_LR_STATE_PENDING | ICH_LR_STATE_ACTIVE)) &&
            (((v >> ICH_LR_VIRTUAL_SHIFT) & ICH_LR_VIRTUAL_MASK) == vintid))
            return true;
    }
    virq_queue_t *q = &PERCPU(timer_queue);
    for (u32 i = q->tail; i != q->head; i++) {
        if (q->buf[i & (VIRQ_QUEUE_SIZE - 1)].vintid == vintid)
            return true;
    }
    return false;
}
#endif

#ifdef ENABLE_VGIC_MODULE
/* S6-D55 observational framebuffer probe.  The Tahoe DCP hook fills the
 * preserved 1280x720 surface with eight 160-pixel BGRA color bars immediately
 * before guest entry.  Compare the complete surface against that exact
 * baseline after the Windows kernel has run for two minutes.  This avoids a
 * multi-megabyte proxy read while a HV_START reply is outstanding. */
static void nwoas_probe_guest_fb(u64 now, bool kernel_phase)
{
    static u64 kernel_start = 0;
    static bool done = false;
    if (!kernel_phase || done)
        return;
    if (!kernel_start) {
        kernel_start = now;
        return;
    }
    if (now - kernel_start < 120 * mrs(CNTFRQ_EL0))
        return;
    done = true;

    const u64 base = 0xbe3f60000UL;
    const u32 width = 1280, height = 720, stride = 5120;
    const u64 size = (u64)stride * height;
    static const u32 bars[8] = {
        0xffffffff, 0xffffff00, 0xff00ffff, 0xff00ff00,
        0xffff00ff, 0xffff0000, 0xff0000ff, 0xff202020,
    };

    /* Drop this CPU's old cache lines before observing guest/DCP-authored
     * framebuffer memory.  Windows' scanout writes must be visible at PoC for
     * DCP as well, so this is the same memory image relevant to HDMI output. */
    dc_ivac_range((void *)base, size);
    sysop("dsb sy");

    const volatile u8 *bytes = (const volatile u8 *)base;
    const volatile u32 *pixels = (const volatile u32 *)base;
    u64 fnv = 0xcbf29ce484222325UL;
    u64 nonzero = 0, mismatch = 0, nonbg = 0, light = 0;
    u32 minx = width, miny = height, maxx = 0, maxy = 0;
    u32 bg = pixels[0];
    u32 nb_minx = width, nb_miny = height, nb_maxx = 0, nb_maxy = 0;
    for (u64 i = 0; i < size; i++) {
        u8 b = bytes[i];
        nonzero += b != 0;
        fnv = (fnv ^ b) * 0x100000001b3UL;
    }
    for (u32 y = 0; y < height; y++) {
        const volatile u32 *row = pixels + (u64)y * (stride / 4);
        for (u32 x = 0; x < width; x++) {
            u32 px = row[x];
            if (px != bars[x / 160]) {
                mismatch++;
                if (x < minx) minx = x;
                if (y < miny) miny = y;
                if (x > maxx) maxx = x;
                if (y > maxy) maxy = y;
            }
            if (px != bg) {
                nonbg++;
                if (x < nb_minx) nb_minx = x;
                if (y < nb_miny) nb_miny = y;
                if (x > nb_maxx) nb_maxx = x;
                if (y > nb_maxy) nb_maxy = y;
            }
            u32 b = px & 0xff, g = (px >> 8) & 0xff, r = (px >> 16) & 0xff;
            light += (r + g + b) >= 600;
        }
    }
    printf("HVLOG: FBPROBE base=0x%lx bytes=%lu fnv=0x%lx nonzero=%lu "
           "bar_mismatch=%lu bbox=%u,%u-%u,%u bg=%08x nonbg=%lu "
           "nbbox=%u,%u-%u,%u light=%lu\n",
           base, size, fnv, nonzero, mismatch,
           mismatch ? minx : 0, mismatch ? miny : 0,
           mismatch ? maxx : 0, mismatch ? maxy : 0,
           bg, nonbg, nonbg ? nb_minx : 0, nonbg ? nb_miny : 0,
           nonbg ? nb_maxx : 0, nonbg ? nb_maxy : 0, light);
    printf("HVLOG: FBPROBE row0=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
           "center=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",
           pixels[80], pixels[240], pixels[400], pixels[560],
           pixels[720], pixels[880], pixels[1040], pixels[1200],
           pixels[(u64)360 * (stride / 4) + 80],
           pixels[(u64)360 * (stride / 4) + 240],
           pixels[(u64)360 * (stride / 4) + 400],
           pixels[(u64)360 * (stride / 4) + 560],
           pixels[(u64)360 * (stride / 4) + 720],
           pixels[(u64)360 * (stride / 4) + 880],
           pixels[(u64)360 * (stride / 4) + 1040],
           pixels[(u64)360 * (stride / 4) + 1200]);

    /* D56: 4x4 OR-downsample of every non-background pixel.  Forty bytes per
     * row encode a 320x180 monochrome preview in 180 bounded log lines. */
    static const char hex[] = "0123456789abcdef";
    printf("HVLOG: FBMASK w=320 h=180 block=4 bg=%08x\n", bg);
    for (u32 oy = 0; oy < 180; oy++) {
        char line[81];
        for (u32 ob = 0; ob < 40; ob++) {
            u8 bits = 0;
            for (u32 bit = 0; bit < 8; bit++) {
                u32 ox = ob * 8 + bit;
                bool marked = false;
                for (u32 dy = 0; dy < 4 && !marked; dy++) {
                    const volatile u32 *row = pixels +
                        (u64)(oy * 4 + dy) * (stride / 4);
                    for (u32 dx = 0; dx < 4; dx++) {
                        if (row[ox * 4 + dx] != bg) {
                            marked = true;
                            break;
                        }
                    }
                }
                if (marked)
                    bits |= 1u << (7 - bit);
            }
            line[2 * ob] = hex[bits >> 4];
            line[2 * ob + 1] = hex[bits & 0xf];
        }
        line[80] = 0;
        printf("HVLOG: FBMASK y=%03u %s\n", oy, line);
    }
    printf("HVLOG: FBMASK END\n");
    extern void nwoas_late_usb_probe(void);
    nwoas_late_usb_probe();
}

// NWOAS stage-8: deliver a guest architectural-timer PPI, dedup'd AND rate-limited.
// The slow tethered hv takes ~10ms per timer cycle — far longer than the guest tick
// period — so the level-asserted timer re-expires before the guest can do any boot
// work (observed: cval always just below now) => livelock. Deliver at most once per
// ~62ms of real counter time per CPU so the guest's boot thread gets real time to
// progress between ticks. Coarsens timekeeping but lets the boot advance.
static void hv_inject_timer_ppi(u32 vintid)
{
    static u64 last_inject[2][8] = {{0}};
    int idx = (vintid == 18) ? 1 : 0;
    u32 cpu = smp_id();
    if (cpu >= 8)
        return;
    if (hv_vgic_virq_outstanding(vintid))   // already pending/active: nothing to do
        return;
    u64 now = mrs(CNTPCT_EL0);
    // NWOAS stage-8+: the ~62ms cap exists only to stop the level-asserted timer from
    // re-expiring faster than the slow tethered hv can service it during winload/UEFI
    // (immediate-re-expiry livelock). Once the guest enters the WINDOWS KERNEL (run21: PC in
    // TTBR1, top16 == 0xffff), the kernel re-arms CNTV_CVAL into the future every tick
    // (observed vctl 0x5->0x1), so that livelock is structurally impossible and the 62ms cap
    // merely starves the kernel's clock to ~16Hz -- the idle loop (HalProcessorIdle WFI at
    // kernel rva 0x434b64) then never gets timely ticks, so system time / DPCs / scheduling
    // stall and boot appears wedged. Relax the cap to ~1ms (1kHz) for the kernel phase only;
    // winload/UEFI (low VAs) keep 62ms so their livelock guard is preserved. hv_get_elr()
    // reads the live guest ELR, so no hv_update_fiq() signature change is needed.
    u64 gpc = hv_get_elr();
    // NWOAS: kernel_phase must be STICKY. hv_get_elr() returns the *live* guest ELR, which on
    // background FIQ/exit paths can momentarily read a low (winload/UEFI runtime) VA even after
    // the Windows kernel is running -> an instantaneous check flaps to false and the 62ms cap
    // re-starves the kernel's clock to ~16Hz (run27: timer observed at ~16-62Hz not 1kHz).
    // Latch it: once ANY delivery has seen a kernel-canonical VA (top16==0xffff), stay in
    // kernel phase for good. winload/UEFI never execute at 0xffff.... VAs, so they keep 62ms
    // and their immediate-re-expiry livelock guard is preserved.
    bool kphase_now = ((gpc >> 48) == 0xffffULL);
    static bool nwoas_kernel_seen = false;
    if (kphase_now)
        nwoas_kernel_seen = true;
    bool kernel_phase = nwoas_kernel_seen;
    nwoas_probe_guest_fb(now, kernel_phase);
    extern void nwoas_watch_hid(u64 now);
    nwoas_watch_hid(now);
    u64 min_gap = kernel_phase ? (mrs(CNTFRQ_EL0) >> 10)   /* ~1ms  (Windows kernel) */
                               : (mrs(CNTFRQ_EL0) >> 4);    /* ~62ms (winload/UEFI)  */
    // NWOAS-TINJ diag (observational, throttled): expose whether CNTV(18) delivery is being
    // starved by the rate-limit. kphase_now vs sticky reveals flapping; gated=1 means skipped.
    {
        static u32 nwoas_tinj_n = 0;
        if (vintid == 18 && ((nwoas_tinj_n++) & 0xff) == 0)
            printf("HVLOG: NWOAS-TINJ kpnow=%d sticky=%d gpc=0x%lx dt=0x%lx mingap=0x%lx gated=%d\n",
                   kphase_now, kernel_phase, gpc, now - last_inject[idx][cpu], min_gap,
                   (now - last_inject[idx][cpu]) < min_gap);
    }
    if ((now - last_inject[idx][cpu]) < min_gap)
        return;
    last_inject[idx][cpu] = now;
    if (hv_vgic3_get_free_lr() != -1) {
        hv_vgic3_inject_irq(vintid, hv_vgic3_get_priority(vintid), false, true, false, 0);
    } else {
        virq_t pending = { .vintid = vintid, .priority = 0x20, .active = false,
                           .pending = true, .hw_status = false, .hw_irq = 0 };
        virq_queue_push(&PERCPU(timer_queue), &pending);
    }
}
#endif

#ifdef ENABLE_VGIC_MODULE
/* NWOAS stage-8 USB-unblock (fix-design workflow generation, adversarially reviewed).
 * M1(T8103) dwc3 xHCI never asserts its AIC wired IRQ (SPI 857, usb@502280000) even on a
 * valid connect -- broken hardware (Asahi dwc3-apple.c:41-60: "port status register shows
 * the correct state but no interrupt ever comes in"). Windows arms 857 ([spi-en] irq=857)
 * and waits forever; the physical 857 never fires ([dev-irq] has no 857). Bridge the dead
 * line, but ONLY deliver when the controller actually has a pending event: poll interrupter-0
 * IMAN.IP / USBSTS.EINT from EL2 (read32, same access as usb_dwc3.c). IMAN.IP=1 means the HW
 * produced an event and only the line is dead -> delivering vINTID 857 makes the Windows
 * USBXHCI ISR run and drain the event ring. Gating on IMAN.IP is essential: a standard xHCI
 * ISR ignores the event ring and returns when IP=0, so a blind deliver makes no progress and a
 * repeated spurious storm risks the guest masking the source. The gate makes every deliver
 * non-spurious AND self-diagnosing: if IMAN.IP never sets, zero delivers + [usb-bridge] proves
 * the source (a)/(b) is dead => the PHY-reset fix (C) is required. SW-mode (hw_status=false)
 * so it is independent of the GICv3-maintenance re-arm bug; guest EOI (HW ICV; IAR1/EOIR1
 * traps are commented out) frees the LR so the dedup below allows the next deliver. */
#define NWOAS_XHCI_BASE 0x502280000UL   /* j274 usb@502280000 = dwc3_1, XHCI regs @ off 0 */
#define NWOAS_USB_SPI   857
#define NWOAS_USB_IDX   1                 /* usb-drd1/atc-phy1 == NWOAS_XHCI_BASE 0x502280000 */
extern int usb_phy_bringup(u32 idx);      /* usb.c:120, not declared in usb.h */

static void hv_bridge_usb_xhci_irq(void)
{
    int icpu = hv_pinned_cpu;
    if (icpu == -1)
        icpu = boot_cpu_idx;
    if ((int)smp_id() != icpu)                 /* single CPU polls/delivers (like hv_tick) */
        return;

    static u64 last_poll = 0;                  /* throttle MMIO polling to ~1ms */
    u64 now = mrs(CNTPCT_EL0);
    if ((now - last_poll) < (mrs(CNTFRQ_EL0) >> 10))
        return;
    last_poll = now;

    static u64 op_base = 0, rt_base = 0;        /* resolve op/runtime base once */
    if (!op_base) {
        u32 caplen = read32(NWOAS_XHCI_BASE + 0x00) & 0xff;
        u32 rtsoff = read32(NWOAS_XHCI_BASE + 0x18) & ~0x1fu;
        if (caplen == 0 || caplen == 0xff)     /* controller MMIO not up yet */
            return;
        op_base = NWOAS_XHCI_BASE + caplen;
        rt_base = NWOAS_XHCI_BASE + rtsoff;
    }

    /* --- NWOAS stage-8 fix-C (approach ii, adversarially corrected) ---
     * Recover the boot port after Windows' destructive xHCI HCRST. On Apple dwc3 the reset
     * pulses the ATCPHY<->eUSB2-repeater link and the root port silently loses the boot device
     * (CCS 1->0 forever, no further event, IMAN.IP never sets -> the deliver gate below can NEVER
     * fire). Re-run m1n1's PROVEN PHY bringup (usb.c:120) for this port. It writes ONLY the USB2
     * PHY (atc regs) + pipehandler MUX and the DEASSERTED dwc3 reset (RESET_N=1); it NEVER asserts
     * DWC3_RESET_N/FORCE_CLAMP, so it does NOT reset the DWC3 core and never touches any xHCI
     * operational register Windows owns. The USB2 PHY reset pulse drops+re-trains the link so the
     * port re-detects the device (CCS 0->1); the still-running, still-armed controller then posts a
     * port-change event -> IMAN.IP=1 -> the existing armed/pending gate below delivers 857. */
    {
        u64 gpc = hv_get_elr();
        static bool nwoas_usb_kseen = false;
        if ((gpc >> 48) == 0xffffULL)             /* Windows kernel canonical VA: sticky latch */
            nwoas_usb_kseen = true;

        u32 usbcmd = read32(op_base + 0x00);      /* bit0 R/S, bit1 HCRST (diag) */
        u32 p0     = read32(op_base + 0x400);     /* PORTSC port1, bit0 CCS */
        u32 p1     = read32(op_base + 0x410);     /* PORTSC port2, bit0 CCS */
        u32 iman0  = read32(rt_base + 0x20);      /* interrupter-0 IMAN, bit1 IE = armed */
        bool ccs      = (p0 & 1u) || (p1 & 1u);
        bool armed_ie = (iman0 & 0x2u);

        static bool nwoas_ccs_seen = false;       /* boot device present at least once */
        static u64  nwoas_last_resync = 0;
        static u32  nwoas_resync_tries = 0;

        if (ccs) {
            nwoas_ccs_seen = true;
            nwoas_resync_tries = 0;               /* healthy link: refill retry budget */
        } else if (nwoas_usb_kseen && nwoas_ccs_seen && armed_ie) {
            u64 cooldown = mrs(CNTFRQ_EL0) >> 1;  /* ~500 ms between attempts */
            if (nwoas_resync_tries < 16 && (now - nwoas_last_resync) > cooldown) {
                nwoas_last_resync = now;
                nwoas_resync_tries++;
                printf("[usb-bridge] CCS lost p0=0x%x p1=0x%x cmd=0x%x iman=0x%x -> phy_bringup(%u) #%u\n",
                       p0, p1, usbcmd, iman0, NWOAS_USB_IDX, nwoas_resync_tries);
                usb_phy_bringup(NWOAS_USB_IDX);
                printf("[usb-bridge] post-bringup p0=0x%x p1=0x%x\n",
                       read32(op_base + 0x400), read32(op_base + 0x410));  /* CCS recovered? */
            }
        }
    }
    /* --- end fix-C; existing armed/pending deliver below is unchanged --- */

    /* interrupter0: IMAN @ rt+0x20 (bit0 IP, bit1 IE), USBSTS @ op+0x04 (bit3 EINT) */
    u32 iman   = read32(rt_base + 0x20);
    u32 usbsts = read32(op_base + 0x04);
    if (iman == 0xffffffffu)                    /* bad/unmapped read guard */
        return;

    bool armed   = (iman & 0x2u);              /* guest enabled the interrupter = ISR exists */
    /* S6-D51: bridge the interrupter's actual IP bit only.  D50 proved that
     * Windows consumed both 16-byte event TRBs (ERDP +0x20) and cleared IP,
     * while the controller's summary USBSTS.EINT bit remained set.  Treating
     * that stale summary as a fresh line request reinjected IRQ698 forever and
     * starved the guest in an empty interrupt loop. */
    bool pending = (iman & 0x1u);

    {   /* instrumentation (throttled ~1/sec): NOT on 'pending' -- if the dwc3 interrupter latches
         * IMAN.IP=1 persistently, logging every ~1ms poll floods the 115200 serial and wedges the
         * hv in printf (same class as the FL1100 flood). Log only every 1024 polls. */
        static u32 dn = 0;
        if (((dn++) & 0x3ffu) == 0) {
            u32 portsc = read32(op_base + 0x400);   /* root port 1 */
            printf("[usb-bridge] iman=0x%x usbsts=0x%x portsc=0x%x armed=%d pend=%d\n",
                   iman, usbsts, portsc, armed, pending);
        }
    }

    if (pending && hv_vgic3_spi_enabled(NWOAS_USB_SPI)) {
        extern void nwoas_usbc_poll_descriptor(void);
        nwoas_usbc_poll_descriptor();
    }
    /* D79: one read-only healthy-controller snapshot 60s after SPI enable. */
    static u64 usbc_ready_time = 0;
    static bool usbc_ready_dumped = false;
    if (hv_vgic3_spi_enabled(NWOAS_USB_SPI) && !usbc_ready_time)
        usbc_ready_time = now;
    if (usbc_ready_time && !usbc_ready_dumped && now-usbc_ready_time > 60*mrs(CNTFRQ_EL0)) {
        usbc_ready_dumped = true;
        extern void nwoas_probe_usbc_rings(u64,u64,u64);
        printf("[usb-d79] late snapshot sts=%x\n",usbsts);
        nwoas_probe_usbc_rings(read32(rt_base+0x30)|((u64)read32(rt_base+0x34)<<32),
                              read32(rt_base+0x38)|((u64)read32(rt_base+0x3c)<<32),
                              read32(op_base+0x30)|((u64)read32(op_base+0x34)<<32));
    }
    /* D73: capture first host-system-error with the guest-owned ring. */
    static bool usbc_hse_dumped = false;
    if (!usbc_hse_dumped && (usbsts & 4u) && hv_vgic3_spi_enabled(NWOAS_USB_SPI)) {
        usbc_hse_dumped = true;
        extern void nwoas_probe_usbc_rings(u64, u64, u64);
        u64 erst = read32(rt_base + 0x30) | ((u64)read32(rt_base + 0x34) << 32);
        u64 erdp = read32(rt_base + 0x38) | ((u64)read32(rt_base + 0x3c) << 32);
        u64 dcba = read32(op_base + 0x30) | ((u64)read32(op_base + 0x34) << 32);
        nwoas_probe_usbc_rings(erst, erdp, dcba);
    }
    /* D71 read-only register snapshot; no DMA buffer dereference. */
    static u32 usb_reg_samples = 0;
    static u64 usb_reg_time = 0;
    if (armed && usb_reg_samples < 3 && now - usb_reg_time >= 10 * mrs(CNTFRQ_EL0)) {
        usb_reg_time = now;
        usb_reg_samples++;
        printf("[usb-regs] cmd=%x sts=%x crcr=%x:%08x dcbaa=%x:%08x "
               "erstsz=%x erstba=%x:%08x erdp=%x:%08x\n",
               read32(op_base), read32(op_base + 4),
               read32(op_base + 0x1c), read32(op_base + 0x18),
               read32(op_base + 0x34), read32(op_base + 0x30),
               read32(rt_base + 0x28),
               read32(rt_base + 0x34), read32(rt_base + 0x30),
               read32(rt_base + 0x3c), read32(rt_base + 0x38));
    }
    if (!armed || !pending)
        return;
    /* D72: IMAN.IE can still belong to UEFI. Wait until the guest explicitly
     * enables this SPI; do not bypass Windows' interrupt masking. */
    if (!hv_vgic3_spi_enabled(NWOAS_USB_SPI))
        return;
    if (hv_vgic_virq_outstanding(NWOAS_USB_SPI))    /* dedup: at most one in flight */
        return;

    u8 prio = hv_vgic3_get_priority(NWOAS_USB_SPI);
    if (hv_vgic3_get_free_lr() != -1) {
        hv_vgic3_inject_irq(NWOAS_USB_SPI, prio, false, true, false, 0);
    } else {
        virq_t q = { .vintid = NWOAS_USB_SPI, .priority = prio, .active = false,
                     .pending = true, .hw_status = false, .hw_irq = 0 };
        virq_queue_push(&PERCPU(irq_queue), &q);   /* same queue as the device path */
    }
}

/* NWOAS §3.2 Wall 2 (2026-07-11 night): FL1100 (USB-A PCIe xHCI) INTx = AIC SPI 698 bridge.
 * After the RPi4-match ACPI (IORT + leaf _DMA removed) lets Windows build the DMA adapter
 * (DCBAAP != 0, CONFIG/ERSTBA set, PORTSC PED=1 on real hardware), the controller still ends
 * halted (RS=0) because usbxhci arms interrupt 698 and waits for the port-change/command-
 * completion event, but the FL1100 INTx line (INTA, datasheet 5.2.1.21 Interrupt Pin=01h ->
 * AIC 698) is never delivered into the guest vGIC (only dwc3 857 is bridged above). Mirror the
 * dwc3 bridge's deliver path for the FL1100: poll interrupter-0 IMAN.IP from EL2 and deliver
 * vINTID 698 when the HW has a pending event. No ATCPHY fix-C (that is dwc3/USB-C only; the
 * FL1100 is a standard PCIe xHCI). Offsets datasheet-verified: BAR 0x6c0000000, CAPLENGTH=0x80,
 * RTSOFF=0x2000 (rt=BAR+0x2000), IMAN=rt+0x20, USBSTS=op+0x04 (EINT bit3), 32-bit MMIO only. */
#ifndef NWOAS_FL1100_BRIDGE
#define NWOAS_FL1100_BRIDGE 1
#endif
#if NWOAS_FL1100_BRIDGE
#define NWOAS_FL1100_BASE 0x6c0000000UL   /* j274 apcie bus2/dev0 FL1100 xHCI BAR0 (fixed by UEFI) */
#define NWOAS_FL1100_SPI  698             /* FL1100 INTA -> AIC 698 -> vGIC SPI 698 (DSDT _PRT/_CRS) */

static void hv_bridge_fl1100_irq(void)
{
    // A mirrored controller event has already been published in Windows's
    // cacheable event ring.  Deliver its line interrupt once on the first CPU
    // that can take IRQs.  D39 proved that restricting delivery to the CPU
    // polling FL1100 strands the interrupt: that CPU remains at PSTATE.I=1
    // inside usbxhci's reset wait even though another guest CPU is takeable.
    // bhl serializes hv_update_fiq() across CPUs, so irq_sent is a sufficient
    // global dedup until Windows advances ERDP and clears both flags.
    extern bool nwoas_seed_guest_pending;
    extern bool nwoas_seed_guest_irq_sent;
    if (nwoas_seed_guest_pending) {
        if (!nwoas_seed_guest_irq_sent && !((hv_get_spsr() >> 7) & 1) &&
            hv_vgic3_get_free_lr() != -1) {
            /* Keep the guest-programmed distributor priority.  D49 captured the
             * fatal SEI with timer 18 ACTIVE at 0x80 and this IRQ ACTIVE at the
             * formerly forced 0x00.  Let the GIC hold equal-priority 698 pending
             * until the active timer is deactivated instead of forcing a nested
             * highest-priority interrupt on the M1 virtual CPU interface. */
            hv_vgic3_inject_irq(NWOAS_FL1100_SPI,
                                hv_vgic3_get_priority(NWOAS_FL1100_SPI),
                                false, true, false, 0);
            nwoas_seed_guest_irq_sent = true;
            printf("HVLOG: EVTMIRROR IRQ698 cpu=%u pc=0x%lx\n", smp_id(), hv_get_elr());
        }
        return;
    }

    // Session4 PRECISE 698 routing (real-HW-derived, replaces both the icpu pin and the all-core deliver):
    // - Pinning to icpu missed usbxhci's core (icpu was a parked/__fastfail core) -> 698 stuck.
    // - Injecting on ALL live cores fixed delivery but disrupted secondary-core bringup -> early boot
    //   stall (~kernel 91); a time-delayed variant crashed (2/2). So instead, target EXACTLY the core
    //   running usbxhci: hv_vm.c sets nwoas_fl_cpu = the core that touches the FL1100 BAR from the kernel.
    //   Run+deliver ONLY there. Before the kernel first touches the BAR (nwoas_fl_cpu<0), fall back to the
    //   original single icpu (safe: no secondary-core disruption during bringup). One core, so 698 goes
    //   to usbxhci without the all-core disruption AND without the wrong (parked) icpu.
    extern int nwoas_fl_cpu;
    int target = nwoas_fl_cpu >= 0 ? nwoas_fl_cpu : (hv_pinned_cpu != -1 ? hv_pinned_cpu : boot_cpu_idx);
    if ((int)smp_id() != target)
        return;
    static u64 fl_last_poll = 0;                 /* throttle MMIO polling to ~1ms (single target core) */
    u64 now = mrs(CNTPCT_EL0);
    if ((now - fl_last_poll) < (mrs(CNTFRQ_EL0) >> 10))
        return;
    fl_last_poll = now;

    /* CRITICAL: NEVER touch the FL1100 PCIe BAR before the Windows kernel is running. Unlike the
     * dwc3 (0x502280000, a core SoC device always decoded), the FL1100 BAR (0x6c0000000) is a PCIe
     * endpoint window that only decodes AFTER UEFI's XHC0._INI sets PCI CMD.MSE=1; reading it while
     * decode is off raises an Apple L2C/IMPDEF bus error from EL2 (data abort, FAR=0x6c0000000 ->
     * "Unhandled exception, rebooting") -- the same failure class that forced DBG2's removal. Gate
     * every FL1100 access on the kernel-canonical-VA latch: by kernel stage _INI has run and CMD.MSE
     * stays 1 (usbxhci is a platform device, never touches PCI CMD; HCRST resets only xHCI regs). */
    static bool fl_kseen = false;
    if ((hv_get_elr() >> 48) == 0xffffULL)     /* Windows kernel canonical VA -> FL1100 BAR is live */
        fl_kseen = true;
    if (!fl_kseen)
        return;

    static u64 fl_op = 0, fl_rt = 0;           /* resolve op/runtime base once */
    if (!fl_op) {
        /* GUARDED probe: even after kseen the FL1100 BAR can still be decode-off (CMD.MSE not yet
         * set by _INI if the UEFI EBS hook didn't cover this endpoint) -> reading it raises an Apple
         * L2C data abort. GUARD_SKIP makes the handler SKIP the faulting load (exc_count++) instead
         * of "Unhandled exception, rebooting". Reboot-proof regardless of the UEFI side. */
        exc_count = 0;
        exc_guard = GUARD_SKIP | GUARD_SILENT;
        u32 caplen = read32(NWOAS_FL1100_BASE + 0x00) & 0xff;
        u32 rtsoff = read32(NWOAS_FL1100_BASE + 0x18) & ~0x1fu;
        exc_guard = GUARD_OFF;
        if (exc_count || caplen == 0 || caplen == 0xff)  /* faulted / decode-off / not ready yet */
            return;                                       /* skip this poll, retry next */
        fl_op = NWOAS_FL1100_BASE + caplen;
        fl_rt = NWOAS_FL1100_BASE + rtsoff;
    }

    /* interrupter0: IMAN @ rt+0x20 (bit0 IP, bit1 IE), USBSTS @ op+0x04 (bit3 EINT), PORTSC0 @
     * op+0x400 (USB2 port0). ALL guarded: a mid-run BAR decode-off must SKIP, never reboot. */
    exc_count = 0;
    exc_guard = GUARD_SKIP | GUARD_SILENT;
    u32 iman   = read32(fl_rt + 0x20);
    u32 usbsts = read32(fl_op + 0x04);
    u32 portsc = read32(fl_op + 0x400);
    exc_guard = GUARD_OFF;
    if (exc_count || iman == 0xffffffffu)       /* faulted or bad/unmapped read */
        return;

    bool armed   = (iman & 0x2u);              /* guest enabled the interrupter = ISR exists */
    bool pending = (iman & 0x1u) || (usbsts & (1u << 3));

    {   /* throttled instrumentation: log ~1/sec ONLY (NOT every poll -- pend stays 1, so logging
         * on 'pending' floods the 115200 serial and wedges the hv in printf). 'outst' = is the
         * delivered 698 stuck in a vGIC LR (guest never took/EOI'd it)? outst=1 persistently =
         * vGIC delivery gap (guest ISR never runs); outst toggling = guest is servicing. */
        static u32 fdn = 0;
        if (((fdn++) & 0xfffu) == 0) { // ~1/4s: reduce flood (was 0x3ff)
            // Session4 vGIC-698 DECIDING DIAGNOSTIC. priority was REFUTED (prio698=0x80 < VPMR=0xf0, yet
            // stuck even when forced 0x00). Now distinguish the two remaining gates:
            //   698 LR STATE = ACTIVE  => guest TOOK it (ISR ran) but never deactivated (EOI/DIR gap under
            //     the commented-out IAR1/EOIR1 emulation) -> stuck active -> no further 698. LRword[63:62].
            //   698 LR STATE = PENDING => guest never took it. If guest PSTATE.I=1 (SPSR_EL2 bit7) it has
            //     IRQs masked during the storm spin; timer survives via constant re-deliver, 698 (deduped)
            //     is stranded. HCR.En(bit0) must be 1 for any LR to present.
            u64 vmcr = mrs(ICH_VMCR_EL2), spsr = mrs(SPSR_EL2), hcr = mrs(ICH_HCR_EL2);
            u64 lr698 = 0;
            for (int lr = 0; lr < 8; lr++) {
                u64 v = hv_vgic3_read_lr(lr);
                if (((v >> ICH_LR_VIRTUAL_SHIFT) & ICH_LR_VIRTUAL_MASK) == NWOAS_FL1100_SPI) { lr698 = v; break; }
            }
            const char *st = (lr698 & ICH_LR_STATE_ACTIVE)
                                 ? ((lr698 & ICH_LR_STATE_PENDING) ? "PEND+ACT" : "ACTIVE")
                                 : ((lr698 & ICH_LR_STATE_PENDING) ? "PENDING" : "none");
            printf("[fl1100-bridge] iman=0x%x usbsts=0x%x outst=%d | prio698=0x%x VPMR=0x%lx VENG1=%lu | "
                   "LR698=0x%lx state=%s | guestI=%lu HCR.En=%lu\n",
                   iman, usbsts, hv_vgic_virq_outstanding(NWOAS_FL1100_SPI),
                   hv_vgic3_get_priority(NWOAS_FL1100_SPI), (vmcr >> 24) & 0xff, (vmcr >> 1) & 1, lr698, st,
                   (spsr >> 7) & 1, hcr & 1);
            // Session4 ISR-hang disassembly: when 698 is ACTIVE (guest inside the hung ISR), dump the
            // instruction words around the live guest PC + x0-x3 so we can decode WHAT the ISR spins on.
            // (Placed here, not hv_exc_irq -- this bridge is the path that actually fires during the
            // hang.) Guest PC via hv_get_elr(); each DISTINCT kernel-VA PC once.
            u64 gelr = hv_get_elr();
            // Broadened: dump ANY kernel-VA spin with guest IRQs masked (PSTATE.I=1), not just when 698
            // is ACTIVE -- the wrong-CPU fix made 698 PENDING on the live core but usbxhci's core spins
            // with IRQs off (at 0x...a7e10450) so it never takes it. We need THAT spin's instructions.
            if ((gelr >> 48) == 0xffffULL && ((spsr >> 7) & 1)) {
                static u64 nwoas_last_isrpc = 0;
                static u32 nwoas_isr_dumps = 0;
                if (gelr != nwoas_last_isrpc && nwoas_isr_dumps < 24) {
                    nwoas_last_isrpc = gelr;
                    nwoas_isr_dumps++;
                    u64 pa = hv_translate(gelr - 16, false, false, NULL);
                    exc_count = 0;
                    enum exc_guard_t g2 = exc_guard;
                    exc_guard = GUARD_SKIP | GUARD_SILENT;
                    u32 w[8];
                    for (int i = 0; i < 8; i++)
                        w[i] = pa ? read32(pa + 4 * i) : 0;
                    exc_guard = g2;
                    printf("HVLOG: NWOAS-ISRPC elr=0x%lx pa=0x%lx flt=%d | %08x %08x %08x %08x [%08x] "
                           "%08x %08x %08x\n",
                           gelr, pa, exc_count, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
                }
            }
        }
    }

    if (!armed || !pending)
        return;
    // Session4 ring-ready gate (early-boot freeze fix): do NOT deliver 698 until Windows usbxhci has
    // actually programmed its event ring (ERSTBA latched by hv_vm.c). Before that, a leftover/spurious
    // FL1100 IMAN.IP (armed IE from UEFI or mid-init) would make the bridge fire 698 while the guest has
    // no event ring / no registered ISR yet -> the interrupt is unhandled -> __fastfail / a loop on an
    // empty ring that STALLS the boot at ~kernel 92 (real HW, session4). Gating on nwoas_fl_erstba!=0
    // means 698 fires only once usbxhci is genuinely ready to consume it (and the PSCE synth is armed).
    extern u64 nwoas_fl_erstba;
    if (!nwoas_fl_erstba)
        return;
    // Session4 takeable-core gate: only deliver 698 on a core the guest can ACTUALLY take it on now --
    // one with IRQs ENABLED (guest PSTATE.I=0). Real HW showed the core with 698 PENDING was IRQ-masked
    // (I=1) so it never took it, while usbxhci's core runs I=0. The bridge fires per-core on every trap
    // exit, so the usbxhci core WILL enter here with I=0; deliver there so 698 is taken on ERET. Skipping
    // masked cores also avoids stranding 698 PENDING in a masked core's LR bank (observed: 283x PENDING).
    if ((hv_get_spsr() >> 7) & 1)
        return;
    if (hv_vgic_virq_outstanding(NWOAS_FL1100_SPI))    /* dedup: at most one in flight (this core) */
        return;

    // S6-D50: use the guest-programmed distributor priority (observed 0x80).
    // D49's CPER proves the former forced 0x00 delivery ended in an implementation-defined
    // Arm SEI while a 0x80 timer LR and the 0x00 IRQ698 LR were both ACTIVE.  Equal priority
    // leaves 698 pending until the active timer is deactivated, avoiding that forced nested
    // vGIC state while preserving the existing delivery route and deduplication.
    u8 prio = hv_vgic3_get_priority(NWOAS_FL1100_SPI);
    if (hv_vgic3_get_free_lr() != -1) {
        hv_vgic3_inject_irq(NWOAS_FL1100_SPI, prio, false, true, false, 0);
    } else {
        virq_t q = { .vintid = NWOAS_FL1100_SPI, .priority = prio, .active = false,
                     .pending = true, .hw_status = false, .hw_irq = 0 };
        virq_queue_push(&PERCPU(irq_queue), &q);
    }
}
#endif // NWOAS_FL1100_BRIDGE
#endif

static void hv_update_fiq(void)
{
    u64 hcr = mrs(HCR_EL2);
    bool fiq_pending = false;

    if (mrs(CNTP_CTL_EL02) == (CNTx_CTL_ISTATUS | CNTx_CTL_ENABLE)) {
        fiq_pending = true;
        reg_clr(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, VM_TMR_FIQ_ENA_ENA_P);
        // NWOAS: do NOT set IMASK here. IMASK is cleared only by the guest's timer ISR
        // (which runs only after an delivery); combined with the rate-limit's "set
        // IMASK but skip the delivery" case it would PERMANENTLY WEDGE the timer — the
        // CTL==(ISTATUS|ENABLE) gate above would stay false forever and this block would
        // never run again. Throttling is done entirely by the rate-limit inside
        // hv_inject_timer_ppi(), which keeps this level gate live between injections.

        //TODO: proper delivery
#ifdef ENABLE_VGIC_MODULE
        hv_inject_timer_ppi(17);   // CNTP: dedup + rate-limited (see helper)
#endif
    } else {
        reg_set(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, VM_TMR_FIQ_ENA_ENA_P);
    }

    u64 nwoas_vctl = mrs(CNTV_CTL_EL02);
    // NWOAS-VGATE diag (observational, throttled): does the CNTV expiry gate below actually
    // pass? If pass=0 dominates while the kernel idles, the guest virtual timer is not reading
    // as fired -- suspect CNTVOFF_EL2 (stolen_time) skew set in hv_exc_exit -- and delivery is
    // starved at the gate, not the rate-limit.
#if !NWOAS_QUIET
    {
        static u32 nwoas_vgate_n = 0;
        if (((nwoas_vgate_n++) & 0x3fff) == 0)
            printf("HVLOG: NWOAS-VGATE vctl=0x%lx vct=0x%lx cval=0x%lx voff=0x%lx pass=%d\n",
                   nwoas_vctl, mrs(CNTVCT_EL0), mrs(CNTV_CVAL_EL02), mrs(CNTVOFF_EL2),
                   nwoas_vctl == (CNTx_CTL_ISTATUS | CNTx_CTL_ENABLE));
    }
#endif
    if (nwoas_vctl == (CNTx_CTL_ISTATUS | CNTx_CTL_ENABLE)) {
        fiq_pending = true;
        reg_clr(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, VM_TMR_FIQ_ENA_ENA_V);
        // NWOAS: no IMASK here either (see CNTP block) — it would wedge the timer when
        // combined with the rate-limit. Throttling is the rate-limit in the helper.

        //TODO: proper delivery
#ifdef ENABLE_VGIC_MODULE
        hv_inject_timer_ppi(18);   // CNTV: dedup + rate-limited (see helper)
#endif
    } else {
        reg_set(SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2, VM_TMR_FIQ_ENA_ENA_V);
    }

#ifdef ENABLE_VGIC_MODULE
    hv_bridge_usb_xhci_irq();   // NWOAS stage-8: bridge the dead M1 xHCI line (857) on real pending
#if NWOAS_FL1100_BRIDGE
    hv_bridge_fl1100_irq();     // NWOAS §3.2 Wall 2: bridge FL1100 (USB-A) INTx = SPI 698 on real pending
#endif
#endif

    fiq_pending |= PERCPU(ipi_pending) || PERCPU(pmc_pending);

    sysop("isb");
#ifndef ENABLE_VGIC_MODULE
    if ((hcr & HCR_VF) && !fiq_pending) {
        hv_write_hcr(hcr & ~HCR_VF);
    } else if (!(hcr & HCR_VF) && fiq_pending) {
        hv_write_hcr(hcr | HCR_VF);
    }
#endif
}

#define SYSREG_MAP(sr, to)                                                                         \
    case SYSREG_ISS(sr):                                                                           \
        if (is_read)                                                                               \
            regs[rt] = _mrs(sr_tkn(to));                                                           \
        else                                                                                       \
            _msr(sr_tkn(to), regs[rt]);                                                            \
        return true;

#define SYSREG_PASS(sr)                                                                            \
    case SYSREG_ISS(sr):                                                                           \
        if (is_read)                                                                               \
            regs[rt] = _mrs(sr_tkn(sr));                                                           \
        else                                                                                       \
            _msr(sr_tkn(sr), regs[rt]);                                                            \
        return true;

static bool hv_handle_msr_unlocked(struct exc_info *ctx, u64 iss)
{
    u64 reg = iss & (ESR_ISS_MSR_OP0 | ESR_ISS_MSR_OP2 | ESR_ISS_MSR_OP1 | ESR_ISS_MSR_CRn |
                     ESR_ISS_MSR_CRm);
    u64 rt = FIELD_GET(ESR_ISS_MSR_Rt, iss);
    bool is_read = iss & ESR_ISS_MSR_DIR;

    u64 *regs = ctx->regs;

    regs[31] = 0;

    switch (reg) {
        SYSREG_PASS(SYS_IMP_APL_CORE_NRG_ACC_DAT);
        SYSREG_PASS(SYS_IMP_APL_CORE_SRM_NRG_ACC_DAT);
        /* Architectural timer, for ECV */
        SYSREG_MAP(SYS_CNTV_CTL_EL0, SYS_CNTV_CTL_EL02)
        SYSREG_MAP(SYS_CNTV_CVAL_EL0, SYS_CNTV_CVAL_EL02)
        SYSREG_MAP(SYS_CNTV_TVAL_EL0, SYS_CNTV_TVAL_EL02)
        SYSREG_MAP(SYS_CNTP_CTL_EL0, SYS_CNTP_CTL_EL02)
        SYSREG_MAP(SYS_CNTP_CVAL_EL0, SYS_CNTP_CVAL_EL02)
        SYSREG_MAP(SYS_CNTP_TVAL_EL0, SYS_CNTP_TVAL_EL02)
        /* Spammy stuff seen on t600x p-cores */
        /* These are PMU/PMC registers */
        SYSREG_PASS(sys_reg(3, 2, 15, 12, 0));
        SYSREG_PASS(sys_reg(3, 2, 15, 13, 0));
        SYSREG_PASS(sys_reg(3, 2, 15, 14, 0));
        SYSREG_PASS(sys_reg(3, 2, 15, 15, 0));
        SYSREG_PASS(sys_reg(3, 1, 15, 7, 0));
        SYSREG_PASS(sys_reg(3, 1, 15, 8, 0));
        SYSREG_PASS(sys_reg(3, 1, 15, 9, 0));
        SYSREG_PASS(sys_reg(3, 1, 15, 10, 0));
        /* Noisy traps */
        SYSREG_PASS(SYS_IMP_APL_HID4)
        SYSREG_PASS(SYS_IMP_APL_EHID4)
        /* We don't normally trap these, but if we do, they're noisy */
        SYSREG_PASS(SYS_IMP_APL_GXF_STATUS_EL1)
        SYSREG_PASS(SYS_IMP_APL_CNTVCT_ALIAS_EL0)
        SYSREG_PASS(SYS_IMP_APL_TPIDR_GL1)
        SYSREG_MAP(SYS_IMP_APL_SPSR_GL1, SYS_IMP_APL_SPSR_GL12)
        SYSREG_MAP(SYS_IMP_APL_ASPSR_GL1, SYS_IMP_APL_ASPSR_GL12)
        SYSREG_MAP(SYS_IMP_APL_ELR_GL1, SYS_IMP_APL_ELR_GL12)
        SYSREG_MAP(SYS_IMP_APL_ESR_GL1, SYS_IMP_APL_ESR_GL12)
        SYSREG_MAP(SYS_IMP_APL_SPRR_PERM_EL1, SYS_IMP_APL_SPRR_PERM_EL12)
        SYSREG_MAP(SYS_IMP_APL_APCTL_EL1, SYS_IMP_APL_APCTL_EL12)
        SYSREG_MAP(SYS_IMP_APL_AMX_CTL_EL1, SYS_IMP_APL_AMX_CTL_EL12)
        /* FIXME:Might be wrong */
        SYSREG_PASS(SYS_IMP_APL_AMX_STATE_T)
        /* pass through PMU handling */
        SYSREG_PASS(SYS_IMP_APL_PMCR1)
        SYSREG_PASS(SYS_IMP_APL_PMCR2)
        SYSREG_PASS(SYS_IMP_APL_PMCR3)
        SYSREG_PASS(SYS_IMP_APL_PMCR4)
        SYSREG_PASS(SYS_IMP_APL_PMESR0)
        SYSREG_PASS(SYS_IMP_APL_PMESR1)
        SYSREG_PASS(SYS_IMP_APL_PMSR)
#ifndef DEBUG_PMU_IRQ
        SYSREG_PASS(SYS_IMP_APL_PMC0)
#endif
        SYSREG_PASS(SYS_IMP_APL_PMC1)
        SYSREG_PASS(SYS_IMP_APL_PMC2)
        SYSREG_PASS(SYS_IMP_APL_PMC3)
        SYSREG_PASS(SYS_IMP_APL_PMC4)
        SYSREG_PASS(SYS_IMP_APL_PMC5)
        SYSREG_PASS(SYS_IMP_APL_PMC6)
        SYSREG_PASS(SYS_IMP_APL_PMC7)
        SYSREG_PASS(SYS_IMP_APL_PMC8)
        SYSREG_PASS(SYS_IMP_APL_PMC9)

        //spammy ntoskrnl regs
        SYSREG_PASS(SYS_IMP_APL_L2C_ERR_STS)

        SYSREG_PASS(sys_reg(2, 0, 0, 1, 4))
        SYSREG_PASS(sys_reg(2, 0, 0, 1, 5))
        SYSREG_PASS(sys_reg(2, 0, 0, 1, 6))
        SYSREG_PASS(sys_reg(2, 0, 0, 1, 7))

        SYSREG_PASS(sys_reg(2, 0, 0, 2, 2))
        SYSREG_PASS(sys_reg(2, 0, 0, 2, 4))
        SYSREG_PASS(sys_reg(2, 0, 0, 2, 5))
        SYSREG_PASS(sys_reg(2, 0, 0, 2, 6))
        SYSREG_PASS(sys_reg(2, 0, 0, 2, 7))

        SYSREG_PASS(sys_reg(2, 0, 0, 3, 4))
        SYSREG_PASS(sys_reg(2, 0, 0, 3, 5))
        SYSREG_PASS(sys_reg(2, 0, 0, 3, 6))
        SYSREG_PASS(sys_reg(2, 0, 0, 3, 7))

        SYSREG_PASS(sys_reg(2, 0, 0, 4, 4))
        SYSREG_PASS(sys_reg(2, 0, 0, 4, 5))

        SYSREG_PASS(sys_reg(2, 0, 0, 5, 4))
        SYSREG_PASS(sys_reg(2, 0, 0, 5, 5))

        //these seem debugging related but looks like windbg/m1n1 debugger still works
        SYSREG_PASS(sys_reg(2, 0, 0, 0, 4))
        SYSREG_PASS(sys_reg(2, 0, 0, 0, 5))
        SYSREG_PASS(sys_reg(2, 0, 0, 0, 6))
        SYSREG_PASS(sys_reg(2, 0, 0, 0, 7))
        SYSREG_PASS(sys_reg(2, 0, 1, 1, 4))

        /* These only need to be trapped if ICH_HCR_EL2.TALL1 is set, currently not in use
        case SYSREG_ISS(ICC_IAR1_EL1):
            if(is_read) {
                regs[rt] = hv_vgic3_do_iar1();
                printf("R: ICC_IAR1_EL1: 0x%lx\n", regs[rt]);
            }
            else{
                printf("W: ICC_IAR1_EL1: 0x%lx\n", regs[rt]);
            }
            return true;
        case SYSREG_ISS(ICC_IGRPEN1_EL1):
            if(is_read) {
                regs[rt] = hv_vgic3_get_igrpen1();
                printf("R: ICC_IGRPEN1_EL1: 0x%lx\n", regs[rt]);
            }
            else{
                hv_vgic3_set_igrpen1(regs[rt]);
                printf("W: ICC_IGRPEN1_EL1: 0x%lx\n", regs[rt]);
            }
            return true;
        case SYSREG_ISS(ICC_BPR1_EL1):
            if(is_read) {
                regs[rt] = 0;
                printf("R: ICC_BPR1_EL1: 0x%lx\n", regs[rt]);
            }
            else{
                printf("W: ICC_BPR1_EL1: 0x%lx\n", regs[rt]);
            }
            return true;
        case SYSREG_ISS(ICC_EOIR1_EL1):
            if(is_read) {
                regs[rt] = 0;
                printf("R: ICC_EOIR1_EL1: 0x%lx\n", regs[rt]);
            }
            else{
                hv_vgic3_do_eoir1(regs[rt]);
                aic_set_mask(regs[rt], false);
                printf("W: ICC_EOIR1_EL1: 0x%lx\n", regs[rt]);
            }
            return true;
        */

#ifdef ENABLE_VGIC_MODULE
        /* NWOAS: force the guest to see the GICv3 system-register interface as
         * enabled. The outer hv sets HW ICC_SRE_EL1.SRE=1 before guest entry, but
         * the inner m1n1 guest clears it again before the UEFI's GICv3 detection
         * runs, so the firmware falls back to the (absent) GICv2 MMIO CPU
         * interface and faults at 0x8. Trapping ICC_SRE_EL1 (ICC_SRE_EL2.Enable=0)
         * lets us always report SRE and ignore writes, so detection picks GICv3. */
        case SYSREG_ISS(ICC_SRE_EL1):
            if (is_read)
                regs[rt] = 0x7; // SRE | DFB | DIB
            // writes ignored: keep the sysreg interface enabled
            return true;

        /* m1n1_windows change - emulate SGIs */
        case SYSREG_ISS(ICC_SGI1R_EL1):
            if(is_read) {
                regs[rt] = 0;
            }
            else{
                u64 sgir_value = regs[rt];
                u32 aff1, aff2, aff3;
                u32 aff0_targets;
                int virq, irm, rs;

                rs = (sgir_value >> ICH_SGI_RS_SHIFT) & ICH_SGI_RS_MASK;
                irm = (sgir_value >> ICH_SGI_IRQMODE_SHIFT) & ICH_SGI_IRQMODE_MASK;
                virq = (sgir_value >> ICH_SGI_IRQ_SHIFT ) & ICH_SGI_IRQ_MASK;
                aff1 = ICH_SGI_AFF1(sgir_value);
                aff2 = ICH_SGI_AFF2(sgir_value);
                aff3 = ICH_SGI_AFF3(sgir_value);
                aff0_targets = sgir_value & ICH_SGI_TARGETLIST_MASK;

                for(int cpu = 0; cpu < num_cpus; cpu++){
                    if(irm == ICH_SGI_TARGET_OTHERS){
                        if(smp_id() == cpu)
                            continue;
                    } else if(irm == ICH_SGI_TARGET_LIST){
                        if(aff0_targets == 0)
                            return false;
                        u64 mpidr =  smp_get_mpidr(cpu);

                        if(MPIDR_AFF3(mpidr) != aff3)
                            continue;
                        if(MPIDR_AFF2(mpidr) != aff2)
                            continue;
                        if(MPIDR_AFF1(mpidr) != aff1)
                            continue;
                        if(!(aff0_targets & BIT( MPIDR_AFF0(mpidr))))
                            continue;
                    } else{
                        return false;
                    }
                    virq_t pending = { 
                        .vintid = virq, 
                        .priority = 0x20, 
                        .active = false, 
                        .pending = true,
                        .hw_status = false,
                        .hw_irq = 0,
                    };
                    virq_queue_push(&PERCPU_N(cpu, sgi_queue), &pending);
                    smp_send_ipi(cpu);
                }
            }
            return true;
        /* m1n1_windows change - advertise GIC */
        case SYSREG_ISS(ID_AA64PFR0_EL1):
            if(is_read) {
                u64 pfr0_value = mrs(ID_AA64PFR0_EL1);
                regs[rt] = pfr0_value | ((0x1 & 0xF) << 24);
            }
            else{
                msr(ID_AA64PFR0_EL1, regs[rt]);
            }
            return true;
#endif
        /* m1n1_windows change - Trap the ARM standard PMU regs */
        case SYSREG_ISS(SYS_PMCR_EL0):
            if(is_read) {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 calculated = 0;
                u64 pmi_mask = PMCR0_IMODE_MASK;
                //
                // Are PMIs enabled? (affects bit 0 of PMCR equivalently)
                //
                if((pmcr0_value & pmi_mask) == (PMCR0_IMODE_FIQ)) {
                    calculated |= PMCR_E;
                }
                //
                // Bits 5:1 are 0 for now,  bits 6 and 7 are on always (long events always on)
                // and bit 9 is checked by PMCR0[20] (since it deals with freeze/overflow)
                //
                if((pmcr0_value & BIT(20)) != 0) {
                    calculated |= PMCR_FZO;
                }
                calculated |= ((BIT(6)) | (BIT(7)));
                printf("HV PMUv3 Redirect: mrs x%ld, PMCR_EL0 = 0x%lx\n", rt, calculated);
                regs[rt] = calculated;
            }
            else {
                //
                // Bits [63:10] will have writes discarded (mostly ARM spec, bit 32 due to lack of support)
                //
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                int cycle_reset_requested = 0;
                //
                // Bit 9 (stop count on overflow) writes affect bit 20 of APL_PMCR0
                //
                if((regs[rt] & BIT(9)) != 0) {
                    pmcr0_value |= BIT(20);
                }
                else {
                    pmcr0_value &= ~(BIT(20));
                }
                
                //
                // Writes to bits [6:3] unsupported since no way of expressing either with Apple PMUs.
                //
                // Writing bit 2 (cycle counter reset) implies setting PMC0 to 0, so check if that's the case.
                //
                if((regs[rt] & PMCR_C) != 0) {
                    cycle_reset_requested = 1;
                }
                //
                // Bit 1 is the same as bit 2 but for event counters, unimplemented for now.
                //
                // Bit 0 controls whether event counters are enabled globally, if this is being set,
                // the closest thing on Apple platforms is the IRQ mode so set that if bit 0 is requested.
                //
                if((regs[rt] & PMCR_E) != 0) {
                    pmcr0_value &= ~(PMCR0_IMODE_MASK);
                    pmcr0_value |= PMCR0_IMODE_FIQ;
                }
                else {
                    pmcr0_value &= ~(PMCR0_IMODE_MASK);
                    pmcr0_value |= PMCR0_IMODE_OFF;
                }
                sysop("isb");
                if(cycle_reset_requested == 1) {
                    pmcr0_value &= ~(BIT(12));
                    pmcr0_value &= ~(BIT(0));
                }
                msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                sysop("isb");
                if(cycle_reset_requested == 1) {
                    sysop("isb");
                    msr(SYS_IMP_APL_PMC0, 0);
                    sysop("isb");
                    pmcr0_value |= BIT(12);
                    pmcr0_value |= BIT(0);
                    sysop("isb");
                    msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                    sysop("isb");
                }
                printf("HV PMUv3 Redirect (OK): msr PMCR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        // NWOAS PMCCNTR: winload reads PMCCNTR_EL0 only inside ETW/timestamp helpers
        // (rva 0xcabbc/0x9018/0x9474). The "PMCCNTR delay-loop stall" hypothesis is
        // REFUTED: the stage-8 wedge loop is NON-trapping (PMC0 advances at 2GHz between
        // the sparse ETW traps), so winload is NOT polling PMCCNTR to wait. The wedge is
        // a busy-wait on some unchanging flag. What we log below is therefore not the
        // counter value but the CALLER (LR) of the ETW helper == the wedge loop's PC.
        case SYSREG_ISS(SYS_PMCCNTR_EL0): {
            if (is_read) {
                u64 pmc0 = mrs(SYS_IMP_APL_PMC0);
                regs[rt] = pmc0;
                // NWOAS stage-8: winload's post-QCSL stall is a NON-trapping loop that only
                // surfaces via the ETW/timestamp helpers it calls -- every trapping winload PC
                // in the stall (rva 0xcabbc/0x9018/0x9474/0x2d6c8) is an `mrs pmccntr_el0`
                // inside such a helper. These helpers are entered via `bl`, so ctx->regs[30]
                // (LR) == the return address into the CALLER == the stuck outer loop, and
                // ctx->regs[0]/[1] == the ETW event id / first args == WHAT winload keeps
                // (re)tracing. winload reads PMCCNTR only a few hundred times total, so log
                // every read (cap 600). Filter the analysis to winload PCs (0x83d...).
                static u32 nwoas_pmcc_n = 0;
                if (nwoas_pmcc_n < 600) {
                    nwoas_pmcc_n++;
                    printf("HVLOG: NWOAS-PMCC elr=0x%lx lr=0x%lx x0=0x%lx x1=0x%lx\n",
                           ctx->elr, ctx->regs[30], ctx->regs[0], ctx->regs[1]);
                }
            } else {
                msr(SYS_IMP_APL_PMC0, regs[rt]);
            }
            return true;
        }
        case SYSREG_ISS(SYS_PMCCFILTR_EL0):
            if(is_read) {
                u64 pmcr1_value = mrs(SYS_IMP_APL_PMCR1);
                u64 calculated_value = 0;
                //
                // If EL0/EL1 counting is disabled, set bit 30 of PMCCFILTR to 1.
                // (This is backwards from how I would've done it but okay I suppose...)
                //
                if((pmcr1_value & BIT(8)) == 0) {
                    calculated_value |= BIT(30);
                }
                if((pmcr1_value & BIT(16)) == 0) {
                    calculated_value |= BIT(31);
                }
                //
                // EL2 counting always happens as far as is known, so bit 27 is always set to 1
                // (bit set to 1 in this case means disable filtering...not sure why it's backwards.)
                //
                calculated_value |= BIT(27);
                printf("HV PMUv3 Redirect: mrs x%ld, PMCCFILTR_EL0 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                u64 pmcr1_value = mrs(SYS_IMP_APL_PMCR1);
                //
                // If we're being asked to disable counting cycles for a given EL, set the appropriate bit for
                // PMCR1 respectively.
                //
                if((regs[rt] & PMCCFILTR_P) == 0) {
                    pmcr1_value |= BIT(16);
                }
                else {
                    pmcr1_value &= ~(BIT(16));
                }
                if((regs[rt] & PMCCFILTR_U) == 0) {
                    pmcr1_value |= BIT(8);
                }
                else {
                    pmcr1_value &= ~(BIT(16));
                }
                sysop("isb");
                msr(SYS_IMP_APL_PMCR1, pmcr1_value);
                sysop("isb");
                printf("HV PMUv3 Redirect (OK): msr PMCCFILTR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        case SYSREG_ISS(SYS_PMCEID0_EL0):
            //
            // Unimplemented for now, return 0 for a read, discard writes.
            //
            if(is_read) {
                regs[rt] = 0;
                printf("HV PMUv3 Redirect: mrs x%ld, PMCEID0_EL0 = 0x%lx\n", rt, regs[rt]);
            }
            else {
                //
                // Do nothing here.
                //
                printf("HV PMUv3 Redirect (skipped write): msr PMCEID0_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        case SYSREG_ISS(SYS_PMCEID1_EL0):
            //
            // Unimplemented for now, return 0 for a read, discard writes.
            //
            if(is_read) {
                regs[rt] = 0;
                printf("HV PMUv3 Redirect: mrs x%ld, PMCEID1_EL0 = 0x%lx\n", rt, regs[rt]);
            }
            else {
                printf("HV PMUv3 Redirect (skipped write): msr PMCEID1_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        case SYSREG_ISS(SYS_PMCNTENCLR_EL0):
            if(is_read) {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 calculated_value = 0;
                //
                // check what perf counters are enabled in PMCR0, and reflect that in the returned PMCNTENCLR/PMCNTENSET value
                //
                if((pmcr0_value & BIT(0)) != 0) {
                    calculated_value |= BIT(31);
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMCNTENCLR_EL0 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 counters_disable_mask = GENMASK(31,0);
                //
                // check if any counters are being requested to be disabled (cycle counter only for now)
                // and deal with those here.
                //
                if((regs[rt] & counters_disable_mask) != 0) {
                    if((regs[rt] & BIT(31)) != 0) {
                        pmcr0_value &= ~(BIT(0));
                    }
                    sysop("isb");
                    msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                    sysop("isb");
                    printf("HV PMUv3 Redirect (OK): msr PMCNTENCLR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
                }
            }
            return true;
        case SYSREG_ISS(SYS_PMCNTENSET_EL0):
            if(is_read) {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 calculated_value = 0;
                //
                // check what perf counters are enabled in PMCR0, and reflect that in the returned PMCNTENCLR/PMCNTENSET value
                //
                if((pmcr0_value & BIT(0)) != 0) {
                    calculated_value |= BIT(31);
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMCNTENSET_EL0 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 counters_enable_mask = GENMASK(31,0);
                //
                // check if any counters are being requested to be disabled (cycle counter only for now)
                // and deal with those here.
                //
                if((regs[rt] & counters_enable_mask) != 0) {
                    if((regs[rt] & BIT(31)) != 0) {
                        pmcr0_value |= BIT(0);
                    }
                    sysop("isb");
                    msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                    sysop("isb");
                    printf("HV PMUv3 Redirect (OK): msr PMCNTENSET_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
                }
            }
            return true;
        SYSREG_MAP(SYS_PMEVCNTR0_EL0, SYS_IMP_APL_PMC2)
        // case SYSREG_ISS(SYS_PMEVTYPER0_EL0):
        //     if(is_read) {
        //         printf("mrs(PMEVTYPER0_EL0)\n");
        //         regs[rt] = 0;
        //         int value = mrs(SYS_IMP_APL_PMCR1);
        //         if(value & GENMASK(23, 16)) {
        //             regs[rt] |= BIT(31); //privileged bit
        //         }
        //         if(value & GENMASK(15, 8)) {
        //             regs[rt] |= BIT(30); //user filter bit
        //         }
        //         regs[rt] |= (mrs(SYS_IMP_APL_PMESR0) & GENMASK(7, 0));
        //     }
        //     else {
        //         int val = mrs(SYS_IMP_APL_PMCR1);
        //         int event = mrs(SYS_IMP_APL_PMESR0) & GENMASK(7, 0);
        //         if(regs[rt] & PMEVTYPER_P) {
        //             printf("msr(PMEVTYPER0_EL0, 0x%08lx): enabling el1 counting of event\n", regs[rt]);
        //             val |= BIT(16);
        //         }
        //         if(regs[rt] & GENMASK(7, 0)) {
        //             printf("msr(PMEVTYPER0_EL0, 0x%08lx): setting event\n", regs[rt]);
        //             event |= regs[rt] & GENMASK(7, 0);
        //             msr(SYS_IMP_APL_PMESR0, event);
        //         }
        //         msr(SYS_IMP_APL_PMCR1, val);
        //     }
        //     return true;
        case SYSREG_ISS(SYS_PMINTENCLR_EL1):
            if(is_read) {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 calculated_value = 0;
                //
                // As before, cycle counter only for now. (bit 12 = PMI enabled for cycle counter)
                //
                if((pmcr0_value & BIT(12)) != 0) {
                    calculated_value |= BIT(31);
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMINTENCLR_EL1 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 counter_irqs_disabled_mask = GENMASK(31,0);
                //
                // Cycle counter only for now (bits 19:12 control all PMIs for the PMUs)
                //
                if((regs[rt] & counter_irqs_disabled_mask) != 0) {
                    if((regs[rt] & (BIT(31))) != 0) {
                        pmcr0_value &= ~(BIT(12));
                    }
                    sysop("isb");
                    msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                    sysop("isb");
                    printf("HV PMUv3 Redirect (OK): msr PMINTENCLR_EL1, x%ld = 0x%lx\n", rt, regs[rt]);
                }

            }
            return true;
        case SYSREG_ISS(SYS_PMINTENSET_EL1):
            if(is_read) {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 calculated_value = 0;
                //
                // As before, cycle counter only for now. (bit 12 = PMI enabled for cycle counter)
                //
                if((pmcr0_value & BIT(12)) != 0) {
                    calculated_value |= BIT(31);
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMINTENSET_EL1 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 counter_irqs_enabled_mask = GENMASK(31,0);
                //
                // Cycle counter only for now (bits 19:12 control all PMIs for the PMUs)
                //
                if((regs[rt] & counter_irqs_enabled_mask) != 0) {
                    if((regs[rt] & BIT(31)) != 0) {
                        pmcr0_value |= BIT(12);
                    }
                    sysop("isb");
                    msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                    sysop("isb");
                    printf("HV PMUv3 Redirect (OK): msr PMINTENSET_EL1, x%ld = 0x%lx\n", rt, regs[rt]);
                }
            }
            return true;
        case SYSREG_ISS(SYS_PMMIR_EL1):
            //
            // return 0 for now, discard writes.
            //
            if(is_read) {
                regs[rt] = 0;
                printf("HV PMUv3 Redirect: mrs x%ld, PMMIR_EL1 = 0x%lx\n", rt, regs[rt]);
            }
            else {
                printf("HV PMUv3 Redirect (skipped write): msr PMMIR_EL1, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        case SYSREG_ISS(SYS_PMOVSCLR_EL0):
            if(is_read) {
                //
                // Read the state of the PMSR register to see if a PMU has overflowed.
                // Cycle counter only for now.
                //
                u64 pmsr_value = mrs(SYS_IMP_APL_PMSR);
                u64 calculated_value = 0;
                u64 pmu_overflowed_mask = GENMASK(9, 0);
                if((pmsr_value & pmu_overflowed_mask) != 0) {
                    //
                    // bit 0 is for PMC 0
                    //
                    if((pmsr_value & BIT(0)) != 0) {
                        calculated_value |= BIT(31);
                    }
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMOVSCLR_EL0 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                //
                // To clear the overflow bit requires a reset of the PMC (to clear PMSR)
                // so we need to disable the counter and re-enable it with the bit set to 0.
                //
                u64 pmcr0_value = mrs(SYS_IMP_APL_PMCR0);
                u64 counter_overflow_mask = GENMASK(31,0);
                if((regs[rt] & counter_overflow_mask) != 0) {
                    if((regs[rt] & BIT(31)) != 0) {
                        pmcr0_value &= ~(BIT(12));
                        pmcr0_value &= ~(BIT(0));
                        sysop("isb");
                        msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                        sysop("isb");
                        sysop("isb");
                        msr(SYS_IMP_APL_PMC0, 0);
                        sysop("isb");
                        pmcr0_value |= BIT(12);
                        pmcr0_value |= BIT(0);
                        sysop("isb");
                        msr(SYS_IMP_APL_PMCR0, pmcr0_value);
                        sysop("isb");
                    }
                printf("HV PMUv3 Redirect (OK): msr PMOVSCLR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
                }
            }
            return true;
        case SYSREG_ISS(SYS_PMOVSSET_EL0):
            if(is_read) {
                //
                // Read the state of the PMSR register to see if a PMU has overflowed.
                // Cycle counter only for now.
                //
                u64 pmsr_value = mrs(SYS_IMP_APL_PMSR);
                u64 calculated_value = 0;
                u64 pmu_overflowed_mask = GENMASK(9, 0);
                if((pmsr_value & pmu_overflowed_mask) != 0) {
                    //
                    // bit 0 is for PMC 0
                    //
                    if((pmsr_value & BIT(0)) != 0) {
                        calculated_value |= BIT(31);
                    }
                }
                printf("HV PMUv3 Redirect: mrs x%ld, PMOVSSET_EL0 = 0x%lx\n", rt, calculated_value);
                regs[rt] = calculated_value;
            }
            else {
                //
                // For now, don't set the overflow bit. If this needs to change, reuse the code from before.
                //
            }
            return true;
        case SYSREG_ISS(SYS_PMSELR_EL0):
            //for now hardcode to set the cycle counter, this will very likely need to change
            if(is_read) {
                regs[rt] = 31;
                printf("HV PMUv3 Redirect: mrs x%ld, PMSELR_EL0 = 0x%lx\n", rt, regs[rt]);
            }
            else {
                printf("HV PMUv3 Redirect (skipped write): msr PMSELR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
            return true;
        //SYSREG_MAP(SYS_PMSWINC_EL0, SYS_IMP_APL_PMC3)
        case SYSREG_ISS(SYS_PMUSERENR_EL0):
            if(is_read) {
                regs[rt] = 0;
                printf("HV PMUv3 Redirect: mrs x%ld, PMUSERENR_EL0 = 0x%lx\n", rt, regs[rt]);
            }
            else {
                printf("HV PMUv3 Redirect (skipped write): msr PMUSERENR_EL0, x%ld = 0x%lx\n", rt, regs[rt]);
            }
           return true;
        // SYSREG_MAP(SYS_PMXEVCNTR_EL0, SYS_IMP_APL_PMC2)
        // case SYSREG_ISS(SYS_PMXEVTYPER_EL0):
        //     if(is_read) {
        //         printf("mrs(PMXEVTYPER_EL0)\n");
        //         regs[rt] = 0;
        //         int value = mrs(SYS_IMP_APL_PMCR1);
        //         if(value & GENMASK(23, 16)) {
        //             regs[rt] |= BIT(31); //privileged bit
        //         }
        //         if(value & GENMASK(15, 8)) {
        //             regs[rt] |= BIT(30); //user filter bit
        //         }
        //         regs[rt] |= (mrs(SYS_IMP_APL_PMESR0) & GENMASK(7, 0));
        //     }
        //     else {
        //         int val = mrs(SYS_IMP_APL_PMCR1);
        //         int event = mrs(SYS_IMP_APL_PMESR0) & GENMASK(7, 0);
        //         if(regs[rt] & PMEVTYPER_P) {
        //             printf("msr(PMEVTYPER0_EL0, 0x%08lx): enabling el1 counting of event\n", regs[rt]);
        //             val |= BIT(16);
        //         }
        //         if(regs[rt] & GENMASK(7, 0)) {
        //             printf("msr(PMEVTYPER0_EL0, 0x%08lx): setting event\n", regs[rt]);
        //             event |= regs[rt] & GENMASK(7, 0);
        //             msr(SYS_IMP_APL_PMESR0, event);
        //         }
        //         msr(SYS_IMP_APL_PMCR1, val);
        //     }
        //     return true;

        /* Outer Sharable TLB maintenance instructions */
        SYSREG_PASS(sys_reg(1, 0, 8, 1, 0)) // TLBI VMALLE1OS
        SYSREG_PASS(sys_reg(1, 0, 8, 1, 1)) // TLBI VAE1OS
        SYSREG_PASS(sys_reg(1, 0, 8, 1, 2)) // TLBI ASIDE1OS
        SYSREG_PASS(sys_reg(1, 0, 8, 5, 1)) // TLBI RVAE1OS

        case SYSREG_ISS(SYS_ACTLR_EL1):
            if (is_read) {
                if (cpu_features->actlr_el2)
                    regs[rt] = mrs(SYS_ACTLR_EL12);
                else
                    regs[rt] = mrs(SYS_IMP_APL_ACTLR_EL12);
            } else {
                if (cpu_features->actlr_el2)
                    msr(SYS_ACTLR_EL12, regs[rt]);
                else
                    msr(SYS_IMP_APL_ACTLR_EL12, regs[rt]);
            }
            return true;

        case SYSREG_ISS(SYS_IMP_APL_IPI_SR_EL1):
            if (is_read)
                regs[rt] = PERCPU(ipi_pending) ? IPI_SR_PENDING : 0;
            else if (regs[rt] & IPI_SR_PENDING)
                PERCPU(ipi_pending) = false;
            return true;

        /* shadow the interrupt mode and state flag */
        case SYSREG_ISS(SYS_IMP_APL_PMCR0):
            if (is_read) {
                u64 val = (mrs(SYS_IMP_APL_PMCR0) & ~PMCR0_IMODE_MASK) | PERCPU(pmc_irq_mode);
                regs[rt] = val | (PERCPU(pmc_pending) ? PMCR0_IACT : 0);
            } else {
                PERCPU(pmc_pending) = !!(regs[rt] & PMCR0_IACT);
                PERCPU(pmc_irq_mode) = regs[rt] & PMCR0_IMODE_MASK;
                msr(SYS_IMP_APL_PMCR0, regs[rt]);
            }
            return true;

        /*
         * Handle this one here because m1n1/Linux (will) use it for explicit cpuidle.
         * We can pass it through; going into deep sleep doesn't break the HV since we
         * don't do any wfis that assume otherwise in m1n1. However, don't het macOS
         * disable WFI ret (when going into systemwide sleep), since that breaks things.
         */
        case SYSREG_ISS(SYS_IMP_APL_CYC_OVRD):
            if (is_read) {
                regs[rt] = mrs(SYS_IMP_APL_CYC_OVRD);
            } else {
                if (regs[rt] & (CYC_OVRD_DISABLE_WFI_RET | CYC_OVRD_FIQ_MODE_MASK))
                    return false;
                msr(SYS_IMP_APL_CYC_OVRD, regs[rt]);
            }
            return true;
            /* clang-format off */
        /* IPI handling */
        SYSREG_PASS(SYS_IMP_APL_IPI_CR_EL1)
        /* M1RACLES reg, handle here due to silly 12.0 "mitigation" */
        case SYSREG_ISS(sys_reg(3, 5, 15, 10, 1)):
            if (is_read)
                regs[rt] = 0;
            return true;
    }
    return false;
}

// NWOAS stage-8: NON-PERTURBING guest-trajectory ring. Recorded in the hot path with
// NO serial I/O (a per-exception printf perturbed SMP rendezvous timing -> spurious
// "missing CPU" panic). Dumped (RLE-compressed: a poll loop collapses to one line with
// a repeat count) only at the rare PSCI call or a heavy periodic checkpoint, so it
// cannot recreate that Heisenbug. Shows the guest PC + faulting address (poll target).
#define PC_RING_SZ 512
static struct { u64 elr, far; u32 esr, cpu; } pc_ring[PC_RING_SZ];
static u32 pc_ring_idx;

static void __attribute__((unused)) dump_pc_ring(const char *why)   // NWOAS_QUIET gates both callers
{
    u32 total = pc_ring_idx;
    u32 n = total < PC_RING_SZ ? total : PC_RING_SZ;
    u32 start = total - n;
    printf("[pc-ring] %s: last %u guest sync-exc (RLE):\n", why, n);
    u64 le = 1, lf = 1; u32 lesr = 0, lcpu = 0, cnt = 0;
    for (u32 k = 0; k < n; k++) {
        u32 i = (start + k) & (PC_RING_SZ - 1);
        if (cnt && pc_ring[i].elr == le && pc_ring[i].far == lf &&
            pc_ring[i].esr == lesr && pc_ring[i].cpu == lcpu) { cnt++; continue; }
        if (cnt)
            printf("  cpu%u elr=0x%lx far=0x%lx ec=0x%lx x%u\n",
                   lcpu, le, lf, FIELD_GET(ESR_EC, lesr), cnt);
        le = pc_ring[i].elr; lf = pc_ring[i].far;
        lesr = pc_ring[i].esr; lcpu = pc_ring[i].cpu; cnt = 1;
    }
    if (cnt)
        printf("  cpu%u elr=0x%lx far=0x%lx ec=0x%lx x%u\n",
               lcpu, le, lf, FIELD_GET(ESR_EC, lesr), cnt);
}

static bool hv_handle_smc(struct exc_info *ctx) {
#if !NWOAS_QUIET
    printf("PSCI SMC DEBUG: req=0x%lx elr=0x%lx args=0x%lx,0x%lx,0x%lx\n",
           ctx->regs[0], ctx->elr, ctx->regs[1], ctx->regs[2], ctx->regs[3]);
    hv_vuart_dump_log();   // NWOAS: the UEFI's full DEBUG output (captured from its UART TX)
    hv_bmgfw_uart_dump();  // NWOAS: bootmgfw's own serial output (snooped in the abort path)
    dump_pc_ring("psci");
#endif
    bool handled_smc = hv_handle_psci_smc(ctx);
    return handled_smc;
}

static bool hv_handle_msr(struct exc_info *ctx, u64 iss)
{
    u64 reg = iss & (ESR_ISS_MSR_OP0 | ESR_ISS_MSR_OP2 | ESR_ISS_MSR_OP1 | ESR_ISS_MSR_CRn |
                     ESR_ISS_MSR_CRm);
    u64 rt = FIELD_GET(ESR_ISS_MSR_Rt, iss);
    bool is_read = iss & ESR_ISS_MSR_DIR;

    u64 *regs = ctx->regs;

    regs[31] = 0;

    switch (reg) {
        /* clang-format on */
        case SYSREG_ISS(SYS_IMP_APL_IPI_RR_LOCAL_EL1): {
            assert(!is_read);
            u64 mpidr = (regs[rt] & 0xff) | (mrs(MPIDR_EL1) & 0xffff00);
            for (int i = 0; i < MAX_CPUS; i++)
                if (mpidr == smp_get_mpidr(i)) {
                    pcpu[i].ipi_queued = true;
                    msr(SYS_IMP_APL_IPI_RR_LOCAL_EL1, regs[rt]);
                    return true;
                }
            return false;
        }
        case SYSREG_ISS(SYS_IMP_APL_IPI_RR_GLOBAL_EL1):
            assert(!is_read);
            u64 mpidr = (regs[rt] & 0xff) | ((regs[rt] & 0xff0000) >> 8);
            for (int i = 0; i < MAX_CPUS; i++) {
                if (mpidr == (smp_get_mpidr(i) & 0xffff)) {
                    pcpu[i].ipi_queued = true;
                    msr(SYS_IMP_APL_IPI_RR_GLOBAL_EL1, regs[rt]);
                    return true;
                }
            }
            return false;
#ifdef DEBUG_PMU_IRQ
        case SYSREG_ISS(SYS_IMP_APL_PMC0):
            if (is_read) {
                regs[rt] = mrs(SYS_IMP_APL_PMC0);
            } else {
                msr(SYS_IMP_APL_PMC0, regs[rt]);
                printf("msr(SYS_IMP_APL_PMC0, 0x%04lx_%08lx)\n", regs[rt] >> 32,
                       regs[rt] & 0xFFFFFFFF);
            }
            return true;
#endif
    }

    return false;
}

static void hv_get_context(struct exc_info *ctx)
{
    ctx->spsr = hv_get_spsr();
    ctx->elr = hv_get_elr();
    ctx->esr = hv_get_esr();
    ctx->far = hv_get_far();
    ctx->afsr1 = hv_get_afsr1();
    ctx->sp[0] = mrs(SP_EL0);
    ctx->sp[1] = mrs(SP_EL1);
    ctx->sp[2] = (u64)ctx;
    ctx->cpu_id = smp_id();
    ctx->mpidr = mrs(MPIDR_EL1);

    sysop("isb");
}

static void hv_exc_entry(void)
{
    // Enable SErrors in the HV, but only if not already pending
    if (!(mrs(ISR_EL1) & 0x100))
        sysop("msr daifclr, 4");

    __atomic_and_fetch(&hv_cpus_in_guest, ~BIT(smp_id()), __ATOMIC_ACQUIRE);
    spin_lock(&bhl);
    hv_wdt_breadcrumb('X');
    exc_entry_time = mrs(CNTPCT_EL0);
    /* disable PMU counters in the hypervisor */
    u64 pmcr0 = mrs(SYS_IMP_APL_PMCR0);
    PERCPU(exc_entry_pmcr0_cnt) = pmcr0 & PMCR0_CNT_MASK;
    msr(SYS_IMP_APL_PMCR0, pmcr0 & ~PMCR0_CNT_MASK);
}

/* Implemented by nwoas_stage8.inc below.  The arm path runs from the locked
 * FIQ slow path once ntoskrnl is resident; the HVC path captures bugcheck
 * arguments before Windows can reset the target. */
static void nwoas_bugcheck_arm(struct exc_info *ctx);
static bool nwoas_bugcheck_handle(struct exc_info *ctx);

static void hv_exc_exit(struct exc_info *ctx)
{
    hv_wdt_breadcrumb('x');
    hv_update_fiq();
    /* reenable PMU counters */
    reg_set(SYS_IMP_APL_PMCR0, PERCPU(exc_entry_pmcr0_cnt));
    msr(CNTVOFF_EL2, stolen_time);
    spin_unlock(&bhl);
    hv_maybe_exit();
    __atomic_or_fetch(&hv_cpus_in_guest, BIT(smp_id()), __ATOMIC_ACQUIRE);

    hv_set_spsr(ctx->spsr);
    hv_set_elr(ctx->elr);
    msr(SP_EL0, ctx->sp[0]);
    msr(SP_EL1, ctx->sp[1]);
}

void hv_exc_sync(struct exc_info *ctx)
{
    hv_wdt_breadcrumb('S');
    hv_get_context(ctx);
    bool handled = false;
    u32 ec = FIELD_GET(ESR_EC, ctx->esr);

    /* NWOAS D45: common bugcheck one-shot probe. HVC is a call-like exception,
     * so ELR normally already names the following instruction.  The helper
     * verifies both ELR and ELR-4 before restoring/emulating the displaced
     * the displaced PACIBSP, making that architectural detail explicit. */
    if (ec == ESR_EC_HVC && FIELD_GET(ESR_ISS, ctx->esr) == 0x124 &&
        nwoas_bugcheck_handle(ctx)) {
        hv_set_elr(ctx->elr);
        hv_update_fiq();
        hv_wdt_breadcrumb('s');
        return;
    }

    // NWOAS stage-8: record guest trajectory to the NON-PERTURBING ring (no serial I/O
    // here — a printf in this hot path perturbed SMP rendezvous timing). The heavy,
    // RLE-compressed periodic dump is the only fallback if the guest never reaches PSCI.
    {
        u32 i = (pc_ring_idx++) & (PC_RING_SZ - 1);
        pc_ring[i].elr = ctx->elr;
        pc_ring[i].far = ctx->far;
        pc_ring[i].esr = (u32)ctx->esr;
        pc_ring[i].cpu = (u32)ctx->cpu_id;
#if !NWOAS_QUIET
        if ((pc_ring_idx & 0x3ffff) == 0)   // NWOAS stage-8: every 256K sync-exc (was 1M) to
            dump_pc_ring("periodic");     // capture the stall-region trajectory; WFx-NOP
                                          // busy-loops RLE-collapse to one line per dump
#endif
    }

    switch (ec) {
        case ESR_EC_MSR:
            hv_wdt_breadcrumb('m');
            handled = hv_handle_msr_unlocked(ctx, FIELD_GET(ESR_ISS, ctx->esr));
            break;
        //
        // for Blizzard/Avalanche and later - we need to explicitly check for SMC EC to handle SMCs
        //
        case ESR_EC_SMC:
            hv_wdt_breadcrumb('s');
            handled = hv_handle_smc(ctx);
            break;
        case ESR_EC_IMPDEF:
            hv_wdt_breadcrumb('a');
            if(ctx->afsr1 == 0x1c00000) {
                /**
                 * m1n1_windows change: add SMC handling support.
                 * 
                 * right now the only reason a guest OS would fire an SMC is due to 
                 * requesting a PSCI service.
                */
                handled = hv_handle_smc(ctx);
                break;
            }
            switch (FIELD_GET(ESR_ISS, ctx->esr)) {
                case ESR_ISS_IMPDEF_MSR:
                    handled = hv_handle_msr_unlocked(ctx, ctx->afsr1);
                    break;
            }
            break;
        //
        // NWOAS stage-8: trap+diagnose guest WFI/WFE (enabled via HCR_EL2.TWI; WFE only if
        // TWE is later added). winload's pre-kernel-handoff stall produces NO trap at all --
        // the CPU just idles in WFI forever -- so the hang is invisible. Trapping WFx here
        // makes each idle-wait a logged sample, then we NOP-advance it (handled -> elr+=4 +
        // hv_update_fiq() in the block below). Safety: a WFI whose wake event never arrives
        // never resumes anyway, so NOP-advancing is at worst identical to the silent halt;
        // and re-running hv_update_fiq() on every WFx gives the rate-limited timer PPI a
        // fresh delivery window. WFI is trapped ONLY when no wake event is pending (a
        // pending vIRQ makes WFI a NOP that does not trap), so every line below is a genuine
        // "guest is waiting for an interrupt that is not being delivered" snapshot. Handled
        // in this pre-hv_exc_entry() fast path (like the PMCCNTR/MSR-unlocked diagnostics) to
        // avoid the SMP-rendezvous / serial perturbation of the full proxy path. Rate-limited
        // to the first 40 traps; after that WFx is silently NOP-advanced.
        case ESR_EC_WFI: {
            hv_wdt_breadcrumb('w');
            static u32 nwoas_wfx_n = 0;
            u32 wfx_seq = nwoas_wfx_n++;
            u32 wfx_iss = (u32)FIELD_GET(ESR_ISS, ctx->esr);
            bool is_wfe = wfx_iss & 1; // ESR.ISS[1:0]=TI: 0=WFI,1=WFE,2=WFIT,3=WFET
            // NWOAS stage-8: log each DISTINCT WFE program-counter once (with the live GP
            // registers that hold the polled flag address), so the late pre-handoff WFE
            // wait is captured even after early boot spinlocks exhaust the first-40 budget.
            // A tight WFE spin sits at one PC, so log-on-change emits it exactly once.
            if (is_wfe) {
                static u64 nwoas_last_wfe_elr = 0;
                static u32 nwoas_wfe_sites = 0;
                if (NWOAS_HOTPATH_LOG && ctx->elr != nwoas_last_wfe_elr && nwoas_wfe_sites < 128) {
                    nwoas_last_wfe_elr = ctx->elr;
                    nwoas_wfe_sites++;
                    printf("HVLOG: NWOAS-WFE-SITE elr=0x%lx daif=0x%lx x0=0x%lx x1=0x%lx "
                           "x2=0x%lx x3=0x%lx x19=0x%lx x20=0x%lx x21=0x%lx lr=0x%lx\n",
                           ctx->elr, (ctx->spsr >> 6) & 0xf, ctx->regs[0], ctx->regs[1],
                           ctx->regs[2], ctx->regs[3], ctx->regs[19], ctx->regs[20],
                           ctx->regs[21], ctx->regs[30]);
                }
            }
            if (NWOAS_HOTPATH_LOG && wfx_seq < 40) {
                u64 daif = (ctx->spsr >> 6) & 0xf; // guest PSTATE.{D,A,I,F} nibble
                printf("HVLOG: NWOAS-WFX n=%u cpu=%lu %s elr=0x%lx spsr=0x%lx daif=0x%lx "
                       "iss=0x%x isr_el1=0x%lx\n",
                       wfx_seq, ctx->cpu_id, is_wfe ? "WFE" : "WFI", ctx->elr, ctx->spsr,
                       daif, wfx_iss, mrs(ISR_EL1));
                printf("HVLOG: NWOAS-WFX n=%u tmr vctl=0x%lx vcval=0x%lx pctl=0x%lx "
                       "pcval=0x%lx vct=0x%lx pct=0x%lx\n",
                       wfx_seq, mrs(CNTV_CTL_EL02), mrs(CNTV_CVAL_EL02), mrs(CNTP_CTL_EL02),
                       mrs(CNTP_CVAL_EL02), mrs(CNTVCT_EL0), mrs(CNTPCT_EL0));
#ifdef ENABLE_VGIC_MODULE
                u64 lr_occ = 0, lr0_vintid = 0;
                for (int lr = 0; lr < 8; lr++) {
                    u64 v = hv_vgic3_read_lr(lr);
                    if (v & (ICH_LR_STATE_PENDING | ICH_LR_STATE_ACTIVE)) {
                        lr_occ |= (1u << lr);
                        if (!lr0_vintid)
                            lr0_vintid = (v >> ICH_LR_VIRTUAL_SHIFT) & ICH_LR_VIRTUAL_MASK;
                    }
                }
                printf("HVLOG: NWOAS-WFX n=%u vgic misr=0x%lx eisr=0x%lx elrsr=0x%lx "
                       "lr_occ=0x%lx vintid=%lu\n",
                       wfx_seq, mrs(ICH_MISR_EL2), mrs(ICH_EISR_EL2), mrs(ICH_ELRSR_EL2),
                       lr_occ, lr0_vintid);
#endif
            }
            handled = true;
            break;
        }
    }

    if (handled) {
        hv_wdt_breadcrumb('#');
        ctx->elr += 4;
        hv_set_elr(ctx->elr);
        hv_update_fiq();
        hv_wdt_breadcrumb('s');
        return;
    }

    /*
     * NWOAS S6-D42 diagnostic: an inner m1n1 running at EL1 reaches HVC #0
     * only through its reboot() stub.  If that reboot followed an unhandled
     * guest exception, the useful syndrome was printed to the emulated UART
     * and captured by hv_vm.c, but the outer Python HV previously stopped at
     * the HVC without dumping that ring.  Emit both observational rings here
     * before entering the existing proxy/shell path.  Do not advance ELR or
     * mark the HVC handled: the failing guest remains stopped exactly as
     * before, while the original exception becomes visible in the log.
     */
    if (ec == ESR_EC_HVC && FIELD_GET(ESR_ISS, ctx->esr) == 0) {
        printf("HVLOG: NWOAS guest HVC#0; dumping captured UART before proxy\n");
        hv_bmgfw_uart_dump();
#if !NWOAS_QUIET
        dump_pc_ring("guest-hvc0");
#endif
    }

    hv_exc_entry();

    switch (ec) {
        case ESR_EC_DABORT_LOWER:
            hv_wdt_breadcrumb('D');
            handled = hv_handle_dabort(ctx);
            break;
        case ESR_EC_MSR:
            hv_wdt_breadcrumb('M');
            handled = hv_handle_msr(ctx, FIELD_GET(ESR_ISS, ctx->esr));
            break;
        case ESR_EC_IMPDEF:
            hv_wdt_breadcrumb('A');
            switch (FIELD_GET(ESR_ISS, ctx->esr)) {
                case ESR_ISS_IMPDEF_MSR:
                    handled = hv_handle_msr(ctx, ctx->afsr1);
                    break;
            }
            break;
    }

    if (handled) {
        hv_wdt_breadcrumb('+');
        ctx->elr += 4;
    } else {
        hv_wdt_breadcrumb('-');
        // VM code can forward a nested SError exception here
        if (FIELD_GET(ESR_EC, ctx->esr) == ESR_EC_SERROR)
            hv_exc_proxy(ctx, START_EXCEPTION_LOWER, EXC_SERROR, NULL);
        else
            hv_exc_proxy(ctx, START_EXCEPTION_LOWER, EXC_SYNC, NULL);
    }

    hv_exc_exit(ctx);
    hv_wdt_breadcrumb('s');
}

void hv_exc_irq(struct exc_info *ctx)
{
    // NWOAS stage-8 guest-PC sampler: on every physical FIQ to EL2, ctx->elr is the guest
    // PC where the guest was interrupted. The pc-ring only records TRAPPING (MMIO) PCs, so
    // it is dominated by the background UEFI USB timers (XhciDxe 0xbdf...) and cannot see
    // winload's non-trapping code. run11 showed winload goes silent right after its QCSL
    // SMC with no ExitBootServices/CPU_ON -- so it may be stuck in NON-USB code (measured-
    // launch / stage-8 handoff prep) while the USB timers merely run in the background.
    // This FIQ-time sample resolves it: a winload PC (0x83c...) => stuck non-USB; a UEFI
    // XhciDxe PC (0xbdf...) => genuinely spinning in USB. Rate-limited (every 256th FIQ).
    {
        static u32 nwoas_pcs_n = 0;
        if (NWOAS_HOTPATH_LOG && ((nwoas_pcs_n++) & 0x3) == 0)   // every 4th FIQ (was 256th; FIQs are sparse)
            printf("HVLOG: NWOAS-PC elr=0x%lx spsr=0x%lx\n", ctx->elr, ctx->spsr);
        // Session4 ISR-hang disassembly: the FL1100 698 ISR takes the interrupt then SPINS forever
        // (state=ACTIVE, never EOIs, ERDP never advances). On a physical timer FIQ, ctx->elr is that
        // spin PC. When the guest is at a kernel VA with IRQs masked (PSTATE.I=1 = inside the ISR/DIRQL),
        // dump the instruction words around it + the GP regs so we can disassemble WHAT it waits on
        // (a memory flag? an MMIO readback?). Each DISTINCT spin PC once (a tight loop sits at one PC).
        if ((ctx->elr >> 48) == 0xffffULL && ((ctx->spsr >> 7) & 1)) {
            static u64 nwoas_last_isrpc = 0;
            static u32 nwoas_isr_dumps = 0;
            if (ctx->elr != nwoas_last_isrpc && nwoas_isr_dumps < 24) {
                nwoas_last_isrpc = ctx->elr;
                nwoas_isr_dumps++;
                u64 pa = hv_translate(ctx->elr - 16, false, false, NULL);
                enum exc_guard_t g = exc_guard;
                exc_count = 0;
                exc_guard = GUARD_SKIP | GUARD_SILENT;
                u32 w[8];
                for (int i = 0; i < 8; i++)
                    w[i] = pa ? read32(pa + 4 * i) : 0;
                exc_guard = g;
                // w[4] is the instruction AT elr. x0-x3 often hold the polled address/value.
                printf("HVLOG: NWOAS-ISRPC elr=0x%lx pa=0x%lx flt=%d | %08x %08x %08x %08x [%08x] %08x "
                       "%08x %08x | x0=0x%lx x1=0x%lx x2=0x%lx x3=0x%lx\n",
                       ctx->elr, pa, exc_count, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7],
                       ctx->regs[0], ctx->regs[1], ctx->regs[2], ctx->regs[3]);
            }
        }
    }
#ifdef ENABLE_VGIC_MODULE
    u32 reason = aic_ack();
    int irq = FIELD_GET(AIC_EVENT_NUM, reason);
    int type = FIELD_GET(AIC_EVENT_TYPE, reason);

    // NWOAS D1 device-visibility (observational): a real device IRQ is (irq!=0 && type!=0);
    // (irq==0 || type==0) is the maintenance path below. Log the first device IRQs (and a
    // throttled tail). Closes the unlogged direct-delivery blind spot: if the boot thread
    // waits on a device-completion event, its IRQ must appear HERE. Silence => the physical
    // device never raised an interrupt => the wait is not satisfiable by a device IRQ.
    if (irq != 0 && type != 0) {
        static u32 nwoas_dev_n = 0;
        if (NWOAS_HOTPATH_LOG && (nwoas_dev_n < 64 || ((nwoas_dev_n & 0xff) == 0)))
            printf("[dev-irq] irq=%d type=%d n=%u elr=0x%lx\n", irq, type, nwoas_dev_n, ctx->elr);
        nwoas_dev_n++;
    }

    u64 misr = mrs(ICH_MISR_EL2);
    u64 eisr = mrs(ICH_EISR_EL2);

    if(irq == 0 || type == 0){//maintenance IRQ?
        // NWOAS stage-8 candidate-1 instrumentation (observational only).
        // Snapshot maintenance-IRQ entry state. Absence of this line after the
        // stage-8 handoff == the maintenance IRQ never fired (queue starvation).
        // MISR bit semantics (per GICv3): the drain gate below keys on EOI, i.e.
        // ICH_MISR_EL2.EOI (bit0, asserted when ICH_EISR_EL2 != 0) -- NOT LRENP
        // (bit2). So the expected nonzero misr here is 0x1 (EOI), not 0x4 (LRENP);
        // an earlier note had this backwards. Which bit M1 actually sets is
        // hardware-only and UNRESOLVED -- the raw misr/eisr are printed below so
        // the true value is observable; do not assume 0x4.
        u64 elrsr_obs = mrs(ICH_ELRSR_EL2);
        u32 iq_d = PERCPU(irq_queue).head   - PERCPU(irq_queue).tail;
        u32 sq_d = PERCPU(sgi_queue).head   - PERCPU(sgi_queue).tail;
        u32 tq_d = PERCPU(timer_queue).head - PERCPU(timer_queue).tail;
        // NWOAS: throttle (printing every maint starves the guest on the 115200
        // serial) and add the CNTP timer state so we can tell whether the guest
        // reprograms the deadline into the future (cval > now) or it keeps
        // re-expiring immediately (cval <= now == timer too fast for the slow hv).
        static u32 maint_n = 0;
        if (NWOAS_HOTPATH_LOG && ((maint_n++) & 0xff) == 0) {
            printf("[vgic-maint] n=%u misr=0x%lx eisr=0x%lx elrsr=0x%lx iq=%u sq=%u"
                   " tq=%u cval=0x%lx now=0x%lx ctl=0x%lx\n",
                   maint_n, misr, eisr, elrsr_obs, iq_d, sq_d, tq_d,
                   mrs(CNTP_CVAL_EL02), mrs(CNTPCT_EL0), mrs(CNTP_CTL_EL02));
        }
        if(misr != 0 && eisr != 0){
            for(int lr = 0; lr < 8; lr++){
                if(eisr & BIT(lr)){
                    u64 lr_val = hv_vgic3_read_lr(lr);
                    u64 intd = (lr_val >> ICH_LR_VIRTUAL_SHIFT) & ICH_LR_VIRTUAL_MASK;
                    hv_vgic3_write_lr(lr, 0);
                    if(intd > 31)
                        aic_set_mask(intd, false);//TODO: check distributor
                }
            }
        }

        while(hv_vgic3_get_free_lr() != -1){
            virq_t pending;
            if (!virq_queue_pop(&PERCPU(timer_queue), &pending))
                break;
            hv_vgic3_inject_irq(
                pending.vintid,
                pending.priority,
                pending.active,
                pending.pending,
                pending.hw_status,
                pending.hw_irq
            );
        }
        while(hv_vgic3_get_free_lr() != -1){
            virq_t pending;
            if (!virq_queue_pop(&PERCPU(sgi_queue), &pending))
                break;
            hv_vgic3_inject_irq(
                pending.vintid,
                pending.priority,
                pending.active,
                pending.pending,
                pending.hw_status,
                pending.hw_irq
            );
        }
        while(hv_vgic3_get_free_lr() != -1){
            virq_t pending;
            if (!virq_queue_pop(&PERCPU(irq_queue), &pending))
                break;
            hv_vgic3_inject_irq(
                pending.vintid,
                pending.priority,
                pending.active,
                pending.pending,
                pending.hw_status,
                pending.hw_irq
            );
        }
        // NWOAS stage-8 candidate-1 instrumentation (observational only).
        // Post-drain snapshot. Robust success criterion is depths==0 (iq=sq=tq=0).
        // elrsr=0xff only holds if M1 implements 8 List Registers; if
        // ICH_VTR_EL2.ListRegs<8 the upper ELRSR bits are RES0 and 0xff never
        // appears even on a clean drain -- so rely on depths==0, not elrsr=0xff.
        if (NWOAS_HOTPATH_LOG)
            printf("[vgic-maint-done] elrsr=0x%lx iq=%u sq=%u tq=%u\n",
                   mrs(ICH_ELRSR_EL2),
                   PERCPU(irq_queue).head   - PERCPU(irq_queue).tail,
                   PERCPU(sgi_queue).head   - PERCPU(sgi_queue).tail,
                   PERCPU(timer_queue).head - PERCPU(timer_queue).tail);
        return;
    }

    // NWOAS Stage 2 (interrupt-storm fix): this is a real device IRQ (irq!=0 && type!=0).
    // AIC v1 does NOT auto-mask a level source on aic_ack (event-register read only, aic.c),
    // and we deliver in SW mode (hw_status=false), so a LEVEL-triggered line -- e.g. the Apple
    // PCIe port INTx aggregate (AIC 695/698/701; FL1100=698, DTB flag=4 level-high) -- stays
    // PHYSICALLY asserted after ack. Without masking it here, EL2 re-enters this FIQ the instant
    // we ERET back to the guest, before its ISR can service + EOI the device -> interrupt storm
    // / hv watchdog reset. Mask the physical AIC source now; the maintenance branch above
    // (aic_set_mask(intd,false) at eisr, ~line 1606) unmasks it when the guest deactivates the
    // vIRQ (EOI -> LR empty -> ICH_EISR). This supplies the missing half of the mask/unmask
    // discipline (deliver=mask / EOI=unmask). Safe for edge IRQs too (they just get one extra
    // mask/unmask). Device IRQs are all SPIs (>31), which the maintenance unmask path covers.
    aic_set_mask(irq, true);

    if(hv_vgic3_get_free_lr() != -1){
        hv_vgic3_inject_irq(
            irq,                         //vintid
            hv_vgic3_get_priority(irq),  //priority
            false,                       //active
            true,                        //pending
            false,                       //hw_status
            0                            //hw_irq
        );
    }
    else{
        virq_t pending = { 
            .vintid = irq, 
            .priority = 0x40, 
            .active = false, 
            .pending = true,
            .hw_status = false,
            .hw_irq = 0,
        };
        // NWOAS stage-8 candidate-1 instrumentation (observational only).
        // Capture the (previously discarded) push result. Behavior is unchanged:
        // the queue is still pushed exactly once. !pushed == silent drop (full
        // queue); depth at/above half without an intervening [vgic-maint] line
        // == maintenance IRQ not draining.
        bool pushed = virq_queue_push(&PERCPU(irq_queue), &pending);
        u32 iq_d_after = PERCPU(irq_queue).head - PERCPU(irq_queue).tail;
        if (!pushed || iq_d_after >= (u32)(VIRQ_QUEUE_SIZE / 2))
            printf("[vgic-irq-q] irq=%d depth=%u/%u misr=0x%lx elrsr=0x%lx %s\n",
                   irq, iq_d_after, (u32)VIRQ_QUEUE_SIZE,
                   misr, mrs(ICH_ELRSR_EL2),
                   pushed ? "QUEUED" : "DROPPED");
    }
#else
    hv_wdt_breadcrumb('I');
    hv_get_context(ctx);
    hv_exc_entry();

    hv_exc_proxy(ctx, START_EXCEPTION_LOWER, EXC_IRQ, NULL);
    hv_exc_exit(ctx);
    hv_wdt_breadcrumb('i');
#endif
}

/* NWOAS stage-8 dispatcher-wait diagnostic (READ-ONLY scheduler walk). Verified offsets
 * (ntoskrnl 22621.525) — see nwoas_stage8.inc. Called once from the wedge one-shot block. */
#include "nwoas_stage8.inc"

void hv_exc_fiq(struct exc_info *ctx)
{
    bool tick = false;

    // NWOAS stage-8 wedge catcher (safe design). The pre-handoff wedge is a silent,
    // non-trapping guest loop; the only thing still running is m1n1's own tick, handled
    // here. We RECORD the guest PC into a ring every tick (pure memory writes below, after
    // hv_exc_entry -- no serial, cannot disturb the uart proxy) and, once the guest has sat
    // in one small PC window for ~3s (a confirmed spin), dump the ring + registers EXACTLY
    // ONCE, and only AFTER hv_tick() has finished servicing the proxy for this tick (so the
    // dump lands in a proxy-idle window, like the MMIO traces that are known safe). This is
    // the fix for wfidiag7, whose printf ran mid-proxy-servicing and desynced it.
    static u64 nwoas_wpc_ring[32];
    static u32 nwoas_wpc_idx;
    static u64 nwoas_wpc_last;
    static u32 nwoas_wpc_near;
    static bool nwoas_wpc_dumped;
    bool nwoas_do_dump = false;

    hv_maybe_exit();

    //
    // TODO: deliver the FIQ to the guest as an IRQ if vGIC is enabled.
    //

    if (mrs(CNTP_CTL_EL0) == (CNTx_CTL_ISTATUS | CNTx_CTL_ENABLE)) {
        msr(CNTP_CTL_EL0, CNTx_CTL_ISTATUS | CNTx_CTL_IMASK | CNTx_CTL_ENABLE);
        tick = true;
    }

    // NWOAS stage-8 tick profiler: moved into hv_tick() (safe slow-path context) -- doing
    // printf here on the fast path, before hv_exc_entry(), is unsafe and broke m1n1 boot
    // (wfidiag6). hv_tick() runs every tick on the interruptible CPU (where the guest runs,
    // including during the pre-handoff stall) and is a known printf-safe context.

    int interruptible_cpu = hv_pinned_cpu;
    if (interruptible_cpu == -1)
        interruptible_cpu = boot_cpu_idx;

    if (smp_id() != interruptible_cpu && !(mrs(ISR_EL1) & 0x40) && hv_want_cpu == -1) {
        // Non-interruptible CPU and it was just a timer tick (or spurious), so just update FIQs
        hv_update_fiq();
        hv_arm_tick(true);
        return;
    }

    // Slow (single threaded) path
    hv_wdt_breadcrumb('F');
    hv_get_context(ctx);
    hv_exc_entry();

    /* Arm before servicing the tick so the probe is installed as soon as a
     * verified ntoskrnl PC is observed, well before the late WHEA reset. */
    nwoas_bugcheck_arm(ctx);

    // NWOAS wedge catcher -- RECORD only (no printf, safe). ctx->elr is the guest PC where
    // the tick interrupted it. Track how long the guest stays within one 8KB window: a tight
    // silent spin accumulates, ordinary forward progress (which moves the PC far every tick
    // at native speed) resets it. Arm a one-shot dump once it has been stuck ~3s (15000
    // ticks at 5kHz).
    if (tick && smp_id() == interruptible_cpu) {
        u64 e = ctx->elr;
        nwoas_wpc_ring[(nwoas_wpc_idx++) & 31] = e;
        u64 d = e > nwoas_wpc_last ? e - nwoas_wpc_last : nwoas_wpc_last - e;
        if (d < 0x2000)
            nwoas_wpc_near++;
        else
            nwoas_wpc_near = 0;
        nwoas_wpc_last = e;
        if (nwoas_wpc_near > 15000 && !nwoas_wpc_dumped) {
            nwoas_wpc_dumped = true;
            nwoas_do_dump = true;
        }
    }

    // Only poll for HV events in the interruptible CPU
    if (tick) {
        if (smp_id() == interruptible_cpu) {
            hv_tick(ctx);
            hv_arm_tick(false);
        } else {
            hv_arm_tick(true);
        }
    }

    // NWOAS wedge catcher -- one-shot DUMP, AFTER hv_tick() serviced the proxy (proxy-idle
    // window). Prints the current guest PC + registers (a polled flag address shows up in
    // the regs) and the 32-entry PC ring (the spin's PC range).
    if (NWOAS_HOTPATH_LOG && nwoas_do_dump) {
        printf("HVLOG: NWOAS-WEDGE stuck near=%u elr=0x%lx spsr=0x%lx\n",
               nwoas_wpc_near, ctx->elr, ctx->spsr);
        printf("HVLOG: NWOAS-WEDGE regs x0=0x%lx x1=0x%lx x2=0x%lx x3=0x%lx x19=0x%lx "
               "x20=0x%lx x21=0x%lx lr=0x%lx\n",
               ctx->regs[0], ctx->regs[1], ctx->regs[2], ctx->regs[3], ctx->regs[19],
               ctx->regs[20], ctx->regs[21], ctx->regs[30]);
        for (int k = 0; k < 32; k++)
            printf("HVLOG: NWOAS-WEDGE wpc[%02d]=0x%lx\n", k,
                   nwoas_wpc_ring[(nwoas_wpc_idx + k) & 31]);
        nwoas_stage8_walk(ctx);   /* Task-D: dispatcher-wait diagnostic (READ-ONLY, one-shot) */
    }

    if (mrs(CNTV_CTL_EL0) == (CNTx_CTL_ISTATUS | CNTx_CTL_ENABLE)) {
        msr(CNTV_CTL_EL0, CNTx_CTL_ISTATUS | CNTx_CTL_IMASK | CNTx_CTL_ENABLE);
        hv_exc_proxy(ctx, START_HV, HV_VTIMER, NULL);
    }

    u64 reg = mrs(SYS_IMP_APL_PMCR0);
    if ((reg & (PMCR0_IMODE_MASK | PMCR0_IACT)) == (PMCR0_IMODE_FIQ | PMCR0_IACT)) {
#ifdef DEBUG_PMU_IRQ
        printf("[FIQ] PMC IRQ, masking and delivering to the guest\n");
#endif
        reg_clr(SYS_IMP_APL_PMCR0, PMCR0_IACT | PMCR0_IMODE_MASK);
        PERCPU(pmc_pending) = true;
    }

    reg = mrs(SYS_IMP_APL_UPMCR0);
    if (FIELD_GET(UPMCR0_IMODE_T8020, reg) == UPMCR0_IMODE_FIQ &&
        (mrs(SYS_IMP_APL_UPMSR) & UPMSR_IACT)) {
        printf("[FIQ] UPMC IRQ, masking");
        reg_clr(SYS_IMP_APL_UPMCR0, UPMCR0_IMODE_T8020);
        hv_exc_proxy(ctx, START_EXCEPTION_LOWER, EXC_FIQ, NULL);
    }

    if (mrs(SYS_IMP_APL_IPI_SR_EL1) & IPI_SR_PENDING) {
#ifdef ENABLE_VGIC_MODULE
        while(hv_vgic3_get_free_lr() != -1){//another CPU sent an IPI, check the sgi_queue
            virq_t pending;
            if (!virq_queue_pop(&PERCPU(sgi_queue), &pending))
                break;
            hv_vgic3_inject_irq(
                pending.vintid,
                pending.priority,
                pending.active,
                pending.pending,
                pending.hw_status,
                pending.hw_irq
            );
        }
#endif
        if (PERCPU(ipi_queued)) {
            PERCPU(ipi_pending) = true;
            PERCPU(ipi_queued) = false;
        }
        msr(SYS_IMP_APL_IPI_SR_EL1, IPI_SR_PENDING);
        sysop("isb");
    }

    hv_maybe_switch_cpu(ctx, START_HV, HV_CPU_SWITCH, NULL);

    // Handles guest timers
    hv_exc_exit(ctx);
    hv_wdt_breadcrumb('f');
}

void hv_exc_serr(struct exc_info *ctx)
{
    hv_wdt_breadcrumb('E');
    hv_get_context(ctx);
    hv_exc_entry();
    hv_exc_proxy(ctx, START_EXCEPTION_LOWER, EXC_SERROR, NULL);
    hv_exc_exit(ctx);
    hv_wdt_breadcrumb('e');
}
