# SPDX-License-Identifier: MIT
# NWOAS S259: run_guest.py -m module (runs after hv.init/load_raw, before hv.start, i.e. before UEFI).
# Must come AFTER usb-s174/guest_map.py (p.pcie_init()).  Contract: research-20261118/wifi/CONTRACT-WIN.md.
#   NWOAS_WIFI=1 (default): force the BCM4378 off (PERST# low, SMC gP0d off, link-down check) so UEFI
#       meets port 0 unpowered exactly as on every earlier boot, then install the control page HOOK
#       that powers the chip when Windows ACPI (\_SB.NWC0.WLN0 _INI/_PS0) asks for it.
#   NWOAS_WIFI=0: no SMC access at all (S256 behaviour); the control page reports STATE_FAILED /
#       ERROR DISABLED, so WLN0._STA hides the device.  The page is still installed because the
#       S259 SSDT reads it (an unmapped IPA would fault the guest).
# Any failure in the checks below RAISES: run_guest --strict-init stops before the guest starts.
import hashlib as _s259_hashlib
import os as _s259_os
import sys as _s259_sys
from pathlib import Path as _S259Path

from m1n1.utils import irange as _s259_irange
from m1n1.hv.types import TraceMode as _S259TraceMode

_s259_here = _S259Path(__file__).resolve().parent
if str(_s259_here) not in _s259_sys.path:
    _s259_sys.path.insert(0, str(_s259_here))
import nwoas_wifi_s259 as _s259  # noqa: E402

# C hypervisor guest-IPA windows and other module windows (same list as sound-s256/audio_module.py)
_S259_FIXED = [("AGX broker page", 0x300000000, 0x1000),
               ("S256 sound window", 0x340000000, 0x40000),
               ("vGIC dist", 0xF00000000, 0x10000), ("vGIC redist x8", 0xF10000000, 0x20000 * 8),
               ("vGIC ITS", 0xF20000000, 0x10000)]


def _s259_overlap(a, n, b, m):
    return a < b + m and b < a + n


def _s259_checks():
    probe = _s259_here.parent / 'wifi-s240' / 'wifi_probe.py'
    h = _s259_hashlib.sha256(probe.read_bytes()).hexdigest()
    if h != _s259.wu.PROBE_SHA256:
        raise RuntimeError(f'[s259] STOP wifi_probe.py sha256 {h} != hardware-validated {_s259.wu.PROBE_SHA256}')
    facts = _s259.wp.adt_facts(u.adt)
    _s259.wu.validate_facts(facts)                      # raises wp.Refusal on any mismatch
    d0 = _s259.ws.dart_info(u.adt)
    if d0['base'] != _s259.DART0_BASE or d0.get('bridge0_dart_phandle') != d0.get('phandle'):
        raise RuntimeError(f'[s259] STOP dart-apcie0 {d0}')
    d1 = u.adt['/arm-io/dart-apcie1'].get_reg(0)[0]
    if d1 != _s259.DART1_BASE:
        raise RuntimeError(f'[s259] STOP dart-apcie1 base {d1:#x}')
    ipa, size = _s259.CTRL_IPA, _s259.CTRL_SIZE
    bad = []
    if _s259_overlap(ipa, size, 0, 0x100000000):
        bad.append('low 4 GiB guest RAM window')
    ram_lo = u.ba.phys_base & ~0xffffffff
    ram_hi = ram_lo + u.ba.mem_size_actual
    if _s259_overlap(ipa, size, ram_lo, ram_hi - ram_lo):
        bad.append(f'DRAM {ram_lo:#x}..{ram_hi:#x}')
    for zone, maps in hv.mmio_maps.overlaps(_s259_irange(ipa, size)):
        if maps:
            bad.append(f'tracer {zone.start:#x}..{zone.stop:#x} {sorted(maps)}')
    for r in u.adt["/arm-io"].ranges:
        if _s259_overlap(ipa, size, r.parent_addr, r.size):
            bad.append(f'/arm-io range {r.parent_addr:#x}+{r.size:#x}')
    hooks = list(_S259_FIXED)
    for env, n in (("NWOAS_AGX_LOCAL_IPA", 0x40000000), ("NWOAS_AGX_POOL_IPA", 0x100000000)):
        v = int(_s259_os.environ.get(env, "0"), 0)
        if v:
            hooks.append((env, v, n))
    for name, b, n in hooks:
        if _s259_overlap(ipa, size, b, n):
            bad.append(f'{name} {b:#x}+{n:#x}')
    # the BARs must sit inside the PCIe 32-bit window CPU range that /arm-io maps 1:1 for the guest
    bar_cpu = _s259.BAR2_BUS + _s259.CPU_OFFSET
    if not any(r.parent_addr <= bar_cpu and bar_cpu + 0x900000 <= r.parent_addr + r.size
               for r in u.adt["/arm-io"].ranges):
        bad.append(f'BAR CPU range {bar_cpu:#x}+0x900000 not inside an /arm-io range (guest could not reach it)')
    if bad:
        raise RuntimeError('[s259] STOP control page / BAR layout: ' + '; '.join(bad))
    return facts


