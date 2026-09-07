# pcie_ptwalk.py (NWOAS) — DIAGNOSTIC (no guest boot needed; killed after DONE-DIAG).
# Verify whether the Device stage-2 re-map actually lands in the stage-2 PTE, and
# whether m1n1 EL2 can read the FL1100 BAR. Disambiguates "re-map didn't apply" vs
# "attribute isn't the fix".
def memattr(pte):
    return (pte >> 2) & 0xf   # stage-2 MemAttr bits [5:2]; 0b1111=Normal-WB(unchanged), 0b00xx=Device

print("[ptwalk] pcie_init ->", p.pcie_init())

A = 0x6c0008330   # the faulting FL1100 register
try:
    pre = p.hv_pt_walk(A)
    print(f"[ptwalk] PRE-remap  PTE({A:#x}) = {pre:#x}  MemAttr={memattr(pre):#06b}")
except Exception as e:
    print("[ptwalk] pre walk err:", e)

PTE_MEMATTR_DEVICE_nGnRE = 0b0001 << 2
DEV_ATTR = hv.PTE_ACCESS | hv.PTE_SH_NS | hv.PTE_S2AP_RW | PTE_MEMATTR_DEVICE_nGnRE
base, size = 0x6c0000000, 0x40000000
print(f"[ptwalk] DEV_ATTR={DEV_ATTR:#x} -> re-map {base:#x}+{size:#x} Device-nGnRE")
print("[ptwalk] hv_map ->", p.hv_map(base, base | DEV_ATTR | hv.PTE_VALID, size, 1))

try:
    post = p.hv_pt_walk(A)
    print(f"[ptwalk] POST-remap PTE({A:#x}) = {post:#x}  MemAttr={memattr(post):#06b}  (Device if 0b00xx)")
except Exception as e:
    print("[ptwalk] post walk err:", e)

try:
    v = p.read32(A)
    print(f"[ptwalk] m1n1 EL2 read {A:#x} = {v:#x}")
except Exception as e:
    print("[ptwalk] m1n1 read err:", e)

print("[ptwalk] DONE-DIAG")
