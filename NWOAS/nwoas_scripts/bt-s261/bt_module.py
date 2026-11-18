# SPDX-License-Identifier: MIT
# NWOAS S261: run_guest.py -m module, REPLACES wifi-s259/wifi_module.py (never load both).
# Runs after hv.init/load_raw, before hv.start (before UEFI), after usb-s174/guest_map.py (p.pcie_init()).
# Contract: research-20261118/bt/CONTRACT-BT.md (Bluetooth) + research-20261118/wifi/CONTRACT-WIN.md (Wi-Fi).
#   Same S259 Wi-Fi behaviour (checks, boot force-off, Wi-Fi control page HOOK, fn0 BAR BYPASS), plus:
#   - the BT control page HOOK at BT_CTRL_IPA (\_SB.NWC1.BTH0 reads it, so it is installed in every mode)
#   - BYPASS (EL2 nGnRE emulation) over the two fn1 BARs, like the fn0 BARs (Windows maps nGnRnE)
#   NWOAS_WIFI=0: no SMC access at all; both pages report DISABLED, both devices hidden (_STA 0).
#   NWOAS_BT=0:   Wi-Fi exactly as S259 (fn1 never touched, window stays 0xE0000000-0xE08FFFFF);
#                 the BT page reports DISABLED and BTH0 is hidden.
# Any failure in the checks below RAISES: run_guest --strict-init stops before the guest starts.
import hashlib as _s261_hashlib
import os as _s261_os
import sys as _s261_sys
from pathlib import Path as _S261Path

from m1n1.utils import irange as _s261_irange
from m1n1.hv.types import TraceMode as _S261TraceMode

_s261_here = _S261Path(__file__).resolve().parent
if str(_s261_here) not in _s261_sys.path:
    _s261_sys.path.insert(0, str(_s261_here))
import nwoas_bt_s261 as _s261  # noqa: E402
_s259 = _s261.s259

# C hypervisor guest-IPA windows and other module windows (same list as wifi-s259/wifi_module.py)
_S261_FIXED = [("AGX broker page", 0x300000000, 0x1000),
               ("S256 sound window", 0x340000000, 0x40000),
               ("vGIC dist", 0xF00000000, 0x10000), ("vGIC redist x8", 0xF10000000, 0x20000 * 8),
               ("vGIC ITS", 0xF20000000, 0x10000)]


def _s261_overlap(a, n, b, m):
    return a < b + m and b < a + n


def _s261_page_checks(name, ipa, size):
    bad = []
    if _s261_overlap(ipa, size, 0, 0x100000000):
        bad.append(f'{name}: low 4 GiB guest RAM window')
    ram_lo = u.ba.phys_base & ~0xffffffff
    ram_hi = ram_lo + u.ba.mem_size_actual
    if _s261_overlap(ipa, size, ram_lo, ram_hi - ram_lo):
        bad.append(f'{name}: DRAM {ram_lo:#x}..{ram_hi:#x}')
    for zone, maps in hv.mmio_maps.overlaps(_s261_irange(ipa, size)):
        if maps:
            bad.append(f'{name}: tracer {zone.start:#x}..{zone.stop:#x} {sorted(maps)}')
    for r in u.adt["/arm-io"].ranges:
        if _s261_overlap(ipa, size, r.parent_addr, r.size):
            bad.append(f'{name}: /arm-io range {r.parent_addr:#x}+{r.size:#x}')
    hooks = list(_S261_FIXED)
    for env, n in (("NWOAS_AGX_LOCAL_IPA", 0x40000000), ("NWOAS_AGX_POOL_IPA", 0x100000000)):
        v = int(_s261_os.environ.get(env, "0"), 0)
        if v:
            hooks.append((env, v, n))
    for hname, b, n in hooks:
        if _s261_overlap(ipa, size, b, n):
            bad.append(f'{name}: {hname} {b:#x}+{n:#x}')
    return bad