_s259_facts = _s259_checks()
_s259_enabled = _s259_os.environ.get('NWOAS_WIFI', '1') != '0'
_s259_run_dir = _s259.make_run_dir()
_s259_wlan = _s259.wu.wlan_info(u.adt)                 # board/antenna/MAC/calibration (MAC never logged)
if _s259_wlan['board'] != 'atlantisb' or _s259_wlan['antenna'] != 'X3' or _s259_wlan['mac'] is None \
        or _s259_wlan['cal_len'] == 0 or _s259_wlan['cal_len'] > _s259.CAL_MAX:
    raise RuntimeError(f"[s259] STOP ADT wlan: board={_s259_wlan['board']!r} antenna={_s259_wlan['antenna']!r} "
                       f"mac={'ok' if _s259_wlan['mac'] else None} cal_len={_s259_wlan['cal_len']}")
_s259_obj = _s259.WifiS259(p, _s259_facts, (lambda base: _s259.wp.RealSmc(u, base)), enabled=_s259_enabled,
                           log=lambda s: print(s, flush=True), run_dir=_s259_run_dir, wlan=_s259_wlan)
print(f"[s259] BOOT ADT wlan board={_s259_wlan['board']} antenna={_s259_wlan['antenna']} cal_len={_s259_wlan['cal_len']}",
      flush=True)
if _s259_enabled:
    _s259_obj.boot_force_off()
else:
    print('[s259] BOOT NWOAS_WIFI=0: no SMC access, Wi-Fi disabled (control page reports DISABLED)', flush=True)
hv.add_tracer(_s259_irange(_s259.CTRL_IPA, _s259.CTRL_SIZE), 'wifi-s259-ctl', _S259TraceMode.HOOK,
              read=_s259_obj.hook_read, write=_s259_obj.hook_write)
# BAR2 (TCM) + BAR0 (registers): hypervisor-emulated so every guest access is done as Device-nGnRE
# at EL2 (see nwoas_wifi_s259.BAR_EMUL).  Installed in every mode; overrides the /arm-io 'HW' map.
_s259_bar_base, _s259_bar_size = _s259.BAR_EMUL
if _s259_bar_base % 0x4000 or _s259_bar_size % 0x4000:
    raise RuntimeError('[s259] STOP BAR emulation range not 16 KiB aligned')
hv.add_tracer(_s259_irange(_s259_bar_base, _s259_bar_size), 'wifi-s259-bar-emul', _S259TraceMode.BYPASS)
print(f'[s259] BOOT BAR emulation (BYPASS, EL2 nGnRE) {_s259_bar_base:#x}+{_s259_bar_size:#x}', flush=True)
hv._nwoas_wifi_s259 = _s259_obj
print(f'[s259] BOOT control page HOOK {_s259.CTRL_IPA:#x}+{_s259.CTRL_SIZE:#x} installed, run dir {_s259_run_dir}',
      flush=True)
