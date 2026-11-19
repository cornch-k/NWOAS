# SPDX-License-Identifier: MIT
"""NWOAS S264C host-side additions on top of s264_common.py (which stays the s264B file).

s264C = s264B + g262R (ANS 5-cell interrupt, port-j274/gpu/hv/hv2-g262Q-to-g262R.patch)
      + S264_STEP_NVME_QD (nvmeqd-s262/nvmeqd_module.py in C)
      + exception policy S264_POL_LOCAL / S264_POL_FATAL (proxyclient HV handlers in C).
Same config layout as s264B: the old reserved[0] is nvme_qd = depth | flags << 32.
"""
import struct

import s264_common as C

C.STEP_NAMES.setdefault(17, "NVME_QD")

CFG_F_EXC_LOCAL = 1 << 7
CFG_F_EXC_FATAL = 1 << 8
CFG_F_NVME_QD = 1 << 9
CFG_F_DEFAULT_C = C.CFG_F_DEFAULT | CFG_F_EXC_LOCAL | CFG_F_NVME_QD   # 0x2ff = s264_default_config
NVME_QD_DEFAULT = 32 | (1 << 32)          # S268: NWOAS_NVME_QD=32, NWOAS_NVME_QD_IRQ=1 -> flags 0x1
POL_LOCAL, POL_FATAL = 1, 2
STEP_NVME_QD, STEP_ARM_TRAPS, STEP_WIFI, STEP_TLBI = 17, 15, 11, 13


def pack_config_c(payload_ptr=0, payload_size=33669120, flags=CFG_F_DEFAULT_C, nvme_qd=NVME_QD_DEFAULT):
    """s264_default_config() of the s264C plan.  Same bytes as s264_common.pack_config except the
    flags word and the first reserved u64 (nvme_qd)."""
    b = bytearray(C.pack_config(payload_ptr=payload_ptr, payload_size=payload_size, flags=flags))
    off = struct.calcsize(C.CFG_FMT) - 8 * 8          # first of the 8 trailing u64
    struct.pack_into("<Q", b, off, nvme_qd)
    return bytes(b)


def steps_of(res):
    """[(id, a, b)] of the STEP ops in plan order."""
    return [(o["id"], o["a"], o["b"]) for o in res["ops"] if o["kind"] == 8]
