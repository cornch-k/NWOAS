/* SPDX-License-Identifier: MIT */
/*
 * NWOAS S251 offline test: the J274 KMD's own scanout client
 * (driver-p15 shared/src/apple_agx_scanout.c, built with APPLE_AGX_BOARD_J274)
 * talks to the UNMODIFIED gpu-hv2 broker + scanout service through the broker
 * MMIO function. The DCP side is a mock platform (no hardware): it proves the
 * ABI/state machine only, never that DCP shows anything.
 *
 * The KMD's Pause callback stands in for the HV tick: it advances a simulated
 * clock by 200 us (5 kHz) and runs one bounded service run, exactly like
 * nwoas_agx_tick() (steps until not PROGRESSED, max 512; vIRQ edge).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hv_agx_scanout_broker.h"
#include "hv_agx_scanout_service.h"
#include "apple_agx_scanout.h" /* KMD (driver-p15), APPLE_AGX_BOARD_J274 */

static int fails;
#define CHECK(cond, ...) do { int ok_ = (cond); printf("%s ", ok_ ? "PASS" : "FAIL"); printf("S251 "); printf(__VA_ARGS__); printf("\n"); if (!ok_) fails++; } while (0)

#define POOL_IPA   UINT64_C(0xa00000000)
#define POOL_BYTES UINT64_C(0x3b800000) /* 1 GiB reserve - 64 MiB private - 8 MiB backend */
#define POOL_IOVA  UINT64_C(0x10000000)
#define LATCH_US   17000u
#define CHUNK      HV_AGX_SCANOUT_SERVICE_CHUNK_SIZE

static struct hv_agx_scanout_broker br;
static struct hv_agx_scanout_service svc;

static struct {
    uint64_t now_us;
    int owner_needed, owner_polls, owner_fail;
    int present_needed, present_polls;
    uint64_t cookie;
    uint32_t swap_id;
    uint64_t applied_us;
    uint64_t mapped[2], unmapped[2], map_calls[2];
    long map_fail_at; /* DCP dart chunk index to fail, -1 = never */
    bool iova_reserved;
    uint64_t last_iova, last_pa;
    uint32_t last_w, last_h, last_stride;
    uint32_t last_dx, last_dy, last_dw, last_dh; /* S253 */
    uint64_t last_size;
    bool last_rgba;
    int quiesce_polls;
    bool quiesced;
    int power_polls;
    bool powered;
    bool power_target;
    uint64_t irqs;
    uint64_t ticks;
    uint64_t max_steps_per_tick;
} m;

static void mock_reset(void)
{
    memset(&m, 0, sizeof(m));
    m.owner_needed = 3;
    m.present_needed = 4; /* ~0.8 ms at 5 kHz, like the 738 us measured in S250 */
    m.map_fail_at = -1;
    m.powered = true;
}

static bool p_translate(void *o, uint64_t ipa, uint64_t *pa) { (void)o; *pa = ipa; return true; }
static bool p_is_ram(void *o, uint64_t pa, uint64_t size)
{
    (void)o;
    return pa >= POOL_IPA && size <= POOL_BYTES && pa - POOL_IPA <= POOL_BYTES - size;
}
static bool p_reserve(void *o, uint64_t size, uint64_t align, uint64_t *iova)
{
    (void)o; (void)align;
    if (m.iova_reserved || size > UINT64_C(0x50000000))
        return false;
    m.iova_reserved = true;
    *iova = POOL_IOVA;
    return true;
}
static void p_free(void *o, uint64_t iova, uint64_t size) { (void)o; (void)iova; (void)size; m.iova_reserved = false; }
static bool p_map(void *o, enum hv_agx_scanout_dart d, uint64_t iova, uint64_t pa, uint64_t size)
{
    (void)o;
    if (size > CHUNK || iova - POOL_IOVA != pa - POOL_IPA)
        return false;
    if (d == HV_AGX_SCANOUT_DART_DCP && (long)m.map_calls[d] == m.map_fail_at)
        return false;
    m.map_calls[d]++;
    m.mapped[d] += size;
    return true;
}
static void p_unmap(void *o, enum hv_agx_scanout_dart d, uint64_t iova, uint64_t size)
{
    (void)o; (void)iova;
    m.unmapped[d] += size;
}
static enum hv_agx_scanout_async_result p_owner(void *o)
{
    (void)o;
    if (m.owner_fail)
        return HV_AGX_SCANOUT_ASYNC_FAILED;
    if (m.owner_polls >= m.owner_needed)
        return HV_AGX_SCANOUT_ASYNC_APPLIED;
    m.owner_polls++;
    return HV_AGX_SCANOUT_ASYNC_PENDING;
}
static bool p_present_begin(void *o, uint64_t iova, uint64_t pa,
                            const struct hv_agx_scanout_request *r, uint64_t *cookie)
{
    (void)o;
    m.last_iova = iova; m.last_pa = pa; m.last_w = r->Width; m.last_h = r->Height;
    m.last_stride = r->Stride; m.last_rgba = r->Format == HV_AGX_SCANOUT_FORMAT_RGBA8888;
    m.last_dx = r->DestX; m.last_dy = r->DestY; m.last_dw = r->DestWidth; m.last_dh = r->DestHeight;
    m.last_size = r->SurfaceSize;
    m.present_polls = 0;
    *cookie = ++m.cookie;
    return true;
}
static enum hv_agx_scanout_async_result p_present_poll(void *o, uint64_t cookie, uint32_t *id)
{
    (void)o;
    if (cookie != m.cookie)
        return HV_AGX_SCANOUT_ASYNC_FAILED;
    if (++m.present_polls < m.present_needed)
        return HV_AGX_SCANOUT_ASYNC_PENDING;
    *id = ++m.swap_id;
    m.applied_us = m.now_us;
    return HV_AGX_SCANOUT_ASYNC_APPLIED;
}
static enum hv_agx_scanout_latch_result p_latch(void *o, uint32_t expected)
{
    (void)o;
    if (expected != m.swap_id)
        return HV_AGX_SCANOUT_LATCH_WRONG_SWAP;
    return m.now_us - m.applied_us >= LATCH_US ? HV_AGX_SCANOUT_LATCHED : HV_AGX_SCANOUT_LATCH_PENDING;
}
static bool p_quiesce_begin(void *o, uint64_t *cookie) { (void)o; m.quiesce_polls = 0; *cookie = ++m.cookie; return true; }
static enum hv_agx_scanout_async_result p_quiesce_poll(void *o, uint64_t cookie)
{
    (void)o;
    if (cookie != m.cookie)
        return HV_AGX_SCANOUT_ASYNC_FAILED;
    if (++m.quiesce_polls < 3)
        return HV_AGX_SCANOUT_ASYNC_PENDING;
    m.quiesced = true;
    return HV_AGX_SCANOUT_ASYNC_APPLIED;
}
static bool p_power_begin(void *o, bool on, uint64_t *cookie) { (void)o; m.power_target = on; m.power_polls = 0; *cookie = ++m.cookie; return true; }
static enum hv_agx_scanout_async_result p_power_poll(void *o, uint64_t cookie)
{
    (void)o;
    if (cookie != m.cookie)
        return HV_AGX_SCANOUT_ASYNC_FAILED;
    if (++m.power_polls < 5)
        return HV_AGX_SCANOUT_ASYNC_PENDING;
    m.powered = m.power_target;
    return HV_AGX_SCANOUT_ASYNC_APPLIED;
}

