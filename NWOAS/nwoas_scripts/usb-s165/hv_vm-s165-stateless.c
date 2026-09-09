/* SPDX-License-Identifier: MIT */

// #define DEBUG

#include "hv.h"
#include "assert.h"
#include "cpu_regs.h"
#include "exception.h"
#include "hv_vgic.h"
#include "iodev.h"
#include "malloc.h"
#include "nvme.h"
#include "smp.h"
#include "string.h"
#include "types.h"
#include "uartproxy.h"
#include "utils.h"

extern uint64_t ram_base;

// NWOAS: hv_pinned_cpu is defined in hv.c but only extern-declared in hv_exc.c;
// hv_handle_dabort() below needs it to never auto-park the pinned (kernel) vCPU.
extern int hv_pinned_cpu;

// NWOAS: consecutive unmapped-IPA aborts (same ELR) from a non-boot/non-pinned
// guest vCPU before it is cleanly parked out of the guest (see hv_handle_dabort).
#define NWOAS_UNMAPPED_PARK_THRESHOLD 16

// NWOAS §6-E / §6-D: keep the FL1100 xHCI control structures (ERST, event-ring segment, DCBAA,
// command ring) that the card DMA-READS coherent with usbxhci's writes. usbxhci allocates them
// CACHEABLE (UEFI used Write-Combining, which is why UEFI works), so on the non-coherent apcie/DART
// DMA path its writes sit in CPU cache and the card reads stale/zero DRAM.
//   NWOAS_ERST_CLEAN: at ring-pointer latch time, dc-civac those structures to DRAM. FACT (real HW,
//     2026-07-12, appark-chainload-2151.log): this one-shot clean is INSUFFICIENT alone -- the event
//     ring stayed empty and DCBAA[1..]=0 for 6+ min. It is RETAINED as break-before-make step 1
//     (flush dirty lines to DRAM before the page is switched to non-cacheable).
//   NWOAS_NC_REMAP (§6-D): after each clean, PERMANENTLY remap that page's stage-2 entry to Normal
//     Non-cacheable (mimics UEFI's WC), so every later usbxhci write bypasses the cache -- removing
//     the "when to clean?" timing race entirely.
//     *** REFUTED ON REAL HW (2026-07-13, appark-chainload-1424.log). *** The remap fired cleanly
//     (NCREMAP x194, no memory corruption, design review 4-lens = sound) BUT it wedged the guest:
//     the 16KB stage-2 granule forces up to 12KB of UNRELATED guest data in the ring page to NC too,
//     and Windows put atomic-accessed data there. Apple Silicon raises an IMPDEF abort (ESR EC=0x3f)
//     on LSE atomics / exclusives to Non-cacheable memory, so the guest's `ldsetalb w1,w2,[0xae0fcafe8]`
//     (offset 0x2fe8 collateral in the NC ring page) faulted 2042x in an unrecoverable loop -> wedge.
//     The predicted granularity hazard fired. Whole-page NC of guest-owned pages is a DEAD END; keep
//     it OFF. (A viable coherence fix would need 4KB stage-2 granule OR hv-owned NC rings the FL1100
//     DMAs to with the hv mirroring the guest's cacheable copy -- deferred: coherence is NOT the
//     primary blocker anyway; the command ring stayed EMPTY = usbxhci never issued Enable Slot, so
//     the real wall is the HCRST reset storm + FL1100 not posting events. See NWOAS-STATUS.)
#define NWOAS_ERST_CLEAN 0
#define NWOAS_NC_REMAP   0

// NWOAS session5 (2026-07-14) COHERENCE TIMING-RACE FIX. The one-shot NWOAS_ERST_CLEAN cleaned the
// FL1100's DMA-read control structures (ERST[0], event-ring segment, DCBAA) to DRAM ONCE at ring-pointer
// latch time, which real HW proved INSUFFICIENT: a later usbxhci re-write (or an FL1100 ERST-read that
// happens many µs after the latch, e.g. at RS=1) re-introduces the stale cached copy -> the No-Snoop
// FL1100 reads a stale/zero ring base again -> posts nothing (IMAN.IP=0). NWOAS_COH_KEEP re-cleans those
// structures (targeted: 16B ERST[0]/DCBAA + 1 page event-ring segment) EVERY time usbxhci touches an
// interrupter register AND periodically on the USBSTS poll, so DRAM is fresh at the instant the card
// DMA-reads it. Targeted-only (never a wide dc-op) and no NC remap (Apple faults LSE atomics on NC), so
// it avoids both the §6-D granule wedge and the session4 broad-dc-op wedge. Verdict = hv-direct IMAN.IP
// (FORK/CMDW): IP=1 + non-synth type-34 TRB + ERDP advance => coherence timing confirmed+fixed.
#ifndef NWOAS_COH_KEEP
#define NWOAS_COH_KEEP 1
#endif

// This coherence-only experiment observes the RAW FL1100 (no synthetic events, no storm taming) so a
// posting is unambiguously the controller's. Force NWOAS_STORM_TAME OFF for this build without editing
// its own #ifndef/#define block below (the guard there makes this pre-definition win): the storm-tame
// USBCMD-rewrite path (#if NWOAS_STORM_TAME) compiles out, and the nwoas_synth_check() call is disabled
// via `#if !NWOAS_STORM_TAME && !NWOAS_COH_KEEP` at its call site (COH_KEEP alone suffices). The synth
// DEFINITIONS (698 bridge / DART / PSCE) are left untouched; only the generation call is gated off so it
// cannot mask the coherence effect by substituting for real FL1100 posting.
#if NWOAS_COH_KEEP
#ifndef NWOAS_STORM_TAME
#define NWOAS_STORM_TAME 0
#endif
#endif

// NWOAS session4 (2026-07-13) — CCS-stabilization experiment. Two Opus adversarial reviews of the
// ring-ready-gate boot (appark-chainload-1859.log) converged: for THIS wall usbxhci enumerates ports
// by DIRECT PORTSC POLLING (it never advances ERDP in the kernel phase — our generated PSCE sits
// UNCONSUMED), and the real blocker is a ~12x HCRST reset storm ending HALTED (RS=0), NOT event
// starvation. The mouse port PORTSC (BAR+0x490) was observed FLAPPING CCS=1 (0x20ae1) <-> CCS=0
// (0x2a0, PLS=RxDetect) in the raw HW reads. Hypothesis H2: usbxhci starts a port reset, the flaky
// FL1100 link drops CCS mid-handshake, the reset never completes -> HCRST -> storm -> give up.
// EXPERIMENT: once a real kernel-VA read of a target PORTSC returns CCS=1, latch it and OR-in bit0
// (CCS) on every later read so usbxhci never observes the drop. ALL other bits (CSC/PR/PRC/PED/PLS/
// speed) pass through unchanged so the CSC-ack and port-reset handshakes still function. This is the
// controlled single-variable test that distinguishes CAUSE (CCS toggle drives the storm -> masking it
// stops the storm + Enable Slot appears) from SYMPTOM (storm is HCRST-induced -> masking CCS does
// nothing). Read-only transform on two register offsets, same proven-safe class as the EINT/IP emulate:
// no NC-remap (banned — Apple Silicon faults LSE atomics to NC memory), no atomics, no dc-ops.
#define NWOAS_CCS_STAB   0 // OFF for the CMDW decision-point diagnostic boot: measure the UNMODIFIED
                           // storm (usbxhci sees raw PORTSC) so CMDW snapshots aren't confounded by masking

// D41: the FL1100 can post to a sub-4GB event ring but not Windows's high ring.
// Advertise it as a 32-bit DMA controller so usbxhci asks the Windows DMA
// adapter for low common/transfer buffers.  This covers every xHCI DMA object,
// whereas copying only the event ring cannot cover later data transfers.
#define NWOAS_FORCE_FL32 1

/* S113 controlled experiment; production/default S103 behavior remains unchanged.
 * PCIe Device Control bit 11 enables No Snoop. Clear only that bit before
 * kernel FL1100 doorbells, verify readback, and retain all other DMA handling. */
#ifndef NWOAS_FL_SNOOP_TEST
#define NWOAS_FL_SNOOP_TEST 0
#endif

/* S114: isolate the legacy broad command/context cache sweep. Other ring
 * initialization and interrupt/event maintenance remain unchanged. */
#ifndef NWOAS_FL_SKIP_LEGACY_CLEAN
#define NWOAS_FL_SKIP_LEGACY_CLEAN 0
#endif

#define PAGE_SIZE       0x4000
#define CACHE_LINE_SIZE 64
#define CACHE_LINE_LOG2 6

#define PTE_ACCESS            BIT(10)
#define PTE_SH_NS             (0b11L << 8)
#define PTE_S2AP_RW           (0b11L << 6)
#define PTE_MEMATTR_UNCHANGED (0b1111L << 2)
// NWOAS §6-D: stage-2 (non-FEAT_S2FWB) MemAttr[3:0] directly encodes the memory type -- 0b1111 =
// Normal Inner/Outer Write-Back (== PTE_MEMATTR_UNCHANGED), 0b0101 = Normal Inner/Outer Non-cacheable
// (== Linux MT_S2_NORMAL_NC = 0x5). Valid here because HCR_EL2.FWB is NOT set (hv.c builds HCR_EL2
// without HCR_FWB); the existing 0b1111 map working as Write-Back independently confirms FWB=0.
#define PTE_MEMATTR_NORMAL_NC (0b0101L << 2)

#define PTE_ATTRIBUTES (PTE_ACCESS | PTE_SH_NS | PTE_S2AP_RW | PTE_MEMATTR_UNCHANGED)
// NWOAS §6-D: identical access/shareability/permission to PTE_ATTRIBUTES, memory type Normal
// Non-cacheable (mimics UEFI's Write-Combining allocation of the FL1100 control structures).
#define PTE_ATTRIBUTES_NC (PTE_ACCESS | PTE_SH_NS | PTE_S2AP_RW | PTE_MEMATTR_NORMAL_NC)

#define PTE_LOWER_ATTRIBUTES GENMASK(13, 2)

#define PTE_VALID BIT(0)
#define PTE_TYPE  BIT(1)
#define PTE_BLOCK 0
#define PTE_TABLE 1
#define PTE_PAGE  1

#define VADDR_L4_INDEX_BITS 12
#define VADDR_L3_INDEX_BITS 11
#define VADDR_L2_INDEX_BITS 11
#define VADDR_L1_INDEX_BITS 8

#define VADDR_L4_OFFSET_BITS 2
#define VADDR_L3_OFFSET_BITS 14
#define VADDR_L2_OFFSET_BITS 25
#define VADDR_L1_OFFSET_BITS 36

#define VADDR_L2_ALIGN_MASK GENMASK(VADDR_L2_OFFSET_BITS - 1, VADDR_L3_OFFSET_BITS)
#define VADDR_L3_ALIGN_MASK GENMASK(VADDR_L3_OFFSET_BITS - 1, VADDR_L4_OFFSET_BITS)
#define PTE_TARGET_MASK     GENMASK(49, VADDR_L3_OFFSET_BITS)
#define PTE_TARGET_MASK_L4  GENMASK(49, VADDR_L4_OFFSET_BITS)

#define ENTRIES_PER_L1_TABLE BIT(VADDR_L1_INDEX_BITS)
#define ENTRIES_PER_L2_TABLE BIT(VADDR_L2_INDEX_BITS)
#define ENTRIES_PER_L3_TABLE BIT(VADDR_L3_INDEX_BITS)
#define ENTRIES_PER_L4_TABLE BIT(VADDR_L4_INDEX_BITS)

#define SPTE_TRACE_READ    BIT(63)
#define SPTE_TRACE_WRITE   BIT(62)
#define SPTE_TRACE_UNBUF   BIT(61)
#define SPTE_TYPE          GENMASK(52, 50)
#define SPTE_MAP           0
#define SPTE_HOOK          1
#define SPTE_PROXY_HOOK_R  2
#define SPTE_PROXY_HOOK_W  3
#define SPTE_PROXY_HOOK_RW 4

#define IS_HW(pte) ((pte) && pte & PTE_VALID)
#define IS_SW(pte) ((pte) && !(pte & PTE_VALID))

#define L1_IS_TABLE(pte) ((pte) && FIELD_GET(PTE_TYPE, pte) == PTE_TABLE)

#define L2_IS_TABLE(pte)     ((pte) && FIELD_GET(PTE_TYPE, pte) == PTE_TABLE)
#define L2_IS_NOT_TABLE(pte) ((pte) && !L2_IS_TABLE(pte))
#define L2_IS_HW_BLOCK(pte)  (IS_HW(pte) && FIELD_GET(PTE_TYPE, pte) == PTE_BLOCK)
#define L2_IS_SW_BLOCK(pte)                                                                        \
    (IS_SW(pte) && FIELD_GET(PTE_TYPE, pte) == PTE_BLOCK && FIELD_GET(SPTE_TYPE, pte) == SPTE_MAP)
#define L3_IS_TABLE(pte)     (IS_SW(pte) && FIELD_GET(PTE_TYPE, pte) == PTE_TABLE)
#define L3_IS_NOT_TABLE(pte) ((pte) && !L3_IS_TABLE(pte))
#define L3_IS_HW_BLOCK(pte)  (IS_HW(pte) && FIELD_GET(PTE_TYPE, pte) == PTE_PAGE)
#define L3_IS_SW_BLOCK(pte)                                                                        \
    (IS_SW(pte) && FIELD_GET(PTE_TYPE, pte) == PTE_BLOCK && FIELD_GET(SPTE_TYPE, pte) == SPTE_MAP)

uint64_t vaddr_bits;

/*
 * We use 16KB page tables for stage 2 translation, and a 64GB (36-bit) guest
 * PA size, which results in the following virtual address space:
 *
 * [L2 index]  [L3 index] [page offset]
 *  11 bits     11 bits    14 bits
 *
 * 32MB L2 mappings look like this:
 * [L2 index]  [page offset]
 *  11 bits     25 bits
 *
 * We implement sub-page granularity mappings for software MMIO hooks, which behave
 * as an additional page table level used only by software. This works like this:
 *
 * [L2 index]  [L3 index] [L4 index]  [Word offset]
 *  11 bits     11 bits    12 bits     2 bits
 *
 * Thus, L4 sub-page tables are twice the size.
 *
 * We use invalid mappings (PTE_VALID == 0) to represent mmiotrace descriptors, but
 * otherwise the page table format is the same. The PTE_TYPE bit is weird, as 0 means
 * block but 1 means both table (at L<3) and page (at L3). For mmiotrace, this is
 * pushed to L4.
 *
 * On SoCs with more than 36-bit PA sizes there is an additional L1 translation level,
 * but no blocks or software mappings are allowed there. This level can have up to 8 bits
 * at this time.
 */

static u64 *hv_Ltop;

void hv_pt_init(void)
{
    const uint64_t pa_bits[] = {32, 36, 40, 42, 44, 48, 52};
    uint64_t pa_range = FIELD_GET(ID_AA64MMFR0_PARange, mrs(ID_AA64MMFR0_EL1));

    vaddr_bits = min(44, pa_bits[pa_range]);

    printf("HV: Initializing for %ld-bit PA range\n", vaddr_bits);

    hv_Ltop = memalign(PAGE_SIZE, sizeof(u64) * ENTRIES_PER_L2_TABLE);
    memset(hv_Ltop, 0, sizeof(u64) * ENTRIES_PER_L2_TABLE);

    u64 sl0 = vaddr_bits > 36 ? 2 : 1;

    msr(VTCR_EL2, FIELD_PREP(VTCR_PS, pa_range) |              // Full PA size
                      FIELD_PREP(VTCR_TG0, 2) |                // 16KB page size
                      FIELD_PREP(VTCR_SH0, 3) |                // PTWs Inner Sharable
                      FIELD_PREP(VTCR_ORGN0, 1) |              // PTWs Cacheable
                      FIELD_PREP(VTCR_IRGN0, 1) |              // PTWs Cacheable
                      FIELD_PREP(VTCR_SL0, sl0) |              // Start level
                      FIELD_PREP(VTCR_T0SZ, 64 - vaddr_bits)); // Translation region == PA

    msr(VTTBR_EL2, hv_Ltop);
}

static u64 *hv_pt_get_l2(u64 from)
{
    u64 l1idx = from >> VADDR_L1_OFFSET_BITS;

    if (vaddr_bits <= 36) {
        assert(l1idx == 0);
        return hv_Ltop;
    }

    u64 l1d = hv_Ltop[l1idx];

    if (L1_IS_TABLE(l1d))
        return (u64 *)(l1d & PTE_TARGET_MASK);

    u64 *l2 = (u64 *)memalign(PAGE_SIZE, ENTRIES_PER_L2_TABLE * sizeof(u64));
    memset64(l2, 0, ENTRIES_PER_L2_TABLE * sizeof(u64));

    l1d = ((u64)l2) | FIELD_PREP(PTE_TYPE, PTE_TABLE) | PTE_VALID;
    hv_Ltop[l1idx] = l1d;
    return l2;
}

static void hv_pt_free_l3(u64 *l3)
{
    if (!l3)
        return;

    for (u64 idx = 0; idx < ENTRIES_PER_L3_TABLE; idx++)
        if (IS_SW(l3[idx]) && FIELD_GET(PTE_TYPE, l3[idx]) == PTE_TABLE)
            free((void *)(l3[idx] & PTE_TARGET_MASK));
    free(l3);
}

static void hv_pt_map_l2(u64 from, u64 to, u64 size, u64 incr)
{
    assert((from & MASK(VADDR_L2_OFFSET_BITS)) == 0);
    assert(IS_SW(to) || (to & PTE_TARGET_MASK & MASK(VADDR_L2_OFFSET_BITS)) == 0);
    assert((size & MASK(VADDR_L2_OFFSET_BITS)) == 0);

    to |= FIELD_PREP(PTE_TYPE, PTE_BLOCK);

    for (; size; size -= BIT(VADDR_L2_OFFSET_BITS)) {
        u64 *l2 = hv_pt_get_l2(from);
        u64 idx = (from >> VADDR_L2_OFFSET_BITS) & MASK(VADDR_L2_INDEX_BITS);

        if (L2_IS_TABLE(l2[idx]))
            hv_pt_free_l3((u64 *)(l2[idx] & PTE_TARGET_MASK));

        l2[idx] = to;
        from += BIT(VADDR_L2_OFFSET_BITS);
        to += incr * BIT(VADDR_L2_OFFSET_BITS);
    }
}

static u64 *hv_pt_get_l3(u64 from)
{
    u64 *l2 = hv_pt_get_l2(from);
    u64 l2idx = (from >> VADDR_L2_OFFSET_BITS) & MASK(VADDR_L2_INDEX_BITS);
    u64 l2d = l2[l2idx];

    if (L2_IS_TABLE(l2d))
        return (u64 *)(l2d & PTE_TARGET_MASK);

    u64 *l3 = (u64 *)memalign(PAGE_SIZE, ENTRIES_PER_L3_TABLE * sizeof(u64));
    if (l2d) {
        u64 incr = 0;
        u64 l3d = l2d;
        if (IS_HW(l2d)) {
            l3d &= ~PTE_TYPE;
            l3d |= FIELD_PREP(PTE_TYPE, PTE_PAGE);
            incr = BIT(VADDR_L3_OFFSET_BITS);
        } else if (IS_SW(l2d) && FIELD_GET(SPTE_TYPE, l3d) == SPTE_MAP) {
            incr = BIT(VADDR_L3_OFFSET_BITS);
        }
        for (u64 idx = 0; idx < ENTRIES_PER_L3_TABLE; idx++, l3d += incr)
            l3[idx] = l3d;
    } else {
        memset64(l3, 0, ENTRIES_PER_L3_TABLE * sizeof(u64));
    }

    // NWOAS §6-D safety: publish the fully-populated L3 table BEFORE the L2 entry is made to point at
    // it. nwoas_remap_nc() reaches this path while the guest is live and SMP, so a sibling vCPU's HW
    // page-table walker may access this 32MB block natively (no trap, no fault -- the mapping is
    // preserved) concurrently. Under ARM's weak ordering, without this barrier that walker could
    // observe the new "L2 -> table" pointer while the table's L3 descriptors are still stale (memalign
    // returns recycled heap), installing a TLB entry for a bogus PA with NO fault and NO log = silent
    // physical-memory corruption. dsb ish orders the L3 stores before the L2 publish for the
    // inner-shareable walkers (VTCR SH0=3). Harmless (minor perf) on the setup-time / pre-SMP paths.
    sysop("dsb ish");

    l2d = ((u64)l3) | FIELD_PREP(PTE_TYPE, PTE_TABLE) | PTE_VALID;
    l2[l2idx] = l2d;
    return l3;
}

static void hv_pt_map_l3(u64 from, u64 to, u64 size, u64 incr)
{
    assert((from & MASK(VADDR_L3_OFFSET_BITS)) == 0);
    assert(IS_SW(to) || (to & PTE_TARGET_MASK & MASK(VADDR_L3_OFFSET_BITS)) == 0);
    assert((size & MASK(VADDR_L3_OFFSET_BITS)) == 0);

    if (IS_HW(to))
        to |= FIELD_PREP(PTE_TYPE, PTE_PAGE);
    else
        to |= FIELD_PREP(PTE_TYPE, PTE_BLOCK);

    for (; size; size -= BIT(VADDR_L3_OFFSET_BITS)) {
        u64 idx = (from >> VADDR_L3_OFFSET_BITS) & MASK(VADDR_L3_INDEX_BITS);
        u64 *l3 = hv_pt_get_l3(from);

        if (L3_IS_TABLE(l3[idx]))
            free((void *)(l3[idx] & PTE_TARGET_MASK));

        l3[idx] = to;
        from += BIT(VADDR_L3_OFFSET_BITS);
        to += incr * BIT(VADDR_L3_OFFSET_BITS);
    }
}

static u64 *hv_pt_get_l4(u64 from)
{
    u64 *l3 = hv_pt_get_l3(from);
    u64 l3idx = (from >> VADDR_L3_OFFSET_BITS) & MASK(VADDR_L3_INDEX_BITS);
    u64 l3d = l3[l3idx];

    if (L3_IS_TABLE(l3d)) {
        return (u64 *)(l3d & PTE_TARGET_MASK);
    }

    if (IS_HW(l3d)) {
        assert(FIELD_GET(PTE_TYPE, l3d) == PTE_PAGE);
        l3d &= PTE_TARGET_MASK;
        l3d |= FIELD_PREP(PTE_TYPE, PTE_BLOCK) | FIELD_PREP(SPTE_TYPE, SPTE_MAP);
    }

    u64 *l4 = (u64 *)memalign(PAGE_SIZE, ENTRIES_PER_L4_TABLE * sizeof(u64));
    if (l3d) {
        u64 incr = 0;
        u64 l4d = l3d;
        l4d &= ~PTE_TYPE;
        l4d |= FIELD_PREP(PTE_TYPE, PTE_PAGE);
        if (FIELD_GET(SPTE_TYPE, l4d) == SPTE_MAP)
            incr = BIT(VADDR_L4_OFFSET_BITS);
        for (u64 idx = 0; idx < ENTRIES_PER_L4_TABLE; idx++, l4d += incr)
            l4[idx] = l4d;
    } else {
        memset64(l4, 0, ENTRIES_PER_L4_TABLE * sizeof(u64));
    }

    l3d = ((u64)l4) | FIELD_PREP(PTE_TYPE, PTE_TABLE);
    l3[l3idx] = l3d;
    return l4;
}

static void hv_pt_map_l4(u64 from, u64 to, u64 size, u64 incr)
{
    assert((from & MASK(VADDR_L4_OFFSET_BITS)) == 0);
    assert((size & MASK(VADDR_L4_OFFSET_BITS)) == 0);

    assert(!IS_HW(to));

    if (IS_SW(to))
        to |= FIELD_PREP(PTE_TYPE, PTE_PAGE);

    for (; size; size -= BIT(VADDR_L4_OFFSET_BITS)) {
        u64 idx = (from >> VADDR_L4_OFFSET_BITS) & MASK(VADDR_L4_INDEX_BITS);
        u64 *l4 = hv_pt_get_l4(from);

        l4[idx] = to;
        from += BIT(VADDR_L4_OFFSET_BITS);
        to += incr * BIT(VADDR_L4_OFFSET_BITS);
    }
}

int hv_map(u64 from, u64 to, u64 size, u64 incr)
{
    u64 chunk;
    bool hw = IS_HW(to);

    if (from & MASK(VADDR_L4_OFFSET_BITS) || size & MASK(VADDR_L4_OFFSET_BITS))
        return -1;

    if (hw && (from & MASK(VADDR_L3_OFFSET_BITS) || size & MASK(VADDR_L3_OFFSET_BITS))) {
        printf("HV: cannot use L4 pages with HW mappings (0x%lx -> 0x%lx)\n", from, to);
        return -1;
    }

    // L4 mappings to boundary
    chunk = min(size, ALIGN_UP(from, BIT(VADDR_L3_OFFSET_BITS)) - from);
    if (chunk) {
        assert(!hw);
        hv_pt_map_l4(from, to, chunk, incr);
        from += chunk;
        to += incr * chunk;
        size -= chunk;
    }

    // L3 mappings to boundary
    u64 boundary = ALIGN_UP(from, MASK(VADDR_L2_OFFSET_BITS));
    // CPU CTRR doesn't like L2 mappings crossing CTRR boundaries!
    // Map everything below the m1n1 base as L3
    if (boundary >= ram_base && boundary < (u64)_base)
        boundary = ALIGN_UP((u64)_base, MASK(VADDR_L2_OFFSET_BITS));
    chunk = ALIGN_DOWN(min(size, boundary - from), BIT(VADDR_L3_OFFSET_BITS));
    if (chunk) {
        hv_pt_map_l3(from, to, chunk, incr);
        from += chunk;
        to += incr * chunk;
        size -= chunk;
    }

    // L2 mappings
    chunk = ALIGN_DOWN(size, BIT(VADDR_L2_OFFSET_BITS));
    if (chunk && (!hw || (to & VADDR_L2_ALIGN_MASK) == 0)) {
        hv_pt_map_l2(from, to, chunk, incr);
        from += chunk;
        to += incr * chunk;
        size -= chunk;
    }

    // L3 mappings to end
    chunk = ALIGN_DOWN(size, BIT(VADDR_L3_OFFSET_BITS));
    if (chunk) {
        hv_pt_map_l3(from, to, chunk, incr);
        from += chunk;
        to += incr * chunk;
        size -= chunk;
    }

    // L4 mappings to end
    if (size) {
        assert(!hw);
        hv_pt_map_l4(from, to, size, incr);
    }

    return 0;
}

int hv_unmap(u64 from, u64 size)
{
    return hv_map(from, 0, size, 0);
}

int hv_map_hw(u64 from, u64 to, u64 size)
{
    return hv_map(from, to | PTE_ATTRIBUTES | PTE_VALID, size, 1);
}

// NWOAS §6-D: like hv_map_hw, but installs the mapping as Normal Non-cacheable (mimics UEFI's WC).
// Plain, non-BBM installer for mappings not yet live in any TLB; the LIVE remap of an already-mapped
// guest page uses nwoas_remap_nc()'s break-before-make sequence instead.
int hv_map_hw_nc(u64 from, u64 to, u64 size)
{
    return hv_map(from, to | PTE_ATTRIBUTES_NC | PTE_VALID, size, 1);
}

int hv_map_sw(u64 from, u64 to, u64 size)
{
    return hv_map(from, to | FIELD_PREP(SPTE_TYPE, SPTE_MAP), size, 1);
}

int hv_map_hook(u64 from, hv_hook_t *hook, u64 size)
{
    return hv_map(from, ((u64)hook) | FIELD_PREP(SPTE_TYPE, SPTE_HOOK), size, 0);
}

u64 hv_translate(u64 addr, bool s1, bool w, u64 *par_out)
{
    if (!(mrs(SCTLR_EL12) & SCTLR_M))
        return addr; // MMU off

    u64 el = FIELD_GET(SPSR_M, hv_get_spsr()) >> 2;
    u64 save = mrs(PAR_EL1);

    if (w) {
        if (s1) {
            if (el == 0)
                asm("at s1e0w, %0" : : "r"(addr));
            else
                asm("at s1e1w, %0" : : "r"(addr));
        } else {
            if (el == 0)
                asm("at s12e0w, %0" : : "r"(addr));
            else
                asm("at s12e1w, %0" : : "r"(addr));
        }
    } else {
        if (s1) {
            if (el == 0)
                asm("at s1e0r, %0" : : "r"(addr));
            else
                asm("at s1e1r, %0" : : "r"(addr));
        } else {
            if (el == 0)
                asm("at s12e0r, %0" : : "r"(addr));
            else
                asm("at s12e1r, %0" : : "r"(addr));
        }
    }

    u64 par = mrs(PAR_EL1);
    if (par_out)
        *par_out = par;
    msr(PAR_EL1, save);

    if (par & PAR_F) {
        dprintf("hv_translate(0x%lx, %d, %d): fault 0x%lx\n", addr, s1, w, par);
        return 0; // fault
    } else {
        return (par & PAR_PA) | (addr & 0xfff);
    }
}

