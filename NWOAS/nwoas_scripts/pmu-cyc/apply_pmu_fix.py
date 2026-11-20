#!/usr/bin/env python3
"""NWOAS PMU cycle-counter fix: apply to a m1n1 tree (g262R or s264G source).  Host only.
  python3 apply_pmu_fix.py <tree>      (edits <tree>/src/hv_exc.c, adds <tree>/src/nwoas_pmu.h)
What it changes: the PMUv3 PMCCFILTR_EL0 emulation programmed Apple PMCR1 with the PMCR1_EL1 encoding.
With HCR_EL2.E2H=1 (exception.c) that encoding at EL2 is the HOST filter; the guest's (EL1, EL0 not in
host) filter is the PMCR1_EL12 alias (Linux 46573d944f00 "drivers/perf: apple_m1: Support host/guest event
filtering", arch/arm64/include/asm/apple_m1_pmu.h: PMCR1_EL12 = sys_reg(3, 1, 15, 7, 2)).  So Windows'
"count EL0 + EL1" request never reached the guest filter, and PMCCNTR_EL0 (= Apple PMC0) barely moved while
Windows ran (S262 S3: 28.4 cycles/us, ~MHz 22-34).  Also fixes the U=1 branch, which cleared the EL1 bit.
"""
import sys
from pathlib import Path

HDR = r'''/* SPDX-License-Identifier: MIT */
/* NWOAS PMU: PMUv3 PMCCFILTR_EL0 <-> Apple PMCR1 counter-0 filter bits (PMC0 = the cycle counter that
 * hv_exc.c returns for PMCCNTR_EL0).  With HCR_EL2.E2H=1 the GUEST filter (EL1, EL0 not in host) is the
 * PMCR1_EL12 alias; the PMCR1_EL1 encoding at EL2 is the host's (Linux 46573d944f00,
 * arch/arm64/include/asm/apple_m1_pmu.h).  Bits: PMCR1[8+n] = count EL0 (A64), PMCR1[16+n] = count EL1. */
#ifndef NWOAS_PMU_H
#define NWOAS_PMU_H

#define NWOAS_SYS_IMP_APL_PMCR1_EL12 sys_reg(3, 1, 15, 7, 2)
#define NWOAS_PMCR1_C0_EL0 (1ULL << 8)
#define NWOAS_PMCR1_C0_EL1 (1ULL << 16)
#define NWOAS_PMCCFILTR_P (1ULL << 31) /* 1 = do not count EL1 */
#define NWOAS_PMCCFILTR_U (1ULL << 30) /* 1 = do not count EL0 */
#define NWOAS_PMCCFILTR_NSH (1ULL << 27)

static inline unsigned long long nwoas_pmccfiltr_to_pmcr1(unsigned long long pmcr1, unsigned long long f)
{
    if (f & NWOAS_PMCCFILTR_P)
        pmcr1 &= ~NWOAS_PMCR1_C0_EL1;
    else
        pmcr1 |= NWOAS_PMCR1_C0_EL1;
    if (f & NWOAS_PMCCFILTR_U)
        pmcr1 &= ~NWOAS_PMCR1_C0_EL0;
    else
        pmcr1 |= NWOAS_PMCR1_C0_EL0;
    return pmcr1;
}

static inline unsigned long long nwoas_pmcr1_to_pmccfiltr(unsigned long long pmcr1)
{
    unsigned long long v = NWOAS_PMCCFILTR_NSH; /* as before: EL2 always reported as counted */
    if (!(pmcr1 & NWOAS_PMCR1_C0_EL0))
        v |= NWOAS_PMCCFILTR_U;
    if (!(pmcr1 & NWOAS_PMCR1_C0_EL1))
        v |= NWOAS_PMCCFILTR_P;
    return v;
}

#endif
'''

def main():
    tree = Path(sys.argv[1])
    exc = tree / "src/hv_exc.c"
    s = exc.read_text()
    a = s.index("        case SYSREG_ISS(SYS_PMCCFILTR_EL0):")
    b = s.index("        case SYSREG_ISS(SYS_PMCEID0_EL0):", a)
    old = s[a:b]
    assert old.count("SYS_IMP_APL_PMCR1") == 3, "unexpected PMCCFILTR block"
    new = '''        case SYSREG_ISS(SYS_PMCCFILTR_EL0):
            /* NWOAS PMU: the guest's filter is PMCR1_EL12 (see nwoas_pmu.h); the old code programmed the
             * host's PMCR1, so PMC0 never counted Windows' EL0/EL1 cycles. */
            if(is_read) {
                regs[rt] = nwoas_pmcr1_to_pmccfiltr(mrs(NWOAS_SYS_IMP_APL_PMCR1_EL12));
                printf("HV PMUv3 Redirect: mrs x%ld, PMCCFILTR_EL0 = 0x%lx (PMCR1_EL12)\\n", rt, regs[rt]);
            }
            else {
                u64 v = nwoas_pmccfiltr_to_pmcr1(mrs(NWOAS_SYS_IMP_APL_PMCR1_EL12), regs[rt]);
                sysop("isb");
                msr(NWOAS_SYS_IMP_APL_PMCR1_EL12, v);
                sysop("isb");
                printf("HV PMUv3 Redirect (OK): msr PMCCFILTR_EL0, x%ld = 0x%lx -> PMCR1_EL12 0x%lx\\n", rt,
                       regs[rt], (u64)mrs(NWOAS_SYS_IMP_APL_PMCR1_EL12));
            }
            return true;
'''
    s = s[:a] + new + s[b:]
    inc = '#include "hv.h"\n'
    assert inc in s, "no hv.h include"
    s = s.replace(inc, inc + '#include "nwoas_pmu.h" /* NWOAS PMU cycle counter */\n', 1)
    exc.write_text(s)
    (tree / "src/nwoas_pmu.h").write_text(HDR)
    print("applied to", tree)

if __name__ == "__main__":
    main()