def _s261_checks():
    probe = _s261_here.parent / 'wifi-s240' / 'wifi_probe.py'
    h = _s261_hashlib.sha256(probe.read_bytes()).hexdigest()
    if h != _s259.wu.PROBE_SHA256:
        raise RuntimeError(f'[s261] STOP wifi_probe.py sha256 {h} != hardware-validated {_s259.wu.PROBE_SHA256}')
    facts = _s259.wp.adt_facts(u.adt)
    _s259.wu.validate_facts(facts)                      # raises wp.Refusal on any mismatch
    d0 = _s259.ws.dart_info(u.adt)
    if d0['base'] != _s259.DART0_BASE or d0.get('bridge0_dart_phandle') != d0.get('phandle'):
        raise RuntimeError(f'[s261] STOP dart-apcie0 {d0}')
    d1 = u.adt['/arm-io/dart-apcie1'].get_reg(0)[0]
    if d1 != _s259.DART1_BASE:
        raise RuntimeError(f'[s261] STOP dart-apcie1 base {d1:#x}')
    bad = _s261_page_checks('Wi-Fi page', _s259.CTRL_IPA, _s259.CTRL_SIZE)
    bad += _s261_page_checks('BT page', _s261.BT_CTRL_IPA, _s261.BT_CTRL_SIZE)
    if _s261_overlap(_s259.CTRL_IPA, _s259.CTRL_SIZE, _s261.BT_CTRL_IPA, _s261.BT_CTRL_SIZE):
        bad.append('Wi-Fi page and BT page overlap')
    # every BAR must sit inside the PCIe 32-bit window CPU range that /arm-io maps 1:1 for the guest
    for name, base, size in (('fn0 BARs', _s259.BAR2_BUS + _s259.CPU_OFFSET, 0x900000),
                             ('fn1 BAR0', *_s261.BT_BAR_EMUL[0]), ('fn1 BAR2', *_s261.BT_BAR_EMUL[1])):
        if not any(r.parent_addr <= base and base + size <= r.parent_addr + r.size for r in u.adt["/arm-io"].ranges):
            bad.append(f'{name} CPU range {base:#x}+{size:#x} not inside an /arm-io range (guest could not reach it)')
    lo, hi = _s261.WINDOW_S261
    if not (_s259.RESERVED_RAM[0] <= lo and hi < sum(_s259.RESERVED_RAM)):
        bad.append(f'S261 root window {lo:#x}-{hi:#x} outside the UEFI RAM reservation {_s259.RESERVED_RAM}')
    if bad:
        raise RuntimeError('[s261] STOP control page / BAR layout: ' + '; '.join(bad))
    return facts


_s261_facts = _s261_checks()
_s261_enabled = _s261_os.environ.get('NWOAS_WIFI', '1') != '0'
_s261_bt_enabled = _s261_os.environ.get('NWOAS_BT', '1') != '0'
_s261_run_dir = _s261.make_run_dir()
_s261_wlan = _s259.wu.wlan_info(u.adt)                 # board/antenna/MAC/calibration (MAC never logged)
if _s261_wlan['board'] != 'atlantisb' or _s261_wlan['antenna'] != 'X3' or _s261_wlan['mac'] is None \
        or _s261_wlan['cal_len'] == 0 or _s261_wlan['cal_len'] > _s259.CAL_MAX:
    raise RuntimeError(f"[s261] STOP ADT wlan: board={_s261_wlan['board']!r} antenna={_s261_wlan['antenna']!r} "
                       f"mac={'ok' if _s261_wlan['mac'] else None} cal_len={_s261_wlan['cal_len']}")
