/* Host test for NWOAS g262U (no hardware): the LATCHED decision (nwoas_latch.h)
 * and the broker's fail_latch (hv_agx_scanout_broker.c) from the patched tree.
 *   cc -std=gnu11 -Wall -Wextra -I<src> test_latch.c <src>/hv_agx_scanout_broker.c -o t && ./t */
#include <stdio.h>
#include <string.h>
#include "nwoas_latch.h"
#include "hv_agx_scanout_broker.h"

static int fails, checks;
#define CHECK(name, cond) do { checks++; if (cond) printf("ok   %s\n", name); else { fails++; printf("FAIL %s\n", name); } } while (0)

int main(void)
{
    const unsigned int SYN = 0x80000005u, REAL = 55u, MODESET = 56u;
    /* plain present: its own id, time based */
    CHECK("own id, delay not elapsed -> wait", nwoas_latch_decide(REAL, REAL, 0, 0) == 0);
    CHECK("own id, delay elapsed -> latched", nwoas_latch_decide(REAL, REAL, 0, 1) == 1);
    CHECK("no id -> not this present", nwoas_latch_decide(0, REAL, 0, 1) == -2);
    /* the 2026-11-20 case: a present retired during the reconnect (synthetic id), then the modeset */
    unsigned int sup = nwoas_latch_supersede(SYN, MODESET);
    CHECK("modeset remembers the id it replaced", sup == SYN);
    CHECK("g262T behaviour would be -2 (expected != applied)", SYN != MODESET);
    CHECK("present replaced by the modeset -> latched", nwoas_latch_decide(SYN, MODESET, sup, 0) == 1);
    CHECK("a real present replaced by the modeset -> latched", nwoas_latch_decide(REAL, MODESET, nwoas_latch_supersede(REAL, MODESET), 0) == 1);
    /* still strict for anything else */
    CHECK("unrelated id stays a mismatch", nwoas_latch_decide(0x1234u, MODESET, sup, 1) == -2);
    CHECK("same id on both sides remembers nothing", nwoas_latch_supersede(MODESET, MODESET) == 0);
    CHECK("no previous id remembers nothing", nwoas_latch_supersede(0, MODESET) == 0);
    CHECK("expected 0 never matches a zero superseded", nwoas_latch_decide(0, MODESET, 0, 1) == -2);
    /* the next present after the modeset is judged on its own id again */
    CHECK("next present waits for its delay", nwoas_latch_decide(57u, 57u, sup, 0) == 0);

    /* broker: a failed latch must not leave applied != latched (that refused every later PRESENT) */
    struct hv_agx_scanout_broker b;
    memset(&b, 0, sizeof(b));
    b.abi_version = HV_AGX_SCANOUT_ABI_VERSION_V2;
    b.state = HV_AGX_SCANOUT_ACTIVE;
    b.applied_sequence = 9;
    b.latched_sequence = 8;
    b.swap_id = SYN;
    CHECK("fail_latch accepts the applied, unlatched sequence", hv_agx_scanout_broker_fail_latch(&b, 9));
    CHECK("fail_latch raises IRQ_ERROR only", (b.irq_status & HV_AGX_SCANOUT_IRQ_MASK) == HV_AGX_SCANOUT_IRQ_ERROR);
    CHECK("fail_latch reports PRESENT_FAILED", b.result == HV_AGX_SCANOUT_RESULT_PRESENT_FAILED);
    CHECK("after fail_latch applied == latched (next PRESENT admissible)", b.latched_sequence == b.applied_sequence);
    CHECK("fail_latch refuses a second report of the same sequence", !hv_agx_scanout_broker_fail_latch(&b, 9));
    CHECK("fail_latch refuses a sequence that is not the applied one", !hv_agx_scanout_broker_fail_latch(&b, 7));

    printf("LATCH TEST: %s (%d/%d)\n", fails ? "FAIL" : "PASS", checks - fails, checks);
    return fails ? 1 : 0;
}