static const struct hv_agx_scanout_platform_ops ops = {
    .translate = p_translate, .is_ram = p_is_ram, .reserve_iova = p_reserve,
    .free_iova = p_free, .map = p_map, .unmap = p_unmap, .owner_poll = p_owner,
    .present_begin = p_present_begin, .present_poll = p_present_poll,
    .present_latch_poll = p_latch, .quiesce_begin = p_quiesce_begin,
    .quiesce_poll = p_quiesce_poll, .power_begin = p_power_begin, .power_poll = p_power_poll,
};

/* nwoas_agx_tick() equivalent (vGIC: the line is always enabled, one LR free) */
static void tick(void)
{
    uint64_t steps = 0;
    m.now_us += 200;
    m.ticks++;
    for (unsigned i = 0; i < 512; i++) {
        steps++;
        if (hv_agx_scanout_service_step(&svc, &br) != HV_AGX_SCANOUT_SERVICE_PROGRESSED)
            break;
    }
    if (steps > m.max_steps_per_tick)
        m.max_steps_per_tick = steps;
    if (hv_agx_scanout_broker_take_irq_edge(&br))
        m.irqs++;
}

/* ---- KMD Io over the broker MMIO function (offsets are absolute broker offsets) ---- */
static APPLE_AGX_SCANOUT_U64 io_now(void *c) { (void)c; return m.now_us / 1000; }
static APPLE_AGX_SCANOUT_BOOL io_r32(void *c, APPLE_AGX_SCANOUT_U32 off, APPLE_AGX_SCANOUT_U32 *v)
{
    uint64_t x = 0; (void)c;
    if (off < HV_AGX_SCANOUT_MMIO_OFFSET || !hv_agx_scanout_broker_mmio(&br, off - HV_AGX_SCANOUT_MMIO_OFFSET, &x, false, 2))
        return APPLE_AGX_SCANOUT_FALSE;
    *v = (APPLE_AGX_SCANOUT_U32)x;
    return APPLE_AGX_SCANOUT_TRUE;
}
static APPLE_AGX_SCANOUT_BOOL io_r64(void *c, APPLE_AGX_SCANOUT_U32 off, APPLE_AGX_SCANOUT_U64 *v)
{
    uint64_t x = 0; (void)c;
    if (off < HV_AGX_SCANOUT_MMIO_OFFSET || !hv_agx_scanout_broker_mmio(&br, off - HV_AGX_SCANOUT_MMIO_OFFSET, &x, false, 3))
        return APPLE_AGX_SCANOUT_FALSE;
    *v = x;
    return APPLE_AGX_SCANOUT_TRUE;
}
static APPLE_AGX_SCANOUT_BOOL io_w32(void *c, APPLE_AGX_SCANOUT_U32 off, APPLE_AGX_SCANOUT_U32 v)
{
    uint64_t x = v; (void)c;
    return off >= HV_AGX_SCANOUT_MMIO_OFFSET && hv_agx_scanout_broker_mmio(&br, off - HV_AGX_SCANOUT_MMIO_OFFSET, &x, true, 2);
}
static APPLE_AGX_SCANOUT_BOOL io_w64(void *c, APPLE_AGX_SCANOUT_U32 off, APPLE_AGX_SCANOUT_U64 v)
{
    uint64_t x = v; (void)c;
    return off >= HV_AGX_SCANOUT_MMIO_OFFSET && hv_agx_scanout_broker_mmio(&br, off - HV_AGX_SCANOUT_MMIO_OFFSET, &x, true, 3);
}
static APPLE_AGX_SCANOUT_BOOL io_pause(void *c) { (void)c; tick(); return APPLE_AGX_SCANOUT_TRUE; }

