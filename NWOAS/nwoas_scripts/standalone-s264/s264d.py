# SPDX-License-Identifier: MIT
"""NWOAS S264D host-side additions on top of s264c.py.

s264D = s264C + S264_STEP_AUDIO_B (sound-s256/audio_module.py stage B + stage S + final header in
C, src/nwoas_s264_audio.c) + NWOAS_WIFI=1 in C (S264_STEP_WIFI d = 1, src/nwoas_s264_wifi.c).  Config layout unchanged: the old reserved[1] is audio_prot
(NWOAS_AUDIO_PROT 0 sense / 1 moderate / 2 full; default 1 = what every launch wrapper sets).
"""
import struct

import s264_common as C
import s264c as X

C.STEP_NAMES.setdefault(18, "AUDIO_B")

CFG_F_AUDIO_B = 1 << 10
CFG_F_WIFI_ON = 1 << 11                                       # NWOAS_WIFI=1 (S268 launcher default)
CFG_F_DEFAULT_D = X.CFG_F_DEFAULT_C | CFG_F_AUDIO_B | CFG_F_WIFI_ON   # 0xeff = s264_default_config
AUDIO_PROT_SENSE, AUDIO_PROT_MODERATE, AUDIO_PROT_FULL = 0, 1, 2
AUDIO_PROT_DEFAULT = AUDIO_PROT_SENSE      # = audio_module.py with NWOAS_AUDIO_PROT unset
PROT_VALUES = {0: (0x00, 0xc9), 1: (0x14, 0x18), 2: (0x14, 0x00)}   # audio_module.py:39
STEP_AUDIO_A, STEP_AUDIO_B, STEP_WIFI = 10, 18, 11
_pack_config_c = X.pack_config_c          # kept: s264d_offline_test.py swaps X.pack_config_c


def pack_config_d(payload_ptr=0, payload_size=33669120, flags=CFG_F_DEFAULT_D,
                  nvme_qd=X.NVME_QD_DEFAULT, audio_prot=AUDIO_PROT_DEFAULT):
    b = bytearray(_pack_config_c(payload_ptr=payload_ptr, payload_size=payload_size, flags=flags,
                                 nvme_qd=nvme_qd))
    off = struct.calcsize(C.CFG_FMT) - 8 * 8 + 8      # second of the 8 trailing u64
    struct.pack_into("<Q", b, off, audio_prot)
    return bytes(b)


def steps_full(res):
    """[(id, a, b, c, d)] of the STEP ops in plan order."""
    return [(o["id"], o["a"], o["b"], o["c"], o["d"]) for o in res["ops"] if o["kind"] == 8]