u64 hv_pt_walk(u64 addr)
{
    dprintf("hv_pt_walk(0x%lx)\n", addr);

    u64 idx = addr >> VADDR_L1_OFFSET_BITS;
    u64 *l2;
    if (vaddr_bits > 36) {
        assert(idx < ENTRIES_PER_L1_TABLE);

        u64 l1d = hv_Ltop[idx];

        dprintf("  l1d = 0x%lx\n", l1d);

        if (!L1_IS_TABLE(l1d)) {
            dprintf("  result: 0x%lx\n", l1d);
            return l1d;
        }
        l2 = (u64 *)(l1d & PTE_TARGET_MASK);
    } else {
        assert(idx == 0);
        l2 = hv_Ltop;
    }

    idx = (addr >> VADDR_L2_OFFSET_BITS) & MASK(VADDR_L2_INDEX_BITS);
    u64 l2d = l2[idx];
    dprintf("  l2d = 0x%lx\n", l2d);

    if (!L2_IS_TABLE(l2d)) {
        if (L2_IS_SW_BLOCK(l2d))
            l2d += addr & (VADDR_L2_ALIGN_MASK | VADDR_L3_ALIGN_MASK);
        if (L2_IS_HW_BLOCK(l2d)) {
            l2d &= ~PTE_LOWER_ATTRIBUTES;
            l2d |= addr & (VADDR_L2_ALIGN_MASK | VADDR_L3_ALIGN_MASK);
        }

        dprintf("  result: 0x%lx\n", l2d);
        return l2d;
    }

    idx = (addr >> VADDR_L3_OFFSET_BITS) & MASK(VADDR_L3_INDEX_BITS);
    u64 l3d = ((u64 *)(l2d & PTE_TARGET_MASK))[idx];
    dprintf("  l3d = 0x%lx\n", l3d);

    if (!L3_IS_TABLE(l3d)) {
        if (L3_IS_SW_BLOCK(l3d))
            l3d += addr & VADDR_L3_ALIGN_MASK;
        if (L3_IS_HW_BLOCK(l3d)) {
            l3d &= ~PTE_LOWER_ATTRIBUTES;
            l3d |= addr & VADDR_L3_ALIGN_MASK;
        }
        dprintf("  result: 0x%lx\n", l3d);
        return l3d;
    }

    idx = (addr >> VADDR_L4_OFFSET_BITS) & MASK(VADDR_L4_INDEX_BITS);
    dprintf("  l4 idx = 0x%lx\n", idx);
    u64 l4d = ((u64 *)(l3d & PTE_TARGET_MASK))[idx];
    dprintf("  l4d = 0x%lx\n", l4d);
    return l4d;
}

#define CHECK_RN                                                                                   \
    if (Rn == 31)                                                                                  \
    return false
#define DECODE_OK                                                                                  \
    if (!val)                                                                                      \
    return true

#define EXT(n, b) (((s32)(((u32)(n)) << (32 - (b)))) >> (32 - (b)))

union simd_reg {
    u64 d[2];
    u32 s[4];
    u16 h[8];
    u8 b[16];
};

static bool emulate_load(struct exc_info *ctx, u32 insn, u64 *val, u64 *width, u64 *vaddr)
{
    u64 Rt = insn & 0x1f;
    u64 Rn = (insn >> 5) & 0x1f;
    u64 uimm12 = (insn >> 10) & 0xfff;
    u64 imm9 = EXT((insn >> 12) & 0x1ff, 9);
    u64 imm7 = EXT((insn >> 15) & 0x7f, 7);
    u64 *regs = ctx->regs;

    union simd_reg simd[32];

    *width = insn >> 30;

    if (val)
        dprintf("emulate_load(%p, 0x%08x, 0x%08lx, %ld\n", regs, insn, *val, *width);

    if ((insn & 0x3fe00400) == 0x38400400) {
        // LDRx (immediate) Pre/Post-index
        CHECK_RN;
        DECODE_OK;
        regs[Rn] += imm9;
        regs[Rt] = *val;
    } else if ((insn & 0x3fc00000) == 0x39400000) {
        // LDRx (immediate) Unsigned offset
        DECODE_OK;
        regs[Rt] = *val;
    } else if ((insn & 0x3fa00400) == 0x38800400) {
        // LDRSx (immediate) Pre/Post-index
        CHECK_RN;
        DECODE_OK;
        regs[Rn] += imm9;
        regs[Rt] = (s64)EXT(*val, 8 << *width);
        if (insn & (1 << 22))
            regs[Rt] &= 0xffffffff;
    } else if ((insn & 0x3fa00000) == 0x39800000) {
        // LDRSx (immediate) Unsigned offset
        DECODE_OK;
        regs[Rt] = (s64)EXT(*val, 8 << *width);
        if (insn & (1 << 22))
            regs[Rt] &= 0xffffffff;
    } else if ((insn & 0x3fc00000) == 0x39c00000) {
        // LDRSB (w register)
        DECODE_OK;
        regs[Rt] = (s64)EXT(*val, 8 << *width);
        regs[Rt] &= 0xffffffff;
    } else if ((insn & 0x3fe04c00) == 0x38604800) {
        // LDRx (register)
        DECODE_OK;
        regs[Rt] = *val;
    } else if ((insn & 0x3fa04c00) == 0x38a04800) {
        // LDRSx (register)
        DECODE_OK;
        regs[Rt] = (s64)EXT(*val, 8 << *width);
        if (insn & (1 << 22))
            regs[Rt] &= 0xffffffff;
    } else if ((insn & 0x3fe00c00) == 0x38400000) {
        // LDURx (unscaled)
        DECODE_OK;
        regs[Rt] = *val;
    } else if ((insn & 0x3fa00c00) == 0x38a00000) {
        // LDURSx (unscaled)
        DECODE_OK;
        regs[Rt] = (s64)EXT(*val, (8 << *width));
        if (insn & (1 << 22))
            regs[Rt] &= 0xffffffff;
    } else if ((insn & 0xfec00000) == 0x28400000) {
        // LD[N]P (Signed offset, 32-bit)
        *width = 3;
        *vaddr = regs[Rn] + (imm7 * 4);
        DECODE_OK;
        u64 Rt2 = (insn >> 10) & 0x1f;
        regs[Rt] = val[0] & 0xffffffff;
        regs[Rt2] = val[0] >> 32;
    } else if ((insn & 0xfec00000) == 0xa8400000) {
        // LD[N]P (Signed offset, 64-bit)
        *width = 4;
        *vaddr = regs[Rn] + (imm7 * 8);
        DECODE_OK;
        u64 Rt2 = (insn >> 10) & 0x1f;
        regs[Rt] = val[0];
        regs[Rt2] = val[1];
    } else if ((insn & 0xfec00000) == 0xa8c00000) {
        // LDP (pre/post-increment, 64-bit)
        *width = 4;
        *vaddr = regs[Rn] + ((insn & BIT(24)) ? (imm7 * 8) : 0);
        DECODE_OK;
        regs[Rn] += imm7 * 8;
        u64 Rt2 = (insn >> 10) & 0x1f;
        regs[Rt] = val[0];
        regs[Rt2] = val[1];
    } else if ((insn & 0xfec00000) == 0xac400000) {
        // LD[N]P (SIMD&FP, 128-bit) Signed offset
        *width = 5;
        *vaddr = regs[Rn] + (imm7 * 16);
        DECODE_OK;
        u64 Rt2 = (insn >> 10) & 0x1f;
        get_simd_state(simd);
        simd[Rt].d[0] = val[0];
        simd[Rt].d[1] = val[1];
        simd[Rt2].d[0] = val[2];
        simd[Rt2].d[1] = val[3];
        put_simd_state(simd);
    } else if ((insn & 0x3fc00000) == 0x3d400000) {
        // LDR (immediate, SIMD&FP) Unsigned offset
        *vaddr = regs[Rn] + (uimm12 << *width);
        DECODE_OK;
        get_simd_state(simd);
        simd[Rt].d[0] = val[0];
        simd[Rt].d[1] = 0;
        put_simd_state(simd);
    } else if ((insn & 0x3fe00c00) == 0x3c400000) {
        // LDURx (unscaled, SIMD&FP)
        *vaddr = regs[Rn] + imm9;
        DECODE_OK;
        get_simd_state(simd);
        simd[Rt].d[0] = val[0];
        simd[Rt].d[1] = val[1];
        put_simd_state(simd);
    } else if ((insn & 0xffc00000) == 0x3dc00000) {
        // LDR (immediate, SIMD&FP) Unsigned offset, 128-bit
        *width = 4;
        *vaddr = regs[Rn] + (uimm12 << *width);
        DECODE_OK;
        get_simd_state(simd);
        simd[Rt].d[0] = val[0];
        simd[Rt].d[1] = val[1];
        put_simd_state(simd);
    } else if ((insn & 0xffe00c00) == 0x3cc00000) {
        // LDURx (unscaled, SIMD&FP, 128-bit)
        *width = 4;
        *vaddr = regs[Rn] + imm9;
        DECODE_OK;
        get_simd_state(simd);
        simd[Rt].d[0] = val[0];
        simd[Rt].d[1] = val[1];
        put_simd_state(simd);
    } else if ((insn & 0x3fe00400) == 0x3c400400) {
        // LDR (immediate, SIMD&FP) Pre/Post-index
        CHECK_RN;
        DECODE_OK;
        regs[Rn] += imm9;
        get_simd_state(simd);
        simd[Rt].d[0] = val[0];
        simd[Rt].d[1] = 0;
        put_simd_state(simd);
    } else if ((insn & 0xffe00400) == 0x3cc00400) {
        // LDR (immediate, SIMD&FP) Pre/Post-index, 128-bit
        *width = 4;
        CHECK_RN;
        DECODE_OK;
        regs[Rn] += imm9;
        get_simd_state(simd);
        simd[Rt].d[0] = val[0];
        simd[Rt].d[1] = val[1];
        put_simd_state(simd);
    } else if ((insn & 0x3fe04c00) == 0x3c604800) {
        // LDR (register, SIMD&FP)
        DECODE_OK;
        get_simd_state(simd);
        simd[Rt].d[0] = val[0];
        simd[Rt].d[1] = 0;
        put_simd_state(simd);
    } else if ((insn & 0xffe04c00) == 0x3ce04800) {
        // LDR (register, SIMD&FP), 128-bit
        *width = 4;
        DECODE_OK;
        get_simd_state(simd);
        simd[Rt].d[0] = val[0];
        simd[Rt].d[1] = val[1];
        put_simd_state(simd);
    } else if ((insn & 0xbffffc00) == 0x0d408400) {
        // LD1 (single structure) No offset, 64-bit
        *width = 3;
        DECODE_OK;
        u64 index = (insn >> 30) & 1;
        get_simd_state(simd);
        simd[Rt].d[index] = val[0];
        put_simd_state(simd);
    } else if ((insn & 0x3ffffc00) == 0x08dffc00) {
        // LDAR*
        DECODE_OK;
        regs[Rt] = *val;
    } else {
        return false;
    }
    return true;
}

static bool emulate_store(struct exc_info *ctx, u32 insn, u64 *val, u64 *width, u64 *vaddr)
{
    u64 Rt = insn & 0x1f;
    u64 Rn = (insn >> 5) & 0x1f;
    u64 imm9 = EXT((insn >> 12) & 0x1ff, 9);
    u64 imm7 = EXT((insn >> 15) & 0x7f, 7);
    u64 *regs = ctx->regs;

    union simd_reg simd[32];

    *width = insn >> 30;

    dprintf("emulate_store(%p, 0x%08x, ..., %ld) = ", regs, insn, *width);

    regs[31] = 0;

    u64 mask = 0xffffffffffffffffUL;

    if (*width < 3)
        mask = (1UL << (8 << *width)) - 1;

    if ((insn & 0x3fe00400) == 0x38000400) {
        // STRx (immediate) Pre/Post-index
        CHECK_RN;
        regs[Rn] += imm9;
        *val = regs[Rt] & mask;
    } else if ((insn & 0x3fc00000) == 0x39000000) {
        // STRx (immediate) Unsigned offset
        *val = regs[Rt] & mask;
    } else if ((insn & 0x3fe04c00) == 0x38204800) {
        // STRx (register)
        *val = regs[Rt] & mask;
    } else if ((insn & 0xfec00000) == 0x28000000) {
        // ST[N]P (Signed offset, 32-bit)
        *vaddr = regs[Rn] + (imm7 * 4);
        u64 Rt2 = (insn >> 10) & 0x1f;
        val[0] = (regs[Rt] & 0xffffffff) | (regs[Rt2] << 32);
        *width = 3;
    } else if ((insn & 0xfec00000) == 0xa8000000) {
        // ST[N]P (Signed offset, 64-bit)
        *vaddr = regs[Rn] + (imm7 * 8);
        u64 Rt2 = (insn >> 10) & 0x1f;
        val[0] = regs[Rt];
        val[1] = regs[Rt2];
        *width = 4;
    } else if ((insn & 0xfec00000) == 0xa8800000) {
        // ST[N]P (immediate, 64-bit, pre/post-index)
        CHECK_RN;
        *vaddr = regs[Rn] + ((insn & BIT(24)) ? (imm7 * 8) : 0);
        regs[Rn] += (imm7 * 8);
        u64 Rt2 = (insn >> 10) & 0x1f;
        val[0] = regs[Rt];
        val[1] = regs[Rt2];
        *width = 4;
    } else if ((insn & 0x3fc00000) == 0x3d000000) {
        // STR (immediate, SIMD&FP) Unsigned offset, 8..64-bit
        get_simd_state(simd);
        *val = simd[Rt].d[0];
    } else if ((insn & 0x3fe04c00) == 0x3c204800) {
        // STR (register, SIMD&FP) 8..64-bit
        get_simd_state(simd);
        *val = simd[Rt].d[0];
    } else if ((insn & 0xffe04c00) == 0x3ca04800) {
        // STR (register, SIMD&FP) 128-bit
        get_simd_state(simd);
        val[0] = simd[Rt].d[0];
        val[1] = simd[Rt].d[1];
        *width = 4;
    } else if ((insn & 0xffc00000) == 0x3d800000) {
        // STR (immediate, SIMD&FP) Unsigned offset, 128-bit
        get_simd_state(simd);
        val[0] = simd[Rt].d[0];
        val[1] = simd[Rt].d[1];
        *width = 4;
    } else if ((insn & 0xffe00000) == 0xbc000000) {
        // STUR (immediate, SIMD&FP) 32-bit
        get_simd_state(simd);
        val[0] = simd[Rt].s[0];
        *width = 2;
    } else if ((insn & 0xffe00000) == 0xfc000000) {
        // STUR (immediate, SIMD&FP) 64-bit
        get_simd_state(simd);
        val[0] = simd[Rt].d[0];
        *width = 3;
    } else if ((insn & 0xffe00000) == 0x3c800000) {
        // STUR (immediate, SIMD&FP) 128-bit
        get_simd_state(simd);
        val[0] = simd[Rt].d[0];
        val[1] = simd[Rt].d[1];
        *width = 4;
    } else if ((insn & 0xffc00000) == 0x2d000000) {
        // STP (SIMD&FP, 128-bit) Signed offset
        *vaddr = regs[Rn] + (imm7 * 4);
        u64 Rt2 = (insn >> 10) & 0x1f;
        get_simd_state(simd);
        val[0] = simd[Rt].s[0] | (((u64)simd[Rt2].s[0]) << 32);
        *width = 3;
    } else if ((insn & 0xffc00000) == 0xad000000) {
        // STP (SIMD&FP, 128-bit) Signed offset
        *vaddr = regs[Rn] + (imm7 * 16);
        u64 Rt2 = (insn >> 10) & 0x1f;
        get_simd_state(simd);
        val[0] = simd[Rt].d[0];
        val[1] = simd[Rt].d[1];
        val[2] = simd[Rt2].d[0];
        val[3] = simd[Rt2].d[1];
        *width = 5;
    } else if ((insn & 0x3fe00c00) == 0x38000000) {
        // STURx (unscaled)
        *val = regs[Rt] & mask;
    } else if ((insn & 0xffffffe0) == 0xd50b7420) {
        // DC ZVA
        *vaddr = regs[Rt];
        memset(val, 0, CACHE_LINE_SIZE);
        *width = CACHE_LINE_LOG2;
    } else if ((insn & 0x3ffffc00) == 0x089ffc00) {
        // STL  qR*
        *val = regs[Rt] & mask;
    } else {
        return false;
    }

    dprintf("0x%lx\n", *width);

    return true;
}

static void emit_mmiotrace(u64 pc, u64 addr, u64 *data, u64 width, u64 flags, bool sync)
{
    struct hv_evt_mmiotrace evt = {
        .flags = flags | FIELD_PREP(MMIO_EVT_CPU, smp_id()),
        .pc = pc,
        .addr = addr,
    };

    if (width > 3)
        evt.flags |= FIELD_PREP(MMIO_EVT_WIDTH, 3) | MMIO_EVT_MULTI;
    else
        evt.flags |= FIELD_PREP(MMIO_EVT_WIDTH, width);

    for (int i = 0; i < (1 << width); i += 8) {
        evt.data = *data++;
        hv_wdt_suspend();
        uartproxy_send_event(EVT_MMIOTRACE, &evt, sizeof(evt));
        if (sync) {
            iodev_flush(uartproxy_iodev);
        }
        hv_wdt_resume();
        evt.addr += 8;
    }
}

bool hv_pa_write(struct exc_info *ctx, u64 addr, u64 *val, int width)
{
    sysop("dsb sy");
    exc_count = 0;
    exc_guard = GUARD_SKIP;
    switch (width) {
        case 0:
            write8(addr, val[0]);
            break;
        case 1:
            write16(addr, val[0]);
            break;
        case 2:
            write32(addr, val[0]);
            break;
        case 3:
            write64(addr, val[0]);
            break;
        case 4:
        case 5:
        case 6:
            for (u64 i = 0; i < (1UL << (width - 3)); i++)
                write64(addr + 8 * i, val[i]);
            break;
        default:
            dprintf("HV: unsupported write width %d\n", width);
            exc_guard = GUARD_OFF;
            return false;
    }
    // Make sure we catch SErrors here
    sysop("dsb sy");
    sysop("isb");
    exc_guard = GUARD_OFF;
    if (exc_count) {
        printf("HV: Exception during write to 0x%lx (width: %d)\n", addr, width);
        // Update exception info with "real" cause
        ctx->esr = hv_get_esr();
        ctx->far = hv_get_far();
        return false;
    }
    return true;
}

bool hv_pa_read(struct exc_info *ctx, u64 addr, u64 *val, int width)
{
    sysop("dsb sy");
    exc_count = 0;
    exc_guard = GUARD_SKIP;
    switch (width) {
        case 0:
            val[0] = read8(addr);
            break;
        case 1:
            val[0] = read16(addr);
            break;
        case 2:
            val[0] = read32(addr);
            break;
        case 3:
            val[0] = read64(addr);
            break;
        case 4:
            val[0] = read64(addr);
            val[1] = read64(addr + 8);
            break;
        case 5:
            val[0] = read64(addr);
            val[1] = read64(addr + 8);
            val[2] = read64(addr + 16);
            val[3] = read64(addr + 24);
            break;
        default:
            dprintf("HV: unsupported read width %d\n", width);
            exc_guard = GUARD_OFF;
            return false;
    }
    sysop("dsb sy");
    exc_guard = GUARD_OFF;
    if (exc_count) {
        dprintf("HV: Exception during read from 0x%lx (width: %d)\n", addr, width);
        // Update exception info with "real" cause
        ctx->esr = hv_get_esr();
        ctx->far = hv_get_far();
        return false;
    }
    return true;
}

bool hv_pa_rw(struct exc_info *ctx, u64 addr, u64 *val, bool write, int width)
{
    if (write)
        return hv_pa_write(ctx, addr, val, width);
    else
        return hv_pa_read(ctx, addr, val, width);
}

/*
 * NWOAS §6-A: FL1100 xHCI event-ring reachability-vs-coherence probe.
 *
 * The stage-8 wall is that FL1100's DMA "completion" events never become
 * visible to the guest usbxhci driver, which then livelocks re-resetting every
 * port. The event ring lives in high DRAM (~0xae0f_c000, ~43.5GB). Two rival
 * theories: (a) FL1100 cannot DMA-reach that high address through its Apple
 * DART (reachability), or (b) FL1100 writes it fine but the guest reads a stale
 * cached copy (coherence). The Python proxy CANNOT read that high DRAM (returns
 * sentinel 0xabad1dea), but EL2 can: memory.c maps all host DRAM (ram_base ..
 * +mem_size_actual) as NORMAL cacheable identity. The ring pointers the guest
 * programs are guest-IPAs, which are NOT always identity to host PA — the
 * harness's low window (map_hw(0,backing,4GB)) backs guest IPA [0,4GB) elsewhere
 * and the guest puts the ring there on some boots — so we stage-2-translate each
 * pointer (nwoas_ipa_to_pa) to its host PA, then read each ring TRB TWICE: once as-is
 * (the guest's cached view), then after a `dc ivac` (invalidate-ONLY, never
 * clean -> never writes stale bytes back over FL1100's DMA) which drops the
 * stale CPU line so the re-read returns what FL1100 actually DMA-wrote. A
 * cached-vs-DRAM divergence is a direct proof of coherence:
 *     DRAM ring all-zero       -> FL1100 never landed the write -> REACHABILITY
 *     cached != DRAM TRB        -> FL1100 wrote, guest reads stale -> COHERENCE
 *     garbage TRB types         -> wrong translation / wrong memory
 * This is a diagnostic, not a fix. It issues no MMIO / doorbell / ERDP writes,
 * so it cannot change the FL1100/usbxhci protocol state. It is NOT side-effect
 * free, though: the ERST `civac` cleans the guest's own line back to DRAM, and
 * the ring `dc ivac` drops the guest's cached ring line (forcing it to re-fetch
 * DRAM on its next read) — a benign, throttled observer effect on cache state.
 */
u64 nwoas_fl_erstba = 0; // event ring segment table base (rt+0x30), guest-programmed (non-static: the
                         // hv_exc.c SPI-698 bridge gates delivery on this so it never fires 698 before
                         // Windows usbxhci has set up its event ring + ISR = the early-boot freeze fix)
static u64 nwoas_fl_dcbaap = 0; // device context base array ptr (op+0x30), for context
static u64 nwoas_fl_crcr = 0;   // command ring control (op+0x18), for the DART fixup below
static bool nwoas_hid_watch_ready = false;
static u32 nwoas_hid_baseline_ep[32] = {0};
#define NWOAS_HID_EXTENDED_OBSERVER 0
#if NWOAS_HID_EXTENDED_OBSERVER
/* S6-D62 staged observer: remember the bounded interrupt-IN report buffers
 * that Windows queued during the D58 late dump.  On the first controller
 * completion, invalidate and print only those buffers; never write them. */
#define NWOAS_HID_REPORTS_PER_EP 4
static u64 nwoas_hid_report_ipa[32][NWOAS_HID_REPORTS_PER_EP] = {{0}};
static u32 nwoas_hid_report_len[32][NWOAS_HID_REPORTS_PER_EP] = {{0}};
static u32 nwoas_hid_report_count[32] = {0};
/* S6-D61 staged observer: retain compact 16x16-pixel tile hashes from the
 * stable Setup surface.  A post-HID comparison can then distinguish a local
 * cursor-sized update from an unrelated full-screen redraw without storing a
 * second 3.5 MiB framebuffer. */
#define NWOAS_HID_FB_WIDTH 1280
#define NWOAS_HID_FB_HEIGHT 720
#define NWOAS_HID_FB_STRIDE 5120
#define NWOAS_HID_FB_TILE 16
#define NWOAS_HID_FB_TILE_COLS (NWOAS_HID_FB_WIDTH / NWOAS_HID_FB_TILE)
#define NWOAS_HID_FB_TILE_ROWS (NWOAS_HID_FB_HEIGHT / NWOAS_HID_FB_TILE)
#define NWOAS_HID_FB_TILE_COUNT (NWOAS_HID_FB_TILE_COLS * NWOAS_HID_FB_TILE_ROWS)
static bool nwoas_hid_fb_baseline_ready = false;
static u64 nwoas_hid_fb_baseline_tiles[NWOAS_HID_FB_TILE_COUNT];
static u64 nwoas_hid_fb_sample_tiles[NWOAS_HID_FB_TILE_COUNT];
#endif
// D48 observational latch: the first non-base ERDP value means Windows consumed
// at least one 16-byte event TRB.  Defer the heavy ring dump until the next
// USBSTS read so the ERDP MMIO write/EOI path itself stays short.
static bool nwoas_postconsume_pending = false;
static bool nwoas_postconsume_dumped = false;
// Session4 precise 698 routing: the physical CPU that most recently touched the FL1100 BAR from the
// Windows kernel = the core running usbxhci. hv_exc.c's SPI-698 bridge delivers ONLY on this core so 698
// reaches usbxhci without the all-core delivery that stalled early boot (and without pinning to a
// parked/__fastfail icpu). -1 until the kernel first touches the BAR (bridge falls back to icpu then).
int nwoas_fl_cpu = -1;

extern uint64_t ram_base; // base of EL2's NORMAL-cacheable DRAM identity map (memory.c:448);
                          // its length mem_size_actual is declared in utils.h.

// [addr,addr+len) fully inside EL2's DRAM identity map? A cache op or read outside it faults, and
// with exc_guard off a fault is flush_and_reboot() (exception.c) — so bounds-check before touching
// any latched/derived pointer (a partial 32-bit latch or a garbage ERST base can be out of range).
static bool nwoas_in_dram(u64 addr, size_t len)
{
    return len && addr >= ram_base && (addr + len) > addr && (addr + len) <= (ram_base + mem_size_actual);
}

// Translate a guest-physical (IPA) ring pointer to the host PA via the stage-2
// tables. CRITICAL: the ring is NOT always identity-mapped. The test harness
// installs a low window (map_hw(0, backing, 4GB)) so guest IPA [0,4GB) is backed
// by high phys [backing, backing+4GB); the guest sometimes puts the ring there
// (e.g. erstba=0x103140) and sometimes in identity-mapped high RAM (0xae0f_c000)
// — it is flaky per boot. Assuming identity read the wrong DRAM (a real bug the
// first hardware run exposed: erstba=0x103140 flagged OUT-OF-DRAM). hv_pt_walk
// reads whatever mapping the module actually installed, so this is correct for
// both the window and the identity case. Returns 0 if unmapped.
static u64 nwoas_ipa_to_pa(u64 ipa)
{
    if (ipa >= BIT(vaddr_bits))
        return 0;
    u64 pte = hv_pt_walk(ipa & ~MASK(VADDR_L3_OFFSET_BITS));
    if (!pte)
        return 0;
    // Our RAM/window mappings are L2/L3 blocks; PA target is PTE_TARGET_MASK bits,
    // page offset is the low VADDR_L3_OFFSET_BITS of the IPA.
    return (pte & PTE_TARGET_MASK) | (ipa & MASK(VADDR_L3_OFFSET_BITS));
}

/* S149: target-side fast path for the synthetic NVMe I/O queue.
 *
 * The controller's admin queue remains in the Python model. Once Windows has
 * created I/O SQ/CQ 1, Python supplies their guest-physical bases and depths
 * through P_HV_NWOAS_NVME_FASTPATH. The two I/O doorbells are then completed
 * here while bhl is held by the EL2 abort path. This removes the per-MMIO
 * rendezvous + USB CDC + Python round trip which dominated every storage I/O
 * and eventually tripped Windows' DPC watchdog.
 *
 * Only NVM read, write and flush are accepted. nvme_rw_guest() retains the
 * independently enforced WINTEST write boundary and guest-PRP validation.
 */
#define NWOAS_NVME_BAR             0x700100000UL
#define NWOAS_NVME_BAR_SIZE        0x4000UL
#define NWOAS_NVME_NAMESPACE_LBAS  61279344UL
#define NWOAS_NVME_WRITE_FIRST_LBA 53839104UL
#define NWOAS_NVME_WRITE_LAST_LBA  59968629UL
#define NWOAS_NVME_MAX_IO_DEPTH    256

#define NWOAS_NVME_SC_SUCCESS        0x000
#define NWOAS_NVME_SC_INVALID_OPCODE 0x001
#define NWOAS_NVME_SC_INVALID_FIELD  0x002
#define NWOAS_NVME_SC_INVALID_NSID   0x00b
#define NWOAS_NVME_SC_LBA_RANGE      0x080
#define NWOAS_NVME_SC_WRITE_RO       0x182
#define NWOAS_NVME_SC_WRITE_ERROR    0x280
#define NWOAS_NVME_SC_READ_ERROR     0x281

struct nwoas_nvme_guest_cmd {
    u8 opcode;
    u8 flags;
    u16 cid;
    u32 nsid;
    u32 cdw2;
    u32 cdw3;
    u64 metadata;
    u64 prp1;
    u64 prp2;
    u32 cdw10;
    u32 cdw11;
    u32 cdw12;
    u32 cdw13;
    u32 cdw14;
    u32 cdw15;
};

struct nwoas_nvme_guest_cqe {
    u32 result;
    u32 reserved;
    u16 sq_head;
    u16 sq_id;
    u16 cid;
    u16 status;
};

static_assert(sizeof(struct nwoas_nvme_guest_cmd) == 64, "invalid guest NVMe command size");
static_assert(sizeof(struct nwoas_nvme_guest_cqe) == 16, "invalid guest NVMe CQE size");

static struct {
    bool armed;
    bool faulted;
    bool deferred;
    bool irq_enabled;
    bool cache_enabled;
    bool link_busy;
    bool local_mask_allowed; /* S160: no pending Python-owned interrupt source. */
    u32 mask;
    u64 sq_base;
    u64 cq_base;
    u16 sq_size;
    u16 cq_size;
    u16 sq_head;
    u16 sq_tail;
    u16 cq_head;
    u16 cq_tail;
    u16 cq_pending;
    u8 cq_phase;
    u32 commands;
    u32 errors;
    u32 link_sequence;
    u32 lifecycle_epoch;
    u16 sq_generation;
    u16 cq_generation;
} nwoas_nvme_fp;

/* S158: cumulative, boot-local measurements. No periodic console traffic and
 * no scheduling change. Keep NS2 host-link latency separate from physical I/O. */
static struct {
    u64 max_ns1_ticks;
    u64 max_ns2_ticks;
    u64 in_flight_since;
    u32 in_flight_nsid;
    u32 ns1_calls;
    u32 ns2_calls;
    u32 max_sq_backlog;
    u32 cq_ack_writes;
    u32 local_mask_reads;
    u32 local_mask_writes;
} nwoas_nvme_diag;

/* S162: one bounded, read-only attribute observation of the real Windows CQ.
 * x21 is the CQ context in the verified 26100 stornvme completion routine.
 * Do not trust that register until its pointer translates into RAM and the
 * first qword resolves to the exact armed CQ IPA. No memory/cache/MMIO writes.
 * Record in RAM; print only on an explicit diagnostic/bugcheck. */
