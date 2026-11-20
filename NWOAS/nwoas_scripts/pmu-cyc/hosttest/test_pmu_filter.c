/* NWOAS PMU host test: nwoas_pmu.h mapping (PMCCFILTR_EL0 <-> PMCR1 counter-0 bits).
 * cc -std=gnu11 -Wall -Wextra -I<tree>/src test_pmu_filter.c && ./a.out */
#include <stdio.h>
#include "nwoas_pmu.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL "); } else printf("ok   "); printf(__VA_ARGS__); printf("\n"); } while (0)

int main(void)
{
    const unsigned long long other = 0xff00ff00fe00feULL & ~(NWOAS_PMCR1_C0_EL0 | NWOAS_PMCR1_C0_EL1);
    /* the value Windows writes on every CPU (logs/agxmed1g-s249-20261120-021523.wXvw0i.vuart, 11 times) */
    unsigned long long v = nwoas_pmccfiltr_to_pmcr1(other, 0x08000000ULL);
    CHECK(v == (other | NWOAS_PMCR1_C0_EL0 | NWOAS_PMCR1_C0_EL1), "Windows 0x08000000 -> count EL0+EL1, other bits kept (0x%llx)", v);
    CHECK(nwoas_pmcr1_to_pmccfiltr(v) == 0x08000000ULL, "read back = 0x08000000");
    v = nwoas_pmccfiltr_to_pmcr1(other | NWOAS_PMCR1_C0_EL0 | NWOAS_PMCR1_C0_EL1, NWOAS_PMCCFILTR_U);
    CHECK(v == (other | NWOAS_PMCR1_C0_EL1), "U=1 clears EL0 only (old code cleared EL1 here)");
    v = nwoas_pmccfiltr_to_pmcr1(other | NWOAS_PMCR1_C0_EL0 | NWOAS_PMCR1_C0_EL1, NWOAS_PMCCFILTR_P);
    CHECK(v == (other | NWOAS_PMCR1_C0_EL0), "P=1 clears EL1 only");
    v = nwoas_pmccfiltr_to_pmcr1(other, NWOAS_PMCCFILTR_P | NWOAS_PMCCFILTR_U);
    CHECK(v == other && nwoas_pmcr1_to_pmccfiltr(v) == (NWOAS_PMCCFILTR_P | NWOAS_PMCCFILTR_U | NWOAS_PMCCFILTR_NSH),
          "P=U=1 -> nothing counted, read back P|U|NSH");
    CHECK(NWOAS_PMCR1_C0_EL0 == (1ULL << 8) && NWOAS_PMCR1_C0_EL1 == (1ULL << 16),
          "bit positions = Linux PMCR1_COUNT_A64_EL0_0_7 / EL1_0_7 bit 0");
    printf(fails ? "PMU FILTER TEST: FAIL\n" : "PMU FILTER TEST: PASS (6/6)\n");
    return fails != 0;
}
