"""Initialize the M1 CPU clusters before entering the Windows UEFI payload.

The normal Linux payload path calls cpufreq_init() from payload.c.  NWOAS
chainloads UEFI through run_guest instead, so that call is skipped unless a
module performs it explicitly.  Keep the implementation in m1n1's existing
T8103 cpufreq code and verify the hardware P-state registers around the call.
"""

CHIP_T8103 = 0x8103
CLUSTER_PSTATE = 0x20020
E_CLUSTER = 0x210E00000
P_CLUSTER = 0x211E00000
PSTATE_BUSY = 1 << 31


def _state(base):
    value = p.read64(base + CLUSTER_PSTATE)
    return value, value & 0x1F, bool(value & PSTATE_BUSY)


chip = p.get_chipid()
if chip != CHIP_T8103:
    raise RuntimeError(f"S140 expected T8103, found chip 0x{chip:x}")

before_e = _state(E_CLUSTER)
before_p = _state(P_CLUSTER)
hv.log(
    "HVLOG: NWOAS-S140 CPU PSTATE before "
    f"E=0x{before_e[0]:016x}/p{before_e[1]}/busy={int(before_e[2])} "
    f"P=0x{before_p[0]:016x}/p{before_p[1]}/busy={int(before_p[2])}"
)

result = p.cpufreq_init()
after_e = _state(E_CLUSTER)
after_p = _state(P_CLUSTER)
hv.log(
    "HVLOG: NWOAS-S140 CPU PSTATE after "
    f"rc={result} E=0x{after_e[0]:016x}/p{after_e[1]}/busy={int(after_e[2])} "
    f"P=0x{after_p[0]:016x}/p{after_p[1]}/busy={int(after_p[2])}"
)

if result != 0:
    raise RuntimeError(f"S140 cpufreq_init failed: {result}")
if after_e[2] or after_p[2]:
    raise RuntimeError("S140 CPU cluster P-state transition remained busy")
if after_e[1] != 5 or after_p[1] != 7:
    raise RuntimeError(
        f"S140 unexpected default P-states: E={after_e[1]} P={after_p[1]}"
    )

print("[s140] T8103 CPU clusters initialized: E pstate 5, P pstate 7")
