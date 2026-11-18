/* NWOAS S262 host test for src/nwoas_nvmeqd.inc (the hypervisor side of the concurrent
 * NVMe fast path).  The ANS engine (nvme_qd_*) is replaced by a fake device that completes
 * commands in random order; the guest is a model stornvme with one SQ/CQ pair.
 * Checks: every command completed exactly once with the right status, guest CQ never
 * overflows, no write outside the Windows zone reaches the device, FLUSH is a full barrier,
 * FUA writes complete only after their own flush, drains leave nothing in flight, device
 * errors / timeouts fault the path and stop submissions, tags are never double-submitted.
 * Build/run: tools-hv2/build-g262P.sh (compiles this against the patched tree).  No hardware involved. */
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPES_H /* keep src/types.h out: its uintptr_t typedef clashes with the host libc */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
#define BIT(x)        (1UL << (x))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#include "nvme.h"
#define NWOAS_PWR_HOST_TEST
#include "nwoas_pwr.h"

static int failures;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            if (failures < 20) {                                                                   \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                        \
                printf(__VA_ARGS__);                                                               \
                printf("\n");                                                                      \
            }                                                                                      \
        }                                                                                          \
    } while (0)

/* ---- m1n1 environment stubs ---- */
static u64 fake_now = 1000;
static u64 host_mrs_CNTPCT_EL0(void) { return fake_now += 3; }
static u64 host_mrs_CNTFRQ_EL0(void) { return 24000000; }
#define mrs(r) host_mrs_##r()
static void dma_rmb(void) {}
static void dma_wmb(void) {}
static u64 timeout_calculate(u64 us) { return fake_now + us * 24; }
static bool timeout_expired(u64 t) { fake_now += 2400; return fake_now > t; }
static int cur_cpu;
static int smp_id(void) { return cur_cpu; }
static int ipis;
static void smp_send_ipi(int cpu) { (void)cpu; ipis++; }
int hv_pinned_cpu = -1, boot_cpu_idx = 0;
static bool quiet = true;
struct exc_info { int unused; };
volatile u32 nwoas_nvmeqd_irq_live;
struct nwoas_pwr_nvme nwoas_pwr_nvme;

/* ---- copies of the hv_vm.c definitions the include uses (hv_vm.c:1220-1306) ---- */
#define NWOAS_NVME_NAMESPACE_LBAS  61279344UL
#define NWOAS_NVME_WRITE_FIRST_LBA 39944960UL
#define NWOAS_NVME_WRITE_LAST_LBA  59968629UL
#define NWOAS_NVME_MAX_IO_DEPTH    256
#define NWOAS_NVME_SC_SUCCESS        0x000
#define NWOAS_NVME_SC_INVALID_OPCODE 0x001
#define NWOAS_NVME_SC_INVALID_FIELD  0x002
#define NWOAS_NVME_SC_INVALID_NSID   0x00b
#define NWOAS_NVME_SC_LBA_RANGE      0x080
#define NWOAS_NVME_SC_WRITE_RO       0x182
#define NWOAS_NVME_EXEC_CANCELLED (-2)
struct nwoas_nvme_guest_cmd {
    u8 opcode, flags;
    u16 cid;
    u32 nsid, cdw2, cdw3;
    u64 metadata, prp1, prp2;
    u32 cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
};
struct nwoas_nvme_guest_cqe {
    u32 result, reserved;
    u16 sq_head, sq_id, cid, status;
};
static struct {
    bool armed, faulted, deferred, irq_enabled, cache_enabled, link_busy, local_mask_allowed;
    u32 mask;
    u64 sq_base, cq_base;
    u16 sq_size, cq_size, sq_head, sq_tail, cq_head, cq_tail, cq_pending;
    u8 cq_phase;
    u32 commands, errors, link_sequence, lifecycle_epoch;
    u16 sq_generation, cq_generation;
} nwoas_nvme_fp;
static struct { u64 max_ns2_ticks; u32 ns2_calls; } nwoas_nvme_diag;
static u64 nwoas_pwr_sq_ts[NWOAS_NVME_MAX_IO_DEPTH];
static bool s229_enabled;
static struct { struct { u32 csts; } control; } s229_front;