static struct {
    u64 va, ipa, par_s1, par_s12, s2, context, pc;
    u32 attempts;
    bool found;
} nwoas_nvme_cq_map;

static u64 nwoas_probe_at_el1(u64 va, bool combined, u64 *par)
{
    u64 saved = mrs(PAR_EL1);
    if (combined)
        asm volatile("at s12e1r, %0\n\tisb" : : "r"(va) : "memory");
    else
        asm volatile("at s1e1r, %0\n\tisb" : : "r"(va) : "memory");
    *par = mrs(PAR_EL1);
    msr(PAR_EL1, saved);
    return (*par & PAR_F) ? 0 : ((*par & PAR_PA) | (va & 0xfff));
}

static void nwoas_nvme_probe_cq_map(struct exc_info *ctx)
{
    if (nwoas_nvme_cq_map.found || nwoas_nvme_cq_map.attempts >= 64 ||
        ctx->elr < 0xffff000000000000UL || ctx->regs[21] < 0xffff000000000000UL)
        return;
    nwoas_nvme_cq_map.attempts++;
    u64 par, context = ctx->regs[21];
    u64 pa = nwoas_probe_at_el1(context, true, &par);
    if (!pa || (pa & 7) || !nwoas_in_dram(pa, 8))
        return;
    u64 va = read64(pa);
    if (va < 0xffff000000000000UL)
        return;
    u64 s1, s12;
    u64 ipa = nwoas_probe_at_el1(va, false, &s1);
    if (ipa != nwoas_nvme_fp.cq_base)
        return;
    u64 cq_pa = nwoas_probe_at_el1(va, true, &s12);
    if (!cq_pa || !nwoas_in_dram(cq_pa, 16) ||
        cq_pa != nwoas_ipa_to_pa(nwoas_nvme_fp.cq_base))
        return;
    nwoas_nvme_cq_map.va = va;
    nwoas_nvme_cq_map.ipa = ipa;
    nwoas_nvme_cq_map.par_s1 = s1;
    nwoas_nvme_cq_map.par_s12 = s12;
    nwoas_nvme_cq_map.s2 = hv_pt_walk(ipa);
    nwoas_nvme_cq_map.context = context;
    nwoas_nvme_cq_map.pc = ctx->elr;
    nwoas_nvme_cq_map.found = true;
}

extern void nwoas_nvme_dump_irq(void);

static void nwoas_nvme_dump_cq_map(void)
{
    nwoas_nvme_dump_irq();
    printf("HVLOG: S162 CQMAP found=%u attempts=%u va=%lx ipa=%lx "
           "s1=%lx s12=%lx s2=%lx ctx=%lx pc=%lx\n",
           nwoas_nvme_cq_map.found, nwoas_nvme_cq_map.attempts,
           nwoas_nvme_cq_map.va, nwoas_nvme_cq_map.ipa,
           nwoas_nvme_cq_map.par_s1, nwoas_nvme_cq_map.par_s12,
           nwoas_nvme_cq_map.s2, nwoas_nvme_cq_map.context, nwoas_nvme_cq_map.pc);
}

void nwoas_nvme_dump_latency(void)
{
    nwoas_nvme_dump_cq_map();
    printf("HVLOG: S158 NVME latency freq=%lu ns1_max=%lu ns2_max=%lu "
           "ns1_calls=%u ns2_calls=%u backlog_max=%u cq_acks=%u\n",
           mrs(CNTFRQ_EL0), nwoas_nvme_diag.max_ns1_ticks,
           nwoas_nvme_diag.max_ns2_ticks, nwoas_nvme_diag.ns1_calls,
           nwoas_nvme_diag.ns2_calls, nwoas_nvme_diag.max_sq_backlog,
           nwoas_nvme_diag.cq_ack_writes);
    printf("HVLOG: S158 NVME in_flight_nsid=%u elapsed=%lu sq=%u/%u cq_pending=%u\n",
           nwoas_nvme_diag.in_flight_nsid,
           nwoas_nvme_diag.in_flight_since ?
               mrs(CNTPCT_EL0) - nwoas_nvme_diag.in_flight_since : 0,
           nwoas_nvme_fp.sq_head, nwoas_nvme_fp.sq_tail, nwoas_nvme_fp.cq_pending);
    printf("HVLOG: S160 NVME mask local_reads=%u local_writes=%u mask=0x%x eligible=%u\n",
           nwoas_nvme_diag.local_mask_reads, nwoas_nvme_diag.local_mask_writes,
           nwoas_nvme_fp.mask, nwoas_nvme_fp.local_mask_allowed);
}

extern void nwoas_nvme_set_fast_irq(u64 level);

static void nwoas_nvme_fastpath_update_irq(void)
{
    bool level = nwoas_nvme_fp.armed && !nwoas_nvme_fp.faulted && nwoas_nvme_fp.irq_enabled &&
                 !(nwoas_nvme_fp.mask & 1) && nwoas_nvme_fp.cq_pending;
    nwoas_nvme_set_fast_irq(level);
}

static void *nwoas_nvme_guest_ptr(u64 ipa, size_t len)
{
    u64 pa = nwoas_ipa_to_pa(ipa);
    if (!pa || !nwoas_in_dram(pa, len))
        return NULL;
    /* Queue entries are naturally aligned and never cross a 16 KiB stage-2 page. */
    if ((ipa & MASK(VADDR_L3_OFFSET_BITS)) + len > PAGE_SIZE)
        return NULL;
    return (void *)pa;
}

#define NWOAS_NVME_EXEC_FATAL (-1)
#define NWOAS_NVME_EXEC_CANCELLED (-2)

static int nwoas_nvme_link_execute(struct exc_info *ctx,
                                   const struct nwoas_nvme_guest_cmd *cmd,
                                   u32 *completion_result)
{
    struct hv_nwoas_nvme_link_req req = {
        .magic = HV_NWOAS_NVME_LINK_MAGIC,
        .version = HV_NWOAS_NVME_LINK_VERSION,
        .size = sizeof(req),
        .sequence = ++nwoas_nvme_fp.link_sequence,
        .sq_generation = nwoas_nvme_fp.sq_generation,
        .cq_generation = nwoas_nvme_fp.cq_generation,
        .response = 0,
        .status = NWOAS_NVME_SC_INVALID_FIELD,
        .result = 0,
        .lifecycle_epoch = nwoas_nvme_fp.lifecycle_epoch,
    };
    memcpy(req.command, cmd, sizeof(req.command));
    u32 sequence = req.sequence;
    u16 sq_generation = req.sq_generation;
    u16 cq_generation = req.cq_generation;
    u16 sq_head = nwoas_nvme_fp.sq_head;
    u32 lifecycle_epoch = req.lifecycle_epoch;

    nwoas_nvme_fp.link_busy = true;
    hv_exc_proxy(ctx, START_HV, HV_NWOAS_NVME_LINK, &req);
    dma_rmb();
    nwoas_nvme_fp.link_busy = false;
    if (!nwoas_nvme_fp.armed || nwoas_nvme_fp.faulted ||
        nwoas_nvme_fp.lifecycle_epoch != lifecycle_epoch ||
        nwoas_nvme_fp.sq_generation != sq_generation ||
        nwoas_nvme_fp.cq_generation != cq_generation || nwoas_nvme_fp.sq_head != sq_head)
        return NWOAS_NVME_EXEC_CANCELLED;
    if (req.magic != HV_NWOAS_NVME_LINK_MAGIC ||
        req.version != HV_NWOAS_NVME_LINK_VERSION || req.size != sizeof(req) ||
        req.sequence != sequence || req.sq_generation != sq_generation ||
        req.cq_generation != cq_generation || req.lifecycle_epoch != lifecycle_epoch ||
        req.response != HV_NWOAS_NVME_LINK_DONE || req.status > 0x7fff)
        return cmd->opcode == 1 ? NWOAS_NVME_SC_WRITE_ERROR : NWOAS_NVME_SC_READ_ERROR;
    *completion_result = req.result;
    return req.status;
}

static int nwoas_nvme_fastpath_execute(struct exc_info *ctx,
                                       const struct nwoas_nvme_guest_cmd *cmd,
                                       u32 *completion_result)
{
    if (cmd->flags || cmd->metadata)
        return NWOAS_NVME_SC_INVALID_FIELD;
    if (cmd->opcode == 0 && cmd->nsid == 0xffffffff) {
        if (cmd->cdw10 || cmd->cdw11 || cmd->cdw12 || cmd->cdw13 || cmd->cdw14 || cmd->cdw15)
            return NWOAS_NVME_SC_INVALID_FIELD;
        return nvme_flush(1) ? NWOAS_NVME_SC_SUCCESS : NWOAS_NVME_EXEC_FATAL;
    }
    if (cmd->nsid == 2)
        return nwoas_nvme_link_execute(ctx, cmd, completion_result);
    if (cmd->nsid != 1)
        return NWOAS_NVME_SC_INVALID_NSID;

    if (cmd->opcode == 0) {
        if (cmd->cdw10 || cmd->cdw11 || cmd->cdw12 || cmd->cdw13 || cmd->cdw14 || cmd->cdw15)
            return NWOAS_NVME_SC_INVALID_FIELD;
        return nvme_flush(1) ? NWOAS_NVME_SC_SUCCESS : NWOAS_NVME_EXEC_FATAL;
    }

    if (cmd->opcode != 1 && cmd->opcode != 2)
        return NWOAS_NVME_SC_INVALID_OPCODE;

    bool write = cmd->opcode == 1;
    u64 lba = (u64)cmd->cdw10 | ((u64)cmd->cdw11 << 32);
    u32 count = (cmd->cdw12 & 0xffff) + 1;
    if ((cmd->cdw12 & 0x3fff0000) || (write ? (cmd->cdw13 & ~0xffu) : cmd->cdw13) ||
        cmd->cdw14 || cmd->cdw15)
        return NWOAS_NVME_SC_INVALID_FIELD;
    if (lba >= NWOAS_NVME_NAMESPACE_LBAS || count > NWOAS_NVME_NAMESPACE_LBAS - lba)
        return NWOAS_NVME_SC_LBA_RANGE;
    if (write && (lba < NWOAS_NVME_WRITE_FIRST_LBA ||
                  lba + count - 1 > NWOAS_NVME_WRITE_LAST_LBA))
        return NWOAS_NVME_SC_WRITE_RO;

    u64 result = nvme_rw_guest(write, lba, count, cmd->prp1, cmd->prp2);
    if (result == NVME_GUEST_FAILED)
        return NWOAS_NVME_EXEC_FATAL;
    if (result == NVME_GUEST_REFUSED)
        return NWOAS_NVME_SC_INVALID_FIELD;

    if (write && ((cmd->cdw12 & BIT(30)) || !nwoas_nvme_fp.cache_enabled) && !nvme_flush(1))
        return NWOAS_NVME_EXEC_FATAL;
    return NWOAS_NVME_SC_SUCCESS;
}

static void nwoas_nvme_fastpath_process(struct exc_info *ctx, u32 budget)
{
    if (!nwoas_nvme_fp.armed || nwoas_nvme_fp.faulted || nwoas_nvme_fp.link_busy)
        return;
    while (budget-- && nwoas_nvme_fp.sq_head != nwoas_nvme_fp.sq_tail &&
           nwoas_nvme_fp.cq_pending < nwoas_nvme_fp.cq_size - 1) {
        struct nwoas_nvme_guest_cmd cmd;
        struct nwoas_nvme_guest_cqe cqe;
        void *src = nwoas_nvme_guest_ptr(nwoas_nvme_fp.sq_base +
                                         (u64)nwoas_nvme_fp.sq_head * sizeof(cmd), sizeof(cmd));
        void *dst = nwoas_nvme_guest_ptr(nwoas_nvme_fp.cq_base +
                                         (u64)nwoas_nvme_fp.cq_tail * sizeof(cqe), sizeof(cqe));
        if (!src || !dst) {
            nwoas_nvme_fp.faulted = true;
            nwoas_nvme_fp.deferred = false;
            nwoas_nvme_fp.errors++;
            break;
        }
        dma_rmb();
        memcpy(&cmd, src, sizeof(cmd));

        u32 completion_result = 0;
        u64 execute_start = mrs(CNTPCT_EL0);
        nwoas_nvme_diag.in_flight_since = execute_start;
        nwoas_nvme_diag.in_flight_nsid = cmd.nsid;
        int result = nwoas_nvme_fastpath_execute(ctx, &cmd, &completion_result);
        u64 elapsed = mrs(CNTPCT_EL0) - execute_start;
        nwoas_nvme_diag.in_flight_since = 0;
        if (cmd.nsid == 2) {
            nwoas_nvme_diag.ns2_calls++;
            if (elapsed > nwoas_nvme_diag.max_ns2_ticks)
                nwoas_nvme_diag.max_ns2_ticks = elapsed;
        } else {
            nwoas_nvme_diag.ns1_calls++;
            if (elapsed > nwoas_nvme_diag.max_ns1_ticks)
                nwoas_nvme_diag.max_ns1_ticks = elapsed;
        }
        if (result == NWOAS_NVME_EXEC_CANCELLED) {
            nwoas_nvme_fp.deferred = nwoas_nvme_fp.armed && !nwoas_nvme_fp.faulted &&
                                     nwoas_nvme_fp.sq_head != nwoas_nvme_fp.sq_tail;
            nwoas_nvme_fastpath_update_irq();
            return;
        }
        if (result == NWOAS_NVME_EXEC_FATAL) {
            /* A submitted physical command may still complete late. Never
             * reuse tag 0 or expose a retryable completion after this point. */
            nwoas_nvme_fp.faulted = true;
            nwoas_nvme_fp.deferred = false;
            nwoas_nvme_fp.errors++;
            break;
        }
        u16 status = result;
        nwoas_nvme_fp.sq_head++;
        if (nwoas_nvme_fp.sq_head == nwoas_nvme_fp.sq_size)
            nwoas_nvme_fp.sq_head = 0;

        cqe = (struct nwoas_nvme_guest_cqe){
            .result = completion_result,
            .reserved = 0,
            .sq_head = nwoas_nvme_fp.sq_head,
            .sq_id = 1,
            .cid = cmd.cid,
            .status = (status << 1) | nwoas_nvme_fp.cq_phase,
        };
        /* Publish the phase-bearing status word last. A guest CPU may inspect
         * this CQ concurrently now that the Python rendezvous is gone. */
        memcpy(dst, &cqe, sizeof(cqe) - sizeof(cqe.status));
        dma_wmb();
        *(volatile u16 *)((u8 *)dst + sizeof(cqe) - sizeof(cqe.status)) = cqe.status;
        nwoas_nvme_fp.cq_tail++;
        nwoas_nvme_fp.cq_pending++;
        nwoas_nvme_fp.commands++;
        if (status)
            nwoas_nvme_fp.errors++;
        if (nwoas_nvme_fp.cq_tail == nwoas_nvme_fp.cq_size) {
            nwoas_nvme_fp.cq_tail = 0;
            nwoas_nvme_fp.cq_phase ^= 1;
        }
    }
    nwoas_nvme_fp.deferred = nwoas_nvme_fp.armed && !nwoas_nvme_fp.faulted &&
                             nwoas_nvme_fp.sq_head != nwoas_nvme_fp.sq_tail;
    nwoas_nvme_fastpath_update_irq();
}

/* Called from the 5 kHz hypervisor tick while bhl is held. This resumes work
 * after a CQ acknowledgement without executing physical I/O in Windows'
 * NVMeCompletionDpcRoutine MMIO trap. One command per tick bounds latency. */
void nwoas_nvme_fastpath_poll(struct exc_info *ctx)
{
    if (nwoas_nvme_fp.deferred && nwoas_nvme_fp.cq_pending < nwoas_nvme_fp.cq_size - 1)
        nwoas_nvme_fastpath_process(ctx, 1);
}

/* action 0=disable, 1=arm, 2=sync policy, 3=query counters, 4=query lifecycle,
 * 5/6=query NS1/NS2 maximum execution ticks, 7=query backlog/ack counters. */
u64 nwoas_nvme_fastpath_control(u64 action, u64 sq_base, u64 sq_size, u64 cq_base,
                                u64 cq_size, u64 flags)
{
    if (action == 0) {
        nwoas_nvme_fp.armed = false;
        nwoas_nvme_fp.deferred = false;
        nwoas_nvme_fp.lifecycle_epoch++;
        nwoas_nvme_fastpath_update_irq();
        return 1;
    }
    if (action == 2) {
        nwoas_nvme_fp.irq_enabled = flags & BIT(1);
        nwoas_nvme_fp.cache_enabled = flags & BIT(2);
        nwoas_nvme_fp.local_mask_allowed = flags & BIT(3);
        nwoas_nvme_fp.mask = flags >> 32;
        nwoas_nvme_fastpath_update_irq();
        return nwoas_nvme_fp.faulted ? 2 : nwoas_nvme_fp.armed;
    }
    if (action == 3)
        return (u64)nwoas_nvme_fp.commands | ((u64)nwoas_nvme_fp.errors << 32);
    if (action == 4)
        return nwoas_nvme_fp.lifecycle_epoch | ((u64)nwoas_nvme_fp.armed << 32) |
               ((u64)nwoas_nvme_fp.faulted << 33) | ((u64)nwoas_nvme_fp.link_busy << 34);
    if (action == 5)
        return nwoas_nvme_diag.max_ns1_ticks;
    if (action == 6)
        return nwoas_nvme_diag.max_ns2_ticks;
    if (action == 7)
        return nwoas_nvme_diag.max_sq_backlog | ((u64)nwoas_nvme_diag.cq_ack_writes << 32);
    if (action == 8)
        return (0x53313630ULL << 32) | nwoas_nvme_fp.mask;
    if (action == 10) {
        nwoas_nvme_dump_cq_map();
        return nwoas_nvme_cq_map.found;
    }
    if (action == 9)
        return nwoas_nvme_diag.local_mask_reads |
               ((u64)nwoas_nvme_diag.local_mask_writes << 32);
    if (action != 1 || !sq_base || !cq_base || (sq_base & 0xfff) || (cq_base & 0xfff) ||
        sq_size < 2 || sq_size > NWOAS_NVME_MAX_IO_DEPTH ||
        cq_size < 2 || cq_size > NWOAS_NVME_MAX_IO_DEPTH)
        return 0;
    if (!nwoas_nvme_guest_ptr(sq_base, sizeof(struct nwoas_nvme_guest_cmd)) ||
        !nwoas_nvme_guest_ptr(sq_base + (sq_size - 1) * sizeof(struct nwoas_nvme_guest_cmd),
                              sizeof(struct nwoas_nvme_guest_cmd)) ||
        !nwoas_nvme_guest_ptr(cq_base, sizeof(struct nwoas_nvme_guest_cqe)) ||
        !nwoas_nvme_guest_ptr(cq_base + (cq_size - 1) * sizeof(struct nwoas_nvme_guest_cqe),
                              sizeof(struct nwoas_nvme_guest_cqe)))
        return 0;

    if (nwoas_nvme_fp.faulted)
        return 0;
    u16 sq_generation = (flags >> 8) & 0xfff;
    u16 cq_generation = (flags >> 20) & 0xfff;
    bool same_sq = nwoas_nvme_fp.sq_base == sq_base && nwoas_nvme_fp.sq_size == sq_size &&
                   nwoas_nvme_fp.sq_generation == sq_generation;
    bool same_cq = nwoas_nvme_fp.cq_base == cq_base && nwoas_nvme_fp.cq_size == cq_size &&
                   nwoas_nvme_fp.cq_generation == cq_generation;
    if (!same_sq) {
        nwoas_nvme_fp.sq_head = 0;
        nwoas_nvme_fp.sq_tail = 0;
    }
    if (!same_cq) {
        nwoas_nvme_fp.cq_head = 0;
        nwoas_nvme_fp.cq_tail = 0;
        nwoas_nvme_fp.cq_pending = 0;
        nwoas_nvme_fp.cq_phase = flags & 1;
    }
    nwoas_nvme_fp.sq_base = sq_base;
    nwoas_nvme_fp.cq_base = cq_base;
    nwoas_nvme_fp.sq_size = sq_size;
    nwoas_nvme_fp.cq_size = cq_size;
    nwoas_nvme_fp.sq_generation = sq_generation;
    nwoas_nvme_fp.cq_generation = cq_generation;
    nwoas_nvme_fp.irq_enabled = flags & BIT(1);
    nwoas_nvme_fp.cache_enabled = flags & BIT(2);
    nwoas_nvme_fp.local_mask_allowed = flags & BIT(3);
    nwoas_nvme_fp.mask = flags >> 32;
    nwoas_nvme_fp.lifecycle_epoch++;
    nwoas_nvme_fp.armed = true;
    nwoas_nvme_fastpath_update_irq();
    printf("NWOAS-S149 NVMe fastpath armed SQ=0x%lx/%lu CQ=0x%lx/%lu\n",
           sq_base, sq_size, cq_base, cq_size);
    return 1;
}

static bool nwoas_nvme_fastpath_mmio(struct exc_info *ctx, u64 ipa, u64 *val, bool write,
                                     u64 width)
{
    if (!nwoas_nvme_fp.armed || ipa < NWOAS_NVME_BAR ||
        ipa >= NWOAS_NVME_BAR + NWOAS_NVME_BAR_SIZE)
        return false;
    u64 off = ipa - NWOAS_NVME_BAR;
    if (width != 2)
        return false;
    /* Keep admin/host IRQ handling on its existing path while it is pending.
     * The Python fallback pulls this mask before any register/PCI operation. */
    if (!nwoas_nvme_fp.faulted && nwoas_nvme_fp.local_mask_allowed &&
        (off == 0x0c || off == 0x10)) {
        if (write) {
            if (off == 0x0c)
                nwoas_nvme_fp.mask |= (u32)*val;
            else
                nwoas_nvme_fp.mask &= ~(u32)*val;
            nwoas_nvme_diag.local_mask_writes++;
            nwoas_nvme_fastpath_update_irq();
        } else {
            *val = nwoas_nvme_fp.mask;
            nwoas_nvme_diag.local_mask_reads++;
        }
        return true;
    }
    if (!write) {
        if (nwoas_nvme_fp.faulted && off == 0x1c) {
            *val = 3; /* CSTS.RDY | CSTS.CFS */
            return true;
        }
        return false;
    }
    if (off == 0x1008) {
        if (*val < nwoas_nvme_fp.sq_size) {
            nwoas_nvme_fp.sq_tail = *val;
            u32 backlog = (nwoas_nvme_fp.sq_tail + nwoas_nvme_fp.sq_size -
                           nwoas_nvme_fp.sq_head) % nwoas_nvme_fp.sq_size;
            if (backlog > nwoas_nvme_diag.max_sq_backlog)
                nwoas_nvme_diag.max_sq_backlog = backlog;
            nwoas_nvme_fp.deferred = nwoas_nvme_fp.sq_head != nwoas_nvme_fp.sq_tail;
            nwoas_nvme_fastpath_process(ctx, 1);
        } else {
            nwoas_nvme_fp.errors++;
        }
        return true;
    }
    if (off == 0x100c) {
        nwoas_nvme_probe_cq_map(ctx);
        nwoas_nvme_diag.cq_ack_writes++;
        if (*val < nwoas_nvme_fp.cq_size) {
            u16 head = *val;
            u16 consumed = (head + nwoas_nvme_fp.cq_size - nwoas_nvme_fp.cq_head) %
                           nwoas_nvme_fp.cq_size;
            if (consumed <= nwoas_nvme_fp.cq_pending) {
                nwoas_nvme_fp.cq_head = head;
                nwoas_nvme_fp.cq_pending -= consumed;
                nwoas_nvme_fp.deferred = nwoas_nvme_fp.sq_head != nwoas_nvme_fp.sq_tail;
                nwoas_nvme_fastpath_update_irq();
            } else {
                nwoas_nvme_fp.errors++;
            }
        } else {
            nwoas_nvme_fp.errors++;
        }
        return true;
    }
    return false;
}

// Cache-maintain [addr,addr+len) under exc_guard (a stray fault is skipped, not a reboot).
//   clean=true  -> dc civac (clean+invalidate): for GUEST-authored data (ERST) — flush the guest's
//                  own line to DRAM, then read its latest coherent value.
//   clean=false -> dc ivac  (invalidate only): for the DEVICE-authored event ring — drop the stale
//                  CPU copy WITHOUT writing it back, so the next read returns FL1100's DMA'd bytes
//                  from DRAM. Using civac here would clean stale init-zeros over FL1100's TRB and
//                  fabricate an all-zero ring = a FALSE "reachability" verdict (adversarial review).
static void nwoas_cache_maint(u64 addr, size_t len, bool clean)
{
    enum exc_guard_t g = exc_guard;
    exc_guard = GUARD_SKIP;
    exc_count = 0;
    sysop("dsb sy");
    for (u64 p = addr & ~63UL; p < addr + len; p += 64) {
        if (clean)
            dc_civac((void *)p);
        else
            dc_ivac((void *)p);
    }
    sysop("dsb sy");
    exc_guard = g;
}

/* D73: bounded USB-C HSE snapshot. CPU-authored ERST is read as-is;
 * event-owned memory is invalidated only, never cleaned over DMA writes. */
void nwoas_probe_usbc_rings(u64 erst_ipa, u64 erdp_ipa, u64 dcbaa_ipa)
{
    u64 pa = nwoas_ipa_to_pa(erst_ipa & ~63UL);
    printf("[usb-d73] erst=%lx pa=%lx erdp=%lx dcbaa=%lx\n",
           erst_ipa, pa, erdp_ipa, dcbaa_ipa);
    enum exc_guard_t saved = exc_guard;
    exc_count = 0;
    exc_guard = GUARD_SKIP | GUARD_SILENT;
    u64 last_cmd = 0;
    for (u32 i = 0; pa && i < 4 && nwoas_in_dram(pa + i * 16, 16); i++) {
        u64 entry = pa + i * 16;
        u64 ring = read64(entry);
        u32 count = read32(entry + 8);
        printf("[usb-d73] ERST%u ring=%lx count=%u\n", i, ring, count);
        u64 rp = nwoas_ipa_to_pa(ring & ~63UL);
        if (!count || count > 4096 || !rp || !nwoas_in_dram(rp, 128))
            continue;
        nwoas_cache_maint(rp, 128, false);
        for (u32 j = 0; j < (i == 0 ? 64u : 8u) && j < count; j++) {
            u64 t = rp + j * 16;
            if (!nwoas_in_dram(t, 16))
                break;
            nwoas_cache_maint(t, 16, false);
            if (((read32(t + 12) >> 10) & 63) == 33)
                last_cmd = read64(t);
            printf("[usb-d74] EVT%u.%u %lx %x %x\n", i, j,
                   read64(t), read32(t + 8), read32(t + 12));
        }
    }
    /* D75: last completed command and next commands, CPU view only.
     * Decode input-context pointers only for Address/Configure/Evaluate Context.
     * No MMIO writes, no cache clean of guest command/context storage. */
    u64 cp = last_cmd ? nwoas_ipa_to_pa(last_cmd) : 0;
    printf("[usb-d75] lastcmd=%lx pa=%lx caps=%x %x %x %x\n", last_cmd, cp,
           read32(0x502280004UL), read32(0x502280008UL),
           read32(0x502280010UL), read32(0x50228001cUL));
    for (u32 i = 0; cp && i < 8 && nwoas_in_dram(cp + i * 16, 16); i++) {
        u64 t = cp + i * 16;
        u32 control = read32(t + 12), type = (control >> 10) & 63;
        u64 ptr = read64(t);
        printf("[usb-d75] CMD%u %lx %x %x\n", i, ptr, read32(t + 8), control);
        if (type != 11 && type != 12 && type != 13)
            continue;
        u64 ctx = nwoas_ipa_to_pa(ptr & ~63UL);
        printf("[usb-d75] inputctx=%lx pa=%lx\n", ptr, ctx);
        for (u32 j = 0; ctx && j < 12 && nwoas_in_dram(ctx + j * 32, 32); j++) {
            u64 c = ctx + j * 32;
            printf("[usb-d75] CTX%u %lx %lx %lx %lx\n", j,
                   read64(c), read64(c + 8), read64(c + 16), read64(c + 24));
        }
    }
    /* D76: device-owned slot1 endpoint state and CPU-authored transfer TRBs. */
    u64 dp = nwoas_ipa_to_pa(dcbaa_ipa & ~63UL);
    u32 csz = (read32(0x502280010UL) & 4) ? 64 : 32;
    u64 devctx = dp && nwoas_in_dram(dp, 16) ? read64(dp + 8) : 0;
    u64 devpa = devctx ? nwoas_ipa_to_pa(devctx) : 0;
    printf("[usb-d76] slot1=%lx pa=%lx csz=%u\n", devctx, devpa, csz);
    for (u32 ep = 1; devpa && ep < 32; ep++) {
        u64 c = devpa + ep * csz;
        if (!nwoas_in_dram(c, 32))
            break;
        nwoas_cache_maint(c, 32, false);
        u32 d0 = read32(c), d1 = read32(c + 4);
        u64 dq = read64(c + 8);
        if (!(d0 & 7) && !dq)
            continue;
        printf("[usb-d76] EP%u d0=%x d1=%x dq=%lx avg=%x\n", ep,d0,d1,dq,read32(c+16));
        u64 rp = nwoas_ipa_to_pa(dq & ~15UL);
        for (u32 j = 0; rp && j < 8 && nwoas_in_dram(rp + j*16,16); j++) {
            u64 t = rp + j*16;
            printf("[usb-d76] TRB%u.%u %lx %x %x\n", ep,j,
                   read64(t),read32(t+8),read32(t+12));
        }
    }
    for (u32 i = 0; i < 2; i++) {
        u64 base = 0x502f00000UL + i * 0x80000;
        printf("[usb-d73] DART%u err=%x addr=%x:%08x tcr0=%x tcr1=%x\n",
               i, read32(base + 0x40), read32(base + 0x54), read32(base + 0x50),
               read32(base + 0x100), read32(base + 0x104));
    }
    printf("[usb-d73] end faults=%d\n", exc_count);
    exc_guard = saved;
}

