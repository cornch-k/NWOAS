/* SPDX-License-Identifier: MIT */
/*
 * NWOAS gpu-hv2 offline tests (host, no hardware). Compiles the UNMODIFIED
 * gpu-hv2 pure-logic sources (config snapshot, guest IPA->PA HVC, power broker +
 * J274 power ops, local reserve, scanout broker, Hwdata profile check) and runs
 * them against the live J274 13.5 ADT blob given as argv[1].
 * Output: one "PASS"/"FAIL"/"INFO" line per check; exit 1 on any FAIL.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adt_host.h"
#include "hv_agx_config_snapshot.h"
#include "hv_guest_ipa_pa.h"
#include "hv_agx_power_broker.h"
#include "hv_agx_local_reserve.h"
#include "hv_agx_scanout_broker.h"
#include "apple_agx_hwdata_profile.h"

uint64_t nwoas_agx_gpu_base;
uint32_t nwoas_agx_base_pstate_override;
static int failures;
int nwoas_scanout_service_tests(void); /* test_scanout_svc.c (NWOAS S251) */
#define CHECK(cond, ...) do { int ok_ = (cond); printf("%s ", ok_ ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!ok_) failures++; } while (0)
#define INFO(...) do { printf("INFO "); printf(__VA_ARGS__); printf("\n"); } while (0)

static int sgx, asc, armio;

static uint64_t get_u64(int node, const char *name, int *ok)
{
    uint64_t v = 0;
    *ok = adt_getprop_copy(NULL, node, name, &v, 8) == 8;
    return v;
}

/* reg[i] of an /arm-io child translated through /arm-io "ranges" */
static int arm_io_reg(int node, int idx, uint64_t *base, uint64_t *size)
{
    uint32_t len, ac = 2, sc = 2, rl;
    adt_getprop_copy(NULL, armio, "#address-cells", &ac, 4);
    adt_getprop_copy(NULL, armio, "#size-cells", &sc, 4);
    const uint32_t *r = adt_getprop(NULL, node, "reg", &len);
    if (!r || (size_t)(idx + 1) * (ac + sc) * 4 > len) return -1;
    const uint32_t *e = r + idx * (ac + sc);
    uint64_t a = e[0] | (ac == 2 ? (uint64_t)e[1] << 32 : 0);
    uint64_t s = e[ac] | (sc == 2 ? (uint64_t)e[ac + 1] << 32 : 0);
    const uint32_t *rg = adt_getprop(NULL, armio, "ranges", &rl);
    uint32_t st = (ac + 2 + sc) * 4;
    for (uint32_t o = 0; rg && o + st <= rl; o += st) {
        const uint32_t *x = (const uint32_t *)((const uint8_t *)rg + o);
        uint64_t cb = x[0] | (ac == 2 ? (uint64_t)x[1] << 32 : 0);
        uint64_t pb = x[ac] | (uint64_t)x[ac + 1] << 32;
        uint64_t rs = x[ac + 2] | (sc == 2 ? (uint64_t)x[ac + 3] << 32 : 0);
        if (a >= cb && a + s <= cb + rs) { *base = pb + (a - cb); *size = s; return 0; }
    }
    return -1;
}

static int read_irqs(int node, uint32_t *out, int cap)
{
    uint32_t len;
    const uint32_t *p = adt_getprop(NULL, node, "interrupts", &len);
    if (!p || len % 4) return -1;
    int n = (int)(len / 4);
    for (int i = 0; i < n && i < cap; i++) out[i] = p[i];
    return n;
}

struct irq_scan { uint32_t want; int hits; char who[128]; };
static void scan_irq(int node, void *ctx)
{
    struct irq_scan *s = ctx;
    uint32_t len;
    const uint32_t *p = adt_getprop(NULL, node, "interrupts", &len);
    for (uint32_t i = 0; p && i < len / 4; i++)
        if (p[i] == s->want) {
            uint32_t nl; const char *n = adt_getprop(NULL, node, "name", &nl);
            s->hits++; snprintf(s->who, sizeof(s->who), "%s", n ? n : "?");
        }
}