#define GUEST_BASE 0x100000000ULL
#define GUEST_SIZE (1u << 20)
static u8 guest_mem[GUEST_SIZE];
static void *nwoas_nvme_guest_ptr(u64 ipa, size_t len)
{
    if (ipa < GUEST_BASE || ipa + len > GUEST_BASE + GUEST_SIZE)
        return NULL;
    return guest_mem + (ipa - GUEST_BASE);
}
static int link_calls;
static int nwoas_nvme_link_execute(struct exc_info *ctx, const struct nwoas_nvme_guest_cmd *cmd,
                                   u32 *res)
{
    (void)ctx;
    (void)cmd;
    link_calls++;
    *res = 0x1234;
    return 0;
}
static void nwoas_nvme_fastpath_update_irq(void) {}

#define printf(...) (quiet ? 0 : printf(__VA_ARGS__))
#include "nwoas_nvmeqd.inc"
#undef printf

static struct nwoas_qd_slot *slot_of(int tag) { return &nwoas_qd.slot[tag]; }

/* ---- fake ANS engine ---- */
#define BAD_PRP 0xdead0000ULL
struct devcmd {
    int tag;
    bool flush, guest_flush, write;
    u64 lba;
};
static struct {
    bool active, faulted;
    u32 depth;
    u64 alloc, inflight;
    struct devcmd dq[64];
    int dq_n;
    struct nvme_qd_cqe done[64];
    int done_n;
    int submits, auto_complete, fail_next_status;
} eng;
static bool chain_flushed[65536];

int nvme_qd_setup(u32 depth, bool want_irq)
{
    memset(&eng, 0, sizeof(eng));
    eng.active = true;
    eng.depth = depth;
    return want_irq ? 590 : 0;
}
bool nvme_qd_active(void) { return eng.active; }
bool nvme_qd_faulted(void) { return eng.faulted; }
int nvme_qd_alloc(void)
{
    if (!eng.active || eng.faulted)
        return -1;
    for (int t = 2; t < 2 + (int)eng.depth; t++)
        if (!(eng.alloc & BIT(t))) {
            eng.alloc |= BIT(t);
            return t;
        }
    return -1;
}
void nvme_qd_free(int tag)
{
    CHECK(!(eng.inflight & BIT(tag)), "free of in-flight tag %d", tag);
    if (!(eng.inflight & BIT(tag)))
        eng.alloc &= ~BIT(tag);
}
static bool guest_flush_in_device(void)
{
    for (int i = 0; i < eng.dq_n; i++)
        if (eng.dq[i].guest_flush)
            return true;
    return false;
}
static void dev_push(struct devcmd d)
{
    CHECK(eng.alloc & BIT(d.tag), "submit on unallocated tag %d", d.tag);
    CHECK(!(eng.inflight & BIT(d.tag)), "double submit tag %d", d.tag);
    CHECK(!guest_flush_in_device(), "submission while a guest FLUSH is on the device");
    eng.inflight |= BIT(d.tag);
    eng.dq[eng.dq_n++] = d;
    eng.submits++;
}
int nvme_qd_rw(int tag, bool write, u64 lba, u32 count, u64 prp1, u64 prp2)
{
    (void)prp2;
    if (!eng.active || eng.faulted)
        return NVME_QD_FAULT;
    if (prp1 == BAD_PRP)
        return NVME_QD_REFUSED;
    CHECK(!write || (lba >= NWOAS_NVME_WRITE_FIRST_LBA &&
                     lba + count - 1 <= NWOAS_NVME_WRITE_LAST_LBA),
          "write outside the zone reached the device lba=%llu", (unsigned long long)lba);
    dev_push((struct devcmd){.tag = tag, .write = write, .lba = lba});
    return NVME_QD_OK;
}
int nvme_qd_flush(int tag)
{
    if (!eng.active || eng.faulted)
        return NVME_QD_FAULT;
    bool guest = slot_of(tag)->state == 0; /* fresh slot = guest FLUSH, RW = FUA chain */
    if (guest)
        CHECK(eng.dq_n == 0, "guest FLUSH submitted with %d commands on the device", eng.dq_n);
    dev_push((struct devcmd){.tag = tag, .flush = true, .guest_flush = guest});
    return NVME_QD_OK;
}
static void dev_complete(int i, u16 status)
{
    struct devcmd d = eng.dq[i];
    eng.dq[i] = eng.dq[--eng.dq_n];
    if (d.flush && slot_of(d.tag)->state == 3 /* CHAIN */ && !status)
        chain_flushed[slot_of(d.tag)->cid] = true;
    eng.done[eng.done_n++] = (struct nvme_qd_cqe){.tag = (u8)d.tag, .status = status};
}
static void dev_complete_random(int max)
{
    while (max-- > 0 && eng.dq_n)
        dev_complete(rand() % eng.dq_n, 0);
}
u32 nvme_qd_reap(struct nvme_qd_cqe *out, u32 max)
{
    if (eng.auto_complete && eng.dq_n)
        dev_complete_random(1);
    u32 n = 0;
    while (n < max && eng.done_n) {
        int i = rand() % eng.done_n;
        out[n] = eng.done[i];
        eng.inflight &= ~BIT(out[n].tag);
        eng.done[i] = eng.done[--eng.done_n];
        n++;
    }
    return n;
}
void nvme_qd_mailbox(void) {}
void nvme_qd_irq_arm(int cpu) { (void)cpu; nwoas_nvmeqd_irq_live = 590; }
void nvme_qd_irq_unmask(void) {}
void nvme_qd_irq_disarm(void) { nwoas_nvmeqd_irq_live = 0; }
void nvme_qd_get_info(struct nvme_qd_info *o) { memset(o, 0, sizeof(*o)); }