/* S6-D57/D58: one-shot late, read-only FL1100 state probe.
 *
 * D56 proved that Windows Setup has replaced the preserved DCP test bars with
 * its language-selection UI.  The remaining question is whether Windows has
 * also enumerated the physical USB devices behind the FL1100.  At that same
 * +120s point, inspect the guest-authored command ring and DCBAA plus the
 * controller-authored event ring/device contexts.  This function performs no
 * MMIO or guest-memory writes.  Cache maintenance follows ownership: clean the
 * guest-authored structures the controller DMA-reads, invalidate-only for the
 * controller-authored structures so stale CPU lines cannot hide DMA results.
 */
void nwoas_late_usb_probe(void)
{
    static bool done = false;
    if (done)
        return;
    done = true;

    printf("HVLOG: LATEUSB BEGIN erstba=0x%lx dcbaap=0x%lx crcr=0x%lx\n",
           nwoas_fl_erstba, nwoas_fl_dcbaap, nwoas_fl_crcr);

    /* Command ring: commands remain in memory after the xHC consumes them.
     * Types 9/11/12/13 are Enable Slot/Address Device/Configure/Evaluate. */
    u32 cmd_nonzero = 0, cmd_printed = 0;
    u64 cr_ipa = nwoas_fl_crcr & ~0x3fUL;
    for (u32 i = 0; cr_ipa && i < 256; i++) {
        u64 pa = nwoas_ipa_to_pa(cr_ipa + (u64)i * 16);
        if (!pa || !nwoas_in_dram(pa, 16)) {
            printf("HVLOG: LATEUSB CMD[%u] ipa=0x%lx pa=0x%lx invalid\n",
                   i, cr_ipa + (u64)i * 16, pa);
            break;
        }
        nwoas_cache_maint(pa, 16, true);
        u64 ptr = *(volatile u64 *)pa;
        u32 status = *(volatile u32 *)(pa + 8);
        u32 control = *(volatile u32 *)(pa + 12);
        if (ptr || status || control) {
            cmd_nonzero++;
            if (cmd_printed < 64) {
                printf("HVLOG: LATEUSB CMD[%u] ptr=0x%lx status=0x%x ctrl=0x%x "
                       "type=%u slot=%u cyc=%u\n",
                       i, ptr, status, control, (control >> 10) & 0x3f,
                       (control >> 24) & 0xff, control & 1);
                cmd_printed++;
            }
        }
        /* A Link TRB closes this command-ring segment.  Bytes after it belong
         * to unrelated guest allocations and must not be counted as commands. */
        if (((control >> 10) & 0x3f) == 6)
            break;
    }
    printf("HVLOG: LATEUSB CMD-SUM nonzero=%u printed=%u\n", cmd_nonzero, cmd_printed);

    /* ERST[0] is guest-authored.  Its segment is controller-authored. */
    u64 erst_ipa = nwoas_fl_erstba & ~0x3fUL;
    u64 erst_pa = erst_ipa ? nwoas_ipa_to_pa(erst_ipa) : 0;
    u64 evt_ipa = 0;
    u32 evt_size = 0;
    if (erst_pa && nwoas_in_dram(erst_pa, 16)) {
        nwoas_cache_maint(erst_pa, 16, true);
        evt_ipa = *(volatile u64 *)erst_pa & ~0x3fUL;
        evt_size = *(volatile u32 *)(erst_pa + 8) & 0xffff;
    }
    printf("HVLOG: LATEUSB ERST ipa=0x%lx pa=0x%lx evt=0x%lx size=%u\n",
           erst_ipa, erst_pa, evt_ipa, evt_size);
    u32 evt_nonzero = 0, evt_printed = 0;
    u32 evt_slot[32] = {0};
    u32 evt_slot2_ep[32] = {0};
    u32 evt_n = evt_size < 256 ? evt_size : 256;
    for (u32 i = 0; evt_ipa && i < evt_n; i++) {
        u64 pa = nwoas_ipa_to_pa(evt_ipa + (u64)i * 16);
        if (!pa || !nwoas_in_dram(pa, 16)) {
            printf("HVLOG: LATEUSB EVT[%u] ipa=0x%lx pa=0x%lx invalid\n",
                   i, evt_ipa + (u64)i * 16, pa);
            break;
        }
        nwoas_cache_maint(pa, 16, false);
        u64 ptr = *(volatile u64 *)pa;
        u32 status = *(volatile u32 *)(pa + 8);
        u32 control = *(volatile u32 *)(pa + 12);
        if (ptr || status || control) {
            evt_nonzero++;
            if (((control >> 10) & 0x3f) == 32) {
                u32 slot = control >> 24;
                u32 ep = (control >> 16) & 0x1f;
                if (slot < 32)
                    evt_slot[slot]++;
                if (slot == 2)
                    evt_slot2_ep[ep]++;
            }
            if (evt_printed < 64) {
                printf("HVLOG: LATEUSB EVT[%u] ptr=0x%lx status=0x%x ctrl=0x%x "
                       "type=%u cc=%u slot=%u ep=%u cyc=%u\n",
                       i, ptr, status, control, (control >> 10) & 0x3f,
                       status >> 24, control >> 24, (control >> 16) & 0x1f,
                       control & 1);
                evt_printed++;
            }
        }
    }
    printf("HVLOG: LATEUSB EVT-SUM nonzero=%u printed=%u\n", evt_nonzero, evt_printed);
    for (u32 slot = 0; slot < 32; slot++) {
        if (evt_slot[slot])
            printf("HVLOG: LATEUSB EVT-XFER slot=%u count=%u\n", slot, evt_slot[slot]);
    }
    for (u32 ep = 0; ep < 32; ep++) {
        if (evt_slot2_ep[ep])
            printf("HVLOG: LATEUSB EVT-XFER slot=2 ep=%u count=%u\n",
                   ep, evt_slot2_ep[ep]);
        nwoas_hid_baseline_ep[ep] = evt_slot2_ep[ep];
    }

    /* DCBAA and device contexts.  FL1100 advertises CSZ=0, hence 32-byte
     * contexts.  Slot dwords expose route/speed/root-port/address/state;
     * endpoint dwords expose type/state/interval/max-packet/dequeue. */
    u64 dc_ipa = nwoas_fl_dcbaap & ~0x3fUL;
    u64 dc_pa = dc_ipa ? nwoas_ipa_to_pa(dc_ipa) : 0;
    u32 live_slots = 0;
    if (dc_pa && nwoas_in_dram(dc_pa, 32 * 8)) {
        nwoas_cache_maint(dc_pa, 32 * 8, true);
        for (u32 slot = 1; slot < 32; slot++) {
            u64 ctx_ipa = *(volatile u64 *)(dc_pa + (u64)slot * 8) & ~0x3fUL;
            if (!ctx_ipa)
                continue;
            live_slots++;
            u64 ctx_pa = nwoas_ipa_to_pa(ctx_ipa);
            if (!ctx_pa || !nwoas_in_dram(ctx_pa, 32 * 32)) {
                printf("HVLOG: LATEUSB SLOT%u ctx=0x%lx pa=0x%lx invalid\n",
                       slot, ctx_ipa, ctx_pa);
                continue;
            }
            nwoas_cache_maint(ctx_pa, 32 * 32, false);
            const volatile u32 *sc = (const volatile u32 *)ctx_pa;
            u32 entries = (sc[0] >> 27) & 0x1f;
            printf("HVLOG: LATEUSB SLOT%u ctx=0x%lx route=0x%x speed=%u entries=%u "
                   "rootport=%u addr=%u state=%u\n",
                   slot, ctx_ipa, sc[0] & 0xfffff, (sc[0] >> 20) & 0xf,
                   entries, (sc[1] >> 16) & 0xff, sc[3] & 0xff,
                   (sc[3] >> 27) & 0x1f);
            u32 epmax = entries < 31 ? entries : 31;
            for (u32 dci = 1; dci <= epmax; dci++) {
                const volatile u32 *ec = (const volatile u32 *)(ctx_pa + (u64)dci * 32);
                u32 epstate = ec[0] & 7;
                u32 eptype = (ec[1] >> 3) & 7;
                u32 maxpkt = ec[1] >> 16;
                u32 interval = (ec[0] >> 16) & 0xff;
                u64 deq = ((u64)ec[3] << 32) | ec[2];
                if (epstate || eptype || maxpkt || deq)
                    printf("HVLOG: LATEUSB SLOT%u DCI%u state=%u type=%u interval=%u "
                           "maxpkt=%u deq=0x%lx dcs=%lu\n",
                           slot, dci, epstate, eptype, interval, maxpkt,
                           deq & ~0xfUL, deq & 1);

                /* D58: for the composite input device, show the queued
                 * interrupt transfer TRBs and the first report bytes.  A
                 * Running endpoint with live Normal TRBs demonstrates that
                 * the Windows HID stack has armed physical polling. */
                if (slot == 2 && dci > 1 && deq) {
                    u64 ring_ipa = deq & ~0xfUL;
                    u32 trb_nonzero = 0;
                    for (u32 ti = 0; ti < 16; ti++) {
                        u64 trb_pa = nwoas_ipa_to_pa(ring_ipa + (u64)ti * 16);
                        if (!trb_pa || !nwoas_in_dram(trb_pa, 16))
                            break;
                        nwoas_cache_maint(trb_pa, 16, true);
                        u64 bp = *(volatile u64 *)trb_pa;
                        u32 st = *(volatile u32 *)(trb_pa + 8);
                        u32 ct = *(volatile u32 *)(trb_pa + 12);
                        if (!(bp || st || ct))
                            continue;
                        trb_nonzero++;
                        u32 tt = (ct >> 10) & 0x3f;
                        printf("HVLOG: LATEUSB XRING slot=2 dci=%u trb=%u ptr=0x%lx "
                               "len=%u ctrl=0x%x type=%u cyc=%u chain=%u ioc=%u\n",
                               dci, ti, bp, st & 0x1ffff, ct, tt, ct & 1,
                               (ct >> 4) & 1, (ct >> 5) & 1);
                        if (tt == 1 && bp && (st & 0x1ffff)) {
#if NWOAS_HID_EXTENDED_OBSERVER
                            if (dci < 32 &&
                                nwoas_hid_report_count[dci] < NWOAS_HID_REPORTS_PER_EP) {
                                u32 saved = nwoas_hid_report_count[dci]++;
                                nwoas_hid_report_ipa[dci][saved] = bp;
                                nwoas_hid_report_len[dci][saved] = st & 0x1ffff;
                            }
#endif
                            u64 bp_pa = nwoas_ipa_to_pa(bp);
                            if (bp_pa && nwoas_in_dram(bp_pa, 16)) {
                                /* IN endpoints are device-authored; never clean
                                 * a possibly stale CPU line over their report. */
                                bool input = eptype == 5 || eptype == 6 || eptype == 7;
                                nwoas_cache_maint(bp_pa, 16, !input);
                                u64 b0 = *(volatile u64 *)bp_pa;
                                u64 b1 = *(volatile u64 *)(bp_pa + 8);
                                printf("HVLOG: LATEUSB XBUF slot=2 dci=%u trb=%u "
                                       "ipa=0x%lx data=%016lx%016lx\n",
                                       dci, ti, bp, b1, b0);
                            }
                        }
                        if (tt == 6)
                            break;
                    }
                    printf("HVLOG: LATEUSB XRING-SUM slot=2 dci=%u nonzero=%u\n",
                           dci, trb_nonzero);
                }
            }
        }
    } else {
        printf("HVLOG: LATEUSB DCBAA ipa=0x%lx pa=0x%lx invalid\n", dc_ipa, dc_pa);
    }
    nwoas_hid_watch_ready = live_slots >= 2;
    printf("HVLOG: LATEUSB END live_slots=%u hid_watch=%u\n",
           live_slots, nwoas_hid_watch_ready ? 1 : 0);
}

#if NWOAS_HID_EXTENDED_OBSERVER
static u64 nwoas_hid_fb_tile_hashes(u64 *tiles)
{
    const u64 fb = 0xbe3f60000UL;
    const u64 bytes = (u64)NWOAS_HID_FB_STRIDE * NWOAS_HID_FB_HEIGHT;
    const volatile u8 *p = (const volatile u8 *)fb;
    u64 whole = 0xcbf29ce484222325UL;

    for (u32 i = 0; i < NWOAS_HID_FB_TILE_COUNT; i++)
        tiles[i] = 0xcbf29ce484222325UL;
    nwoas_cache_maint(fb, bytes, false);
    for (u32 y = 0; y < NWOAS_HID_FB_HEIGHT; y++) {
        for (u32 x = 0; x < NWOAS_HID_FB_WIDTH; x++) {
            u32 tile = (y / NWOAS_HID_FB_TILE) * NWOAS_HID_FB_TILE_COLS +
                       x / NWOAS_HID_FB_TILE;
            u64 offset = (u64)y * NWOAS_HID_FB_STRIDE + (u64)x * 4;
            for (u32 channel = 0; channel < 4; channel++) {
                u8 value = p[offset + channel];
                whole = (whole ^ value) * 0x100000001b3UL;
                tiles[tile] = (tiles[tile] ^ value) * 0x100000001b3UL;
            }
        }
    }
    return whole;
}

static void nwoas_hid_dump_saved_reports(u32 ep)
{
    if (ep >= 32)
        return;
    u32 count = nwoas_hid_report_count[ep];
    if (count > NWOAS_HID_REPORTS_PER_EP)
        count = NWOAS_HID_REPORTS_PER_EP;
    for (u32 i = 0; i < count; i++) {
        u64 ipa = nwoas_hid_report_ipa[ep][i];
        u32 length = nwoas_hid_report_len[ep][i];
        u64 pa = ipa ? nwoas_ipa_to_pa(ipa) : 0;
        u32 bytes = length < 32 ? length : 32;
        if (!pa || !bytes || !nwoas_in_dram(pa, bytes)) {
            printf("HVLOG: HIDACT REPORT ep=%u index=%u ipa=0x%lx pa=0x%lx invalid\n",
                   ep, i, ipa, pa);
            continue;
        }
        /* Device-authored interrupt-IN buffer: invalidate only. */
        nwoas_cache_maint(pa, bytes, false);
        const volatile u8 *p = (const volatile u8 *)pa;
        u64 words[4] = {0};
        u32 nonzero = 0;
        for (u32 j = 0; j < bytes; j++) {
            u8 value = p[j];
            words[j / 8] |= (u64)value << ((j % 8) * 8);
            nonzero += value != 0;
        }
        printf("HVLOG: HIDACT REPORT ep=%u index=%u ipa=0x%lx len=%u "
               "sample=%u nonzero=%u data=%016lx%016lx%016lx%016lx\n",
               ep, i, ipa, length, bytes, nonzero,
               words[3], words[2], words[1], words[0]);
    }
}
#endif

/* S6-D59/D61: passive HID activity watcher.  Once D58 has established the
 * baseline, sample the controller-authored event ring once per second and log
 * only a new slot-2 interrupt completion.  No polling output means no serial
 * load while the owner sleeps.  A type-32 event on DCI3/5/9 is the missing
 * physical mouse/keyboard report proof. */
void nwoas_watch_hid(u64 now)
{
    static u64 last = 0;
    static bool found = false;
#if NWOAS_HID_EXTENDED_OBSERVER
    static bool fb_done = false;
    static u64 found_at = 0;
    static u32 fb_samples = 0;
#endif
    if (!nwoas_hid_watch_ready)
        return;
    if (now - last < mrs(CNTFRQ_EL0))
        return;
    last = now;

#if NWOAS_HID_EXTENDED_OBSERVER
    if (!nwoas_hid_fb_baseline_ready) {
        u64 baseline = nwoas_hid_fb_tile_hashes(nwoas_hid_fb_baseline_tiles);
        nwoas_hid_fb_baseline_ready = true;
        printf("HVLOG: HIDWATCH FBBASE fnv=0x%lx tiles=%u grid=%ux%u\n",
               baseline, NWOAS_HID_FB_TILE_COUNT,
               NWOAS_HID_FB_TILE_COLS, NWOAS_HID_FB_TILE_ROWS);
    }
#endif

    u64 erst_ipa = nwoas_fl_erstba & ~0x3fUL;
    u64 erst_pa = erst_ipa ? nwoas_ipa_to_pa(erst_ipa) : 0;
    if (!erst_pa || !nwoas_in_dram(erst_pa, 16))
        return;
    nwoas_cache_maint(erst_pa, 16, true);
    u64 evt_ipa = *(volatile u64 *)erst_pa & ~0x3fUL;
    u32 evt_size = *(volatile u32 *)(erst_pa + 8) & 0xffff;
    if (!evt_ipa || !evt_size || evt_size > 4096)
        return;

    u32 count[32] = {0};
    u32 activity_printed = 0;
    u32 n = evt_size < 256 ? evt_size : 256;
    for (u32 i = 0; i < n; i++) {
        u64 pa = nwoas_ipa_to_pa(evt_ipa + (u64)i * 16);
        if (!pa || !nwoas_in_dram(pa, 16))
            break;
        nwoas_cache_maint(pa, 16, false);
        u64 ptr = *(volatile u64 *)pa;
        u32 status = *(volatile u32 *)(pa + 8);
        u32 control = *(volatile u32 *)(pa + 12);
        u32 type = (control >> 10) & 0x3f;
        u32 slot = control >> 24;
        u32 ep = (control >> 16) & 0x1f;
        if (type == 32 && slot == 2 && ep < 32) {
            count[ep]++;
            if (!found && ep > 1 && count[ep] > nwoas_hid_baseline_ep[ep] &&
                activity_printed < 8) {
                printf("HVLOG: HIDACT event=%u ptr=0x%lx status=0x%x ctrl=0x%x "
                       "ep=%u cc=%u residual=%u\n",
                       i, ptr, status, control, ep, status >> 24, status & 0xffffff);
                activity_printed++;
            }
        }
    }

    for (u32 ep = 2; ep < 32; ep++) {
        if (count[ep] > nwoas_hid_baseline_ep[ep]) {
            if (!found) {
                printf("HVLOG: HIDACT PHYSICAL-REPORT slot=2 ep=%u baseline=%u now=%u\n",
                       ep, nwoas_hid_baseline_ep[ep], count[ep]);
#if NWOAS_HID_EXTENDED_OBSERVER
                nwoas_hid_dump_saved_reports(ep);
                found_at = now;
#endif
            }
            found = true;
        }
    }

#if NWOAS_HID_EXTENDED_OBSERVER
    /* Hash the scanout at +1/+2/+3s after the first physical HID completion.
     * This lets Windows consume the report and render before we sample.  If the
     * Windows software cursor is in this surface, its movement changes the
     * known Setup tile set.  The changed-tile bbox distinguishes a local cursor
     * update from a broad redraw.  A hardware cursor plane may remain separate,
     * so an unchanged hash does not negate the HID event. */
    if (found && !fb_done &&
        now - found_at >= (u64)(fb_samples + 1) * mrs(CNTFRQ_EL0)) {
        u64 fnv = nwoas_hid_fb_tile_hashes(nwoas_hid_fb_sample_tiles);
        u32 changed_tiles = 0;
        u32 min_tx = NWOAS_HID_FB_TILE_COLS, min_ty = NWOAS_HID_FB_TILE_ROWS;
        u32 max_tx = 0, max_ty = 0;
        for (u32 i = 0; i < NWOAS_HID_FB_TILE_COUNT; i++) {
            if (nwoas_hid_fb_sample_tiles[i] == nwoas_hid_fb_baseline_tiles[i])
                continue;
            u32 tx = i % NWOAS_HID_FB_TILE_COLS;
            u32 ty = i / NWOAS_HID_FB_TILE_COLS;
            changed_tiles++;
            if (tx < min_tx) min_tx = tx;
            if (ty < min_ty) min_ty = ty;
            if (tx > max_tx) max_tx = tx;
            if (ty > max_ty) max_ty = ty;
        }
        fb_samples++;
        bool changed = changed_tiles != 0;
        printf("HVLOG: HIDACT FB sample=%u fnv=0x%lx changed=%u "
               "tiles=%u bbox=%u,%u-%u,%u\n",
               fb_samples, fnv, changed ? 1 : 0, changed_tiles,
               changed ? min_tx * NWOAS_HID_FB_TILE : 0,
               changed ? min_ty * NWOAS_HID_FB_TILE : 0,
               changed ? (max_tx + 1) * NWOAS_HID_FB_TILE - 1 : 0,
               changed ? (max_ty + 1) * NWOAS_HID_FB_TILE - 1 : 0);
        if (changed || fb_samples >= 3)
            fb_done = true;
    }
#endif
}

// NWOAS session5 (2026-07-14) CONTROLLER-POSTING TRACKER. The KEY unknown left after the DART-stale
// finding: does the FL1100 EVER post an event (IMAN.IP or USBSTS.EINT going live), or does it never
// post at all? This samples the RAW controller state directly from the BAR -- NOT the guest's synth-
// spoofed reads -- on every USBSTS poll in the storm hot path, and keeps running counters so we can
// tell "never posts" (ip_seen/eint_seen stay 0 across thousands of calls -> points to no-fresh-change /
// needs a port reset) from "posts sometimes" (nonzero -> points to a consume/delivery gap instead).
// Guarded exactly like nwoas_dump_fork below (a mid-run PCI decode-off must SKIP, never reboot).
// Read-only: only reads the BAR, never writes.
static void nwoas_track_posting(void)
{
    static u32 calls = 0, ip_seen = 0, eint_seen = 0;
    const u64 bar = 0x6c0000000UL; // j274 apcie bus2/dev0 FL1100 xHCI BAR0 (fixed by UEFI)
    enum exc_guard_t g = exc_guard;
    exc_count = 0;
    exc_guard = GUARD_SKIP | GUARD_SILENT;
    u32 caplen = read32(bar + 0x00) & 0xff;
    u32 rtsoff = read32(bar + 0x18) & ~0x1fu;
    u64 op = bar + caplen, rt = bar + rtsoff;
    u32 usbsts = read32(op + 0x04);
    u32 iman = read32(rt + 0x20);
    u32 erdp = read32(rt + 0x38);
    exc_guard = g;
    if (exc_count || caplen == 0 || caplen == 0xff)
        return; // decode-off mid-run -- skip this sample, don't corrupt the counters
    calls++;
    bool ip = (iman & 1) != 0, eint = (usbsts & 8) != 0;
    bool firstpost = (ip || eint) && !ip_seen && !eint_seen;
    if (ip)
        ip_seen++;
    if (eint)
        eint_seen++;
    if (firstpost || (calls & 0xff) == 0)
        // hch = raw hardware USBSTS.HCH(bit0): distinguishes "controller running" (hch=0) from the
        // storm-tame failure mode where usbxhci spins waiting for HCH=1 after a suppressed RS-clear.
        printf("HVLOG: CTLR-POST calls=%u ip_seen=%u eint_seen=%u erdp=0x%x usbsts=0x%x hch=%d firstpost=%d\n",
               calls, ip_seen, eint_seen, erdp, usbsts, usbsts & 1, firstpost ? 1 : 0);
}

// NWOAS FORK diagnostic (2026-07-13, session4). The multi-angle root-cause analysis found the §3.2
// wall is a MECHANISM mismatch: UEFI XhciDxe POLLS PORTSC MMIO to detect the port and issue Enable
// Slot; Windows usbxhci waits for a Port Status Change Event (TRB type 34) + SPI 698 that the FL1100
// never posts, so it storms HCRST and never issues a command (command ring DUMPED EMPTY). The ONE datum
// no boot ever captured: at the instant the event ring is empty, is interrupter-0 ARMED (IMAN.IE=1) and
// RUNNING (USBCMD.RS=1) while a DEVICE port has a change bit pending? The bridge/fldiag read PORT0
// (op+0x400) which has NO device; the mouse is op+0x490 (Low-speed) and the install USB op+0x4c0
// (SuperSpeed). This emits that correlated snapshot to decide the fix:
//   RS=1 & IE=1 & a device-port change pending & ring empty => BRANCH B (armed but FL1100 will not post
//     a fresh PSCE for a change latched before arm) => fix = hv generates the PSCE TRB + delivers 698.
//   RS=0 / IE=0 while a device change is pending           => BRANCH A (HCRST storm disarms first)
//     => fix = hv must tame the storm before the event can post.
// All FL1100-BAR reads are guarded (a mid-run PCI decode-off must SKIP, never reboot). Read-only.
static void nwoas_dump_fork(u64 elr)
{
    const u64 bar = 0x6c0000000UL; // j274 apcie bus2/dev0 FL1100 xHCI BAR0 (fixed by UEFI)
    enum exc_guard_t g = exc_guard;
    exc_count = 0;
    exc_guard = GUARD_SKIP | GUARD_SILENT;
    u32 caplen = read32(bar + 0x00) & 0xff;
    u32 hcs1 = read32(bar + 0x04); // HCSPARAMS1: MaxSlots[7:0]
    u32 hcs2 = read32(bar + 0x08); // HCSPARAMS2: MaxScratchpadBufs = (hi[31:27]<<5)|lo[25:21]
    u32 rtsoff = read32(bar + 0x18) & ~0x1fu;
    u64 op = bar + caplen, rt = bar + rtsoff;
    u32 usbcmd = read32(op + 0x00);
    u32 usbsts = read32(op + 0x04); // HCH b0, HSE b2, EINT b3, PCD b4, CNR b11, HCE b12
    u32 config = read32(op + 0x38); // MaxSlotsEn[7:0]
    u32 iman = read32(rt + 0x20);
    u32 erstsz = read32(rt + 0x28);
    u32 erdp = read32(rt + 0x38);
    u32 scratch = (((hcs2 >> 27) & 0x1f) << 5) | ((hcs2 >> 21) & 0x1f);
    // PORTSC are BAR-relative in the NWOAS-FLC trace (off = paddr - BAR): mouse @ BAR+0x490, install
    // USB @ BAR+0x4c0. (Equivalently op+0x410 / op+0x440, since op=BAR+caplen=BAR+0x80 and PORTSC[0]
    // = op+0x400 = BAR+0x480.) Use BAR-relative to match the verified offsets exactly.
    u32 p490 = read32(bar + 0x490); // mouse (Low-speed)
    u32 p4c0 = read32(bar + 0x4c0); // install USB (SuperSpeed)
    exc_guard = g;
    if (exc_count || caplen == 0 || caplen == 0xff) {
        printf("HVLOG: FORK read-fault/decode-off caplen=0x%x\n", caplen);
        return;
    }
    const u32 CHG = 0xfe0000u; // PORTSC change bits CSC(17)..CEC(23)
    // USBSTS decode: HCH=running-halted, HSE/HCE=controller/system error. The storm is the controller
    // HALTING (HCH=1) right after RS=1 -> capture HSE/HCE to see if it halts on an internal DMA error.
    printf("HVLOG: FORK RS=%d IE=%d IP=%d | USBSTS=0x%x HCH=%d HSE=%d HCE=%d CNR=%d | MaxSlotsEn=%u "
           "MaxSlots=%u Scratch=%u | ERSTSZ=%u ERDP=0x%x | mouse(490)=0x%x CCS=%d chg=%d | "
           "usb(4c0)=0x%x CCS=%d chg=%d | elr=0x%lx\n",
           usbcmd & 1, (iman >> 1) & 1, iman & 1, usbsts, usbsts & 1, (usbsts >> 2) & 1,
           (usbsts >> 12) & 1, (usbsts >> 11) & 1, config & 0xff, hcs1 & 0xff, scratch, erstsz, erdp,
           p490, p490 & 1, (p490 & CHG) != 0, p4c0, p4c0 & 1, (p4c0 & CHG) != 0, elr);
}