/* ---- fake pmgr for hv_agx_power_platform.c (HV_AGX_POWER_BROKER_HOST_TEST) ---- */
static char pm_log[512];
int pmgr_adt_power_enable(const char *path) { strcat(pm_log, "+"); strcat(pm_log, path); strcat(pm_log, " "); return 0; }
int pmgr_adt_power_disable(const char *path) { strcat(pm_log, "-"); strcat(pm_log, path); strcat(pm_log, " "); return 0; }

/* ---- fake guest memory for the HVC 0x4d31 handler ---- */
static struct hv_guest_ipa_pa_request req __attribute__((aligned(0x4000)));
static uint64_t fake_translate(uint64_t ipa, void *o) { (void)o; if (ipa == 0x1000000) return (uint64_t)(uintptr_t)&req; return ipa >= 0x880000000ull && ipa < 0x900000000ull ? ipa : 0; }
static bool fake_is_ram(uint64_t pa, uint64_t sz, void *o) { (void)o; return pa == (uint64_t)(uintptr_t)&req || (pa >= 0x880000000ull && pa + sz <= 0x900000000ull); }

static const unsigned char *read_sgx_prop(void *c, const char *name, unsigned int *len)
{
    return adt_getprop(NULL, *(int *)c, name, len);
}

static bool ident(void *c, uint64_t ipa, uint64_t *pa) { (void)c; *pa = ipa; return true; }