/* ---- guest model ---- */
#define SQ_IPA (GUEST_BASE + 0x10000)
#define CQ_IPA (GUEST_BASE + 0x20000)
enum { K_READ, K_WRITE, K_FUA, K_BADZONE, K_FLUSH, K_NS2, K_BADPRP, K_N };
static struct {
    u16 sq_size, cq_size, sq_tail, cq_head;
    u8 phase;
    u16 next_cid;
    int outstanding;
    u8 kind[65536];
    bool live[65536];
    long completed, by_kind[K_N];
} g;
static struct exc_info ctx;

static void guest_arm(u16 sq_size, u16 cq_size, u32 depth, u32 flags, bool cache)
{
    memset(&nwoas_nvme_fp, 0, sizeof(nwoas_nvme_fp));
    memset(&g, 0, sizeof(g));
    memset(&nwoas_qd, 0, sizeof(nwoas_qd));
    memset(chain_flushed, 0, sizeof(chain_flushed));
    memset(guest_mem, 0, sizeof(guest_mem));
    u64 r = nwoas_qd_config(depth, flags);
    CHECK(r >> 63, "config refused depth=%u flags=%u", depth, flags);
    CHECK(nwoas_qd_config(depth, flags) == 0, "second config accepted");
    nwoas_nvme_fp.armed = true;
    nwoas_nvme_fp.sq_base = SQ_IPA;
    nwoas_nvme_fp.cq_base = CQ_IPA;
    nwoas_nvme_fp.sq_size = sq_size;
    nwoas_nvme_fp.cq_size = cq_size;
    nwoas_nvme_fp.cq_phase = 1;
    nwoas_nvme_fp.cache_enabled = cache;
    nwoas_nvme_fp.irq_enabled = true;
    g.sq_size = sq_size;
    g.cq_size = cq_size;
    g.phase = 1;
}