static void nwoas_dump_evtring(struct exc_info *ctx, u64 elr)
{
    if (!nwoas_fl_erstba)
        return;
    u64 erstba_ipa = nwoas_fl_erstba & ~0x3fUL;
    u64 erstba = nwoas_ipa_to_pa(erstba_ipa); // stage-2 xlate: handles low window AND identity RAM
    if (!erstba || !nwoas_in_dram(erstba, 16)) {
        printf("HVLOG: EVTDUMP erstba ipa=0x%lx -> pa=0x%lx not-in-DRAM [0x%lx,0x%lx)\n", erstba_ipa,
               erstba, ram_base, ram_base + mem_size_actual);
        return;
    }
    // ERST[0] = { ring seg base (8B @+0, low6 rsvd), ring seg size (16b @+8) }. Guest-authored ->
    // clean+invalidate so we read the guest's latest value.
    nwoas_cache_maint(erstba, 16, true);
    u64 e0 = 0, e1 = 0;
    if (!hv_pa_read(ctx, erstba, &e0, 3) || !hv_pa_read(ctx, erstba + 8, &e1, 3)) {
        printf("HVLOG: EVTDUMP erst-fault erstba(pa)=0x%lx\n", erstba);
        return;
    }
    u64 ring_ipa = e0 & ~0x3fUL; // ring segment base is also a guest IPA -> translate per TRB below
    u32 rsize = (u32)(e1 & 0xffff);
    printf("HVLOG: EVTDUMP erstba ipa=0x%lx pa=0x%lx ring(ipa)=0x%lx size=%u dcbaap=0x%lx elr=0x%lx\n",
           erstba_ipa, erstba, ring_ipa, rsize, nwoas_fl_dcbaap, elr);
    if (!ring_ipa || rsize == 0 || rsize > 4096)
        return;
    u32 n = rsize < 12 ? rsize : 12;
    for (u32 i = 0; i < n; i++) {
        u64 a_ipa = ring_ipa + (u64)i * 16;
        u64 a = nwoas_ipa_to_pa(a_ipa); // per-TRB xlate (segment may straddle a page boundary)
        if (!a || !nwoas_in_dram(a, 16)) {
            printf("HVLOG: EVTDUMP TRB[%u] ring ipa=0x%lx -> pa=0x%lx not-in-DRAM\n", i, a_ipa, a);
            break;
        }
        // (1) CACHED view: what the guest CPU currently sees (its possibly-stale copy). On the same
        //     core the guest hot-polls this ring during the livelock, so EL2's read hits that line.
        u64 cp = 0, csc = 0;
        bool cok = hv_pa_read(ctx, a, &cp, 3) && hv_pa_read(ctx, a + 8, &csc, 3);
        // (2) invalidate-ONLY (never clean -> never clobber FL1100's DMA), then re-read = DRAM view.
        nwoas_cache_maint(a, 16, false);
        u64 dp = 0, dsc = 0;
        bool dok = hv_pa_read(ctx, a, &dp, 3) && hv_pa_read(ctx, a + 8, &dsc, 3);
        if (!cok || !dok) {
            printf("HVLOG: EVTDUMP TRB[%u] read-fault\n", i);
            break;
        }
        u32 dctrl = (u32)(dsc >> 32), cctrl = (u32)(csc >> 32);
        u32 dstatus = (u32)dsc;
        // DRAM view is authoritative for FL1100's write. cached!=DRAM divergence = direct proof of
        // COHERENCE (guest reads stale). All-zero DRAM across the ring = REACHABILITY (or DART
        // misroute, §6-B). Garbage TRB type = wrong translation. (CC=status[31:24], type=
        // ctrl[15:10], cycle=ctrl[0].)
        printf("HVLOG: EVTDUMP TRB[%u] dram ptr=0x%lx cc=%u type=%u cyc=%u ctrl=0x%x | cache ptr=0x%lx ctrl=0x%x %s\n",
               i, dp, (dstatus >> 24) & 0xff, (dctrl >> 10) & 0x3f, dctrl & 1, dctrl, cp, cctrl,
               (dp != cp || dctrl != cctrl) ? "DIVERGE=COHERENCE" : "same");
    }

    // NWOAS §6-D DIAGNOSTIC — dump the COMMAND ring (guest->device READ direction). The event ring
    // above is the device->guest WRITE direction; it is empty-and-coherent, which cannot by itself
    // separate reachability from command-READ coherence. So also read the command ring the guest
    // authored: cached view (what usbxhci wrote) vs DRAM view (what FL1100 would DMA-read).
    //   cache has valid TRBs but DRAM is stale/zero  => COMMAND-READ COHERENCE (FL1100 reads stale
    //       empty commands -> never completes -> empty event ring). => §6-D NC-remap is the fix.
    //   DRAM has the same valid TRBs as cache        => reachability/DART (FL1100 gets the command
    //       but cannot post the completion, or cannot fetch despite valid DRAM). => revisit DART.
    // crcr (op+0x18): bits[63:6]=command ring pointer, bit0=RCS. Enable Slot=type9, Addr Dev=type11.
    if (nwoas_fl_crcr) {
        u64 cr_ipa = nwoas_fl_crcr & ~0x3fUL;
        u64 cr = nwoas_ipa_to_pa(cr_ipa);
        if (!cr || !nwoas_in_dram(cr, 16)) {
            printf("HVLOG: CMDDUMP crcr ipa=0x%lx -> pa=0x%lx not-in-DRAM\n", cr_ipa, cr);
        } else {
            printf("HVLOG: CMDDUMP crcr ipa=0x%lx pa=0x%lx rcs=%lu\n", cr_ipa, cr,
                   nwoas_fl_crcr & 1);
            for (u32 i = 0; i < 8; i++) {
                u64 a = nwoas_ipa_to_pa(cr_ipa + (u64)i * 16);
                if (!a || !nwoas_in_dram(a, 16)) {
                    printf("HVLOG: CMDDUMP TRB[%u] not-in-DRAM\n", i);
                    break;
                }
                u64 cp = 0, csc = 0;
                bool cok = hv_pa_read(ctx, a, &cp, 3) && hv_pa_read(ctx, a + 8, &csc, 3);
                nwoas_cache_maint(a, 16, false); // invalidate-only -> DRAM view (FL1100's read source)
                u64 dp = 0, dsc = 0;
                bool dok = hv_pa_read(ctx, a, &dp, 3) && hv_pa_read(ctx, a + 8, &dsc, 3);
                if (!cok || !dok) {
                    printf("HVLOG: CMDDUMP TRB[%u] read-fault\n", i);
                    break;
                }
                u32 dctrl = (u32)(dsc >> 32), cctrl = (u32)(csc >> 32);
                printf("HVLOG: CMDDUMP TRB[%u] dram ptr=0x%lx type=%u cyc=%u ctrl=0x%x | cache "
                       "ptr=0x%lx type=%u ctrl=0x%x %s\n",
                       i, dp, (dctrl >> 10) & 0x3f, dctrl & 1, dctrl, cp, (cctrl >> 10) & 0x3f, cctrl,
                       (dp != cp || dctrl != cctrl) ? "DIVERGE=CMD-COHERENCE" : "same");
            }
        }
    }
}

/* ==== NWOAS §6-C reachability FIX — outer-hv apcie-DART identity-map maintainer ================
 * ROOT CAUSE (proven kmutil-free, 2026-07-12): the FL1100 xHCI's apcie DART (0x682008000, stream 1)
 * is in TRANSLATE mode (correct — the apcie DART does NOT support bypass, per AppleWOA UEFI source),
 * but its page tables lack the identity L2 mapping for the high-RAM regions where Windows places the
 * xHCI DMA structures (event ring / DCBAA / device contexts / command+transfer rings). FL1100's DMA
 * to those IOVAs hits NO_PTE and is silently dropped -> the event ring never receives completion
 * TRBs -> usbxhci livelocks re-resetting every port -> no enumeration, no cursor.
 * (fix-C's DART BYPASS was fundamentally wrong: it turned OFF the only working path, TRANSLATE.)
 * FIX: from EL2 (stable, unlike the Python break-in that crashed), WIRE identity L2 tables into the
 * DART L1 for the regions the guest's ring pointers land in, keep TRANSLATE on, and re-assert on
 * each (kernel-gated) ERSTBA/DCBAAP/CRCR write since the livelock HCRST / Windows re-inits the DART.
 * The IOVA == host PA here (guest high RAM is identity-mapped), so the leaf PTE is a plain identity.
 */
#define FL_DART_BASE              0x682008000UL
#define FL_DART_SID               1
#define DART_TCR_OFF              0x100
#define DART_TTBR_OFF             0x200
#define DART_TCR_TRANSLATE        0x80
#define DART_STREAM_SELECT        0x34
#define DART_STREAM_CMD           0x20
#define DART_CMD_INVAL            (1u << 20)
#define DART_CMD_BUSY             (1u << 2)
#define DART_L2_IDENTITY(pa)      (((pa) & ~0x3fffUL) | 0x3UL | (0xfffUL << 40)) // leaf: VALID|SP|addr
#define DART_L1_POINTER(l2)       (((l2) & ~0x3fffUL) | 0x1UL) // L1 -> L2 table: VALID only (dart.c)
#define NWOAS_DART_POOL           48

// Session 6 path-B seed.  Keep the guest's Windows ERST untouched for diagnosis,
// but point the physical FL1100 interrupter at one EL2-owned 16KB DART page below
// 4GB.  ERST occupies the first 4KB and its event segment the next 4KB.  If the
// controller posts here while the Windows high ring remains empty, the remaining
// fault is specifically the high-IOVA device-write path.  This is a seed/probe;
// it does not mirror events into the guest ring or claim working Windows USB.
#define NWOAS_LOW_EVENT_SEED       0
#define NWOAS_LOW_EVENT_MIRROR     1
#define NWOAS_SEED_PAGE_IOVA       0xffffc000UL
#define NWOAS_SEED_RING_IOVA       (NWOAS_SEED_PAGE_IOVA + 0x1000UL)
#define NWOAS_SEED_RING_TRBS       256

// Exported for the IRQ bridge. They stay false when the D41 low-allocation
// experiment compiles the seed/proxy out.
bool nwoas_seed_guest_pending = false;
bool nwoas_seed_guest_irq_sent = false;

#if NWOAS_LOW_EVENT_SEED
static u8 nwoas_seed_page[0x4000] __attribute__((aligned(0x4000)));
static bool nwoas_seed_active = false;
static bool nwoas_seed_seen = false;
static u32 nwoas_seed_generation = 0;
static u32 nwoas_seed_deq = 0;
static u8 nwoas_seed_ccs = 1;
static u32 nwoas_seed_guest_enq = 0;
static u8 nwoas_seed_guest_pcs = 1;
static u32 nwoas_seed_mirrored = 0;
#endif

// Static, 16KB-aligned L2 tables (768KB BSS). No heap dependency; each maps one 32MB region.
static u64 nwoas_dart_l2[NWOAS_DART_POOL][2048] __attribute__((aligned(0x4000)));
static u32 nwoas_dart_pool_used = 0;
static DECLARE_SPINLOCK(nwoas_dart_lock); // serialize fixup across guest SMP vCPUs

static bool nwoas_ram(u64 a, size_t len)
{
    return a >= ram_base && (a + len) > a && (a + len) <= (ram_base + mem_size_actual);
}

// Wire an identity L2 table into DART L1 for the 32MB region containing `iova`, if that L1 entry is
// currently invalid. Idempotent (skips mapped regions and low IOVA, which UEFI's flat L2 covers).
// The DART L1 table is guest RAM: use hv_pa_read/hv_pa_write (fault-guarded — a carveout hole is
// skipped, not a reboot) rather than raw LDR/STR.
static void nwoas_dart_wire(struct exc_info *ctx, u64 l1phys, u64 iova)
{
    if ((iova >> 32) == 0)
        return; // IOVA < 4GB is already mapped by UEFI's flat window L2
    u64 l1i = (iova >> 25) & 0x7ff;
    u64 l1ea = l1phys + 8 * l1i;
    if (!nwoas_ram(l1ea, 8))
        return;
    u64 l1e = 0;
    if (!hv_pa_read(ctx, l1ea, &l1e, 3))
        return; // faulted (e.g. carveout hole) -> skip, no reboot
    if (l1e & 1)
        return; // region already mapped (by UEFI or a prior wire)
    if (nwoas_dart_pool_used >= NWOAS_DART_POOL)
        return;
    u64 *l2 = nwoas_dart_l2[nwoas_dart_pool_used++];
    u64 region = l1i << 25;
    for (u64 j = 0; j < 2048; j++)
        l2[j] = DART_L2_IDENTITY(region + j * 0x4000);
    nwoas_cache_maint((u64)l2, 0x4000, true);      // flush the L2 table so the DART reads it from DRAM
    u64 ent = DART_L1_POINTER((u64)l2);
    if (!hv_pa_write(ctx, l1ea, &ent, 3)) {
        nwoas_dart_pool_used--; // undo the pool consume if the L1 write faulted
        return;
    }
    nwoas_cache_maint(l1ea & ~0x3fUL, 64, true);   // flush the L1 entry line
}

// NWOAS session4 (2026-07-13) — FL1100 high-address DMA fix (Opus DART review, appark-2055).
// ROOT CAUSE (strong hypothesis): the FL1100 claims AC64=1 but effectively TRUNCATES DMA addresses to
// 32 bits. Windows places its xHCI structures HIGH (ERSTBA=0xa_e0fc9000, upper32=0xA); the card drops
// the upper 0xA and DMAs to IOVA 0xe0fc9000, which the FL1100 DART's flat low window (IOVA 0..4GB ->
// PA 0xAE0FCC000 linearly) maps to a zeroed carve-out page (NOT the real structure, and NO fault). So
// the card reads a garbage ERST, never finds the event ring, posts no events -> usbxhci 10s timeout ->
// HCRST storm. UEFI worked only because ITS structures were already <4GB (nothing to truncate).
// FIX: for each real high structure PA, overwrite the flat-window L2 entry at the TRUNCATED-low index
// so the card's truncated DMA reaches the REAL high PA. Pure DART leaf edit (no cacheability change ->
// unrelated to the banned NC-remap granule hazard). This is BOTH the fix and the decisive diagnostic:
// if the event ring starts posting real TRBs (IMAN.IP->1, storm stops) the truncation hypothesis is
// confirmed; if nothing changes it is refuted (-> coherence or refuses-high).
#define NWOAS_DART_LOWALIAS 1
#if NWOAS_DART_LOWALIAS
// Alias ONE 16KB page: map the truncated-low IOVA (real_pa & 0xffffffff) to the REAL high PA by
// overwriting the existing flat-window L2 leaf at the truncated index. l1phys = FL1100 DART L1 table.
static void nwoas_dart_alias_low(struct exc_info *ctx, u64 l1phys, u64 real_pa)
{
    u64 trunc = real_pa & 0xffffffffUL;      // what the (32-bit-truncating) FL1100 actually puts on the bus
    u64 l1i = (trunc >> 25) & 0x7ff;         // flat-window L1 index (0..127 for IOVA<4GB)
    u64 l1ea = l1phys + 8 * l1i;
    if (!nwoas_ram(l1ea, 8))
        return;
    u64 l1e = 0;
    if (!hv_pa_read(ctx, l1ea, &l1e, 3))
        return;
    if (!(l1e & 1))
        return;                              // flat window must already map this region (UEFI built it)
    u64 l2phys = (l1e & ~0x3fffUL) + 8 * ((trunc >> 14) & 0x7ff); // the L2 leaf for this 16KB page
    if (!nwoas_ram(l2phys, 8))
        return;
    u64 ent = DART_L2_IDENTITY(real_pa);     // leaf built from the REAL high PA, installed at trunc index
    if (!hv_pa_write(ctx, l2phys, &ent, 3))
        return;
    nwoas_cache_maint(l2phys & ~0x3fUL, 64, true);
}
#endif

#if NWOAS_LOW_EVENT_SEED
// Replace one already-present flat-window DART leaf with a low IOVA -> EL2-owned
// physical page mapping.  The page is restored by the next cold boot.  With
// NWOAS_LOW_EVENT_MIRROR enabled, controller-authored events are copied in
// producer order to Windows's original event ring.
static bool nwoas_dart_map_seed(struct exc_info *ctx, u64 l1phys)
{
    const u64 iova = NWOAS_SEED_PAGE_IOVA;
    const u64 pa = (u64)nwoas_seed_page;
    u64 l1ea = l1phys + 8 * ((iova >> 25) & 0x7ff);
    u64 l1e = 0;

    if (!nwoas_ram(pa, sizeof(nwoas_seed_page)) || !nwoas_ram(l1ea, 8) ||
        !hv_pa_read(ctx, l1ea, &l1e, 3) || !(l1e & 1))
        return false;

    u64 l2ea = (l1e & ~0x3fffUL) + 8 * ((iova >> 14) & 0x7ff);
    u64 ent = DART_L2_IDENTITY(pa);
    if (!nwoas_ram(l2ea, 8) || !hv_pa_write(ctx, l2ea, &ent, 3))
        return false;
    nwoas_cache_maint(l2ea & ~0x3fUL, 64, true);
    return true;
}

// Reassert the low ring after an ERDP half-write or immediately before R/S=1.
// Windows keeps its own register/ring model; the physical controller consumes
// this low ring and the mirror publishes completed TRBs into Windows's ring.
static void nwoas_seed_restore_regs(void)
{
    const u64 rt = 0x6c0000000UL + 0x2000;

    if (!nwoas_seed_active)
        return;
    write32(rt + 0x28, 1); // ERSTSZ
    write32(rt + 0x30, (u32)NWOAS_SEED_PAGE_IOVA);
    write32(rt + 0x34, 0);
    write32(rt + 0x38, (u32)(NWOAS_SEED_RING_IOVA + (u64)nwoas_seed_deq * 16));
    write32(rt + 0x3c, 0);
}

static void nwoas_seed_arm(struct exc_info *ctx)
{
    u32 ttbr = read32(FL_DART_BASE + DART_TTBR_OFF + 16 * FL_DART_SID);
    if (!(ttbr & 0x80000000)) {
        printf("HVLOG: EVTSEED arm failed: no valid DART TTBR\n");
        return;
    }

    u64 l1phys = ((u64)(ttbr & 0x7fffffff)) << 12;
    memset(nwoas_seed_page, 0, sizeof(nwoas_seed_page));
    *(u64 *)&nwoas_seed_page[0] = NWOAS_SEED_RING_IOVA;
    *(u32 *)&nwoas_seed_page[8] = NWOAS_SEED_RING_TRBS;
    nwoas_cache_maint((u64)nwoas_seed_page, sizeof(nwoas_seed_page), true);

    spin_lock(&nwoas_dart_lock);
    write32(FL_DART_BASE + DART_TCR_OFF + 4 * FL_DART_SID, DART_TCR_TRANSLATE);
    bool mapped = nwoas_dart_map_seed(ctx, l1phys);
    if (mapped) {
        write32(FL_DART_BASE + DART_STREAM_SELECT, 0xffffffff);
        write32(FL_DART_BASE + DART_STREAM_CMD, DART_CMD_INVAL);
        for (int i = 0; i < 1000 &&
                        (read32(FL_DART_BASE + DART_STREAM_CMD) & DART_CMD_BUSY); i++)
            ;
    }
    spin_unlock(&nwoas_dart_lock);

    if (!mapped) {
        printf("HVLOG: EVTSEED arm failed: map iova=0x%lx pa=0x%lx l1=0x%lx\n",
               NWOAS_SEED_PAGE_IOVA, (u64)nwoas_seed_page, l1phys);
        return;
    }

    nwoas_seed_active = true;
    nwoas_seed_seen = false;
    nwoas_seed_deq = 0;
    nwoas_seed_ccs = 1;
    nwoas_seed_guest_enq = 0;
    nwoas_seed_guest_pcs = 1;
    nwoas_seed_guest_pending = false;
    nwoas_seed_guest_irq_sent = false;
    nwoas_seed_mirrored = 0;
    nwoas_seed_generation++;
    nwoas_seed_restore_regs();
    printf("HVLOG: EVTSEED armed gen=%u erst_iova=0x%lx ring_iova=0x%lx pa=0x%lx\n",
           nwoas_seed_generation, NWOAS_SEED_PAGE_IOVA, NWOAS_SEED_RING_IOVA,
           (u64)nwoas_seed_page);
}

static void nwoas_seed_poll(struct exc_info *ctx)
{
    if (!nwoas_seed_active)
        return;

    // Device-authored memory: invalidate only, never clean stale zeros over DMA.
    nwoas_cache_maint((u64)&nwoas_seed_page[0x1000], 0x1000, false);
    for (u32 pass = 0; pass < 16; pass++) {
        u32 i = nwoas_seed_deq;
        u32 *trb = (u32 *)&nwoas_seed_page[0x1000 + i * 16];
        u32 ctrl = trb[3];
        u32 type = (ctrl >> 10) & 0x3f;
        if ((ctrl & 1) != nwoas_seed_ccs || !type)
            break;

        if (!nwoas_seed_seen) {
            const u64 bar = 0x6c0000000UL;
            u32 sts = read32(bar + 0x84);
            u32 iman = read32(bar + 0x2020);
            nwoas_seed_seen = true;
            printf("HVLOG: EVTSEED POSTED gen=%u idx=%u type=%u cyc=%u cc=%u ptr=0x%x%08x "
                   "status=0x%x ctrl=0x%x usbsts=0x%x iman=0x%x\n",
                   nwoas_seed_generation, i, type, ctrl & 1, trb[2] >> 24,
                   trb[1], trb[0], trb[2], ctrl, sts, iman);
        }

#if NWOAS_LOW_EVENT_MIRROR
        // Resolve Windows interrupter-0's event segment from its untouched
        // guest ERST and copy the controller-authored TRB there. Publish DW3
        // (which carries the cycle bit) last, as required by xHCI.
        u64 erst_pa = nwoas_ipa_to_pa(nwoas_fl_erstba & ~0x3fUL);
        u64 ring_ipa = 0, erst_meta = 0;
        if (!erst_pa || !nwoas_in_dram(erst_pa, 16))
            break;
        nwoas_cache_maint(erst_pa, 16, true);
        if (!hv_pa_read(ctx, erst_pa, &ring_ipa, 3) ||
            !hv_pa_read(ctx, erst_pa + 8, &erst_meta, 3))
            break;
        ring_ipa &= ~0x3fUL;
        u32 ring_trbs = (u32)(erst_meta & 0xffff);
        if (!ring_ipa || !ring_trbs || ring_trbs > 4096 ||
            nwoas_seed_guest_enq >= ring_trbs)
            break;
        u64 guest_trb = nwoas_ipa_to_pa(ring_ipa + (u64)nwoas_seed_guest_enq * 16);
        if (!guest_trb || !nwoas_in_dram(guest_trb, 16))
            break;
        u64 lo = *(u64 *)&trb[0];
        u32 status = trb[2];
        u32 guest_ctrl = (ctrl & ~1u) | nwoas_seed_guest_pcs;
        u64 hi = (u64)status | ((u64)guest_ctrl << 32);
        if (!hv_pa_write(ctx, guest_trb, &lo, 3))
            break;
        sysop("dmb ish");
        if (!hv_pa_write(ctx, guest_trb + 8, &hi, 3))
            break;
        sysop("dsb ish");
        nwoas_cache_maint(guest_trb, 16, true);
        nwoas_seed_guest_pending = true;
        if (nwoas_seed_mirrored < 64)
            printf("HVLOG: EVTMIRROR gen=%u src=%u dst=%u type=%u cc=%u pcs=%u guest=0x%lx\n",
                   nwoas_seed_generation, i, nwoas_seed_guest_enq, type,
                   status >> 24, nwoas_seed_guest_pcs, guest_trb);
        nwoas_seed_mirrored++;
        nwoas_seed_guest_enq++;
        if (nwoas_seed_guest_enq == ring_trbs) {
            nwoas_seed_guest_enq = 0;
            nwoas_seed_guest_pcs ^= 1;
        }
#endif

        // Acknowledge this low-ring event to the physical controller and move
        // its dequeue pointer. Bit 3 clears Event Handler Busy.
        nwoas_seed_deq++;
        if (nwoas_seed_deq == NWOAS_SEED_RING_TRBS) {
            nwoas_seed_deq = 0;
            nwoas_seed_ccs ^= 1;
        }
        const u64 rt = 0x6c0000000UL + 0x2000;
        write32(rt + 0x38,
                (u32)(NWOAS_SEED_RING_IOVA + (u64)nwoas_seed_deq * 16) | 0x8);
        write32(rt + 0x3c, 0);
    }
}
#endif

// Ensure the FL1100 DART identity-maps the high-RAM regions of the latched ring pointers. Keeps
// TRANSLATE on (never bypass), wires a +/-256MB window around each pointer to catch adjacent device
// contexts / transfer rings, then TLB-invalidates. Cheap after the first pass (wire is idempotent).
static void nwoas_dart_fixup(struct exc_info *ctx)
{
    u32 ttbr = read32(FL_DART_BASE + DART_TTBR_OFF + 16 * FL_DART_SID);
    if (!(ttbr & 0x80000000))
        return; // no valid TTBR yet
    u64 l1phys = ((u64)(ttbr & 0x7fffffff)) << 12;
    if (!nwoas_ram(l1phys, 0x4000))
        return;
    spin_lock(&nwoas_dart_lock);
    write32(FL_DART_BASE + DART_TCR_OFF + 4 * FL_DART_SID, DART_TCR_TRANSLATE); // keep TRANSLATE on
    u64 ptrs[3] = {nwoas_fl_erstba & ~0x3fUL, nwoas_fl_dcbaap & ~0x3fUL, nwoas_fl_crcr & ~0x3fUL};
    for (int k = 0; k < 3; k++) {
        if ((ptrs[k] >> 32) == 0)
            continue;
        u64 base = ptrs[k] & ~((1UL << 25) - 1);
        nwoas_dart_wire(ctx, l1phys, base); // the pointer's OWN region first (survives pool exhaustion)
        for (int w = 1; w <= 8; w++) {       // then the +/-256MB window (adjacent contexts / rings)
            nwoas_dart_wire(ctx, l1phys, base + ((u64)w << 25));
            nwoas_dart_wire(ctx, l1phys, base - ((u64)w << 25));
        }
    }
#if NWOAS_DART_LOWALIAS
    // THE FIX: for each real high structure, alias its TRUNCATED-low IOVA -> real high PA (see
    // nwoas_dart_alias_low). +/-256-page (4MB) window around each pointer to also catch adjacent device
    // contexts / transfer rings; widen if a later wall shows up at context/transfer-ring DMA.
    static u32 nwoas_alias_log = 0;
    for (int k = 0; k < 3; k++) {
        if ((ptrs[k] >> 32) == 0)
            continue;
        u64 pg = ptrs[k] & ~0x3fffUL;
        // First-test window: +/-16 pages (+/-256KB) — the enumeration structures (erstba/ring/dcbaap/
        // crcr) all sit within ~0x5000 of each other, so this covers them while keeping the spinlock
        // critical section short (~33 leaf writes/ptr, not 513) to avoid an SMP-latch wedge. Widen later
        // if a wall appears at device-context / transfer-ring DMA (those may land further out).
        for (s64 off = -16L * 0x4000; off <= 16L * 0x4000; off += 0x4000)
            nwoas_dart_alias_low(ctx, l1phys, pg + (u64)off);
        if (nwoas_alias_log < 6) {
            nwoas_alias_log++;
            printf("HVLOG: DART-LOWALIAS ptr=0x%lx trunc=0x%lx +/-4MB\n", ptrs[k],
                   ptrs[k] & 0xffffffffUL);
        }
    }
#endif
    write32(FL_DART_BASE + DART_STREAM_SELECT, 0xffffffff);
    write32(FL_DART_BASE + DART_STREAM_CMD, DART_CMD_INVAL);
    for (int i = 0; i < 1000 && (read32(FL_DART_BASE + DART_STREAM_CMD) & DART_CMD_BUSY); i++)
        ;
    spin_unlock(&nwoas_dart_lock);
}