_s261_bt = _s261.bt_info(u.adt)                          # BD address never logged
if _s261_bt_enabled and (_s261_bt['bdaddr'] is None or not 0 < len(_s261_bt['calbf']) <= _s261.CAL_MAX
                         or not 0 < len(_s261_bt['cal']) <= _s261.CAL_MAX):
    raise RuntimeError(f"[s261] STOP ADT bluetooth: bdaddr={'ok' if _s261_bt['bdaddr'] else None} "
                       f"cal={len(_s261_bt['cal'])} calbf={len(_s261_bt['calbf'])} (set NWOAS_BT=0 to boot without it)")
_s261_obj = _s261.WifiBtS261(p, _s261_facts, (lambda base: _s259.wp.RealSmc(u, base)), enabled=_s261_enabled,
                             bt_enabled=_s261_bt_enabled, bt=_s261_bt,
                             log=lambda s: print(s, flush=True), run_dir=_s261_run_dir, wlan=_s261_wlan)
print(f"[s259] BOOT ADT wlan board={_s261_wlan['board']} antenna={_s261_wlan['antenna']} cal_len={_s261_wlan['cal_len']}",
      flush=True)
print(f"[s261] BOOT ADT bluetooth cal={len(_s261_bt['cal'])} calbf={len(_s261_bt['calbf'])} "
      f"bdaddr={'present' if _s261_bt['bdaddr'] else 'MISSING'} NWOAS_BT={int(_s261_bt_enabled)}", flush=True)
if _s261_enabled:
    _s261_obj.boot_force_off()
else:
    print('[s259] BOOT NWOAS_WIFI=0: no SMC access, Wi-Fi and Bluetooth disabled (control pages report DISABLED)',
          flush=True)
hv.add_tracer(_s261_irange(_s259.CTRL_IPA, _s259.CTRL_SIZE), 'wifi-s259-ctl', _S261TraceMode.HOOK,
              read=_s261_obj.hook_read, write=_s261_obj.hook_write)
hv.add_tracer(_s261_irange(_s261.BT_CTRL_IPA, _s261.BT_CTRL_SIZE), 'bt-s261-ctl', _S261TraceMode.HOOK,
              read=_s261_obj.bt_hook_read, write=_s261_obj.bt_hook_write)
# fn0 BAR2+BAR0 exactly as S259; fn1 BAR0 and BAR2 the same way (every guest access done at EL2, nGnRE)
_s261_emul = [(_s259.BAR_EMUL, 'wifi-s259-bar-emul')] + [(r, 'bt-s261-bar-emul') for r in _s261.BT_BAR_EMUL]
for (_s261_b, _s261_n), _s261_id in _s261_emul:
    if _s261_b % 0x4000 or _s261_n % 0x4000:
        raise RuntimeError(f'[s261] STOP BAR emulation range {_s261_b:#x}+{_s261_n:#x} not 16 KiB aligned')
    hv.add_tracer(_s261_irange(_s261_b, _s261_n), _s261_id, _S261TraceMode.BYPASS)
    print(f'[s261] BOOT BAR emulation (BYPASS, EL2 nGnRE) {_s261_id} {_s261_b:#x}+{_s261_n:#x}', flush=True)
# S261b: fn1 BAR fence, closed until the Windows driver arms it (nwoas_bt_s261.WifiBtS261._fence)
_s261_obj.fence_install(hv, fence_on_wifi_reset=_s261_os.environ.get('NWOAS_BT_FENCE_WIFI_RESET', '1') != '0')
print(f'[s261] BOOT fn1 BAR fence HOOK installed (closed, gen {_s261_obj.bt_gen}), '
      f'fence on Wi-Fi chip reset={int(_s261_obj.fence_on_wifi_reset)}', flush=True)
hv._nwoas_wifi_s259 = _s261_obj
hv._nwoas_bt_s261 = _s261_obj
print(f'[s261] BOOT control pages HOOK wifi {_s259.CTRL_IPA:#x}+{_s259.CTRL_SIZE:#x} '
      f'bt {_s261.BT_CTRL_IPA:#x}+{_s261.BT_CTRL_SIZE:#x} installed, run dir {_s261_run_dir}', flush=True)