static bool guest_submit(int kind)
{
    if (g.outstanding >= g.sq_size - 1)
        return false;
    struct nwoas_nvme_guest_cmd c;
    memset(&c, 0, sizeof(c));
    u16 cid = g.next_cid++;
    CHECK(!g.live[cid], "cid reuse");
    c.cid = cid;
    c.nsid = 1;
    c.prp1 = GUEST_BASE + 0x40000;
    u64 lba = NWOAS_NVME_WRITE_FIRST_LBA + (u64)(rand() % 1000000);
    switch (kind) {
        case K_READ: c.opcode = 2; lba = (u64)(rand() % 60000000); break;
        case K_WRITE: c.opcode = 1; break;
        case K_FUA: c.opcode = 1; c.cdw12 |= BIT(30); break;
        case K_BADZONE: c.opcode = 1; lba = 128006 + (u64)(rand() % 1000); break;
        case K_FLUSH: c.opcode = 0; c.nsid = (rand() & 1) ? 1 : 0xffffffff; c.prp1 = 0; break;
        case K_NS2: c.opcode = 2; c.nsid = 2; lba = 1; break;
        case K_BADPRP: c.opcode = 2; c.prp1 = BAD_PRP; break;
    }
    if (c.opcode) {
        c.cdw10 = (u32)lba;
        c.cdw11 = (u32)(lba >> 32);
        c.cdw12 |= (u32)(rand() % 8); /* 1..8 blocks */
    }
    memcpy(guest_mem + (SQ_IPA - GUEST_BASE) + (u64)g.sq_tail * 64, &c, 64);
    g.kind[cid] = (u8)kind;
    g.live[cid] = true;
    g.outstanding++;
    nwoas_pwr_sq_ts[g.sq_tail] = fake_now;
    g.sq_tail = (g.sq_tail + 1) % g.sq_size;
    return true;
}

static void guest_doorbell(void) /* hv_vm.c 0x1008 handler, QD branch */
{
    nwoas_nvme_fp.sq_tail = g.sq_tail;
    cur_cpu = rand() % 8;
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_SQDB);
}

static void guest_consume(int max)
{
    int n = 0;
    while (max-- > 0) {
        struct nwoas_nvme_guest_cqe e;
        memcpy(&e, guest_mem + (CQ_IPA - GUEST_BASE) + (u64)g.cq_head * 16, 16);
        if ((e.status & 1) != g.phase)
            break;
        u16 st = e.status >> 1;
        CHECK(g.live[e.cid], "completion for dead cid %u", e.cid);
        g.live[e.cid] = false;
        g.outstanding--;
        g.completed++;
        int k = g.kind[e.cid];
        g.by_kind[k]++;
        u16 want = k == K_BADZONE ? NWOAS_NVME_SC_WRITE_RO :
                   k == K_BADPRP  ? NWOAS_NVME_SC_INVALID_FIELD : 0;
        CHECK(st == want, "cid %u kind %d status 0x%x want 0x%x", e.cid, k, st, want);
        if (k == K_FUA || (k == K_WRITE && !nwoas_nvme_fp.cache_enabled))
            CHECK(chain_flushed[e.cid], "write cid %u completed before its own flush", e.cid);
        g.cq_head = (g.cq_head + 1) % g.cq_size;
        if (!g.cq_head)
            g.phase ^= 1;
        n++;
    }
    if (!n)
        return;
    /* hv_vm.c 0x100c handler */
    u16 consumed = (g.cq_head + nwoas_nvme_fp.cq_size - nwoas_nvme_fp.cq_head) % nwoas_nvme_fp.cq_size;
    CHECK(consumed <= nwoas_nvme_fp.cq_pending, "guest consumed more than posted");
    nwoas_nvme_fp.cq_head = g.cq_head;
    nwoas_nvme_fp.cq_pending -= consumed;
    cur_cpu = 0;
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_CQDB);
}

static int pick_kind(void)
{
    int r = rand() % 100;
    return r < 60 ? K_READ : r < 78 ? K_WRITE : r < 88 ? K_FUA : r < 91 ? K_BADZONE :
           r < 96 ? K_FLUSH : r < 98 ? K_NS2 : K_BADPRP;
}

static void check_invariants(void)
{
    CHECK(nwoas_nvme_fp.cq_pending <= nwoas_nvme_fp.cq_size - 1, "guest CQ overflow %u",
          nwoas_nvme_fp.cq_pending);
    CHECK(nwoas_nvme_fp.cq_pending + nwoas_qd.busy <= nwoas_nvme_fp.cq_size - 1u,
          "promised completions exceed CQ room");
    int busy = 0;
    for (int t = 0; t < 64; t++)
        busy += nwoas_qd.slot[t].state != 0;
    CHECK(busy == (int)nwoas_qd.busy, "busy count %u != slots %d", nwoas_qd.busy, busy);
}