// NWOAS §6-D DIAGNOSTIC — is the FL1100 apcie DART (0x682008000 s1) actually mapping the ring IOVAs
// at RUNTIME (not the static dartcheck), and is it faulting? Plus DCBAA slot allocation + USBCMD RS.
// Decisive reachability probe: if the ring IOVA has no valid DART PTE, or the DART ERROR flag is set
// on it, the FL1100 cannot DMA events -> empty ring is REACHABILITY, and the DART fixup isn't taking.
static void nwoas_dump_dart_dcbaa(struct exc_info *ctx, u64 elr)
{
    (void)elr;
    // FL1100 DART stream-1 registers.
    u32 tcr  = read32(FL_DART_BASE + DART_TCR_OFF + 4 * FL_DART_SID);
    u32 ttbr = read32(FL_DART_BASE + DART_TTBR_OFF + 16 * FL_DART_SID);
    u32 err  = read32(FL_DART_BASE + 0x40);
    u32 errl = read32(FL_DART_BASE + 0x50);
    u32 errh = read32(FL_DART_BASE + 0x54);
    printf("HVLOG: DARTDIAG s1 tcr=0x%x ttbr=0x%x err=0x%x errlo=0x%x errhi=0x%x\n",
           tcr, ttbr, err, errl, errh);
    // NWOAS session5 (2026-07-14): decode the T8020 DART ERROR register the way m1n1's own dart8020
    // show_error() does -- FLAG(bit31) is the "valid latched error" bit. Observed err=0xd000400 has
    // FLAG=0 (no valid latch), STREAM=13 (NOT FL1100 stream 1), and no fault-cause bit set -> it is a
    // STALE leftover, not live FL1100 truncation. Read-only; the raw line above is kept for continuity.
    {
        u32 e_flag = (err >> 31) & 1, e_stream = (err >> 24) & 0xf, e_code = err & 0xffffff;
        printf("HVLOG: DARTERR %s stream=%u(isFL1100=%d) nottbr=%d nopmd=%d nopte=%d wr=%d rd=%d "
               "code=0x%x lo=0x%x hi=0x%x\n",
               e_flag ? "LIVE-FAULT(FLAG=1)" : "STALE(FLAG=0,ignore)", e_stream,
               e_stream == FL_DART_SID, e_code & 1, (e_code >> 1) & 1, (e_code >> 2) & 1,
               (e_code >> 3) & 1, (e_code >> 4) & 1, e_code, errl, errh);
    }
    // Walk the DART PTE for each ring IOVA (== guest PA, high-RAM identity). L1i=(iova>>25)&0x7ff,
    // L2i=(iova>>14)&0x7ff, 16KB leaf. A valid identity leaf = DART_L2_IDENTITY(iova).
    u64 iovas[3] = {nwoas_fl_erstba & ~0x3fUL, nwoas_fl_dcbaap & ~0x3fUL, nwoas_fl_crcr & ~0x3fUL};
    const char *nm[3] = {"erstba", "dcbaap", "crcr"};
    if (ttbr & 0x80000000) {
        u64 l1phys = ((u64)(ttbr & 0x7fffffff)) << 12;
        for (int k = 0; k < 3; k++) {
            u64 iova = iovas[k];
            if (!iova) continue;
            u64 l1i = (iova >> 25) & 0x7ff, l2i = (iova >> 14) & 0x7ff;
            u64 l1e = 0, l2e = 0;
            bool ok1 = nwoas_ram(l1phys + 8 * l1i, 8) && hv_pa_read(ctx, l1phys + 8 * l1i, &l1e, 3);
            u64 l2phys = l1e & ~0x3fffUL;
            bool ok2 = (l1e & 1) && nwoas_ram(l2phys + 8 * l2i, 8) &&
                       hv_pa_read(ctx, l2phys + 8 * l2i, &l2e, 3);
            u64 want = DART_L2_IDENTITY(iova);
            printf("HVLOG: DARTDIAG %s iova=0x%lx l1[%lu]=0x%lx l2[%lu]=0x%lx %s%s\n",
                   nm[k], iova, l1i, ok1 ? l1e : 0, l2i, ok2 ? l2e : 0,
                   (ok2 && (l2e & 1)) ? "MAPPED" : "NO-PTE",
                   (ok2 && l2e == want) ? "-IDENTITY-OK" : (ok2 && (l2e & 1)) ? "-WRONG-TARGET" : "");
        }
    } else {
        printf("HVLOG: DARTDIAG s1 TTBR invalid (no page table)\n");
    }
    // Session4 Path-B TRUNCATION PROBE (Opus review 2026-07-13). Hypothesis: the FL1100 truncates the
    // upper-32 IOVA bits, so its DMA to a high structure (0xA_e0fcXXXX) actually lands at the LOW iova
    // (0xe0fcXXXX), which the DART low-window resolves to the backing page -- not the real ring. For each
    // latched pointer, walk the LIVE DART for its TRUNCATED (low-32) form, resolve the target PA,
    // invalidate-only (dc ivac, NEVER civac -- civac would write stale cached zeros over a device DMA
    // write), and dump 64B as event/completion TRBs. Real FL1100 TRBs (type 32/33/34) at a truncated
    // target == truncation CONFIRMED + the exact landing PA the fix must alias. Read-only.
    if (ttbr & 0x80000000) {
        u64 l1phys = ((u64)(ttbr & 0x7fffffff)) << 12;
        u64 ringseg = 0, ep = nwoas_ipa_to_pa(nwoas_fl_erstba & ~0x3fUL);
        if (ep && nwoas_in_dram(ep, 8)) {
            u64 e0 = 0;
            if (hv_pa_read(ctx, ep, &e0, 3))
                ringseg = e0 & ~0x3fUL;
        }
        u64 tp[4] = {nwoas_fl_erstba & ~0x3fUL, ringseg, nwoas_fl_dcbaap & ~0x3fUL, nwoas_fl_crcr & ~0x3fUL};
        const char *tn[4] = {"erstba", "ringseg", "dcbaap", "crcr"};
        for (int k = 0; k < 4; k++) {
            u64 hi = tp[k];
            if (!hi || (hi >> 32) == 0)
                continue; // only high pointers can be truncated
            u64 trunc = hi & 0xffffffffUL;
            u64 l1i = (trunc >> 25) & 0x7ff, l2i = (trunc >> 14) & 0x7ff, l1e = 0, l2e = 0;
            bool ok1 = nwoas_ram(l1phys + 8 * l1i, 8) && hv_pa_read(ctx, l1phys + 8 * l1i, &l1e, 3);
            u64 l2phys = l1e & ~0x3fffUL;
            bool ok2 = ok1 && (l1e & 1) && nwoas_ram(l2phys + 8 * l2i, 8) &&
                       hv_pa_read(ctx, l2phys + 8 * l2i, &l2e, 3);
            u64 tgt = (ok2 && (l2e & 1)) ? (l2e & ~0x3fffUL & ~(0xfffUL << 40)) : 0;
            printf("HVLOG: TRUNC %s hi=0x%lx trunc=0x%lx -> dartPA=0x%lx %s\n", tn[k], hi, trunc, tgt,
                   tgt ? "" : "NO-PTE");
            if (tgt && nwoas_in_dram(tgt, 64)) {
                nwoas_cache_maint(tgt, 64, false); // dc ivac only -- read the DEVICE's DRAM view
                for (int t = 0; t < 4; t++) {
                    u64 w0 = 0, w1 = 0;
                    if (hv_pa_read(ctx, tgt + t * 16, &w0, 3) &&
                        hv_pa_read(ctx, tgt + t * 16 + 8, &w1, 3))
                        printf("HVLOG: TRUNC %s TRB[%d] pa=0x%lx w0=0x%lx type=%u cc=%u cyc=%u\n", tn[k], t,
                               tgt + t * 16, w0, (u32)((w1 >> 42) & 0x3f), (u32)((w1 >> 24) & 0xff),
                               (u32)((w1 >> 32) & 1));
                }
            }
        }
    }
    // DCBAA: did usbxhci allocate a device slot? DCBAA[0]=scratchpad, [1..]=device context ptrs.
    if (nwoas_fl_dcbaap) {
        u64 dp = nwoas_ipa_to_pa(nwoas_fl_dcbaap & ~0x3fUL);
        if (dp && nwoas_in_dram(dp, 0x20)) {
            nwoas_cache_maint(dp, 0x20, true);
            u64 spa_iova = 0;
            for (int i = 0; i < 4; i++) {
                u64 e = 0;
                if (hv_pa_read(ctx, dp + 8 * i, &e, 3)) {
                    printf("HVLOG: DCBAADIAG [%d]=0x%lx\n", i, e);
                    if (i == 0)
                        spa_iova = e; // DCBAA[0] = Scratchpad Buffer Array base
                }
            }
            // SCRATCHPAD: the controller DMA-reads DCBAA[0] -> the Scratchpad Buffer Array -> each
            // buffer on RS=1 to init internal state. If those are unreachable/stale it can HALT (HCH=1)
            // immediately after RS=1 = exactly this storm's root. Deref + check reachability of the
            // array and the first buffers. (Guest-authored -> clean to read the value it wrote.)
            u64 spa_pa = spa_iova ? nwoas_ipa_to_pa(spa_iova & ~0x3fUL) : 0;
            printf("HVLOG: SCRATCH array ipa=0x%lx pa=0x%lx reachable=%d\n", spa_iova & ~0x3fUL, spa_pa,
                   spa_pa && nwoas_in_dram(spa_pa, 0x40));
            if (spa_pa && nwoas_in_dram(spa_pa, 0x40)) {
                nwoas_cache_maint(spa_pa, 0x40, true);
                for (int i = 0; i < 3; i++) {
                    u64 buf = 0;
                    if (hv_pa_read(ctx, spa_pa + 8 * i, &buf, 3) && buf) {
                        u64 buf_pa = nwoas_ipa_to_pa(buf & ~0xfffUL);
                        printf("HVLOG: SCRATCH buf[%d] ipa=0x%lx pa=0x%lx reachable=%d\n", i,
                               buf & ~0xfffUL, buf_pa, buf_pa && nwoas_in_dram(buf_pa, 0x1000));
                    }
                }
            }
        }
    }
}

/* ==== NWOAS §6-C coherence FIX — software DMA coherence for the FL1100's cacheable rings ========
 * The apcie/DART DMA path is NOT cache-coherent for the guest under EL2 stage-2 (AppleWOA UEFI
 * source AppleDartIoMmuDxe.c:909). UEFI's IOMMU driver compensates (allocates the xHCI rings
 * Write-Combining + per-Map clean/invalidate); Windows uses its own DMA allocator and allocates
 * CACHEABLE rings, so: usbxhci's command TRBs stick in CPU cache and never reach DRAM -> the FL1100
 * DMA-reads STALE (empty) commands -> posts no completion -> usbxhci command times out -> HCRST ->
 * device drops -> reset storm forever. (Our §6-A "reachability" read only tested the event-ring
 * WRITE direction; the real choke is the command-READ direction, untested there.)
 * FIX (device-agnostic, in the hv trap, mirroring UEFI): on the guest's DOORBELL write (which makes
 * the FL1100 fetch the command ring) CLEAN the command ring + DCBAA + device contexts to DRAM; when
 * usbxhci polls USBSTS with EINT set, INVALIDATE the event ring so it sees the FL1100's completions.
 * Reuses nwoas_ipa_to_pa (ring lands low-window or high-RAM per boot) + nwoas_cache_maint + latches.
 */
static u64 nwoas_fl_dboff = 0; // doorbell array offset (read32(BAR+0x14)&~3), cached

// Clean (dc civac -> PoC) a guest ring region given its latched IOVA. len bytes.
static void nwoas_coh_clean(u64 iova, size_t len)
{
    u64 pa = nwoas_ipa_to_pa(iova & ~0x3fUL);
    if (nwoas_in_dram(pa, len))
        nwoas_cache_maint(pa, len, true);
}

static DECLARE_SPINLOCK(nwoas_remap_lock); // serialize the live stage-2 remap across guest SMP vCPUs

// NWOAS §6-D: permanently remap a guest IPA range's stage-2 mapping to Normal Non-cacheable (0x5),
// mimicking UEFI's Write-Combining allocation of the FL1100 control structures. As break-before-make
// step 1 it cleans (dc civac -> PoC) the ENTIRE 16KB page(s) it is about to flip, not just the caller's
// sub-page struct: Windows allocates these structures 4KB-aligned but the stage-2 granule is 16KB, so
// up to 12KB of unrelated guest data shares the page and MUST also be flushed, or a leftover dirty
// write-back line could later evict and silently clobber data written through the new NC alias
// (mismatched-attribute hazard, ARM DDI0487). (Callers may still nwoas_coh_clean() the exact struct
// beforehand to read it back coherently -- that is a separate, smaller clean and does NOT by itself
// satisfy this full-page BBM requirement.) Once a page is NC, every later usbxhci write to it bypasses
// the CPU cache and lands in DRAM, so the non-coherent apcie/DART DMA always reads the real data -- the
// "when did we clean?" timing race that made the one-shot NWOAS_ERST_CLEAN insufficient (real HW,
// 2026-07-12) is gone. Idempotent: a page already NC is left as-is (and not re-cleaned).
// Serialized by nwoas_remap_lock because hv_pt_get_l3() may memalign + split an L2 block and malloc.c
// has no internal lock. MUST be called with bhl held (i.e. from within a trap handler): bhl serializes
// concurrent remaps, and a sibling vCPU that observes the transient pte==0 during the invalid window
// faults into the bhl-serialized trap path where Layer-1 auto-park makes it benign. (NOTE: bhl does
// NOT cover the L2-block-split publish ordering inside hv_pt_get_l3 for siblings running natively --
// that relies on the dsb ish barrier added there.) The only op inside that can fault is
// nwoas_cache_maint (it runs under its own exc_guard); the page-table stores + sysops cannot.
// Bounds-checked; skips pages outside DRAM or without a live HW map.
static void nwoas_remap_nc(u64 iova, size_t len)
{
    if (!len)
        return;
    u64 base = iova & ~0x3fUL;                            // ring pointers are only 64B-aligned
    u64 first = base & ~(u64)(PAGE_SIZE - 1);             // round DOWN to the 16KB page
    u64 last = (base + len - 1) & ~(u64)(PAGE_SIZE - 1);  // last page a sub-page struct may straddle
    u32 pages = 0;
    spin_lock(&nwoas_remap_lock);
    for (u64 pg = first; pg <= last; pg += PAGE_SIZE) {
        u64 pa = nwoas_ipa_to_pa(pg);
        if (!pa || !nwoas_in_dram(pa, PAGE_SIZE))
            continue; // unmapped, or the whole 16KB page is not in DRAM -> never touch out-of-range
        // Locate the L3 leaf, splitting the covering L2 block into a page table if needed (this is
        // the mechanism the task relies on; hv_pt_get_l3 copies the block mapping down unchanged).
        u64 *l3 = hv_pt_get_l3(pg);
        u64 idx = (pg >> VADDR_L3_OFFSET_BITS) & MASK(VADDR_L3_INDEX_BITS);
        u64 old = l3[idx];
        if (!IS_HW(old))
            continue; // only rewrite a live HW page; never clobber a SW/hook/unmapped descriptor
        // Preserve the ACTUAL output PA from the leaf (low window OR identity RAM -- never assume).
        u64 nc =
            (old & PTE_TARGET_MASK) | PTE_ATTRIBUTES_NC | PTE_VALID | FIELD_PREP(PTE_TYPE, PTE_PAGE);
        if (old != nc) {
            // BBM step 1: the clean must cover the WHOLE page whose cacheability changes, not just the
            // sub-page struct the caller passed (Windows allocates 4KB-aligned, the granule is 16KB).
            // Flush every dirty cacheable line in the page to DRAM (dc civac) BEFORE the switch, else a
            // leftover write-back line could later evict and silently clobber data written through the
            // new NC alias (mismatched-attribute hazard, ARM DDI0487). pa is 16KB-aligned + in-DRAM.
            nwoas_cache_maint(pa, PAGE_SIZE, true);
            // Break-before-make: changing cacheability on a live, valid stage-2 entry is a
            // mismatched-attribute / TLB-conflict hazard (ARM DDI0487), so pass through invalid.
            l3[idx] = 0;                // (2) break
            sysop("dsb ish");           //     publish the invalid PTE to the (coherent) table walker
            sysop("tlbi vmalls12e1is"); // (3) flush ALL stage-1+2 TLB for this VMID (inner-shareable):
            sysop("dsb ish");           //     covers stage-2 entries AND the pre-split 32MB L2 block
            l3[idx] = nc;               // (4) make: install the Non-cacheable descriptor
            sysop("dsb ish");           //     publish the new PTE to the walker
            sysop("isb");               //     resync this PE's instruction stream
            pages++;                    // count only pages actually flipped (idempotent re-calls skip)
        }
    }
    spin_unlock(&nwoas_remap_lock);
    printf("HVLOG: NCREMAP ipa=0x%lx pa=0x%lx len=0x%lx pages=%u\n", base, nwoas_ipa_to_pa(first),
           (u64)len, pages);
}

// On a doorbell write: flush usbxhci's just-written command TRBs + DCBAA + device contexts to DRAM
// so the FL1100 DMA-reads the real data instead of stale cache.  Also flush the first 16 TRBs of
// each live endpoint transfer ring.  D63 proved that a keyboard report can reach the interrupt-IN
// buffer while the xHC never posts its chained Event Data completion: the Normal TRB was visible to
// the No-Snoop controller, but the transfer-ring chain was not kept coherent.  Keep this to 0x100
// bytes so we never clean adjacent device-authored report buffers back over fresh input data.
static void nwoas_fl_clean_cmd(struct exc_info *ctx)
{
    static u32 transfer_log = 0;
    if (nwoas_fl_crcr)
        nwoas_coh_clean(nwoas_fl_crcr, 0x8000); // command ring: 2 pages
    if (!nwoas_fl_dcbaap)
        return;
    u64 dp = nwoas_ipa_to_pa(nwoas_fl_dcbaap & ~0x3fUL);
    if (!nwoas_in_dram(dp, 0x4000))
        return;
    nwoas_cache_maint(dp, 0x4000, true); // DCBAA, then read it back (fresh from DRAM) for the ctxs
    /* DCBAA[0] is the scratchpad-buffer-array pointer, not a device slot. */
    for (int i = 1; i < 16; i++) {
        u64 ctxp = 0;
        if (hv_pa_read(ctx, dp + 8 * i, &ctxp, 3) && ctxp) {
            nwoas_coh_clean(ctxp, 0x1000); // device/input context: 1 page
            u64 cp = nwoas_ipa_to_pa(ctxp & ~0x3fUL);
            if (!cp || !nwoas_in_dram(cp, 32 * 32))
                continue;
            const volatile u32 *slot = (const volatile u32 *)cp;
            u32 entries = (slot[0] >> 27) & 0x1f;
            if (entries > 31)
                entries = 31;
            for (u32 dci = 1; dci <= entries; dci++) {
                const volatile u32 *ep = (const volatile u32 *)(cp + (u64)dci * 32);
                u64 deq = ((u64)ep[3] << 32) | ep[2];
                u64 ring = deq & ~0xfUL;
                if (!ring)
                    continue;
                nwoas_coh_clean(ring, 0x100); // guest-authored TRBs only; no report buffers
                if (transfer_log < 32) {
                    transfer_log++;
                    printf("HVLOG: XFER-CLEAN slot=%d dci=%u ring=0x%lx bytes=0x100\n",
                           i, dci, ring);
                }
            }
        }
    }
}

// When usbxhci sees EINT, invalidate the event ring so the CPU reads the FL1100's DMA'd completions.
static void nwoas_fl_inval_evt(struct exc_info *ctx)
{
    if (!nwoas_fl_erstba)
        return;
    u64 ep = nwoas_ipa_to_pa(nwoas_fl_erstba & ~0x3fUL);
    if (!nwoas_in_dram(ep, 16))
        return;
    /* ERST is guest-authored and device-read-only.  Preserve a possibly dirty
     * usbxhci update before reading its ring pointer; invalidate-only here can
     * discard that update and send us to an older ring. */
    nwoas_cache_maint(ep, 16, true);
    u64 rb = 0;
    if (!hv_pa_read(ctx, ep, &rb, 3) || !rb)
        return;
    u64 ring = nwoas_ipa_to_pa(rb & ~0x3fUL);
    if (nwoas_in_dram(ring, 0x1000))
        nwoas_cache_maint(ring, 0x1000, false); // invalidate the event ring (a page)
}

#if NWOAS_COH_KEEP
// NWOAS session5 COHERENCE KEEP-FRESH (see the NWOAS_COH_KEEP macro comment). Flush the FL1100's
// DMA-READ control structures to DRAM so the No-Snoop card always reads usbxhci's latest values, not a
// stale CPU-cached copy. Called on every interrupter-register write (the precise moment usbxhci finishes
// touching ERSTBA/ERDP/IMAN) and, throttled, on the USBSTS poll. Fixes the one-shot §6-E timing race.
// SAFETY (why clean is correct + cannot clobber a device DMA write):
//   * ERST[0] (16B) and DCBAA head (16B) are GUEST-AUTHORED and DEVICE-READ-ONLY -- the FL1100 never
//     writes them -- so dc civac (clean->PoC) only flushes usbxhci's own dirty line and can never
//     overwrite device-DMA'd data. Always safe.
//   * The event-ring segment (a page) is written by usbxhci ONCE at ring init (zeroes/cycle state) and
//     thereafter only READ by usbxhci (it advances ERDP via MMIO, not memory) while the FL1100 DMA-WRITES
//     completions into it. Because usbxhci never re-dirties it, cleaning it flushes ONLY the init writes;
//     a later clean of an untouched (already-invalidated) line is a no-op and cannot clobber a posted
//     event. To keep even that risk to a single point, ring_page=true is passed ONLY on ERSTBA (re)program
//     (rare, right after usbxhci zero-inits the ring); ERDP/IMAN touches and the poll pass it false.
// All targeted (<=1 page); no wide dc-op, no NC remap. nwoas_cache_maint runs under its own exc_guard;
// nwoas_ipa_to_pa + nwoas_in_dram bounds-check every derived pointer before it is touched.
static void nwoas_coh_keep_fresh(struct exc_info *ctx, bool ring_page)
{
    if (!nwoas_fl_erstba)
        return;
    u64 ep = nwoas_ipa_to_pa(nwoas_fl_erstba & ~0x3fUL);
    if (nwoas_in_dram(ep, 16)) {
        nwoas_cache_maint(ep, 16, true); // ERST[0]: FL1100 DMA-reads this to locate the event ring
        if (ring_page) {
            // Read the (now-fresh) ring segment base from ERST[0].DW0 and flush its first page too.
            u64 seg = 0;
            if (hv_pa_read(ctx, ep, &seg, 3) && (seg & ~0x3fUL)) {
                u64 rp = nwoas_ipa_to_pa(seg & ~0x3fUL);
                if (nwoas_in_dram(rp, 0x1000))
                    nwoas_cache_maint(rp, 0x1000, true); // usbxhci's ring zero-init -> DRAM
            }
        }
    }
    if (nwoas_fl_dcbaap) {
        u64 dp = nwoas_ipa_to_pa(nwoas_fl_dcbaap & ~0x3fUL);
        if (nwoas_in_dram(dp, 16))
            nwoas_cache_maint(dp, 16, true); // DCBAA head: another FL1100 DMA-read, guest-authored
    }
}

// NWOAS session5 diagnostic (Variant B): read the FL1100's PCIe Device Control register from ECAM config
// space and log its No Snoop (bit11) / Relaxed Ordering (bit4) state. The FL1100 datasheet default is
// 0x2810 = No Snoop Enable(bit11)=1 + Relaxed Ordering(bit4)=1: its DMA reads do NOT snoop the CPU cache,
// which is the mechanism behind the ERST-stale coherence hypothesis. ECAM base 0x690200000 = j274 apcie
// bus2/dev0/fn0 (fixed by the RC; the same window UEFI XHC0._INI writes CMD to). Config space is decoded
// once the bus is enumerated (Windows kernel phase, the caller's gate) INDEPENDENT of BAR MSE, is mapped
// Device-nGnRnE in EL2's /arm-io identity map (same range as the FL1100 BAR the hv already reads), and a
// read to a missing function returns 0xffffffff via RC master-abort (never a bus error). Guarded exactly
// like nwoas_track_posting. Once/boot (self-throttling via the static `done`).
#if NWOAS_FL_SNOOP_TEST
static void nwoas_fl_snoop_test(void)
{
    static u32 reports = 0;
    const u64 ecam = 0x690200000UL;
    enum exc_guard_t saved = exc_guard;
    exc_count = 0;
    exc_guard = GUARD_SKIP | GUARD_SILENT;
    u32 id = read32(ecam);
    u16 cap = read16(ecam + 0x70);
    if (exc_count || id != 0x11001b73u || (cap & 0xff) != 0x10) {
        exc_guard = saved;
        if (reports++ < 4)
            printf("HVLOG: S113 SNOOP SKIP id=%x cap=%x\n", id, cap);
        return;
    }
    u16 before = read16(ecam + 0x78);
    u16 after = before;
    if (!exc_count && (before & 0x0800)) {
        /* 16-bit write: do not write the adjacent W1C Device Status register. */
        write16(ecam + 0x78, before & ~0x0800u);
        sysop("dsb sy");
        after = read16(ecam + 0x78);
    }
    bool fault = exc_count != 0;
    exc_guard = saved;
    if (reports < 4 || (before & 0x0800)) {
        if (reports++ < 32)
            printf("HVLOG: S113 SNOOP before=%x after=%x verified=%d fault=%d\n",
                   before, after, !fault && after == (u16)(before & ~0x0800u), fault);
    }
}
#endif

static void nwoas_log_devctl(void)
{
    static bool done = false;
    if (done)
        return;
    const u64 devctl = 0x690200078UL; // ECAM 0x690200000 + PCIe cap Device Control @ config offset 0x78
    enum exc_guard_t g = exc_guard;
    exc_count = 0;
    exc_guard = GUARD_SKIP | GUARD_SILENT;
    u32 dc = read32(devctl);
    exc_guard = g;
    if (exc_count || dc == 0xffffffffu) // read fault (decode-off/unmapped) or master-abort -> not usable
        return;
    done = true;
    u32 d16 = dc & 0xffff; // [15:0] = Device Control ; [31:16] = Device Status
    printf("HVLOG: FL-DEVCTL 0x%x nosnoop=%d relaxord=%d\n", d16, (d16 >> 11) & 1, (d16 >> 4) & 1);
}
#endif // NWOAS_COH_KEEP

/* ==== NWOAS §3.2 FIX (2026-07-13, session4) — hv GENERATES the Port Status Change Event ==========
 * ROOT CAUSE (5-agent analysis + FORK diagnostic, real HW): the wall is a poll-vs-event MECHANISM
 * mismatch, NOT coherence/addressing. UEFI XhciDxe POLLS PORTSC MMIO (XhcPollPortStatusChange) to
 * detect a connect/reset and issue Enable Slot. Windows usbxhci is EVENT-driven: it blocks waiting for
 * a Port Status Change Event (TRB type 34) DMA-posted to interrupter-0's ring + signalled via SPI 698.
 * The FL1100 never posts it (event ring stays all-zero; FORK shows USBSTS HSE=0 HCE=0 -> the controller
 * is HEALTHY, not erroring -- it just isn't posting), so usbxhci never issues Enable Slot (command ring
 * DUMPED EMPTY) and storms USBCMD 0x0->0x5->0x2(HCRST)->0x0 forever.
 * FIX: the hv writes the PSCE the driver is starving for straight into the guest event ring and delivers
 * SPI 698 -- i.e. the hv does UEFI's port poll on Windows's behalf. The write is EL2->EL1 to Normal
 * CACHEABLE memory = CPU-to-CPU coherent (the FL1100 is not involved), so NO non-cacheable remap and
 * thus NO Apple atomic-on-NC trap (unlike §6-D).
 * LIMITATION (honest): once usbxhci issues Enable Slot the REAL FL1100 posts its Command Completion at
 * its OWN internal enqueue (ring base) which will collide with this hv-tracked enqueue -> the wall then
 * moves to Enable-Slot completion (a separate, later problem; likely needs a full event-ring proxy).
 * SUCCESS SIGNAL for this step: command ring gets a type-9 (Enable Slot) TRB, or DCBAADIAG[1]!=0, or the
 * storm stops. NOT a claim of "USB works" -- only real HW cursor movement is that.
 */
#ifndef NWOAS_SYNTH_PSCE
#define NWOAS_SYNTH_PSCE 1
#endif
#ifndef NWOAS_STORM_TAME
#define NWOAS_STORM_TAME 1
#endif
#if NWOAS_SYNTH_PSCE
#define NWOAS_FL1100_SPI_VM 698 // FL1100 INTA -> AIC 698 -> vGIC SPI 698 (matches hv_exc.c bridge)

static u32 nwoas_synth_enq = 0;    // hv's event-ring enqueue index (TRB units), reset on ERSTBA re-arm
static u8 nwoas_synth_pcs = 1;     // producer cycle state, starts 1 (matches UEFI EventRing CCS=1)
static u16 nwoas_synth_done = 0;   // per-PortID "already delivered this arm-cycle" bitmap
static u32 nwoas_synth_total = 0;  // global delivery cap guard (runaway safety)
// nwoas_synth_pending>0 means we have delivered a PSCE usbxhci has not yet consumed. usbxhci is spinning
// in a USBSTS POLL loop, so it discovers events via USBSTS.EINT (bit3) / IMAN.IP (bit0) -- both are set
// ONLY by the controller (RW1C, the hv cannot set them by writing). Since the FL1100 is not posting, the
// hv must EMULATE those bits on the guest's READ so usbxhci notices the generated event and reads the ring.
// Cleared when usbxhci writes ERDP (= it consumed the event ring).
static u32 nwoas_synth_pending = 0;

// Reset the synth tracking when usbxhci (re)programs ERSTBA = a fresh ring (ERDP back at ring base,
// consumer cycle 1). Called from the ring-pointer latch.
static void nwoas_synth_reset(void)
{
    nwoas_synth_enq = 0;
    nwoas_synth_pcs = 1;
    nwoas_synth_done = 0;
    nwoas_synth_pending = 0;
}

// Write one Port Status Change Event TRB (xHCI 6.4.2.3) at the hv enqueue for `portid`, then deliver 698.
// PSCE layout: DW0[31:24]=Port ID; DW1=rsvd; DW2[31:24]=Completion Code (1=Success); DW3 bit0=Cycle,
// bits[15:10]=TRB Type (34). ring_pa/ring_sz are the (already validated, in-DRAM) event ring.
static void nwoas_synth_psce_port(struct exc_info *ctx, u64 ring_pa, u32 ring_sz, u8 portid)
{
    if (nwoas_synth_total >= 4096) // runaway guard: if the driver never consumes, stop (do not spin)
        return;
    u64 trb = ring_pa + (u64)nwoas_synth_enq * 16;
    if (!nwoas_in_dram(trb, 16))
        return;
    u64 lo = (u64)portid << 24;                                   // DW0 (Port ID) ; DW1 = 0 (high half)
    u64 hi = ((u64)(1u << 24)) |                                  // DW2: Completion Code = 1 (Success)
             ((u64)(((34u << 10) | (nwoas_synth_pcs & 1))) << 32); // DW3: Type=34, Cycle=PCS
    // Write the payload (DW0/DW1) BEFORE the cycle-carrying half (DW3), with a barrier between, so a
    // direct ring poller can never observe a valid cycle over a stale payload (xHCI producer rule).
    if (!hv_pa_write(ctx, trb, &lo, 3))
        return;
    sysop("dmb ish"); // order the payload store before the cycle-bit store
    if (!hv_pa_write(ctx, trb + 8, &hi, 3))
        return;
    sysop("dsb ish"); // publish the TRB (both EL2 writer and EL1 reader hit Normal cacheable = coherent)
    // Also clean the TRB to DRAM so it survives any later dc_ivac of the ring and is robust regardless of
    // cache state (belt-and-suspenders; the CPU-to-CPU path is already coherent via the cache).
    nwoas_cache_maint(trb, 16, true);
    if (nwoas_synth_total < 24) // throttle the log, keep delivering
        printf("HVLOG: SYNTH-PSCE port=%u enq=%u pcs=%u trb=0x%lx inject698\n", portid, nwoas_synth_enq,
               nwoas_synth_pcs, trb);
    nwoas_synth_enq++;
    if (nwoas_synth_enq >= ring_sz) { // wrap toggles the producer cycle state
        nwoas_synth_enq = 0;
        nwoas_synth_pcs ^= 1;
    }
    nwoas_synth_done |= (u16)(1u << (portid & 0xf));
    nwoas_synth_total++;
    if (nwoas_synth_pending < 8)
        nwoas_synth_pending++; // emulate EINT/IP on reads until usbxhci consumes (writes ERDP)
    // NOTE: the SPI 698 delivery is done ONCE per nwoas_synth_check pass (not here), so that writing a
    // PSCE for both ports back-to-back cannot place vINTID 698 into two vGIC LRs (= architecturally
    // UNPREDICTABLE). One line interrupt drains the whole ring anyway.
}

