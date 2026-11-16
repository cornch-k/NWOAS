# NWOAS gpu-hv2 launcher module (DRAFT, NOT RUN ON HARDWARE).
# run_guest.py -m module; must come AFTER layout-s231/guest_module.py (which shrinks the
# guest mem_size for the low-window backing) and only with an m1n1-gpu-hv2 NWOAS_AGX=1 HV.
# Sends struct nwoas_agx_config (src/nwoas_agx.h, 512 bytes) with P_HV_NWOAS_AGX_CONFIG.
# The HV installs it in hv_start(); every value is re-validated there (fail closed).
#
# Environment:
#   NWOAS_AGX_MODE=log|mediate      (default log)
#   NWOAS_AGX_LOCAL_IPA=0x...       384 MiB local reserve, identity guest RAM (mediate only;
#                                   must match the Mu test payload's reservation)
#   NWOAS_AGX_POOL_IPA=0x...        4 GiB pool window IPA (default 0 = off; upstream J313 0xd00000000)
#   NWOAS_AGX_SCANOUT_INTID=889     synthetic vINTID of the ACPI AGX0 device
#   NWOAS_AGX_BASE_PSTATE=0         override only if the ADT lacks gpu-perf-base-pstate
#   NWOAS_AGX_DCP_SERVICE=1         S251 (mediate + m1n1-g251F or later only): the HV answers the
#                                   scanout window itself (DCP iBoot async swap, ABI v2, time-based
#                                   LATCHED, display power). Unset/0: config bytes identical to S249.
#   NWOAS_AGX_DCP_LATCH_US=17000    S251: LATCHED = APPLIED + this (0 = HV default 17000 us)
#   NWOAS_AGX_DCP_NO_CLEAN=1        S251: skip the HV's cache clean over each presented surface
# README: port-j274/gpu/hv/README-dcp-s251.md
import os, struct

P_HV_NWOAS_AGX_CONFIG = 0xc38
MAGIC, VERSION, SIZE = 0x4741574e, 1, 512
REGION = {"GUEST_RAM": 0, "HEAP": 1, "FIRMWARE": 2, "ADT": 3, "BOOT_ARGS": 4,
          "FRAMEBUFFER": 5, "LOW_MEMORY": 6, "DART_TABLES": 7, "RAMDISK": 8}

mode = {"log": 1, "mediate": 2}[os.environ.get("NWOAS_AGX_MODE", "log")]
local_ipa = int(os.environ.get("NWOAS_AGX_LOCAL_IPA", "0"), 0)
pool_ipa = int(os.environ.get("NWOAS_AGX_POOL_IPA", "0"), 0)
scanout = int(os.environ.get("NWOAS_AGX_SCANOUT_INTID", "889"), 0)
pstate = int(os.environ.get("NWOAS_AGX_BASE_PSTATE", "0"), 0)
assert mode == 1 or local_ipa, "mediate needs NWOAS_AGX_LOCAL_IPA"
# S251 opt-in (src/nwoas_agx.h NWOAS_AGX_CFG_FLAG_DCP_SERVICE / _DCP_NO_CLEAN, field dcp_latch_us)
dcp_service = os.environ.get("NWOAS_AGX_DCP_SERVICE", "0") == "1"
dcp_latch_us = int(os.environ.get("NWOAS_AGX_DCP_LATCH_US", "0"), 0)
dcp_no_clean = os.environ.get("NWOAS_AGX_DCP_NO_CLEAN", "0") == "1"
assert dcp_service or not (dcp_latch_us or dcp_no_clean), "DCP options need NWOAS_AGX_DCP_SERVICE=1"
assert not dcp_service or (mode == 2 and scanout), "DCP service needs mediate and a scanout vINTID"
assert 0 <= dcp_latch_us <= 100000, dcp_latch_us
cfg_flags = (0x2 if dcp_service else 0) | (0x4 if dcp_no_clean else 0)

ram_base = hv.phys_base
ram_end = u.ba.phys_base + u.ba.mem_size          # host RAM end; includes the low-window backing
low_backing = ram_end - 0x100000000 - 0x34000      # layout-s231/guest_module.py LOW_BACKING
regions = [("GUEST_RAM", ram_base, ram_end - ram_base),
           ("LOW_MEMORY", low_backing, ram_end - low_backing),
           ("ADT", hv.adt_base, 0x100000),          # HYPOTHESIS: upper bound of the ADT copy
           ("BOOT_ARGS", hv.guest_base + hv.bootargs_off, 0x4000)]
vid = u.ba.video
if vid.base and vid.stride and vid.height:
    regions.append(("FRAMEBUFFER", vid.base, (vid.stride * vid.height + 0x3fff) & ~0x3fff))
assert len(regions) <= 16

snap = struct.pack("<QQQ4Q", ram_base, ram_end - ram_base, hv.entry, 0, 0, 0, 0)
snap += struct.pack("<II", len(regions), 0)
for kind, base, size in regions:
    snap += struct.pack("<IIQQ", REGION[kind], 0, base, size)
snap += b"\0" * (24 * (16 - len(regions)))
hdr = struct.pack("<IIIIQQQIIQII", MAGIC, VERSION, SIZE, mode, u.heap_base, local_ipa, pool_ipa,
                  pstate, cfg_flags, hv.entry, scanout, dcp_latch_us)
blob = hdr + snap
assert len(blob) == SIZE, len(blob)

buf = u.malloc(SIZE)
iface.writemem(buf, blob)
ret = p.request(P_HV_NWOAS_AGX_CONFIG, buf, SIZE)
hv.log(f"HVLOG: NWOAS-AGX config mode={mode} ret={ret} heap_limit={u.heap_base:#x} "
       f"ram={ram_base:#x}..{ram_end:#x} local={local_ipa:#x} pool={pool_ipa:#x} vintid={scanout}")
assert ret == 1, "HV refused the AGX config (see NWOAS-AGX: lines)"
if dcp_service:
    hv.log(f"HVLOG: NWOAS-AGX S251 DCP scanout service requested flags={cfg_flags:#x} "
           f"latch_us={dcp_latch_us or 17000} (HV takes DCP over on the first REGISTER_POOL)")
