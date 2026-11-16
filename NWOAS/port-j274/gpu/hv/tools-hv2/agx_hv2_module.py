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
                  pstate, 0, hv.entry, scanout, 0)
blob = hdr + snap
assert len(blob) == SIZE, len(blob)

buf = u.malloc(SIZE)
iface.writemem(buf, blob)
ret = p.request(P_HV_NWOAS_AGX_CONFIG, buf, SIZE)
hv.log(f"HVLOG: NWOAS-AGX config mode={mode} ret={ret} heap_limit={u.heap_base:#x} "
       f"ram={ram_base:#x}..{ram_end:#x} local={local_ipa:#x} pool={pool_ipa:#x} vintid={scanout}")
assert ret == 1, "HV refused the AGX config (see NWOAS-AGX: lines)"