// On each USBSTS poll (kernel-gated), if interrupter-0 is armed and a connected device port has an
// unserviced change bit, generate the PSCE usbxhci is waiting for. Reads the FL1100 BAR guarded.
static void nwoas_synth_check(struct exc_info *ctx)
{
    if (!nwoas_fl_erstba)
        return;
    const u64 bar = 0x6c0000000UL;
    enum exc_guard_t g = exc_guard;
    exc_count = 0;
    exc_guard = GUARD_SKIP | GUARD_SILENT;
    u32 caplen = read32(bar + 0x00) & 0xff;
    u32 rtsoff = read32(bar + 0x18) & ~0x1fu;
    u32 iman = read32(bar + rtsoff + 0x20);
    u32 p490 = read32(bar + 0x490); // mouse   (USB2 port2 -> PortID 2)
    u32 p4c0 = read32(bar + 0x4c0); // install (USB3 port1 -> PortID 5)
    exc_guard = g;
    if (exc_count || caplen == 0 || caplen == 0xff || !(iman & 0x2)) // need an ARMED interrupter
        return;
    // Resolve the ring PA from ERST[0] (guest-authored -> clean to read its latest value).
    u64 erstba = nwoas_ipa_to_pa(nwoas_fl_erstba & ~0x3fUL);
    if (!erstba || !nwoas_in_dram(erstba, 16))
        return;
    nwoas_cache_maint(erstba, 16, true);
    u64 e0 = 0, e1 = 0;
    if (!hv_pa_read(ctx, erstba, &e0, 3) || !hv_pa_read(ctx, erstba + 8, &e1, 3))
        return;
    u64 ring_pa = nwoas_ipa_to_pa(e0 & ~0x3fUL);
    u32 ring_sz = (u32)(e1 & 0xffff);
    // Clamp to 256 TRBs = one 16KB page: the standard usbxhci event-ring segment is a single page, and
    // we translate the ring base ONCE (ring_pa + enq*16). A larger segment could straddle a non-linear
    // window/identity mapping boundary and make ring_pa+enq*16 point at the wrong guest page. With the
    // <=2 delivers/arm-cycle dedup, enq never approaches even 256, so this clamp is purely a safety bound.
    if (!ring_pa || !nwoas_in_dram(ring_pa, 16) || ring_sz == 0 || ring_sz > 256)
        return;
    // Session4 synth-trigger redesign: fire for a CONNECTED port (PORTSC.CCS=1), NOT only when a change
    // bit is set. Real HW: once 698 is delivered to usbxhci's core, the ISR reads an EMPTY event ring and
    // loops (which stalls the boot at ~kernel 92); the ports are already PED=1 enabled with the change
    // bits CLEARED, so the old (change-bit) trigger never fired. Feeding a PSCE for the connected port on
    // every pass (deduped per PortID per arm-cycle) gives the ISR the event it is starving for so it can
    // advance to Enable Slot instead of looping. (void)CHG kept for reference.
    (void)(0xfe0000u);
    u32 before = nwoas_synth_total;
    if ((p490 & 1) && !(nwoas_synth_done & (1u << 2)))
        nwoas_synth_psce_port(ctx, ring_pa, ring_sz, 2);
    if ((p4c0 & 1) && !(nwoas_synth_done & (1u << 5)))
        nwoas_synth_psce_port(ctx, ring_pa, ring_sz, 5);
    // Deliver the FL1100 line interrupt SPI 698 AT MOST ONCE per pass (even if both ports got a PSCE) --
    // one line IRQ drains the whole ring, and this prevents vINTID 698 landing in two LRs. Gated on the
    // shared "at most one in flight" check the FL1100 bridge uses (hv_exc.c) + a free LR.
    if (nwoas_synth_total != before && !hv_vgic_virq_outstanding(NWOAS_FL1100_SPI_VM) &&
        hv_vgic3_get_free_lr() != -1)
        // priority 0x00 = non-maskable: beat the guest's live VPMR so 698 is actually taken (session4
        // vGIC-698 finding -- get_priority(698) is numerically >= VPMR while usbxhci spins at raised IRQL,
        // masking its own device interrupt; the working timer PPI delivers at a lower/urgent priority).
        hv_vgic3_inject_irq(NWOAS_FL1100_SPI_VM, 0x00, false, true, false, 0);
}
#endif // NWOAS_SYNTH_PSCE

/* D80: translation-mode USB DMA does not observe dirty cached ERST.
 * Clean only CPU-authored ERST before Run; invalidate device-owned event
 * segments before IRQ delivery. Never clean event-ring initialization zeros. */
void nwoas_usbc_event_coherence(u64 rt, bool before_run)
{
    u32 n = read32(rt+0x28) & 0xffff;
    u64 e = read32(rt+0x30)|((u64)read32(rt+0x34)<<32);
    u64 ep = nwoas_ipa_to_pa(e & ~63UL);
    if (!n || n > 16 || !ep || !nwoas_in_dram(ep,n*16))
        return;
    if (before_run) {
        nwoas_cache_maint(ep,n*16,true);
        printf("[usb-d80] ERST clean ipa=%lx n=%u\n",e,n);
        return;
    }
    for (u32 i=0;i<n;i++) {
        u64 r = read64(ep+i*16);
        u32 count = read32(ep+i*16+8);
        u64 rp = nwoas_ipa_to_pa(r & ~63UL);
        if (count && count<=256 && rp && nwoas_in_dram(rp,count*16))
            nwoas_cache_maint(rp,count*16,false);
    }
}

/* D81: retain hardware-proven USB DART bypass and translate low payload
 * buffer IPAs in CPU-authored Normal/Data TRBs before the endpoint doorbell.
 * Event Data opaque cookies and immediate data are never addresses. */
static void nwoas_usbc_payload_alias(u64 op, u32 slot, u32 ep)
{
    if (slot != 1 || ep < 1 || ep > 31)
        return;
    u64 dcba=read32(op+0x30)|((u64)read32(op+0x34)<<32);
    u64 dp=nwoas_ipa_to_pa(dcba & ~63UL);
    if (!dp || !nwoas_in_dram(dp,16)) return;
    u64 ctx=read64(dp+8);
    if (!ctx) return;
    u64 cp=nwoas_ipa_to_pa(ctx);
    u32 csz=(read32(0x502280010UL)&4)?64:32;
    if (!cp || !nwoas_in_dram(cp+ep*csz,32)) return;
    nwoas_cache_maint(cp+ep*csz,32,false);
    u64 dq=read64(cp+ep*csz+8)&~15UL;
    if (!dq) return;
    /* S157 A/B: do not retain a transfer-ring address across doorbells.
     * A retired segment can be recycled while the hardware output DQ stays
     * unchanged. Re-read the current context each time. A stale output DQ can
     * still stop this walk early; that is a measured limitation, not a reason
     * to write through an unvalidated cached segment address. */
    u64 rp=nwoas_ipa_to_pa(dq);
    static u32 logged=0, link_logged=0;
    u64 visited[8];
    for (u32 seg=0;rp && seg<8;seg++) {
        bool duplicate=false;
        for (u32 k=0;k<seg;k++)
            if (visited[k]==rp) duplicate=true;
        if (duplicate) break;
        visited[seg]=rp;
        u64 next=0;
        for (u32 j=0;j<256 && nwoas_in_dram(rp+j*16,16);j++) {
            u64 t=rp+j*16, ptr=read64(t);
            u32 st=read32(t+8),ctl=read32(t+12),type=(ctl>>10)&63;
            u32 len=st&0x1ffff;
            if (type==0) break;
            if (type==6) {
                /* D82: follow and, if low, alias Link TRB as well as payloads.
                 * A link already changed to backing PA need not be stage2 mapped. */
                next=nwoas_in_dram(ptr & ~15UL,16) ? (ptr & ~15UL) :
                     nwoas_ipa_to_pa(ptr & ~15UL);
                if (ptr && ptr<0x100000000UL && next && nwoas_in_dram(next,16)) {
                    write64(t,next);nwoas_cache_maint(t,16,true);
                }
                if (link_logged++<16)
                    printf("[usb-d83] EP%u segment%u link=%lx next=%lx\n",ep,seg,ptr,next);
                break;
            }
            if ((type!=1 && type!=3) || (ctl&BIT(6)) || !ptr || ptr>=0x100000000UL || !len)
                continue;
            u64 pa=nwoas_ipa_to_pa(ptr);
            if (!pa || !nwoas_in_dram(pa,len)) continue;
            write64(t,pa);
            nwoas_cache_maint(t,16,true);
            if (logged++<20)
                printf("[usb-d83] EP%u segment%u payload %lx -> %lx len=%x\n",ep,seg,ptr,pa,len);
        }
        if (!next || !nwoas_in_dram(next,16)) break;
        rp=next;
    }
    sysop("dsb sy");
}

/* D79 read-only descriptor capture: observe GET_DESCRIPTOR(Device) buffer
 * at EP0 doorbell, then inspect only after controller IRQ becomes pending. */
static u64 nwoas_usbc_descriptor = 0;
static bool nwoas_usbc_identified = false;
static void nwoas_usbc_watch_descriptor(u64 op)
{
    if (nwoas_usbc_identified)
        return;
    u64 dcba = read32(op + 0x30) | ((u64)read32(op + 0x34) << 32);
    u64 dp = nwoas_ipa_to_pa(dcba & ~63UL);
    if (!dp || !nwoas_in_dram(dp, 16))
        return;
    u64 cp = nwoas_ipa_to_pa(read64(dp + 8));
    u32 csz = (read32(0x502280010UL) & 4) ? 64 : 32;
    if (!cp || !nwoas_in_dram(cp + csz, 32))
        return;
    nwoas_cache_maint(cp + csz, 32, false);
    u64 dq = read64(cp + csz + 8) & ~15UL;
    u64 rp = nwoas_ipa_to_pa(dq & ~0x1ffUL);
    for (u32 j = 0; rp && j < 31 && nwoas_in_dram(rp+j*16,32); j++) {
        u64 t = rp+j*16, setup = read64(t);
        if (((read32(t+12)>>10)&63) == 2 && (setup & 0xffffffffUL) == 0x01000680 &&
            ((read32(t+28)>>10)&63) == 3) {
            u64 bp = nwoas_ipa_to_pa(read64(t+16));
            if (bp && nwoas_in_dram(bp,18))
                nwoas_usbc_descriptor = bp;
        }
    }
}
void nwoas_usbc_poll_descriptor(void)
{
    if (!nwoas_usbc_descriptor || nwoas_usbc_identified)
        return;
    u64 p = nwoas_usbc_descriptor;
    nwoas_cache_maint(p, 18, false);
    if (read8(p) != 18 || read8(p+1) != 1)
        return;
    nwoas_usbc_identified = true;
    printf("[usb-d79] DEVICE vid=%04x pid=%04x bcd=%04x class=%x configs=%u\n",
           read16(p+8),read16(p+10),read16(p+12),read8(p+4),read8(p+17));
}

/* D77: USB-C DMA must use the same guest-IPA translation as FL1100.
 * D76 EP7 TRBs used sub-4GB buffers while USB DART was bypassed, so device
 * accesses did not reach the guest low-window backing. Share the runtime-
 * reserved T8020 tables; do not modify FL1100 tables or the debug USB0 DART.
 * Called before a Windows Run write while the xHC is halted. */
static void nwoas_usbc_dma_start(u64 op)
{
    static bool installed = false;
    if (installed || !(read32(op + 4) & 1))
        return;
    u32 ttbr = read32(FL_DART_BASE + 0x210);
    u64 l1 = (u64)(ttbr & 0x7fffffff) << 12;
    if (!(ttbr & BIT(31)) || !nwoas_in_dram(l1, 0x4000))
        return;
    /* Check representative low-window mappings against the EL2 stage-2 map. */
    const u64 checks[] = {0x100000, 0xfe9d4000};
    for (u32 i = 0; i < 2; i++) {
        u64 a = checks[i], l1e = read64(l1 + (a >> 25) * 8);
        u64 l2 = l1e & 0xffffffc000UL;
        if (!(l1e & 1) || !nwoas_in_dram(l2, 0x4000))
            return;
        u64 leaf = read64(l2 + ((a >> 14) & 2047) * 8);
        u64 mapped = (leaf & 0xffffffc000UL) | (a & 0x3fff);
        if (!(leaf & 1) || mapped != nwoas_ipa_to_pa(a)) {
            printf("[usb-d77] skip mapping mismatch iova=%lx dart=%lx stage2=%lx\n",
                   a, mapped, nwoas_ipa_to_pa(a));
            return;
        }
    }
    for (u32 i = 0; i < 2; i++) {
        u64 base = 0x502f00000UL + i * 0x80000;
        if (read32(base + 0x60) & BIT(15)) {
            printf("[usb-d77] skip locked DART%u\n", i);
            return;
        }
    }
    /* T8103 DTS: dwc3_1 uses dart_0 SID0 and dart_1 SID1. */
    for (u32 i = 0; i < 2; i++) {
        u64 base = 0x502f00000UL + i * 0x80000;
        for (u32 j = 0; j < 4; j++)
            write32(base + 0x200 + 16*i + 4*j, read32(FL_DART_BASE + 0x210 + 4*j));
        set32(base + 0xfc, BIT(i));
        sysop("dsb sy");
        write32(base + 0x100 + 4*i, 0x80);
        write32(base + 0x34, BIT(i));
        write32(base + 0x20, BIT(20));
        int ret = poll32(base + 0x20, BIT(2), 0, 100);
        printf("[usb-d77] DART%u SID%u ttbr=%x tcr=%x flush=%d\n", i,i,
               read32(base+0x200+16*i),read32(base+0x100+4*i),ret);
    }
    installed = true;
}