int main(int argc, char **argv)
{
    if (argc < 2 || adt_load(argv[1])) { fprintf(stderr, "usage: %s adt.bin\n", argv[0]); return 2; }
    armio = adt_path_offset(NULL, "/arm-io");
    sgx = adt_path_offset(NULL, "/arm-io/sgx");
    asc = adt_path_offset(NULL, "/arm-io/gfx-asc");
    CHECK(sgx > 0 && asc > 0, "ADT has /arm-io/sgx and /arm-io/gfx-asc");

    uint64_t sb = 0, ss = 0, s1 = 0, s1s = 0, ab = 0, as = 0, a1 = 0, a1s = 0;
    arm_io_reg(sgx, 0, &sb, &ss); arm_io_reg(sgx, 1, &s1, &s1s);
    arm_io_reg(asc, 0, &ab, &as); arm_io_reg(asc, 1, &a1, &a1s);
    INFO("sgx reg[0]=0x%llx+0x%llx reg[1]=0x%llx+0x%llx", sb, ss, s1, s1s);
    INFO("gfx-asc reg[0]=0x%llx+0x%llx reg[1]=0x%llx+0x%llx", ab, as, a1, a1s);
    CHECK(ab >= sb && ab + as <= sb + ss, "gfx-asc reg[0] inside sgx reg[0] (one ACPI memory resource covers both)");

    uint32_t si[32], ai[32];
    int ns = read_irqs(sgx, si, 32), na = read_irqs(asc, ai, 32);
    printf("INFO sgx interrupts (%d):", ns); for (int i = 0; i < ns; i++) printf(" %u", si[i]); printf("\n");
    printf("INFO gfx-asc interrupts (%d):", na); for (int i = 0; i < na; i++) printf(" %u", ai[i]); printf("\n");
    CHECK(ns + na == 9, "9 AGX hardware interrupts (sgx + gfx-asc)");
    uint32_t cand[] = {889, 900};
    for (unsigned k = 0; k < 2; k++) {
        struct irq_scan s = {cand[k], 0, ""};
        adt_walk(scan_irq, &s);
        CHECK(s.hits == 0, "INTID %u not used by any ADT node (%s)", cand[k], s.hits ? s.who : "free");
    }

    int ok1, ok2;
    uint64_t gb = get_u64(sgx, "gpu-region-base", &ok1), gs = get_u64(sgx, "gpu-region-size", &ok2);
    uint64_t hb = get_u64(sgx, "gfx-handoff-base", &ok1), hs = get_u64(sgx, "gfx-handoff-size", &ok2);
    uint64_t shb = get_u64(sgx, "gfx-shared-region-base", &ok1), shs = get_u64(sgx, "gfx-shared-region-size", &ok2);
    INFO("gpu-region 0x%llx+0x%llx handoff 0x%llx+0x%llx shared 0x%llx+0x%llx", gb, gs, hb, hs, shb, shs);

    /* carveouts */
    int cm = adt_path_offset(NULL, "/chosen/carveout-memory-map");
    uint64_t big[2] = {0, 0}; char bigname[33] = "";
    int hit[3] = {0, 0, 0};
    uint64_t rg[3][2] = {{gb, gs}, {hb, hs}, {shb, shs}};
    for (int p = adt_first_prop(cm), i = 0; cm > 0 && i < adt_nprops(cm); i++, p = adt_next_prop(p)) {
        uint32_t len; const uint64_t *v = adt_prop_value(p, &len);
        if (strncmp(adt_prop_name(p), "region-id-", 10) || len != 16) continue;
        uint64_t r[2]; memcpy(r, v, 16);
        if (r[1] > big[1]) { big[0] = r[0]; big[1] = r[1]; snprintf(bigname, sizeof(bigname), "%s", adt_prop_name(p)); }
        for (int k = 0; k < 3; k++) if (rg[k][0] >= r[0] && rg[k][0] + rg[k][1] <= r[0] + r[1]) hit[k]++;
    }
    INFO("normal-DRAM owner (largest carveout): %s = 0x%llx+0x%llx", bigname, big[0], big[1]);
    CHECK(hit[0] == 1 && hit[1] == 1 && hit[2] == 1, "each GPU range inside exactly one carveout (%d/%d/%d)", hit[0], hit[1], hit[2]);

    /* config snapshot (real hv2 code, HOST_TEST) */
    struct hv_agx_config_snapshot cs;
    nwoas_agx_gpu_base = gb;
    uint32_t bp; int have_bp = adt_getprop_copy(NULL, sgx, "gpu-perf-base-pstate", &bp, 4) == 4;
    INFO("gpu-perf-base-pstate %s", have_bp ? "present" : "MISSING in this ADT");
    bool v0 = hv_agx_config_snapshot_from_adt(adt_blob, &cs);
    CHECK(have_bp ? v0 : !v0, "config snapshot without override: %s (expected %s)", v0 ? "valid" : "refused", have_bp ? "valid" : "refused");
    nwoas_agx_base_pstate_override = 1;
    bool v1 = hv_agx_config_snapshot_from_adt(adt_blob, &cs);
    CHECK(v1, "config snapshot with launcher override 1: %s", v1 ? "valid" : "refused");
    if (v1) {
        INFO("snapshot: perf_states=%u tables=%u base=%u max=%u period=%u gpu_region=0x%llx scalars=0x%llx",
             cs.perf_state_count, cs.perf_state_table_count, cs.base_pstate, cs.max_pstate,
             cs.power_sample_period_ms, (unsigned long long)cs.gpu_region_base, (unsigned long long)cs.scalar_presence);
        for (unsigned i = 0; i < cs.perf_state_count; i++)
            INFO("  pstate %u: %u Hz %u mV", i, cs.perf_states[i].frequency_hz, cs.perf_states[i].voltage_mv);
        bool j313 = cs.perf_state_count == 7 && cs.perf_state_table_count == 1 && cs.max_pstate == 6 && cs.power_sample_period_ms == 8;
        INFO("J313 decoder constants (7/1/6/8) %s on J274 (base p-state itself is not in this ADT)", j313 ? "match" : "DIFFER");
    }
    nwoas_agx_base_pstate_override = 3; /* Asahi t8103-j274.dts: apple,perf-base-pstate = <3> */
    CHECK(hv_agx_config_snapshot_from_adt(adt_blob, &cs) && cs.base_pstate == 3,
          "config snapshot with J274 base p-state 3 (launcher default): base=%u", cs.base_pstate);
    nwoas_agx_base_pstate_override = 1;
    nwoas_agx_gpu_base = gb + 0x4000;
    CHECK(!hv_agx_config_snapshot_from_adt(adt_blob, &cs), "config snapshot refused when probe gpu-region differs");
    nwoas_agx_gpu_base = gb;
    nwoas_agx_base_pstate_override = 0;

    /* Hwdata profile (J313 generated table) */
    struct { int sgx; } ctx = {sgx};
    unsigned mism = 0, missing = 0;
    for (unsigned i = 0; i < AGX_HWDATA_INPUT_COUNT; i++) {
        const AGX_HWDATA_INPUT *e = &AgxHwdataInputs[i];
        uint32_t len = 0;
        const unsigned char *p = adt_getprop(NULL, sgx, e->Name, &len);
        if (e->Length == 0xffffffffu) { if (p) mism++; continue; }
        if (!p) { missing++; mism++; INFO("  hwdata input MISSING in J274 ADT: %s", e->Name); continue; }
        if (len != e->Length || memcmp(p, e->Bytes, len)) { mism++; INFO("  hwdata input differs: %s (len %u vs %u)", e->Name, len, e->Length); }
    }
    INFO("Hwdata inputs: %u of %u differ from the selected profile (%u missing)", mism, AGX_HWDATA_INPUT_COUNT, missing);
#ifdef NWOAS_AGX_HWDATA_J274
    CHECK(mism == 0 && AgxHwdataInputsMatch(read_sgx_prop, &ctx.sgx),
          "J274 Hwdata profile matches this ADT exactly (profile_inputs_valid=1)");
#else
    CHECK(1, "J313 Hwdata profile on this ADT: profile_inputs_valid would be %u (0 = RR PREPARE refused)", mism == 0);
#endif
    (void)ctx;

    /* HVC 0x4d31 / 0x4d32 ABI (real hv_guest_ipa_pa.c) */
    uint32_t st = 0, seq = 0;
    CHECK(!hv_guest_ipa_pa_handle(0x4d30, 0x1000000, fake_translate, fake_is_ram, NULL, &st), "other HVC immediates are not consumed");
    memset(&req, 0, sizeof(req)); req.version = 1; req.operation = 1; req.count = 2;
    req.ipa[0] = 0x880000000ull; req.ipa[1] = 0x880001000ull;
    hv_guest_ipa_pa_handle(0x4d31, 0x1000000, fake_translate, fake_is_ram, NULL, &st);
    CHECK(st == HV_GUEST_IPA_PA_STATUS_SUCCESS && req.pa[0] == 0x880000000ull && req.pa[1] == 0x880001000ull, "0x4d31 translates 2 pages (status %u)", st);
    req.count = 65; hv_guest_ipa_pa_handle(0x4d31, 0x1000000, fake_translate, fake_is_ram, NULL, &st);
    CHECK(st == HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST, "0x4d31 count 65 refused (status %u)", st);
    req.count = 1; req.ipa[0] = 0x880000800ull; hv_guest_ipa_pa_handle(0x4d31, 0x1000000, fake_translate, fake_is_ram, NULL, &st);
    CHECK(st == HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST, "0x4d31 unaligned page refused (status %u)", st);
    req.ipa[0] = 0x200000000ull; hv_guest_ipa_pa_handle(0x4d31, 0x1000000, fake_translate, fake_is_ram, NULL, &st);
    CHECK(st == HV_GUEST_IPA_PA_STATUS_UNMAPPED && req.pa[0] == 0, "0x4d31 unmapped IPA -> UNMAPPED, pa cleared (status %u)", st);
    hv_guest_ipa_pa_handle(0x4d31, 0x2000000, fake_translate, fake_is_ram, NULL, &st);
    CHECK(st == HV_GUEST_IPA_PA_STATUS_NOT_RAM, "0x4d31 request page outside RAM -> NOT_RAM (status %u)", st);
    CHECK(hv_guest_arm_consumed_handle(0x4d32, (1ull << 32) | 7, &seq) && seq == 7, "0x4d32 ARM_CONSUMED v1 seq 7");
    CHECK(hv_guest_arm_consumed_handle(0x4d32, (2ull << 32) | 7, &seq) && seq == 0, "0x4d32 wrong version -> seq 0 (INVALID)");

    /* Power broker + J274 ops (same ADT domains as J313) */
    struct hv_agx_power_broker b;
    hv_agx_power_broker_init(&b, hv_agx_power_j313_ops(), NULL);
    uint64_t v = 0;
    hv_agx_power_broker_mmio(&b, HV_AGX_POWER_REG_MAGIC, &v, false, 2);
    CHECK(v == HV_AGX_POWER_MAGIC, "power broker magic 0x%llx", (unsigned long long)v);
    v = 1; hv_agx_power_broker_mmio(&b, HV_AGX_POWER_REG_REQUEST_SEQUENCE, &v, true, 3);
    v = HV_AGX_POWER_CMD_ON; hv_agx_power_broker_mmio(&b, HV_AGX_POWER_REG_COMMAND, &v, true, 2);
    hv_agx_power_broker_mmio(&b, HV_AGX_POWER_REG_STATE, &v, false, 2);
    CHECK(v == HV_AGX_POWER_ON, "power ON reaches state ON (%llu)", (unsigned long long)v);
    v = 2; hv_agx_power_broker_mmio(&b, HV_AGX_POWER_REG_REQUEST_SEQUENCE, &v, true, 3);
    v = HV_AGX_POWER_CMD_OFF; hv_agx_power_broker_mmio(&b, HV_AGX_POWER_REG_COMMAND, &v, true, 2);
    hv_agx_power_broker_mmio(&b, HV_AGX_POWER_REG_STATE, &v, false, 2);
    CHECK(v == HV_AGX_POWER_OFF, "power OFF reaches state OFF (%llu)", (unsigned long long)v);
    CHECK(!strcmp(pm_log, "+/arm-io/gfx-asc +/arm-io/sgx -/arm-io/sgx -/arm-io/gfx-asc "), "pmgr order: %s", pm_log);

    /* Local reserve (ABI v3) against the 13.5 carveouts, identity translate */
    INFO("local reserve size 0x%llx (%llu MiB), ABI v%u", (unsigned long long)HV_AGX_LOCAL_BYTES, (unsigned long long)(HV_AGX_LOCAL_BYTES >> 20), HV_AGX_LOCAL_ABI_VERSION);
    uint64_t c0 = 0xa00000000ull;
    bool inside = hv_agx_local_carveout_allows(c0, big[0], big[1], true);
    CHECK(inside, "example candidate 0x%llx inside %s", c0, bigname);
    CHECK(!hv_agx_local_carveout_allows(gb & ~0xffffull, gb, gs, false), "candidate overlapping gpu-region refused");
    struct hv_agx_local_range ex[1] = {{shb, shs}};
    struct hv_agx_local_receipt rc; struct hv_agx_local_failure fl;
    bool sel = hv_agx_local_select_detailed(c0, HV_AGX_LOCAL_BYTES, ex, 1, ident, NULL, &rc, &fl);
    CHECK(sel && rc.guest_ipa == c0 && rc.host_pa == c0 && rc.bytes == HV_AGX_LOCAL_BYTES, "local reserve receipt IPA=PA=0x%llx bytes=0x%llx", (unsigned long long)rc.guest_ipa, (unsigned long long)rc.bytes);
    v = 0; hv_agx_local_receipt_mmio(&rc, HV_AGX_LOCAL_REG_MAGIC, false, 2, &v);
    uint64_t ver = 0; hv_agx_local_receipt_mmio(&rc, HV_AGX_LOCAL_REG_VERSION, false, 2, &ver);
    CHECK(v == 0x4c584741 && ver == 3, "receipt magic 0x%llx version %llu (KMD/Mu require v3)", (unsigned long long)v, (unsigned long long)ver);
    CHECK(!hv_agx_local_select_detailed(shb & ~0xffffull, HV_AGX_LOCAL_BYTES, ex, 1, ident, NULL, &rc, &fl), "candidate overlapping gfx-shared-region refused (reason %u)", fl.reason);

    /* Scanout broker with capabilities cleared (no J313 panel on J274) */
    struct hv_agx_scanout_broker sc;
    hv_agx_scanout_broker_init(&sc); sc.capabilities = 0;
    v = 1; hv_agx_scanout_broker_mmio(&sc, HV_AGX_SCANOUT_REG_CAPABILITIES, &v, false, 2);
    uint64_t mg = 0; hv_agx_scanout_broker_mmio(&sc, HV_AGX_SCANOUT_REG_MAGIC, &mg, false, 2);
    CHECK(v == 0, "scanout capabilities read 0 (magic 0x%llx)", (unsigned long long)mg);
    uint64_t en = 0; CHECK(hv_agx_scanout_broker_mmio(&sc, HV_AGX_SCANOUT_REG_IRQ_ENABLE, &en, true, 2), "scanout IRQ_ENABLE write 0 accepted (KMD AdmissionMaskInterrupt)");
    CHECK(hv_agx_scanout_broker_mmio(&sc, HV_AGX_SCANOUT_REG_IRQ_STATUS, &en, false, 2) && en == 0, "scanout IRQ_STATUS reads 0 (no synthetic IRQ ever pending)");

    /* NWOAS S251: DCP scanout service driven by the J274 KMD client (mock DCP) */
    failures += nwoas_scanout_service_tests();

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
