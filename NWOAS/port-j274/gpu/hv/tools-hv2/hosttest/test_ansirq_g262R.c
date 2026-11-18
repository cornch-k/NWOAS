/* NWOAS S262 g262R host test: the ANS2 interrupt choice (src/nwoas_ans_irq.h, the exact code
 * nvme_qd_probe_irq() runs) against a real ADT dump, plus synthetic shapes.
 * usage: test_ansirq_g262R <adt.bin> [expected line]   (J274 13.5 dump: expected 590)
 * No hardware involved. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adt_host.h"
#include "nwoas_ans_irq.h"

static int failures;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static void synthetic(void)
{
    unsigned int irq, idx;
    const unsigned int five[5] = {584, 583, 586, 585, 590}, one[1] = {590}, four[4] = {583, 584, 585, 586};
    const unsigned int dup[5] = {584, 583, 586, 585, 585};
    CHECK(nwoas_ans_irq_pick(five, 20, 95, 95, 896, &irq, &idx) == 0 && irq == 590 && idx == 4, "5 cells");
    CHECK(nwoas_ans_irq_pick(one, 4, 95, 95, 896, &irq, &idx) == 0 && irq == 590 && idx == 0, "1 cell");
    CHECK(nwoas_ans_irq_pick(four, 16, 95, 95, 896, &irq, &idx) == NWOAS_ANS_IRQ_CELLS && !irq, "4 cells refused");
    CHECK(nwoas_ans_irq_pick(five, 18, 95, 95, 896, &irq, &idx) == NWOAS_ANS_IRQ_CELLS, "odd length");
    CHECK(nwoas_ans_irq_pick(five, 20, 94, 95, 896, &irq, &idx) == NWOAS_ANS_IRQ_PARENT, "wrong parent");
    CHECK(nwoas_ans_irq_pick(five, 20, 0, 95, 896, &irq, &idx) == NWOAS_ANS_IRQ_PARENT, "no parent");
    CHECK(nwoas_ans_irq_pick(five, 20, 95, 95, 590, &irq, &idx) == NWOAS_ANS_IRQ_RANGE, "line >= nr_irq");
    CHECK(nwoas_ans_irq_pick(dup, 20, 95, 95, 896, &irq, &idx) == NWOAS_ANS_IRQ_MAILBOX, "5th = mailbox line");
    CHECK(nwoas_ans_irq_pick(NULL, 0, 95, 95, 896, &irq, &idx) == NWOAS_ANS_IRQ_NO_PROP, "no property");
}

int main(int argc, char **argv)
{
    synthetic();
    if (argc < 2 || adt_load(argv[1])) {
        printf("usage: %s <adt.bin> [expected]\n", argv[0]);
        return 2;
    }
    int ans = adt_path_offset(adt_blob, "/arm-io/ans"), aicn = adt_path_offset(adt_blob, "/arm-io/aic");
    CHECK(ans >= 0 && aicn >= 0, "nodes ans=%d aic=%d", ans, aicn);
    if (ans < 0 || aicn < 0)
        return 1;
    uint32_t len = 0, parent = 0, aic_ph = 0, irq = 0, idx = 0;
    const uint32_t *cells = adt_getprop(adt_blob, ans, "interrupts", &len);
    adt_getprop_copy(adt_blob, ans, "interrupt-parent", &parent, sizeof(parent));
    adt_getprop_copy(adt_blob, aicn, "AAPL,phandle", &aic_ph, sizeof(aic_ph));
    printf("/arm-io/ans interrupts len=%u:", len);
    for (uint32_t i = 0; cells && i < len / 4; i++)
        printf(" %u", cells[i]);
    printf("  interrupt-parent=0x%x  /arm-io/aic AAPL,phandle=0x%x\n", parent, aic_ph);
    int r = nwoas_ans_irq_pick(cells, len, parent, aic_ph, 0, &irq, &idx);
    printf("g262R choice: reason=%d irq=%u (cell %u of %u)\n", r, irq, idx, len / 4);
    /* the g262Q rule on the same bytes */
    int old_ok = cells && len == 4 && cells[0] && cells[0] < 0x1000;
    printf("g262Q rule (len == 4): %s\n", old_ok ? "usable" : "unusable -> polling only");
    if (argc > 2)
        CHECK(r == 0 && irq == (uint32_t)strtoul(argv[2], NULL, 0), "expected %s, got r=%d irq=%u", argv[2], r, irq);
    printf("%s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
