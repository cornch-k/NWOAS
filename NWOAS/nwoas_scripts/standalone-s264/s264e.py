# SPDX-License-Identifier: MIT
"""NWOAS S264E host-side additions on top of s264d.py.

s264E = s264D + the S261 module set in C (bt-s261/bt_module.py instead of wifi-s259/wifi_module.py:
BT control page 0x348004000, fn1 BAR emulation + fence, WifiBtS261 in src/nwoas_s264_wifi.c),
the S261 Bluetooth payload, bounded RTKit/SMC/AFK waits, and the sound protection default moderate.
Config layout unchanged (flags + audio_prot).
"""
import s264_common as C
import s264d as D

C.HOOK_NAMES.setdefault(7, "BT_CTL")
C.HOOK_NAMES.setdefault(8, "BT_FENCE")

CFG_F_S261 = 1 << 12                     # bt_module.py set (S268 daily launcher)
CFG_F_BT_ON = 1 << 13                    # NWOAS_BT=1
CFG_F_BT_NOFENCE_WR = 1 << 14            # NWOAS_BT_FENCE_WIFI_RESET=0
CFG_F_DEFAULT_E = D.CFG_F_DEFAULT_D | CFG_F_S261 | CFG_F_BT_ON          # 0x3eff = s264_default_config
AUDIO_PROT_DEFAULT = D.AUDIO_PROT_MODERATE                              # user decision 2026-11-20
PAYLOAD = "nwoas_scripts/fw-bt-s261/out/s261/m1n1-payload-s261-bt.bin"
PAYLOAD_SHA = "26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92"
_pack_config_d = D.pack_config_d


def pack_config_e(payload_ptr=0, payload_size=33669120, flags=CFG_F_DEFAULT_E,
                  nvme_qd=D.X.NVME_QD_DEFAULT, audio_prot=AUDIO_PROT_DEFAULT):
    return _pack_config_d(payload_ptr=payload_ptr, payload_size=payload_size, flags=flags, nvme_qd=nvme_qd,
                          audio_prot=audio_prot)


def wifi_d(flags):
    """S264_STEP_WIFI d for a flags word (bit0 WIFI_ON, bit1 S261, bit2 BT_ON, bit3 no fence on reset)."""
    d = 1 if flags & D.CFG_F_WIFI_ON else 0
    if flags & CFG_F_S261:
        d |= 2 | (4 if flags & CFG_F_BT_ON else 0) | (8 if flags & CFG_F_BT_NOFENCE_WR else 0)
    return d