static bool hv_emulate_rw_aligned(struct exc_info *ctx, u64 pte, u64 vaddr, u64 ipa, u64 *val,
                                  bool is_write, u64 width, u64 elr, u64 par)
{
    assert(pte);
    assert(((ipa & 0x3fff) + (1 << width)) <= 0x4000);

    u64 target = pte & PTE_TARGET_MASK_L4;
    u64 paddr = target | (vaddr & MASK(VADDR_L4_OFFSET_BITS));
    u64 flags = FIELD_PREP(MMIO_EVT_ATTR, FIELD_GET(PAR_ATTR, par)) |
                FIELD_PREP(MMIO_EVT_SH, FIELD_GET(PAR_SH, par));

    // For split ops, treat hardware mapped pages as SPTE_MAP
    if (IS_HW(pte))
        pte = target | FIELD_PREP(PTE_TYPE, PTE_BLOCK) | FIELD_PREP(SPTE_TYPE, SPTE_MAP);

    if (is_write) {
        // Write
        hv_wdt_breadcrumb('3');

        if (pte & SPTE_TRACE_WRITE)
            emit_mmiotrace(elr, ipa, val, width, flags | MMIO_EVT_WRITE, pte & SPTE_TRACE_UNBUF);

        hv_wdt_breadcrumb('4');

        switch (FIELD_GET(SPTE_TYPE, pte)) {
            case SPTE_PROXY_HOOK_R:
                paddr = ipa;
                // fallthrough
            case SPTE_MAP:
                hv_wdt_breadcrumb('5');
                dprintf("HV: SPTE_MAP[W] @0x%lx 0x%lx -> 0x%lx (w=%d): 0x%lx\n", elr, ipa, paddr,
                        1 << width, val[0]);
                if ((elr >> 48) == 0xffff && width == 2 &&
                    paddr >= 0x502280000UL && paddr < 0x502290000UL) {
                    u64 op = 0x502280000UL + (read32(0x502280000UL) & 0xff);
                    u64 db = 0x502280000UL + (read32(0x502280014UL) & ~3u);
                    if (paddr >= db+4 && paddr < db+128*4)
                        nwoas_usbc_payload_alias(op,(paddr-db)/4,val[0]&0xff);
                    if (paddr == db + 4 && (val[0] & 0xff) == 1)
                        nwoas_usbc_watch_descriptor(op);
                }
                // NWOAS §6-C coherence: BEFORE the guest's FL1100 doorbell write reaches the card
                // (which makes it DMA-read the command ring), flush the command ring + DCBAA +
                // contexts to DRAM so the FL1100 reads the real TRBs, not stale cache. Kernel-gated,
                // doorbell-only (DBOFF=read32(BAR+0x14)&~3), so not on the hot USBSTS poll.
                if ((elr >> 48) == 0xffff && paddr >= 0x6c0000000UL && paddr < 0x6c0100000UL) {
                    u64 woff = paddr - 0x6c0000000UL;
                    if (!nwoas_fl_dboff) {
                        u64 db = 0;
                        if (hv_pa_read(ctx, 0x6c0000014UL, &db, 2))
                            nwoas_fl_dboff = db & ~0x3UL;
                    }
                    if (nwoas_fl_dboff && woff >= nwoas_fl_dboff && woff < nwoas_fl_dboff + 0x100) {
#if NWOAS_FL_SNOOP_TEST
                        nwoas_fl_snoop_test();
#endif
#if NWOAS_FL_SKIP_LEGACY_CLEAN
                        static bool reported_skip = false;
                        if (!reported_skip) {
                            reported_skip = true;
                            printf("HVLOG: S114 legacy doorbell cache sweep disabled\n");
                        }
#else
                        nwoas_fl_clean_cmd(ctx);
#endif
                    }
                }
                // NWOAS session4 (2026-07-13) — USBCMD-write decision-point snapshot. Opus review of the
                // CCS-stab boot found the port flap is a SYMPTOM of each HCRST (the first flap follows
                // usbxhci's takeover HCRST), so masking ports chases a symptom. Instead capture the
                // PRE-write controller+port state at EACH USBCMD (off=0x80) write -- the instant usbxhci
                // decides RS=1 or HCRST -- plus dt_us since the last such write. Read-out: at a HCRST
                // (new=0x2), a LARGE dt_us => usbxhci waited then timed out (polled-register wait if erdp
                // stays at ring base); a TINY dt_us => synchronous health-check abort, and sts/iman/PORTSC
                // name the offending value. erdp advancing past base => it consumed events (re-opens the
                // interrupt-timeout hypo). Capped at 48 lines (whole storm), kernel+FL1100 gated, read-only.
                if ((elr >> 48) == 0xffff && paddr >= 0x6c0000000UL && paddr < 0x6c0100000UL &&
                    (paddr - 0x6c0000000UL) == 0x80) {
                    static u32 nwoas_cmdw = 0;
                    static u64 nwoas_cmdw_tsc = 0;
                    if (nwoas_cmdw < 48) {
                        nwoas_cmdw++;
                        u64 tsc = mrs(CNTPCT_EL0);
                        u64 dt_us = nwoas_cmdw_tsc ? (tsc - nwoas_cmdw_tsc) * 1000000 / mrs(CNTFRQ_EL0) : 0;
                        nwoas_cmdw_tsc = tsc;
                        const u64 b = 0x6c0000000UL;
                        enum exc_guard_t g = exc_guard;
                        exc_count = 0;
                        exc_guard = GUARD_SKIP | GUARD_SILENT;
                        u32 cap = read32(b) & 0xff, ro = read32(b + 0x18) & ~0x1fu;
                        u32 sts = read32(b + cap + 0x04), iman = read32(b + ro + 0x20);
                        u32 erdp = read32(b + ro + 0x38);
                        // Session4 Path-B: read back the REAL FL1100 interrupter ring pointers from HW.
                        // The FL1100 set IMAN.IP once (it DID try to post one event) yet the Windows event
                        // ring (0xae0fc8000) stayed empty of real events -> the one event went elsewhere.
                        // Hypothesis: the FL1100's ERSTBA is STALE (still the UEFI segment table at a LOW
                        // address ~0x103140), so the card DMA-writes events to the UEFI ring, not Windows's.
                        // ERSTSZ=rt+0x28, ERSTBA=rt+0x30(lo)/0x34(hi). Windows expects ERSTBA=0xae0fc9000.
                        u32 erstsz = read32(b + ro + 0x28);
                        u32 erstlo = read32(b + ro + 0x30), ersthi = read32(b + ro + 0x34);
                        u32 pm = read32(b + 0x490), pi = read32(b + 0x4c0);
                        exc_guard = g;
                        printf("HVLOG: CMDW new=0x%lx sts=0x%x iman=0x%x ersz=0x%x erstba=0x%x%08x "
                               "erdp=0x%x mouse=0x%x inst=0x%x dt_us=%lu pend=%u\n",
                               val[0], sts, iman, erstsz, ersthi, erstlo, erdp, pm, pi, dt_us,
                               nwoas_synth_pending);
                    }
                }
#if NWOAS_STORM_TAME
                // Session5 storm-tame: hold the controller RUNNING through usbxhci's HCRST/RS-clear for a
                // BOUNDED window, to test whether the FL1100 -- left running instead of being reset every 10s
                // -- posts the pending Port Status Change Event (IMAN.IP->1, ERDP advances, a non-synth event
                // ring TRB). Gated to the Windows kernel + FL1100 BAR + USBCMD + ERSTBA-programmed (storm
                // phase, not initial setup). Rewrites a HCRST(bit1) or an RS-clear(bit0 cleared after it had
                // been set) into RS=1 keeping INTE: tamed = (val & ~0x2) | 0x1. After N tames the window
                // closes -> pass-through (avoid permanent divergence). This rewrites val[0] BEFORE the
                // hv_pa_write below forwards it, so the tamed value is what actually reaches the hardware.
                if ((elr >> 48) == 0xffff && paddr >= 0x6c0000000UL && paddr < 0x6c0100000UL &&
                    (paddr - 0x6c0000000UL) == 0x80 && width == 2 && nwoas_fl_erstba) {
                    static u32 nwoas_tame_n = 0;
                    static u8  nwoas_tame_rs = 0;   // did the guest have R/S set previously?
                    u32 v = (u32)val[0];
                    bool is_hcrst   = (v & 0x2u) != 0;
                    bool is_rsclear = nwoas_tame_rs && !(v & 0x1u);
                    if ((is_hcrst || is_rsclear) && nwoas_tame_n < 16) {
                        u32 tamed = (v & ~0x2u) | 0x1u; // clear HCRST, force R/S=1, keep INTE(bit2) + others
                        if (nwoas_tame_n < 24)
                            printf("HVLOG: STORM-TAME #%u guest=0x%x -> 0x%x (keep RUNNING)\n", nwoas_tame_n,
                                   v, tamed);
                        nwoas_tame_n++;
                        val[0] = tamed;
                        v = tamed;
                    } else if (nwoas_tame_n >= 16) {
                        static bool nwoas_tame_done = false;
                        if (!nwoas_tame_done) {
                            nwoas_tame_done = true;
                            printf("HVLOG: STORM-TAME window exhausted, pass-through\n");
                        }
                    }
                    nwoas_tame_rs = (v & 0x1u) ? 1 : 0;
                }
#endif
#if NWOAS_LOW_EVENT_SEED
                // The guest's preceding ERDP writes target its high ring.  Put the
                // physical interrupter back on the seed immediately before start.
                if ((elr >> 48) == 0xffff && paddr == 0x6c0000080UL &&
                    width == 2 && (val[0] & 1) && nwoas_seed_active)
                    nwoas_seed_restore_regs();
#endif
                if (!hv_pa_write(ctx, paddr, val, width))
                    return false;
                // NWOAS §6-A: latch the FL1100 event-ring pointers as the guest programs them.
                // ERSTBA = rt+0x30 = BAR+0x2030 (rtsoff=0x2000); DCBAAP = op+0x30 = BAR+0xb0
                // (caplen=0x80). Written as one 64-bit store or two 32-bit halves; handle both.
                // GATE on the Windows kernel ((elr>>48)==0xffff): UEFI programs its OWN ring first
                // (erstba=0x103140) and we must NOT read that — only Windows's ring livelocks. With
                // the gate, nwoas_fl_erstba stays 0 through UEFI and holds Windows's value once its
                // usbxhci programs the ring, so the (also-gated) dump can only read the right ring.
                if ((elr >> 48) == 0xffff && paddr >= 0x6c0000000UL && paddr < 0x6c0100000UL) {
                    u64 off = paddr - 0x6c0000000UL;
                    // ERDP points at the next event after consumption. Event-ring
                    // segment bases are 64-byte aligned, so pointer bits[5:4]
                    // becoming nonzero proves a real advance within the first
                    // four TRBs.  D47 advanced by 0x20.  Latch only; the read-side
                    // diagnostic below performs the serial-heavy snapshot.
                    if (!nwoas_postconsume_dumped &&
                        (off == 0x2038 || off == 0x203c) &&
                        ((u32)val[0] & 0x30u))
                        nwoas_postconsume_pending = true;
#if NWOAS_SYNTH_PSCE
                    // usbxhci writes ERDP (rt+0x38=BAR+0x2038, or its high half 0x203c) after it has
                    // consumed the event ring -> our generated PSCE was seen; stop emulation EINT/IP.
                    if (off == 0x2038 || off == 0x203c)
                        nwoas_synth_pending = 0;
#endif
                    // Latch ERSTBA(0x2030) / DCBAAP(0xb0) / CRCR(op+0x18=0x98). ring_ptr marks a
                    // COMPLETE pointer (64-bit store, or the high 32-bit half) so the DART fixup runs
                    // only once the address is fully written, never on a half-written value.
                    bool ring_ptr = false;
                    if (width == 3) {
                        if (off == 0x2030) { nwoas_fl_erstba = val[0]; ring_ptr = true; }
                        else if (off == 0xb0) { nwoas_fl_dcbaap = val[0]; ring_ptr = true; }
                        else if (off == 0x98) { nwoas_fl_crcr = val[0]; ring_ptr = true; }
                    } else if (width == 2) {
                        u32 lo = (u32)val[0];
                        if (off == 0x2030)
                            nwoas_fl_erstba = (nwoas_fl_erstba & 0xffffffff00000000UL) | lo;
                        else if (off == 0x2034) {
                            nwoas_fl_erstba = (nwoas_fl_erstba & 0xffffffffUL) | ((u64)lo << 32);
                            ring_ptr = true;
                        } else if (off == 0xb0)
                            nwoas_fl_dcbaap = (nwoas_fl_dcbaap & 0xffffffff00000000UL) | lo;
                        else if (off == 0xb4) {
                            nwoas_fl_dcbaap = (nwoas_fl_dcbaap & 0xffffffffUL) | ((u64)lo << 32);
                            ring_ptr = true;
                        } else if (off == 0x98)
                            nwoas_fl_crcr = (nwoas_fl_crcr & 0xffffffff00000000UL) | lo;
                        else if (off == 0x9c) {
                            nwoas_fl_crcr = (nwoas_fl_crcr & 0xffffffffUL) | ((u64)lo << 32);
                            ring_ptr = true;
                        }
                    }
                    // NWOAS §6-C FIX: as soon as the guest finishes programming a ring pointer, make
                    // the FL1100 DART identity-map its high-RAM region so FL1100's DMA reaches it.
                    if (ring_ptr) {
                        nwoas_dart_fixup(ctx);
#if NWOAS_LOW_EVENT_SEED
                        if (off == 0x2030 || off == 0x2034)
                            nwoas_seed_arm(ctx);
#endif
#if NWOAS_SYNTH_PSCE
                        // A (re)written ERSTBA = a fresh event ring (ERDP back at base, consumer cycle
                        // 1). Restart the hv's enqueue/cycle/per-port tracking so generated PSCEs land
                        // where usbxhci will next dequeue, with the correct cycle bit.
                        if (off == 0x2030 || off == 0x2034)
                            nwoas_synth_reset();
#endif
#if NWOAS_ERST_CLEAN
                        // NWOAS §6-E — THE control-structure coherence fix (2026-07-12 diagnosis).
                        // Reachability is proven OK (DARTDIAG: rings MAPPED-IDENTITY-OK, no fault),
                        // yet the event ring stays empty even though USBSTS.PCD=1 (the card DID
                        // detect the port change). Root cause: the FL1100 DMA-READS its control
                        // structures — the Event Ring Segment Table (ERST @ erstba) and the DCBAA —
                        // straight from DRAM, but Windows usbxhci writes them into CACHEABLE memory
                        // (UEFI used Write-Combining, which is why UEFI works). Those writes sit in
                        // CPU cache and never reach DRAM, so the card reads a STALE/zero ERST, does
                        // not know where the event ring is, and posts NO events -> usbxhci times out
                        // -> HCRST storm -> DCBAA[1..]=0 (never reaches Enable Slot). The §6-C doorbell
                        // clean never touched the ERST (it is programmed at setup, with no doorbell),
                        // which is why it did not help. So: the instant usbxhci finishes writing a
                        // ring pointer, CLEAN (dc civac -> PoC) the structures the card reads so the
                        // card sees the real data. Layer-1 auto-park makes any resulting fault benign.
                        if (nwoas_fl_erstba) {
                            // BBM step 1: flush usbxhci's dirty cacheable lines to DRAM, THEN (§6-D)
                            // permanently remap the page Non-cacheable so later writes bypass cache.
                            nwoas_coh_clean(nwoas_fl_erstba, 0x1000); // ERST itself
#if NWOAS_NC_REMAP
                            nwoas_remap_nc(nwoas_fl_erstba, 0x1000);
#endif
                            // ERST[0] = { event ring segment base (8B), size (16b) }. Read it (now
                            // coherent) and clean the ring segment too (usbxhci's initial cycle bits).
                            u64 ep = nwoas_ipa_to_pa(nwoas_fl_erstba & ~0x3fUL);
                            u64 seg = 0;
                            if (ep && nwoas_in_dram(ep, 8) && hv_pa_read(ctx, ep, &seg, 3) &&
                                (seg & ~0x3fUL)) {
                                nwoas_coh_clean(seg & ~0x3fUL, 0x1000);
#if NWOAS_NC_REMAP
                                nwoas_remap_nc(seg & ~0x3fUL, 0x1000);
#endif
                            }
                        }
                        if (nwoas_fl_dcbaap) {
                            nwoas_coh_clean(nwoas_fl_dcbaap, 0x4000); // DCBAA (device ctx ptr array)
#if NWOAS_NC_REMAP
                            nwoas_remap_nc(nwoas_fl_dcbaap, 0x4000);
#endif
                        }
                        if (nwoas_fl_crcr) {
                            nwoas_coh_clean(nwoas_fl_crcr, 0x8000);   // command ring (2 pages)
#if NWOAS_NC_REMAP
                            nwoas_remap_nc(nwoas_fl_crcr, 0x8000);
#endif
                        }
#endif
                    }
#if NWOAS_LOW_EVENT_SEED
                    if (nwoas_seed_active && (off == 0x2038 || off == 0x203c)) {
#if NWOAS_LOW_EVENT_MIRROR
                        nwoas_seed_guest_pending = false;
                        nwoas_seed_guest_irq_sent = false;
#endif
                        nwoas_seed_restore_regs();
                    }
#endif
#if NWOAS_COH_KEEP
                    // NWOAS session5 coherence timing-race fix: the instant usbxhci finishes touching an
                    // interrupter register, re-flush the FL1100's DMA-read control structures to DRAM so
                    // the No-Snoop card reads fresh data at RS=1 / next post. ERSTBA (re)program also
                    // flushes the just-zero-inited ring page (ring_page=true, rare); ERDP/IMAN touches --
                    // which can be frequent once events flow -- do the cheap 32B ERST[0]+DCBAA clean only.
                    // Gate the ring-page flush on ring_ptr (COMPLETE pointer): a width==2 low-half write
                    // to 0x2030 leaves nwoas_fl_erstba half-written (stale hi | new lo), which for a high
                    // address truncates to a <4GB value that translates in-DRAM via the low window -> a
                    // spurious 16B + garbage-derived 4KB civac at the wrong page. The DART fixup/synth-reset
                    // above already require ring_ptr for exactly this reason; match that discipline.
                    if ((off == 0x2030 || off == 0x2034) && ring_ptr)
                        nwoas_coh_keep_fresh(ctx, true);
                    else if (off == 0x2020 || off == 0x2038 || off == 0x203c)
                        nwoas_coh_keep_fresh(ctx, false);
#endif
                }
                break;
            case SPTE_HOOK: {
                hv_wdt_breadcrumb('6');
                hv_hook_t *hook = (hv_hook_t *)target;
                if (!hook(ctx, ipa, val, true, width))
                    return false;
                dprintf("HV: SPTE_HOOK[W] @0x%lx 0x%lx -> 0x%lx (w=%d) @%p: 0x%lx\n", elr, ipa,
                        paddr, 1 << width, hook, val);
                break;
            }
            case SPTE_PROXY_HOOK_RW:
            case SPTE_PROXY_HOOK_W: {
                if (nwoas_nvme_fastpath_mmio(ctx, ipa, val, true, width))
                    break;
                hv_wdt_breadcrumb('7');
                struct hv_vm_proxy_hook_data hook = {
                    .flags = FIELD_PREP(MMIO_EVT_WIDTH, width) | MMIO_EVT_WRITE | flags,
                    .id = FIELD_GET(PTE_TARGET_MASK_L4, pte),
                    .addr = ipa,
                    .data = {0},
                };
                memcpy(hook.data, val, 1 << width);
                hv_exc_proxy(ctx, START_HV, HV_HOOK_VM, &hook);
                break;
            }
            default:
                printf("HV: invalid SPTE 0x%016lx for IPA 0x%lx\n", pte, ipa);
                return false;
        }
    } else {
        hv_wdt_breadcrumb('3');
        switch (FIELD_GET(SPTE_TYPE, pte)) {
            case SPTE_PROXY_HOOK_W:
                paddr = ipa;
                // fallthrough
            case SPTE_MAP:
                hv_wdt_breadcrumb('4');
                if (!hv_pa_read(ctx, paddr, val, width))
                    return false;
                /* S154: USBHUB3 resets the root hub with the xHC global
                 * interrupt gate disabled and polls USBSTS/IMAN for command
                 * progress.  Make controller-authored event TRBs visible on
                 * those real pending reads as well as before IRQ delivery.
                 * This is invalidate-only and is restricted to Windows kernel
                 * reads of the non-debug USB-C xHC. */
                if ((elr >> 48) == 0xffff && width == 2 &&
                    paddr >= 0x502280000UL && paddr < 0x502290000UL) {
                    u64 base = 0x502280000UL;
                    u64 op = base + (read32(base) & 0xff);
                    u64 rt = base + (read32(base + 0x18) & ~0x1fu);
                    bool event_pending = (paddr == op + 0x04 && (val[0] & BIT(3))) ||
                                         (paddr == rt + 0x20 && (val[0] & BIT(0)));
                    if (event_pending)
                        nwoas_usbc_event_coherence(rt, false);
                }
                dprintf("HV: SPTE_MAP[R] @0x%lx 0x%lx -> 0x%lx (w=%d): 0x%lx\n", elr, ipa, paddr,
                        1 << width, val[0]);
                // NWOAS stage-8 diag: the run9 terminal state is a livelock of the guest USB
                // stack polling the emulated FL1100 xHCI BAR (0x6c0000000). Log what the driver
                // reads (USBSTS 0x84 / PORTSC 0x480+ / ERDP 0x2038 / doorbell 0x8xxx) so we can
                // tell whether the polled status ever changes (FL1100 not advancing) vs waits on
                // an interrupt that never arrives. Rate-limited (every 4096th FL1100 read, cap
                // 400 lines) to avoid perturbing this hottest trap path / saturating the vuart.
                if (paddr >= 0x6c0000000UL && paddr < 0x6c0100000UL) {
                    u64 off = paddr - 0x6c0000000UL;
#if NWOAS_FORCE_FL32
                    // xHCI HCCPARAMS1.AC64 is bit 0 at BAR+0x10.  The register
                    // is 32-bit and read-only; only the guest-visible value is
                    // changed. UEFI already uses low DMA, so seeing AC64=0 is
                    // valid there as well and avoids a stage-timing dependency.
                    if (off == 0x10 && width == 2) {
                        static bool nwoas_fl32_logged = false;
                        u64 raw = val[0];
                        val[0] &= ~1UL;
                        if (!nwoas_fl32_logged) {
                            nwoas_fl32_logged = true;
                            printf("HVLOG: FL32 HCCPARAMS1 raw=0x%lx guest=0x%lx AC64=0\n",
                                   raw, val[0]);
                        }
                    }
#endif
                    // Session4 precise 698 routing: record the core running usbxhci (the one reading the
                    // FL1100 BAR from the Windows kernel) so hv_exc.c's bridge delivers 698 ONLY here.
                    if ((elr >> 48) == 0xffff)
                        nwoas_fl_cpu = (int)smp_id();
#if NWOAS_SYNTH_PSCE
                    // THE FIX (read side): while a hv-generated PSCE is unconsumed, EMULATE the
                    // event-notification bits the (non-posting) FL1100 never set, so usbxhci's poll loop
                    // notices it and reads the ring. USBSTS.EINT = 0x84 bit3; IMAN(rt+0x20=BAR+0x2020)
                    // .IP = bit0. Kernel-gated. Cleared when usbxhci writes ERDP (consumed the ring).
                    if (nwoas_synth_pending && (elr >> 48) == 0xffff) {
                        if (off == 0x84)
                            val[0] |= 0x8;
                        else if (off == 0x2020)
                            val[0] |= 0x1;
                    }
#endif
#if NWOAS_LOW_EVENT_SEED && NWOAS_LOW_EVENT_MIRROR
                    // Keep Windows's register view coherent with mirrored low-ring
                    // events until it advances its own ERDP.
                    if (nwoas_seed_guest_pending && (elr >> 48) == 0xffff) {
                        if (off == 0x84)
                            val[0] |= 0x8;
                        else if (off == 0x2020)
                            val[0] |= 0x1;
                    }
#endif
#if NWOAS_CCS_STAB
                    // Session4 CCS-stabilization experiment (see the NWOAS_CCS_STAB macro comment).
                    // Mask the flaky FL1100 link's CCS drops so usbxhci's port-reset handshake can
                    // complete. Latch per target port on the first CCS=1 seen; thereafter OR-in bit0
                    // (CCS) on kernel-VA reads. All other PORTSC bits pass through unchanged.
                    if ((elr >> 48) == 0xffff && (off == 0x490 || off == 0x4c0)) {
                        static u8 nwoas_ccs_seen = 0;   // bit1=port0x490, bit2=port0x4c0
                        static u32 nwoas_ccs_masks = 0; // throttle the log
                        u8 pbit = (off == 0x490) ? 1 : 2;
                        if (val[0] & 1)
                            nwoas_ccs_seen |= pbit;
                        else if (nwoas_ccs_seen & pbit) {
                            u64 raw = val[0];
                            val[0] |= 1; // hold CCS=1 so the device stays "connected" across the drop
                            if (nwoas_ccs_masks < 32) {
                                nwoas_ccs_masks++;
                                printf("HVLOG: CCS-STAB off=0x%lx raw=0x%lx -> 0x%lx elr=0x%lx\n", off,
                                       raw, val[0], elr);
                            }
                        }
                    }
#endif
                    // NWOAS §6-A: the livelock is usbxhci polling USBSTS (off=0x84) forever while
                    // it waits on a completion event that never appears. Piggy-back on that hot
                    // poll to dump the physical event ring from EL2. Heavily throttled (every
                    // 16384th poll, cap 16 dumps) so it fires DURING the livelock without flooding
                    // the vuart or perturbing timing. Only reads DRAM -> cannot alter guest state.
                    // GATE on the WINDOWS kernel: (elr>>48)==0xffff selects canonical kernel VAs
                    // (0xfffff8...); UEFI XhciDxe runs at low VAs (elr>>48==0). The first hardware
                    // run wasted all 16 dumps on UEFI's ring (erstba=0x103140) before Windows even
                    // loaded — we need Windows's ring, which is the one that livelocks.
                    if (off == 0x84 && nwoas_fl_erstba && (elr >> 48) == 0xffff) {
#if NWOAS_SYNTH_PSCE
                        // THE FIX: on the USBSTS poll usbxhci spins in during the storm, feed it the Port
                        // Status Change Event it is starving for (generate into the ring + deliver 698).
                        // Throttled to every 16th poll (cheap: a few guarded MMIO reads + dedup) so it
                        // reacts within ~µs of a port change without perturbing the hot path.
                        // Fire on EVERY USBSTS read (was every 16th). Now that the takeable-core fix
                        // makes 698 actually deliverable, the ISR reads USBSTS only a few times before it
                        // finds an empty ring and returns (then 698 re-delivers -> a loop that stalls the
                        // boot CPU). Feeding a valid PSCE on the FIRST USBSTS read lets the ISR consume it
                        // -> advance ERDP -> clear IMAN.IP -> the 698 loop stops and the boot proceeds.
                        // The internal per-port dedup (nwoas_synth_done) prevents redundant writes.
#if !NWOAS_STORM_TAME && !NWOAS_COH_KEEP
                        // Confound-off during storm-tame OR the coherence experiment (NWOAS_COH_KEEP): with
                        // synth disabled, nwoas_synth_pending stays 0 so the EINT/IP read-emulation above is
                        // inert too -> IMAN.IP/EINT/ERDP reflect ONLY the real FL1100. A post is then
                        // unambiguously the controller's (not a substituted synthetic PSCE).
                        nwoas_synth_check(ctx);
#endif
#endif
                        // session5: sample the RAW controller IMAN.IP/USBSTS.EINT on EVERY USBSTS poll
                        // (no throttle here -- nwoas_track_posting keeps its own internal counters/
                        // throttle) so we don't miss a rare/transient FL1100 post between the FORK
                        // throttle's sparser samples. Read-only.
                        nwoas_track_posting();
#if NWOAS_LOW_EVENT_SEED
                        nwoas_seed_poll(ctx);
#endif
#if NWOAS_COH_KEEP
                        // Coherence keep-fresh on the USBSTS poll too (throttled, cheap 32B path): the
                        // FL1100 reads ERST at RS=1 -- potentially many polls after the last ERSTBA/ERDP
                        // write -- and usbxhci may re-dirty ERST/DCBAA between interrupter writes. Re-clean
                        // 16B ERST[0] + 16B DCBAA every 64th poll (ring_page=false: the 4KB ring flush is
                        // done at the ERSTBA-write trigger, needless on the hottest path). Far more often
                        // than the §6-E one-shot, rare enough not to perturb the poll timing.
                        static u32 nwoas_coh_poll = 0;
                        if ((nwoas_coh_poll++ & 0x3f) == 0)
                            nwoas_coh_keep_fresh(ctx, false);
                        // Variant B: log the FL1100 PCIe Device Control No Snoop / Relaxed Ordering bits
                        // from ECAM config space (once/boot, guarded, self-throttling via its static done).
                        nwoas_log_devctl();
#endif
                        // FORK is 1 lightweight line -> fire it ~4x more often (and with a bigger cap) so
                        // it captures the fast RS=1->HCH=1 storm before the post-storm serial goes quiet.
                        static u32 nwoas_fork_poll = 0;
                        static u32 nwoas_fork_dumps = 0;
                        if (nwoas_fork_dumps < 48 && (nwoas_fork_poll++ & 0xfff) == 0) {
                            nwoas_fork_dumps++;
                            nwoas_dump_fork(elr);            // session4: RS/HCH/scratch/port snapshot
                        }
                        // Full dumps (event ring + DART + scratchpad reachability) are heavier -> rarer.
                        static u32 nwoas_evt_poll = 0;
                        static u32 nwoas_evt_dumps = 0;
                        if (nwoas_evt_dumps < 16 && (nwoas_evt_poll++ & 0x3fff) == 0) {
                            nwoas_evt_dumps++;
                            nwoas_dump_evtring(ctx, elr);
                            nwoas_dump_dart_dcbaa(ctx, elr); // §6-D: runtime DART reachability + DCBAA + scratch
                        }
                    }
                    // run11 showed the post-boot.wim stall is a USB port change-event storm
                    // (USBSTS.PCD stuck, ERDP advancing, WFI refuted). Log-on-change for
                    // USBSTS (0x84) and PORTSC[0..7] (0x480..0x4f0) captures exactly which
                    // port's change bits (CSC/PEC/PRC/PLC/WRC) keep toggling, without
                    // flooding. ERDP (0x2038) advances constantly, so sample it at a low rate
                    // as a liveness heartbeat.
                    // NWOAS §6-C coherence: when usbxhci reads USBSTS with EINT set (an event is
                    // pending), invalidate the event ring so the CPU reads the FL1100's DMA'd
                    // completions from DRAM instead of a stale cached copy.
                    // A real FL1100 EINT requires invalidating the device-written event ring before
                    // Windows consumes it.  Skip only while an hv-generated PSCE is pending because
                    // the synth path already publishes its TRB through the coherent CPU cache.
                    // S151: NWOAS_COH_KEEP intentionally disables synthetic PSCE generation, but
                    // NWOAS_SYNTH_PSCE remains compiled in.  Do not let that compile-time setting
                    // suppress visibility of a real FL1100 event.  Invalidate only for a real EINT;
                    // a pending hv-generated PSCE already lives in the coherent CPU cache and must
                    // not be discarded by this device-DMA maintenance path.
                    if (off == 0x84 && (val[0] & 0x8) && !nwoas_synth_pending &&
                        (elr >> 48) == 0xffff)
                        nwoas_fl_inval_evt(ctx);
                    // D48: one deferred post-consume snapshot.  It shows the
                    // event TRB Windows just consumed and whether usbxhci then
                    // placed Enable Slot / Address Device on the command ring.
                    // No controller or guest-memory writes are issued here.
                    if (off == 0x84 && nwoas_postconsume_pending &&
                        !nwoas_postconsume_dumped && (elr >> 48) == 0xffff) {
                        nwoas_postconsume_pending = false;
                        nwoas_postconsume_dumped = true;
                        printf("HVLOG: POST-CONSUME snapshot elr=0x%lx\n", elr);
                        nwoas_dump_evtring(ctx, elr);
                        nwoas_dump_dart_dcbaa(ctx, elr);
                    }
                    int slot = -1;
                    if (off == 0x84)
                        slot = 0;
                    else if (off >= 0x480 && off <= 0x4f0 && (off & 0xf) == 0)
                        slot = 1 + (int)((off - 0x480) >> 4);
                    else if (off == 0x80)        // USBCMD (RS bit0 / HCRST bit1 / INTE bit2)
                        slot = 9;
                    if (slot >= 0 && slot <= 9) {
                        static u64 nwoas_fl_last[10];
                        static u16 nwoas_fl_seen = 0;
                        static u32 nwoas_fl_changes = 0;
                        if (!(nwoas_fl_seen & (1 << slot)) || nwoas_fl_last[slot] != val[0]) {
                            if (nwoas_fl_changes < 1000) {
                                nwoas_fl_changes++;
                                printf("HVLOG: NWOAS-FLC off=0x%lx val=0x%lx -> 0x%lx elr=0x%lx\n",
                                       off,
                                       (nwoas_fl_seen & (1 << slot)) ? nwoas_fl_last[slot] : ~0UL,
                                       val[0], elr);
                            }
                            nwoas_fl_last[slot] = val[0];
                            nwoas_fl_seen |= (1 << slot);
                        }
                    } else if (off == 0x2038) {
                        static u32 nwoas_erdp_n = 0;
                        if ((nwoas_erdp_n++ & 0xffff) == 0)
                            printf("HVLOG: NWOAS-ERDP val=0x%lx elr=0x%lx\n", val[0], elr);
                    }
                }
                break;
            case SPTE_HOOK: {
                hv_wdt_breadcrumb('5');
                hv_hook_t *hook = (hv_hook_t *)target;
                if (!hook(ctx, ipa, val, false, width))
                    return false;
                dprintf("HV: SPTE_HOOK[R] @0x%lx 0x%lx -> 0x%lx (w=%d) @%p: 0x%lx\n", elr, ipa,
                        paddr, 1 << width, hook, val);
                break;
            }
            case SPTE_PROXY_HOOK_RW:
            case SPTE_PROXY_HOOK_R: {
                if (nwoas_nvme_fastpath_mmio(ctx, ipa, val, false, width))
                    break;
                hv_wdt_breadcrumb('6');
                struct hv_vm_proxy_hook_data hook = {
                    .flags = FIELD_PREP(MMIO_EVT_WIDTH, width) | flags,
                    .id = FIELD_GET(PTE_TARGET_MASK_L4, pte),
                    .addr = ipa,
                };
                hv_exc_proxy(ctx, START_HV, HV_HOOK_VM, &hook);
                memcpy(val, hook.data, 1 << width);
                break;
            }
            default:
                printf("HV: invalid SPTE 0x%016lx for IPA 0x%lx\n", pte, ipa);
                return false;
        }

        hv_wdt_breadcrumb('7');
        if (pte & SPTE_TRACE_READ)
            emit_mmiotrace(elr, ipa, val, width, flags, pte & SPTE_TRACE_UNBUF);
    }

    hv_wdt_breadcrumb('*');

    return true;
}

static bool hv_emulate_rw(struct exc_info *ctx, u64 pte, u64 vaddr, u64 ipa, u8 *val, bool is_write,
                          u64 bytes, u64 elr, u64 par)
{
    u64 aval[HV_MAX_RW_WORDS];

    bool advance = (IS_HW(pte) || (IS_SW(pte) && FIELD_GET(SPTE_TYPE, pte) == SPTE_MAP)) ? 1 : 0;
    u64 off = 0;
    u64 width;

    bool first = true;

    u64 left = bytes;
    u64 paddr = (pte & PTE_TARGET_MASK_L4) | (vaddr & MASK(VADDR_L4_OFFSET_BITS));

    while (left > 0) {
        memset(aval, 0, sizeof(aval));

        if (left >= 64 && (ipa & 63) == 0)
            width = 6;
        else if (left >= 32 && (ipa & 31) == 0)
            width = 5;
        else if (left >= 16 && (ipa & 15) == 0)
            width = 4;
        else if (left >= 8 && (ipa & 7) == 0)
            width = 3;
        else if (left >= 4 && (ipa & 3) == 0)
            width = 2;
        else if (left >= 2 && (ipa & 1) == 0)
            width = 1;
        else
            width = 0;

        u64 chunk = 1 << width;

        /*
        if (chunk != bytes)
            printf("HV: Splitting unaligned %ld-byte %s: %ld bytes @ 0x%lx\n", bytes,
                is_write ? "write" : "read", chunk, vaddr);
        */

        if (is_write)
            memcpy(aval, val + off, chunk);

        if (advance)
            pte = (paddr & PTE_TARGET_MASK_L4) | (pte & ~PTE_TARGET_MASK_L4);

        if (!hv_emulate_rw_aligned(ctx, pte, vaddr, ipa, aval, is_write, width, elr, par)) {
            if (!first)
                printf("HV: WARNING: Failed to emulate split op but part of it did commit!\n");
            return false;
        }

        if (!is_write)
            memcpy(val + off, aval, chunk);

        left -= chunk;
        off += chunk;

        ipa += chunk;
        vaddr += chunk;
        if (advance)
            paddr += chunk;

        first = 0;
    }

    return true;
}

// NWOAS stage-8: capture bootmgfw's OWN serial output. bootmgfw drives the S5L UART via a
// mapping the vuart hook doesn't capture (its UTXH writes route to a non-vuart pte), so its
// boot log / panic message is invisible in the vuart vlog. Snoop every UTXH (offset 0x020)
// write here in the abort path (post instruction-decode, so the byte is in val[]) into a
// dedicated ring, dumped at the PSCI reset. Non-perturbing: a plain ring store, no serial I/O.
#define BMGFW_UART_SZ 65536
static u8 bmgfw_uart_ring[BMGFW_UART_SZ];
static u32 bmgfw_uart_idx;

void hv_bmgfw_uart_dump(void)
{
    u32 total = bmgfw_uart_idx;
    u32 n = total < BMGFW_UART_SZ ? total : BMGFW_UART_SZ;
    u32 start = total - n;
    printf("[bmgfw-uart] last %u bytes of UART UTXH writes (incl. bootmgfw):\n", n);
    char line[160];
    u32 li = 0;
    for (u32 k = 0; k < n; k++) {
        char c = bmgfw_uart_ring[(start + k) & (BMGFW_UART_SZ - 1)];
        if (c == '\r')
            continue;
        if (c == '\n' || li >= sizeof(line) - 1) {
            line[li] = 0;
            printf("B| %s\n", line);
            li = 0;
            if (c == '\n')
                continue;
        }
        line[li++] = c;
    }
    if (li) {
        line[li] = 0;
        printf("B| %s\n", line);
    }
    printf("[bmgfw-uart] end\n");
}

bool hv_handle_dabort(struct exc_info *ctx)
{
    hv_wdt_breadcrumb('0');
    u64 esr = hv_get_esr();
    bool is_write = esr & ESR_ISS_DABORT_WnR;

    u64 far = hv_get_far();
    u64 par;
    u64 ipa = hv_translate(far, true, is_write, &par);

    dprintf("hv_handle_abort(): stage 1 0x%0lx -> 0x%lx\n", far, ipa);

    if (!ipa) {
        printf("HV: stage 1 translation failed at VA 0x%0lx\n", far);
        return false;
    }

    if (ipa >= BIT(vaddr_bits)) {
        printf("hv_handle_abort(): IPA out of bounds: 0x%0lx -> 0x%lx\n", far, ipa);
        return false;
    }

    u64 pte = hv_pt_walk(ipa);

    if (!pte) {
        // NWOAS: a runaway *secondary* guest vCPU can storm unmapped-IPA aborts (a
        // phys_base-sensitive guest-payload AP stack-underflow bug lands SP just below
        // phys_base, in the RAM-LOW..RAM-HIGH gap). The stock path calls hv_exc_proxy
        // here (a blocking 115200-baud register dump) while holding bhl; under a headless
        // (stdin-EOF) run the proxy returns instantly and the AP re-faults forever,
        // starving the boot/pinned CPU of bhl -> global wedge (confirmed: evtdump-1951 +
        // chainload-1523, heartbeats stop). The parked AP is already permanently broken,
        // so once it storms, exit it FROM the guest cleanly instead of dumping: it unwinds
        // to the outer-m1n1 park loop, drops out of hv_cpus_in_guest, and stops taking bhl.
        // NEVER auto-park the boot or pinned CPU (they run the guest kernel; a real kernel
        // crash there must still surface via the proxy).
        int cpu = smp_id();
        static u64 nwoas_last_unmapped_elr[MAX_CPUS];
        static u32 nwoas_unmapped_streak[MAX_CPUS];
        u64 elr = ctx->elr;
        if (cpu < MAX_CPUS && elr == nwoas_last_unmapped_elr[cpu]) {
            nwoas_unmapped_streak[cpu]++;
        } else {
            if (cpu < MAX_CPUS) {
                nwoas_last_unmapped_elr[cpu] = elr;
                nwoas_unmapped_streak[cpu] = 1;
            }
            printf("HV: Unmapped IPA 0x%lx (cpu%d elr=0x%lx)\n", ipa, cpu, elr);
        }
        if (cpu < MAX_CPUS && cpu != boot_cpu_idx && cpu != hv_pinned_cpu &&
            nwoas_unmapped_streak[cpu] == NWOAS_UNMAPPED_PARK_THRESHOLD) {
            printf("HV: NWOAS parking runaway guest cpu%d (unmapped-IPA storm @elr=0x%lx "
                   "ipa=0x%lx) to prevent bhl-starvation wedge\n", cpu, elr, ipa);
            hv_exit_cpu(cpu);   // hv_maybe_exit() in hv_exc_exit() parks it after this return
            // hv_should_exit[cpu] now stays set for good (hv_start_secondary never clears it),
            // so if the guest ever re-CPU_ON's this broken core it takes exactly ONE proxy dump
            // then immediately re-exits -- it never re-enters the storm and bhl is released at
            // once (no re-park flood). This is deliberately more protective than clearing the
            // flag, which would reopen a 15x blocking-dump window while the streak re-climbs.
            // The reset below is thus belt-and-suspenders for the counter only.
            nwoas_unmapped_streak[cpu] = 0;
            return true;        // skip the blocking proxy dump; elr+=4 is discarded by hv_exit_guest
        }
        return false;
    }

    if (IS_HW(pte)) {
        printf("HV: Data abort on mapped page (0x%lx -> 0x%lx)\n", far, pte);
        // Try again, this is usually a race
        ctx->elr -= 4;
        return true;
    }

    hv_wdt_breadcrumb('1');

    assert(IS_SW(pte));

    u64 elr = ctx->elr;
    u64 elr_pa = hv_translate(elr, false, false, NULL);
    if (!elr_pa) {
        printf("HV: Failed to fetch instruction for data abort at 0x%lx\n", elr);
        return false;
    }

    u32 insn = read32(elr_pa);
    u64 width;

    hv_wdt_breadcrumb('2');

    u64 vaddr = far;

    u8 val[HV_MAX_RW_SIZE] ALIGNED(HV_MAX_RW_SIZE);
    memset(val, 0, sizeof(val));

    if (is_write) {
        hv_wdt_breadcrumb('W');

        if (!emulate_store(ctx, insn, (u64 *)val, &width, &vaddr)) {
            printf("HV: store not emulated: 0x%08x at 0x%lx\n", insn, ipa);
            return false;
        }
    } else {
        hv_wdt_breadcrumb('R');

        if (!emulate_load(ctx, insn, NULL, &width, &vaddr)) {
            printf("HV: load not emulated: 0x%08x at 0x%lx\n", insn, ipa);
            return false;
        }
    }

    // NWOAS: snoop UART TX (UTXH, offset 0x020 of the S5L UART @ 0x235200000) so bootmgfw's
    // own serial output is captured even when it routes to a non-vuart pte.
    if (is_write && (far & ~0xfffUL) == 0x235200000UL && (far & 0xfffUL) == 0x020UL)
        bmgfw_uart_ring[(bmgfw_uart_idx++) & (BMGFW_UART_SZ - 1)] = val[0];

    /*
     Check for HW page-straddling conditions
     Right now we only support the case where the page boundary is exactly halfway
     through the read/write.
    */
    u64 bytes = 1 << width;
    u64 vaddrp0 = vaddr & ~MASK(VADDR_L3_OFFSET_BITS);
    u64 vaddrp1 = (vaddr + bytes - 1) & ~MASK(VADDR_L3_OFFSET_BITS);

    if (vaddrp0 == vaddrp1) {
        // Easy case, no page straddle
        if (far != vaddr) {
            printf("HV: faulted at 0x%lx, but expecting 0x%lx\n", far, vaddr);
            return false;
        }

        if (!hv_emulate_rw(ctx, pte, vaddr, ipa, val, is_write, bytes, elr, par))
            return false;
    } else {
        // Oops, we're straddling a page boundary
        // Treat it as two separate loads or stores

        assert(bytes > 1);
        hv_wdt_breadcrumb('s');

        u64 off = vaddrp1 - vaddr;

        u64 vaddr2;
        const char *other;
        if (far == vaddr) {
            other = "upper";
            vaddr2 = vaddrp1;
        } else {
            if (far != vaddrp1) {
                printf("HV: faulted at 0x%lx, but expecting 0x%lx\n", far, vaddrp1);
                return false;
            }
            other = "lower";
            vaddr2 = vaddr;
        }

        u64 par2;
        u64 ipa2 = hv_translate(vaddr2, true, esr & ESR_ISS_DABORT_WnR, &par2);
        if (!ipa2) {
            printf("HV: %s half stage 1 translation failed at VA 0x%0lx\n", other, vaddr2);
            return false;
        }
        if (ipa2 >= BIT(vaddr_bits)) {
            printf("hv_handle_abort(): %s half IPA out of bounds: 0x%0lx -> 0x%lx\n", other, vaddr2,
                   ipa2);
            return false;
        }

        u64 pte2 = hv_pt_walk(ipa2);
        if (!pte2) {
            printf("HV: Unmapped %s half IPA 0x%lx\n", other, ipa2);
            return false;
        }

        hv_wdt_breadcrumb('S');

        printf("HV: Emulating %s straddling page boundary as two ops @ 0x%lx (%ld bytes)\n",
               is_write ? "write" : "read", vaddr, bytes);

        bool upper_ret;
        if (far == vaddr) {
            if (!hv_emulate_rw(ctx, pte, vaddr, ipa, val, is_write, off, elr, par))
                return false;
            upper_ret =
                hv_emulate_rw(ctx, pte2, vaddr2, ipa2, val + off, is_write, bytes - off, elr, par2);
        } else {
            if (!hv_emulate_rw(ctx, pte2, vaddr2, ipa2, val, is_write, off, elr, par2))
                return false;
            upper_ret =
                hv_emulate_rw(ctx, pte, vaddrp1, ipa, val + off, is_write, bytes - off, elr, par);
        }

        if (!upper_ret) {
            printf("HV: WARNING: Failed to emulate upper half but lower half did commit!\n");
            return false;
        }
    }

    if (vaddrp0 != vaddrp1) {
        printf("HV: Straddled r/w data:\n");
        hexdump(val, bytes);
    }

    hv_wdt_breadcrumb('8');
    if (!is_write && !emulate_load(ctx, insn, (u64 *)val, &width, &vaddr))
        return false;

    hv_wdt_breadcrumb('9');

    return true;
}