static void hv_install_like2(bool service, bool scaled)
{
    hv_agx_scanout_broker_init(&br);
    br.capabilities = 0;
    if (service) { /* scanout_select() in nwoas_agx_broker.c */
        hv_agx_scanout_broker_init_v2(&br, true);
        br.capabilities |= HV_AGX_SCANOUT_CAP_DISPLAY_POWER;
        if (scaled) /* NWOAS S253 */
            br.capabilities |= HV_AGX_SCANOUT_CAP_SCALED_SOURCE;
        br.capabilities &= ~HV_AGX_SCANOUT_CAP_RGBA8888;
        hv_agx_scanout_broker_set_pool_size(&br, POOL_BYTES);
        hv_agx_scanout_service_init(&svc, &ops, NULL);
    }
}
static void hv_install_like(bool service) { hv_install_like2(service, true); }

static uint32_t r32(uint32_t reg) { uint64_t v = 0; hv_agx_scanout_broker_mmio(&br, reg, &v, false, 2); return (uint32_t)v; }
static uint64_t r64(uint32_t reg) { uint64_t v = 0; hv_agx_scanout_broker_mmio(&br, reg, &v, false, 3); return v; }
static bool w32(uint32_t reg, uint32_t val) { uint64_t v = val; return hv_agx_scanout_broker_mmio(&br, reg, &v, true, 2); }
static bool w64(uint32_t reg, uint64_t val) { uint64_t v = val; return hv_agx_scanout_broker_mmio(&br, reg, &v, true, 3); }

/* Raw CMD_SET_POWER as the KMD (step 5) is to issue it; waits for the receipt. */
static uint32_t raw_power(uint64_t seq, uint32_t on, uint64_t *waited_ticks)
{
    uint64_t t0 = m.ticks;
    w32(HV_AGX_SCANOUT_REG_POWER, on);
    w64(HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, seq);
    w32(HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_SET_POWER);
    for (int i = 0; i < 100000 && r64(HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) != seq; i++)
        tick();
    if (waited_ticks)
        *waited_ticks = m.ticks - t0;
    return r32(HV_AGX_SCANOUT_REG_RESULT);
}

/* NWOAS S253 helpers */
static void wait_latched(void)
{
    for (int i = 0; i < 100000 && r64(HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) !=
                                      r64(HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE); i++)
        tick();
}
static void client_geometry(APPLE_AGX_SCANOUT_CLIENT *k, uint32_t w, uint32_t h, uint64_t size,
                            uint32_t x, uint32_t y, uint32_t dw, uint32_t dh)
{
    k->PresentWidth = w; k->PresentHeight = h; k->PresentStride = w * 4u;
    k->PresentSurfaceBytes = size;
    k->PresentDestOrigin = x | (y << 16); k->PresentDestSize = dw | (dh << 16);
}
/* Raw PRESENT with arbitrary registers; returns the RESULT register. */
static uint32_t raw_present(APPLE_AGX_SCANOUT_CLIENT *k, uint32_t w, uint32_t h, uint32_t stride,
                            uint64_t size, uint32_t origin, uint32_t dsize)
{
    uint64_t seq = k->NextSequence++;
    w64(HV_AGX_SCANOUT_REG_SURFACE_OFFSET, 0);
    w64(HV_AGX_SCANOUT_REG_SURFACE_SIZE, size);
    w32(HV_AGX_SCANOUT_REG_WIDTH, w); w32(HV_AGX_SCANOUT_REG_HEIGHT, h); w32(HV_AGX_SCANOUT_REG_STRIDE, stride);
    w32(HV_AGX_SCANOUT_REG_FORMAT, HV_AGX_SCANOUT_FORMAT_BGRA8888);
    w32(HV_AGX_SCANOUT_REG_DEST_ORIGIN, origin); w32(HV_AGX_SCANOUT_REG_DEST_SIZE, dsize);
    w64(HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, seq);
    w32(HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_PRESENT);
    if (r64(HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) != seq &&
        r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_OK) {
        for (int i = 0; i < 100000 && r64(HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) != seq; i++)
            tick();
    }
    return r32(HV_AGX_SCANOUT_REG_RESULT);
}
#define FULL_PANEL (1920u | (1080u << 16))

