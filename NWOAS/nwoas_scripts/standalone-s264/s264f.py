# SPDX-License-Identifier: MIT
"""NWOAS S264F host-side additions on top of s264e.py.

s264F = s264E + power-s260/power_module.py in C (S264_STEP_PWR = 19, cfg flag 1 << 15, default on as in the
S268 launcher with the S262 default; every NWOAS_PWR_* switch default off as in Python).
Config: the old reserved[2] is pwr = flags 7:0 | sec_tick_hz 23:8 | timer_gap_us 43:24 | print_s 55:44 | SLC 56.
"""
import struct

import s264_common as C
import s264e as E

C.STEP_NAMES.setdefault(19, "PWR")

CFG_F_PWR = 1 << 15
CFG_F_DEFAULT_F = E.CFG_F_DEFAULT_E | CFG_F_PWR          # 0xbeff = s264_default_config
STEP_PWR = 19
_pack_config_e = E.pack_config_e


def pwr_field(flags=0, sec_hz=0, gap_us=0, print_s=0, slc=False):
    return flags | sec_hz << 8 | gap_us << 24 | print_s << 44 | (1 << 56 if slc else 0)


def pack_config_f(payload_ptr=0, payload_size=33669120, flags=CFG_F_DEFAULT_F, nvme_qd=E.D.X.NVME_QD_DEFAULT,
                  audio_prot=E.AUDIO_PROT_DEFAULT, pwr=0):
    b = bytearray(_pack_config_e(payload_ptr=payload_ptr, payload_size=payload_size, flags=flags,
                                 nvme_qd=nvme_qd, audio_prot=audio_prot))
    off = struct.calcsize(C.CFG_FMT) - 8 * 8 + 16      # third of the 8 trailing u64
    struct.pack_into("<Q", b, off, pwr)
    return bytes(b)