static void run_random(const char *name, u16 sqs, u16 cqs, u32 depth, u32 flags, bool cache,
                       int steps)
{
    guest_arm(sqs, cqs, depth, flags, cache);
    for (int i = 0; i < steps && !failures; i++) {
        int burst = rand() % 12;
        for (int b = 0; b < burst; b++)
            guest_submit(pick_kind());
        if (burst)
            guest_doorbell();
        check_invariants();
        dev_complete_random(rand() % 10);
        if (rand() % 3 == 0) {
            cur_cpu = 0;
            nwoas_qd_service(&ctx, NWOAS_QD_CTX_IRQ);
        }
        if (rand() % 5 == 0) {
            cur_cpu = 0;
            nwoas_qd_service(&ctx, NWOAS_QD_CTX_TICK);
        }
        guest_consume(rand() % 16);
        check_invariants();
    }
    /* finish: keep completing until the guest has everything */
    for (int i = 0; i < 100000 && g.outstanding && !failures; i++) {
        dev_complete_random(4);
        cur_cpu = 0;
        nwoas_qd_service(&ctx, NWOAS_QD_CTX_TICK);
        guest_consume(64);
    }
    CHECK(!g.outstanding, "%s: %d commands never completed", name, g.outstanding);
    CHECK(!nwoas_nvme_fp.faulted, "%s: unexpected fault", name);
    CHECK(nwoas_qd.busy == 0 && eng.dq_n == 0 && !eng.alloc, "%s: leftovers", name);
    printf("%-28s completed=%ld read=%ld write=%ld fua=%ld badzone=%ld flush=%ld ns2=%ld badprp=%ld "
           "dev_submits=%d inflight_max=%llu stall_tag/cq/barrier=%llu/%llu/%llu chain=%llu\n",
           name, g.completed, g.by_kind[K_READ], g.by_kind[K_WRITE], g.by_kind[K_FUA],
           g.by_kind[K_BADZONE], g.by_kind[K_FLUSH], g.by_kind[K_NS2], g.by_kind[K_BADPRP],
           eng.submits, (unsigned long long)nwoas_qd.st.inflight_max,
           (unsigned long long)nwoas_qd.st.stall_tags, (unsigned long long)nwoas_qd.st.stall_cq,
           (unsigned long long)nwoas_qd.st.stall_barrier,
           (unsigned long long)nwoas_qd.st.chain_flushes);
}

static void test_drain(void)
{
    guest_arm(256, 256, 32, NWOAS_QD_F_IRQ, true);
    for (int i = 0; i < 40; i++)
        guest_submit(i % 3 ? K_READ : K_FUA);
    guest_doorbell();
    CHECK(nwoas_qd.busy > 0, "drain: nothing in flight");
    eng.auto_complete = 1;
    CHECK(nwoas_qd_drain(), "drain failed");
    eng.auto_complete = 0;
    CHECK(nwoas_qd.busy == 0 && eng.dq_n == 0, "drain left busy=%u dev=%d", nwoas_qd.busy, eng.dq_n);
    /* completions of drained commands were posted, the rest stay queued in the SQ */
    guest_consume(256);
    printf("%-28s ok, guest got %ld, sq backlog %u\n", "drain", g.completed, nwoas_qd_backlog());
}

static void test_device_error(void)
{
    guest_arm(256, 256, 16, 0, true);
    for (int i = 0; i < 10; i++)
        guest_submit(K_READ);
    guest_doorbell();
    int before = eng.submits;
    dev_complete(0, 0x281);
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_IRQ);
    CHECK(nwoas_nvme_fp.faulted, "device error did not fault");
    for (int i = 0; i < 5; i++)
        guest_submit(K_READ);
    guest_doorbell();
    CHECK(eng.submits == before, "submitted after fault");
    printf("%-28s ok, faulted, submits stay %d\n", "device error", eng.submits);
}

static void test_timeout(void)
{
    guest_arm(256, 256, 8, 0, true);
    guest_submit(K_READ);
    guest_doorbell();
    fake_now += 2 * 24000000ULL;
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_TICK);
    CHECK(nwoas_nvme_fp.faulted && nwoas_qd.st.timeouts == 1, "timeout did not fault");
    CHECK(eng.alloc != 0, "timed-out tag was freed");
    printf("%-28s ok\n", "timeout");
}

