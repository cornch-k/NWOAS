/* S208 bounded compiled harness for the changed EOI-note call site.
 *
 * It does NOT re-implement the logic under test. It #includes fragments that
 * are extracted verbatim (by gen.sh, with `sed`) from the candidate worktree
 * src/hv_exc.c: the real `nwoas_nvme_note_eoi` definition + its state, and the
 * real maintenance-EOI loop that contains the changed call site
 * `nwoas_nvme_note_eoi(intd, hv_get_elr());`. Everything the fragments call
 * (hv_get_elr, hv_vgic3_read_lr/write_lr, aic_set_mask, mrs, BIT, ICH_LR_*)
 * is stubbed here so the fragment compiles and runs natively.
 *
 * Two goals:
 *   (1) Poisoned context vs fake architectural PC: hv_get_elr() returns a known
 *       ARCH_PC; a distinct POISON stands for the never-initialized ctx->elr.
 *       The fixed call site must record ARCH_PC, never POISON.
 *   (2) An unrelated interrupt (intd != 900) must not touch the NVMe counters.
 *
 * A contrast unit (harness_s163.c, same fragments but with the pre-fix
 * `ctx->elr` call site) shows the defect it fixes: it records POISON.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;

/* ---- architectural / GIC stubs the extracted fragment references ---- */
#define BIT(x) (1UL << (x))
#include "frag_gic_fields.inc"
#define CNTPCT_EL0 0 /* token; mrs() below ignores it */

static u64 g_arch_pc;     /* what hv_get_elr() returns (real ELR_EL2) */
static u64 g_fake_clock;  /* monotonic-ish counter for mrs(CNTPCT_EL0) */
static u64 g_lr_value[8]; /* per-LR value returned by hv_vgic3_read_lr */
static int g_lr_cleared[8];
static int g_aic_unmasked_cnt;

static inline u64 mrs_impl(void) { return ++g_fake_clock; }
#define mrs(reg) mrs_impl()

static u64 hv_get_elr(void) { return g_arch_pc; }
static u64 hv_vgic3_read_lr(int lr) { return g_lr_value[lr]; }
static void hv_vgic3_write_lr(int lr, u64 v) { (void)v; g_lr_cleared[lr] = 1; }
static void aic_set_mask(u64 intd, int m) { (void)intd; (void)m; g_aic_unmasked_cnt++; }

/* ---- verbatim fragment 1: state + nwoas_nvme_note_eoi (from hv_exc.c) ---- */
#include "frag_note_eoi.inc"

/* Pre-fix contrast build defines S208_CONTRAST and supplies ctx->elr. */
#ifdef S208_CONTRAST
struct exc_info { u64 elr; };
static struct exc_info g_ctx;
#define CTX_PTR (&g_ctx)
#endif

/* ---- run the extracted maintenance-EOI loop once ---- */
static void run_eoi_loop(u64 eisr)
{
    u64 misr = 1;
#ifdef S208_CONTRAST
    struct exc_info *ctx = CTX_PTR;
    (void)ctx;
#include "frag_loop_s163.inc"
#else
#include "frag_loop.inc"
#endif
}

static void reset_state(void)
{
    nwoas_nvme_last_eoi = 0;
    nwoas_nvme_irq_injections = 0;
    nwoas_nvme_irq_eois = 0;
    nwoas_nvme_irq_gated = 0;
    nwoas_nvme_irq_last_pc = 0;
    nwoas_nvme_eoi_last_pc = 0;
    memset(g_lr_cleared, 0, sizeof(g_lr_cleared));
    g_aic_unmasked_cnt = 0;
    g_fake_clock = 0;
}

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", (msg)); fails++; } \
    else printf("ok  : %s\n", (msg)); } while (0)

#define ARCH_PC  0xAAAA0000BBBB0000UL
#define POISON   0xDEADBEEFCAFEF00DUL

int main(void)
{
#ifdef S208_CONTRAST
    const char *variant = "S163 (pre-fix, ctx->elr)";
#else
    const char *variant = "S208 (fixed, hv_get_elr)";
#endif
    printf("== variant: %s ==\n", variant);

    g_arch_pc = ARCH_PC;
#ifdef S208_CONTRAST
    g_ctx.elr = POISON;           /* uninitialized-stack stand-in */
#endif

    /* Case 1: NVMe interrupt (intd == 900) in LR0. */
    reset_state();
    g_lr_value[0] = (u64)900 << ICH_LR_VIRTUAL_SHIFT;
    run_eoi_loop(BIT(0));
    CHECK(nwoas_nvme_irq_eois == 1, "intd=900 records exactly one EOI");
    CHECK(g_lr_cleared[0] == 1, "LR0 cleared (delivery semantics preserved)");
#ifdef S208_CONTRAST
    CHECK(nwoas_nvme_eoi_last_pc == POISON,
          "pre-fix records POISON (demonstrates the defect)");
#else
    CHECK(nwoas_nvme_eoi_last_pc == ARCH_PC,
          "fixed records architectural PC, not poisoned context");
    CHECK(nwoas_nvme_eoi_last_pc != POISON,
          "fixed never records the poisoned context value");
#endif

    /* Case 2: unrelated interrupt (intd != 900) must not touch NVMe counters. */
    reset_state();
    g_lr_value[0] = (u64)902 << ICH_LR_VIRTUAL_SHIFT;  /* some other SPI */
    run_eoi_loop(BIT(0));
    CHECK(nwoas_nvme_irq_eois == 0, "unrelated IRQ leaves eoi counter at 0");
    CHECK(nwoas_nvme_eoi_last_pc == 0, "unrelated IRQ leaves eoi_last_pc at 0");
    CHECK(nwoas_nvme_last_eoi == 0, "unrelated IRQ leaves last_eoi timestamp at 0");
    CHECK(g_lr_cleared[0] == 1, "unrelated IRQ still EOIs/clears its own LR");
    CHECK(g_aic_unmasked_cnt == 1, "unrelated SPI (>31) still unmasked at AIC");

    /* Case 3: mixed batch, NVMe in LR2 among unrelated LRs. */
    reset_state();
    g_lr_value[1] = (u64)33  << ICH_LR_VIRTUAL_SHIFT;
    g_lr_value[2] = (u64)900 << ICH_LR_VIRTUAL_SHIFT;
    g_lr_value[3] = (u64)41  << ICH_LR_VIRTUAL_SHIFT;
    run_eoi_loop(BIT(1) | BIT(2) | BIT(3));
    CHECK(nwoas_nvme_irq_eois == 1, "one NVMe EOI counted in a mixed batch");
#ifndef S208_CONTRAST
    CHECK(nwoas_nvme_eoi_last_pc == ARCH_PC, "mixed batch records arch PC for NVMe");
#endif
    CHECK(g_lr_cleared[1] && g_lr_cleared[2] && g_lr_cleared[3],
          "all EOId LRs cleared in mixed batch");

    printf("== %s: %d failure(s) ==\n", variant, fails);
    return fails ? 1 : 0;
}