int nwoas_scanout_service_tests(void)
{
    APPLE_AGX_SCANOUT_CLIENT k;
    APPLE_AGX_SCANOUT_IO io = {NULL, io_now, io_r32, io_r64, io_w32, io_w64, io_pause};
    APPLE_AGX_SCANOUT_RESULT res;
    APPLE_AGX_SCANOUT_U32 swap = 0;

    fails = 0;
    /* Geometry agreement HV <-> KMD (both J274 builds) */
    CHECK(HV_AGX_SCANOUT_J313_WIDTH == APPLE_AGX_SCANOUT_J313_WIDTH &&
          HV_AGX_SCANOUT_J313_HEIGHT == APPLE_AGX_SCANOUT_J313_HEIGHT &&
          HV_AGX_SCANOUT_J313_STRIDE == APPLE_AGX_SCANOUT_J313_STRIDE &&
          HV_AGX_SCANOUT_J313_SURFACE_SIZE == APPLE_AGX_SCANOUT_J313_SURFACE_SIZE,
          "HV and KMD agree on %ux%u stride %u surface 0x%llx", HV_AGX_SCANOUT_J313_WIDTH,
          HV_AGX_SCANOUT_J313_HEIGHT, HV_AGX_SCANOUT_J313_STRIDE,
          (unsigned long long)HV_AGX_SCANOUT_J313_SURFACE_SIZE);
    CHECK(HV_AGX_SCANOUT_CAP_DISPLAY_POWER != APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE &&
          (HV_AGX_SCANOUT_CAP_DISPLAY_POWER & APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES) == 0,
          "CAP_DISPLAY_POWER 0x%x is a free bit on the KMD side", HV_AGX_SCANOUT_CAP_DISPLAY_POWER);

    /* NWOAS S253: ABI agreement for the scaled source */
    CHECK(HV_AGX_SCANOUT_CAP_SCALED_SOURCE == APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE &&
          (unsigned)HV_AGX_SCANOUT_REG_DEST_ORIGIN == (unsigned)APPLE_AGX_SCANOUT_REG_DEST_ORIGIN &&
          (unsigned)HV_AGX_SCANOUT_REG_DEST_SIZE == (unsigned)APPLE_AGX_SCANOUT_REG_DEST_SIZE &&
          HV_AGX_SCANOUT_MMIO_SIZE == APPLE_AGX_SCANOUT_MMIO_SIZE,
          "S253 HV and KMD agree: CAP_SCALED_SOURCE 0x%x, DEST regs 0x%x/0x%x, window 0x%llx",
          HV_AGX_SCANOUT_CAP_SCALED_SOURCE, HV_AGX_SCANOUT_REG_DEST_ORIGIN,
          HV_AGX_SCANOUT_REG_DEST_SIZE, (unsigned long long)HV_AGX_SCANOUT_MMIO_SIZE);

    /* 1. Default (service off): the KMD's v2 qualification fails as today */
    mock_reset();
    hv_install_like(false);
    AppleAgxScanoutInitialize(&k, &io, 1, 40000);
    res = AppleAgxScanoutQualify(&k);
    CHECK(res == AppleAgxScanoutMissingCapabilities, "service off: KMD Qualify -> MissingCapabilities (%d), as before", res);
    uint64_t pv = 0;
    CHECK(!hv_agx_scanout_broker_mmio(&br, HV_AGX_SCANOUT_REG_POWER, &pv, false, 2),
          "service off: power register +0x9c refused (unchanged)");
    w64(HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, 7);
    w32(HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_SET_POWER);
    CHECK(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_INVALID_COMMAND,
          "service off: CMD_SET_POWER -> INVALID_COMMAND (unchanged)");
    pv = 0;
    CHECK(!hv_agx_scanout_broker_mmio(&br, HV_AGX_SCANOUT_REG_DEST_ORIGIN, &pv, false, 2) &&
          !hv_agx_scanout_broker_mmio(&br, HV_AGX_SCANOUT_REG_DEST_SIZE, &pv, true, 2),
          "S253 service off: DEST_ORIGIN/DEST_SIZE refused (as past the old 0xa0 window)");

    /* 2. Service on: qualify exactly like scanout_windows.c:1355-1363 */
    mock_reset();
    hv_install_like(true);
    AppleAgxScanoutInitialize(&k, &io, 1, 40000);
    res = AppleAgxScanoutQualify(&k);
    CHECK(res == AppleAgxScanoutOk && k.AbiVersion == APPLE_AGX_SCANOUT_ABI_VERSION_V2 &&
          (k.Capabilities & APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES) == APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES,
          "service on: KMD Qualify Ok, ABI v%u caps 0x%x (KMD needs 0x%x)", k.AbiVersion, k.Capabilities,
          APPLE_AGX_SCANOUT_V2_PRESENT_CAPABILITIES);
    CHECK((k.Capabilities & HV_AGX_SCANOUT_CAP_DISPLAY_POWER) && (k.Capabilities & APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE),
          "service on: CAP_DISPLAY_POWER and CAP_SCALED_SOURCE set (S253, caps 0x%x)", k.Capabilities);
    CHECK(r32(HV_AGX_SCANOUT_REG_DEST_ORIGIN) == 0 && r32(HV_AGX_SCANOUT_REG_DEST_SIZE) == FULL_PANEL,
          "S253 DEST_ORIGIN/DEST_SIZE read back 0 / 1920x1080 before any write");

    /* 3. REGISTER_POOL through the KMD client (deadline 2000 ms as AdmissionScanoutStart) */
    uint64_t t0 = m.ticks;
    res = AppleAgxScanoutRegisterPool(&k, POOL_IPA, POOL_BYTES, io_now(NULL) + 2000);
    uint64_t reg_ticks = m.ticks - t0;
    CHECK(res == AppleAgxScanoutOk && br.state == HV_AGX_SCANOUT_READY,
          "RegisterPool Ok (%d) state %u after %llu ticks = %.1f ms simulated (max %llu steps/tick)",
          res, br.state, (unsigned long long)reg_ticks, reg_ticks * 0.2,
          (unsigned long long)m.max_steps_per_tick);
    CHECK(m.mapped[0] == POOL_BYTES && m.mapped[1] == POOL_BYTES &&
          m.map_calls[0] == POOL_BYTES / CHUNK && m.owner_polls == m.owner_needed,
          "pool mapped in dart-disp0 and dart-dcp at one IOVA (%llu x 256 KiB each), owner taken first",
          (unsigned long long)m.map_calls[0]);
    CHECK(reg_ticks * 200 < 2000000, "registration well inside the KMD's 2 s deadline");

    /* 4. PresentApplied (synchronous) */
    res = AppleAgxScanoutPresentApplied(&k, 0, io_now(NULL) + 2000, &swap);
    CHECK(res == AppleAgxScanoutOk && swap != 0 && m.last_w == 1920 && m.last_h == 1080 &&
          m.last_stride == 7680 && m.last_iova == POOL_IOVA && m.last_pa == POOL_IPA && !m.last_rgba &&
          m.last_dx == 0 && m.last_dy == 0 && m.last_dw == 1920 && m.last_dh == 1080,
          "PresentApplied Ok swap_id %u, 1920x1080 stride 7680 BGRA at iova 0x%llx pa 0x%llx", swap,
          (unsigned long long)m.last_iova, (unsigned long long)m.last_pa);
    uint64_t applied = r64(HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE);
    res = AppleAgxScanoutPresentApplied(&k, 0x800000, io_now(NULL) + 2000, &swap);
    CHECK(res == AppleAgxScanoutBrokerRejected && r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_BUSY,
          "second present before LATCHED -> BUSY (%d)", res);
    t0 = m.now_us;
    while (r64(HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) != applied && m.now_us - t0 < 100000)
        tick();
    CHECK(r64(HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) == applied && m.now_us - t0 >= LATCH_US - 1000,
          "LATCHED seq %llu %llu us after APPLIED (delay %u us)", (unsigned long long)applied,
          (unsigned long long)(m.now_us - t0), LATCH_US);

    /* 5. QueuePresent + LATCHED IRQ through EnableInterrupts/ConsumeInterrupt */
    CHECK(AppleAgxScanoutEnableInterrupts(&k) == AppleAgxScanoutOk, "EnableInterrupts Ok");
    m.irqs = 0;
    hv_agx_scanout_broker_mmio(&br, HV_AGX_SCANOUT_REG_IRQ_STATUS, &pv, false, 2);
    w32(HV_AGX_SCANOUT_REG_IRQ_STATUS, (uint32_t)pv); /* clear the latch of step 4 */
    APPLE_AGX_SCANOUT_U64 qseq = 0;
    res = AppleAgxScanoutQueuePresent(&k, 0x800000, &qseq);
    CHECK(res == AppleAgxScanoutOk && qseq, "QueuePresent Ok seq %llu", (unsigned long long)qseq);
    CHECK(AppleAgxScanoutQueuePresent(&k, 0, &qseq) == AppleAgxScanoutBusy, "QueuePresent while pending -> Busy (client)");
    uint64_t q0 = m.now_us;
    while (!m.irqs && m.now_us - q0 < 200000)
        tick();
    APPLE_AGX_SCANOUT_U32 irq = 0;
    APPLE_AGX_SCANOUT_U64 lseq = 0;
    res = AppleAgxScanoutConsumeInterrupt(&k, &irq, &lseq);
    CHECK(m.irqs == 1 && res == AppleAgxScanoutOk && (irq & APPLE_AGX_SCANOUT_IRQ_LATCHED) && lseq == k.NextSequence - 1 &&
          !k.PresentPending,
          "one vIRQ edge, ConsumeInterrupt LATCHED seq %llu after %llu us, pending cleared",
          (unsigned long long)lseq, (unsigned long long)(m.now_us - q0));
    for (int i = 0; i < 50; i++)
        tick();
    CHECK(m.irqs == 1, "no second edge until a new latch (status cleared by the KMD)");
    CHECK(svc.presents_applied == 2 && svc.latches == 2 && svc.latch_failures == 0,
          "service counters: %llu applied, %llu latched", (unsigned long long)svc.presents_applied,
          (unsigned long long)svc.latches);

    /* 5b. NWOAS S253: scaled sources through the KMD client */
    uint32_t rr;
    client_geometry(&k, 1280, 720, UINT64_C(5120) * 720, 0, 0, 1920, 1080);
    res = AppleAgxScanoutPresentApplied(&k, 0, io_now(NULL) + 2000, &swap);
    CHECK(res == AppleAgxScanoutOk && m.last_w == 1280 && m.last_h == 720 && m.last_stride == 5120 &&
          m.last_dx == 0 && m.last_dy == 0 && m.last_dw == 1920 && m.last_dh == 1080 &&
          m.last_size == UINT64_C(5120) * 720,
          "S253 1280x720 stretched -> service request src 1280x720 stride 5120, dst 0,0 1920x1080 (%d)", res);
    wait_latched();
    /* 1440x900 aspect fit: (1440*1080+450)/900 = 1728, x = (1920-1728)/2 = 96 */
    client_geometry(&k, 1440, 900, UINT64_C(5760) * 900, 96, 0, 1728, 1080);
    res = AppleAgxScanoutPresentApplied(&k, 0x800000, io_now(NULL) + 2000, &swap);
    CHECK(res == AppleAgxScanoutOk && m.last_w == 1440 && m.last_h == 900 && m.last_dx == 96 &&
          m.last_dy == 0 && m.last_dw == 1728 && m.last_dh == 1080 &&
          r32(HV_AGX_SCANOUT_REG_DEST_ORIGIN) == 96u && r32(HV_AGX_SCANOUT_REG_DEST_SIZE) == (1728u | (1080u << 16)),
          "S253 1440x900 aspect fit -> dst 96,0 1728x1080, DEST regs read back (%d)", res);
    wait_latched();
    /* 800x600 centred 1:1, surface size rounded up to 64 KiB (0x1d4c00 -> 0x1e0000) */
    client_geometry(&k, 800, 600, UINT64_C(0x1e0000), 560, 240, 800, 600);
    res = AppleAgxScanoutPresentApplied(&k, 0, io_now(NULL) + 2000, &swap);
    CHECK(res == AppleAgxScanoutOk && m.last_w == 800 && m.last_h == 600 && m.last_dx == 560 &&
          m.last_dy == 240 && m.last_dw == 800 && m.last_dh == 600 && m.last_size == UINT64_C(0x1e0000),
          "S253 800x600 centred, size rounded to 64 KiB -> dst 560,240 800x600 (%d)", res);
    wait_latched();
    client_geometry(&k, 800, 600, UINT64_C(0x1d4c00), 560, 240, 800, 600);
    res = AppleAgxScanoutPresentApplied(&k, 0x800000, io_now(NULL) + 2000, &swap);
    CHECK(res == AppleAgxScanoutOk && m.last_size == UINT64_C(0x1d4c00),
          "S253 800x600 with the exact size stride*height (what the KMD writes) accepted (%d)", res);
    wait_latched();

    /* 5c. NWOAS S253: bad geometry is refused (terminal INVALID_SURFACE) */
    uint64_t applied_before = svc.presents_applied;
    static const struct {
        uint32_t w, h, stride; uint64_t size; uint32_t origin, dsize; const char *what;
    } bad[] = {
        {1366, 768, 5464, UINT64_C(5464) * 768, 0, FULL_PANEL, "width not a multiple of 16 (1366x768)"},
        {1280, 721, 5120, UINT64_C(5120) * 721, 0, FULL_PANEL, "odd height (1280x721)"},
        {1280, 720, 5632, UINT64_C(5632) * 720, 0, FULL_PANEL, "stride != width*4"},
        {1280, 720, 5120, UINT64_C(5120) * 720 + 0x1000, 0, FULL_PANEL, "size neither stride*height nor its 64 KiB round-up"},
        {1280, 720, 5120, UINT64_C(5120) * 719, 0, FULL_PANEL, "size smaller than stride*height"},
        {1920, 1200, 7680, UINT64_C(7680) * 1200, 0, FULL_PANEL, "source taller than the panel (1920x1200)"},
        {2048, 1080, 8192, UINT64_C(8192) * 1080, 0, FULL_PANEL, "source wider than the panel"},
        {0, 720, 0, 0, 0, FULL_PANEL, "zero width"},
        {1280, 720, 5120, UINT64_C(5120) * 720, 0, 0, "empty destination"},
        {1280, 720, 5120, UINT64_C(5120) * 720, 0, 1920u, "destination of height 0"},
        {1280, 720, 5120, UINT64_C(5120) * 720, 200, FULL_PANEL, "destination past the right edge"},
        {1280, 720, 5120, UINT64_C(5120) * 720, 1u << 16, FULL_PANEL, "destination past the bottom edge"},
        {1280, 720, 5120, UINT64_C(5120) * 720, 0, 1921u | (1080u << 16), "destination wider than the panel"},
        {1280, 720, 5120, UINT64_C(5120) * 720, 0, 640u | (360u << 16), "destination smaller than the source"},
        {1920, 1080, 7680, UINT64_C(0x7e9000), 0, 1280u | (720u << 16), "native source into a partial rectangle"},
        {1920, 1080, 7680, UINT64_C(0x7e9000), 8, FULL_PANEL, "native source with origin 8,0"},
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        rr = raw_present(&k, bad[i].w, bad[i].h, bad[i].stride, bad[i].size, bad[i].origin, bad[i].dsize);
        CHECK(rr == HV_AGX_SCANOUT_RESULT_INVALID_SURFACE &&
              r64(HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == k.NextSequence - 1 &&
              r32(HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_ACTIVE,
              "S253 refused INVALID_SURFACE: %s (result %u)", bad[i].what, rr);
    }
    CHECK(svc.presents_applied == applied_before, "S253 no refused geometry reached the service");

    /* 5d. NWOAS S253: native traffic unchanged (the KMD's default geometry) */
    client_geometry(&k, 1920, 1080, HV_AGX_SCANOUT_J313_SURFACE_SIZE, 0, 0, 1920, 1080);
    res = AppleAgxScanoutPresentApplied(&k, 0, io_now(NULL) + 2000, &swap);
    CHECK(res == AppleAgxScanoutOk && m.last_w == 1920 && m.last_h == 1080 && m.last_stride == 7680 &&
          m.last_dx == 0 && m.last_dy == 0 && m.last_dw == 1920 && m.last_dh == 1080 &&
          m.last_size == HV_AGX_SCANOUT_J313_SURFACE_SIZE,
          "S253 native 1920x1080 after scaled ones -> dst 0,0 1920x1080 (%d)", res);
    wait_latched();

    /* 6. Wrong geometry is refused by the broker (terminal) */
    uint64_t seq = k.NextSequence++;
    w64(HV_AGX_SCANOUT_REG_SURFACE_OFFSET, 0);
    w64(HV_AGX_SCANOUT_REG_SURFACE_SIZE, UINT64_C(0xfa0000));
    w32(HV_AGX_SCANOUT_REG_WIDTH, 2560); w32(HV_AGX_SCANOUT_REG_HEIGHT, 1600); w32(HV_AGX_SCANOUT_REG_STRIDE, 10240);
    w32(HV_AGX_SCANOUT_REG_FORMAT, HV_AGX_SCANOUT_FORMAT_BGRA8888);
    w64(HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, seq);
    w32(HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_PRESENT);
    CHECK(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_INVALID_SURFACE && r64(HV_AGX_SCANOUT_REG_RECEIPT_SEQUENCE) == seq,
          "2560x1600 (J313) present refused INVALID_SURFACE on J274");

    /* 6b. RGBA is refused while CAP_RGBA8888 is not advertised */
    seq = k.NextSequence++;
    w64(HV_AGX_SCANOUT_REG_SURFACE_OFFSET, 0);
    w64(HV_AGX_SCANOUT_REG_SURFACE_SIZE, HV_AGX_SCANOUT_J313_SURFACE_SIZE);
    w32(HV_AGX_SCANOUT_REG_WIDTH, 1920); w32(HV_AGX_SCANOUT_REG_HEIGHT, 1080); w32(HV_AGX_SCANOUT_REG_STRIDE, 7680);
    w32(HV_AGX_SCANOUT_REG_FORMAT, HV_AGX_SCANOUT_FORMAT_RGBA8888);
    w64(HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, seq);
    w32(HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_PRESENT);
    CHECK(!(br.capabilities & HV_AGX_SCANOUT_CAP_RGBA8888) &&
          r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_INVALID_SURFACE,
          "RGBA8888 present refused (only BGRA shown on the J274 iBoot path so far)");

    /* 7. Display power off / present while off / on */
    uint64_t wt = 0;
    seq = k.NextSequence++;
    uint32_t pr = raw_power(seq, 0, &wt);
    CHECK(pr == HV_AGX_SCANOUT_RESULT_OK && r32(HV_AGX_SCANOUT_REG_POWER) == 0 && !m.powered &&
          r32(HV_AGX_SCANOUT_REG_STATE) == HV_AGX_SCANOUT_ACTIVE &&
          r64(HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE) == seq && r64(HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) == seq,
          "SET_POWER(0) OK in %llu ticks, power reg 0, state ACTIVE, APPLIED == LATCHED == %llu",
          (unsigned long long)wt, (unsigned long long)seq);
    k.NextSequence = seq + 1;
    res = AppleAgxScanoutPresentApplied(&k, 0, io_now(NULL) + 2000, &swap);
    CHECK(res == AppleAgxScanoutOk, "present accepted while the display is off (%d)", res);
    while (r64(HV_AGX_SCANOUT_REG_LATCHED_SEQUENCE) != r64(HV_AGX_SCANOUT_REG_APPLIED_SEQUENCE))
        tick();
    seq = k.NextSequence++;
    pr = raw_power(seq, 1, &wt);
    CHECK(pr == HV_AGX_SCANOUT_RESULT_OK && r32(HV_AGX_SCANOUT_REG_POWER) == 1 && m.powered,
          "SET_POWER(1) OK in %llu ticks, power reg 1", (unsigned long long)wt);
    seq = k.NextSequence++;
    pr = raw_power(seq, 2, NULL);
    CHECK(pr == HV_AGX_SCANOUT_RESULT_INVALID_COMMAND, "SET_POWER(2) -> INVALID_COMMAND");
    k.NextSequence = seq + 1;

    /* 8. RELEASE through the KMD client */
    res = AppleAgxScanoutRelease(&k, io_now(NULL) + 2000);
    CHECK(res == AppleAgxScanoutOk && br.state == HV_AGX_SCANOUT_UNREGISTERED && m.quiesced &&
          m.unmapped[0] == POOL_BYTES && m.unmapped[1] == POOL_BYTES && !m.iova_reserved,
          "Release Ok: POST framebuffer restored first, both DARTs unmapped, IOVA freed");

    /* 9. Map failure rolls back both DARTs */
    mock_reset();
    hv_install_like(true);
    m.map_fail_at = 100;
    AppleAgxScanoutInitialize(&k, &io, 1, 40000);
    AppleAgxScanoutQualify(&k);
    res = AppleAgxScanoutRegisterPool(&k, POOL_IPA, POOL_BYTES, io_now(NULL) + 2000);
    CHECK(res == AppleAgxScanoutBrokerRejected && k.LastBrokerResult == HV_AGX_SCANOUT_RESULT_MAP_FAILED &&
          br.state == HV_AGX_SCANOUT_UNREGISTERED && m.unmapped[0] == m.mapped[0] &&
          m.unmapped[1] == m.mapped[1] && !m.iova_reserved,
          "dart-dcp map failure -> MAP_FAILED, rollback unmapped %llu + %llu bytes",
          (unsigned long long)m.unmapped[0], (unsigned long long)m.unmapped[1]);

    /* 10. Takeover failure fails closed */
    mock_reset();
    hv_install_like(true);
    m.owner_fail = 1;
    AppleAgxScanoutInitialize(&k, &io, 1, 40000);
    AppleAgxScanoutQualify(&k);
    res = AppleAgxScanoutRegisterPool(&k, POOL_IPA, POOL_BYTES, io_now(NULL) + 2000);
    CHECK(res == AppleAgxScanoutBrokerRejected && k.LastBrokerResult == HV_AGX_SCANOUT_RESULT_MAP_FAILED &&
          m.mapped[0] == 0 && m.mapped[1] == 0,
          "DCP takeover failure -> REGISTER MAP_FAILED, nothing mapped");

    /* 11. Wrong pool size is refused before the service runs */
    mock_reset();
    hv_install_like(true);
    AppleAgxScanoutInitialize(&k, &io, 1, 40000);
    AppleAgxScanoutQualify(&k);
    res = AppleAgxScanoutRegisterPool(&k, POOL_IPA, POOL_BYTES - 0x10000, io_now(NULL) + 2000);
    CHECK(res == AppleAgxScanoutBrokerRejected && k.LastBrokerResult == HV_AGX_SCANOUT_RESULT_INVALID_POOL &&
          m.owner_polls == 0,
          "pool size != segment 2 -> INVALID_POOL, DCP not touched");

    /* 12. NWOAS S253: without CAP_SCALED_SOURCE only native geometry, dest = full panel */
    mock_reset();
    hv_install_like2(true, false);
    AppleAgxScanoutInitialize(&k, &io, 1, 40000);
    AppleAgxScanoutQualify(&k);
    res = AppleAgxScanoutRegisterPool(&k, POOL_IPA, POOL_BYTES, io_now(NULL) + 2000);
    CHECK(res == AppleAgxScanoutOk && !(k.Capabilities & APPLE_AGX_SCANOUT_CAP_SCALED_SOURCE),
          "S253 cap clear: RegisterPool Ok, KMD sees no CAP_SCALED_SOURCE");
    res = AppleAgxScanoutPresentApplied(&k, 0, io_now(NULL) + 2000, &swap);
    CHECK(res == AppleAgxScanoutOk && m.last_w == 1920 && m.last_dx == 0 && m.last_dy == 0 &&
          m.last_dw == 1920 && m.last_dh == 1080,
          "S253 cap clear: native present Ok (no DEST writes), service dst full panel (%d)", res);
    wait_latched();
    pv = 0;
    CHECK(!hv_agx_scanout_broker_mmio(&br, HV_AGX_SCANOUT_REG_DEST_ORIGIN, &pv, false, 2),
          "S253 cap clear: DEST_ORIGIN refused");
    seq = k.NextSequence++;
    w64(HV_AGX_SCANOUT_REG_SURFACE_SIZE, UINT64_C(5120) * 720);
    w32(HV_AGX_SCANOUT_REG_WIDTH, 1280); w32(HV_AGX_SCANOUT_REG_HEIGHT, 720); w32(HV_AGX_SCANOUT_REG_STRIDE, 5120);
    w64(HV_AGX_SCANOUT_REG_REQUEST_SEQUENCE, seq);
    w32(HV_AGX_SCANOUT_REG_COMMAND, HV_AGX_SCANOUT_CMD_PRESENT);
    CHECK(r32(HV_AGX_SCANOUT_REG_RESULT) == HV_AGX_SCANOUT_RESULT_INVALID_SURFACE,
          "S253 cap clear: 1280x720 refused INVALID_SURFACE (pre-S253 behaviour)");

    printf("S251 scanout service: %d failure(s)\n", fails);
    return fails;
}