static void test_flush_barrier(void)
{
    guest_arm(256, 256, 32, 0, true);
    for (int i = 0; i < 6; i++)
        guest_submit(K_WRITE);
    guest_submit(K_FLUSH);
    guest_submit(K_READ);
    guest_doorbell();
    CHECK(eng.dq_n == 6, "flush or read passed the barrier (%d on device)", eng.dq_n);
    dev_complete_random(6);
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_IRQ);
    CHECK(eng.dq_n == 1 && eng.dq[0].guest_flush, "flush not alone on the device");
    dev_complete_random(1);
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_IRQ);
    CHECK(eng.dq_n == 1 && !eng.dq[0].flush, "read after flush not submitted");
    dev_complete_random(1);
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_IRQ);
    guest_consume(16);
    CHECK(g.completed == 8, "flush barrier: completed %ld", g.completed);
    printf("%-28s ok\n", "flush barrier");
}

static void test_strict_fua(void)
{
    guest_arm(256, 256, 32, NWOAS_QD_F_STRICT, true);
    guest_submit(K_READ);
    guest_submit(K_FUA);
    guest_submit(K_READ);
    guest_doorbell();
    CHECK(eng.dq_n == 1, "strict: FUA write did not wait (%d)", eng.dq_n);
    dev_complete_random(1);
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_IRQ);
    CHECK(eng.dq_n == 1 && eng.dq[0].write, "strict: FUA write not alone");
    dev_complete_random(1);
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_IRQ);
    CHECK(eng.dq_n == 1 && eng.dq[0].flush, "strict: chained flush not alone");
    dev_complete_random(1);
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_IRQ);
    CHECK(eng.dq_n == 1 && !eng.dq[0].write && !eng.dq[0].flush, "strict: read not released");
    dev_complete_random(1);
    nwoas_qd_service(&ctx, NWOAS_QD_CTX_IRQ);
    guest_consume(16);
    CHECK(g.completed == 3, "strict: completed %ld", g.completed);
    printf("%-28s ok\n", "strict FUA");
}

static void test_config_rules(void)
{
    memset(&nwoas_nvme_fp, 0, sizeof(nwoas_nvme_fp));
    memset(&nwoas_qd, 0, sizeof(nwoas_qd));
    CHECK(nwoas_qd_config(0, 0) == 1, "depth 0 must be the explicit off answer");
    CHECK(nwoas_qd_config(62, 0) == 0, "depth 62 accepted");
    CHECK(nwoas_qd_config(8, 8) == 0, "unknown flag accepted");
    nwoas_nvme_fp.lifecycle_epoch = 1;
    CHECK(nwoas_qd_config(8, 0) == 0, "config accepted after the guest armed once");
    nwoas_nvme_fp.lifecycle_epoch = 0;
    u64 r = nwoas_qd_config(8, NWOAS_QD_F_IRQ);
    CHECK(r == ((1ULL << 63) | (590ULL << 32) | (8 << 8) | 1), "config answer %llx",
          (unsigned long long)r);
    printf("%-28s ok\n", "config rules");
}

int main(int argc, char **argv)
{
    srand(argc > 1 ? (unsigned)atoi(argv[1]) : 262);
    test_config_rules();
    test_flush_barrier();
    test_strict_fua();
    test_drain();
    test_device_error();
    test_timeout();
    run_random("random qd8 sq256 cq256", 256, 256, 8, NWOAS_QD_F_IRQ, true, 20000);
    run_random("random qd32 sq256 cq256", 256, 256, 32, NWOAS_QD_F_IRQ, true, 20000);
    run_random("random qd61 sq64 cq16", 64, 16, 61, 0, true, 20000);
    run_random("random qd32 nocache", 256, 256, 32, 0, false, 10000);
    run_random("random qd16 strict", 128, 128, 16, NWOAS_QD_F_STRICT, true, 10000);
    run_random("random qd1", 32, 32, 1, 0, true, 5000);
    printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
