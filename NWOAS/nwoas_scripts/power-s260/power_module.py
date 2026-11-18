# NWOAS S260 launcher module (run_guest.py -m, LAST module; needs memory-s180/link_module.py first).
# Only for an m1n1-gpu-hv2 g260M (or later) HV.  HARDWARE-UNVERIFIED.  Not run by the agent that wrote it.
#
# Environment (all default off = g254L behaviour except the HV's always-on counters):
#   NWOAS_PWR_WFI=1           C-1  idle guest WFI really sleeps in EL2
#   NWOAS_PWR_SECTICK=<hz>    C-2  secondary HV tick <hz> (10..5000; 100 = reference) while both guest
#                                  timer FIQ routes are open, 5 kHz otherwise.  0/unset = 5 kHz always
#   NWOAS_PWR_TGAP_US=<us>    C-4  Windows-kernel timer PPI minimum gap (0..62500; unset = 1 ms default)
#   NWOAS_PWR_NVME_SHUT=1     S-1  ANS flush + clean shutdown before PSCI reset/off
#   NWOAS_PWR_OFF=1                PSCI SYSTEM_OFF -> SMC MBSE offw/off1 (reboot fallback after 2 s)
#   NWOAS_PWR_SMC=1           ph0  read-only SMC sensor reads at boot and at every NWSTAT-MARK
#   NWOAS_PWR_SMC_KEYS=a,b    ph0  up to 8 keys (default PSTR,PDTR,PMVR,TH0x,TW0P,F0Ac)
#   NWOAS_PWR_SLC=1           ③    mcc_enable_cache() before the guest starts (status logged either way)
#   NWOAS_PWR_PRINT=<s>       ph0  one NWOAS-PWR summary line in the .vuart log every <s> seconds
#   NWOAS_PWR_LIVE=1               allow job output lines "NWSTAT-SET wfi=0|1 sectick=0|1 tgap=0|1"
#                                  to toggle C-1/C-2/C-4 at run time (same boot A/B)
# Job output lines "NWSTAT-MARK <label>" take an HV counter snapshot (+ SMC read) into
# <NWOAS_LINK_DIR>/hvstats.jsonl and print one summary line; power-s260/hvstats_report.py makes tables.
import json, os, sys, time
sys.path.insert(0, '/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts/power-s260')
import nwoas_pwr_layout as _PL  # noqa: E402

def _pwr_env_int(name, default=0):
    v = os.environ.get(name, '')
    return int(v, 0) if v.strip() else default

_pwr_flags = 0
if os.environ.get('NWOAS_PWR_WFI', '0') == '1': _pwr_flags |= _PL.F_WFI_SLEEP
_pwr_sec_hz = _pwr_env_int('NWOAS_PWR_SECTICK')
if _pwr_sec_hz: _pwr_flags |= _PL.F_SEC_TICK
_pwr_gap = os.environ.get('NWOAS_PWR_TGAP_US', '').strip()
_pwr_gap_us = int(_pwr_gap, 0) if _pwr_gap else 0
if _pwr_gap: _pwr_flags |= _PL.F_TIMER_GAP
if os.environ.get('NWOAS_PWR_NVME_SHUT', '0') == '1': _pwr_flags |= _PL.F_NVME_SHUT
if os.environ.get('NWOAS_PWR_OFF', '0') == '1': _pwr_flags |= _PL.F_SMC_OFF
if os.environ.get('NWOAS_PWR_SMC', '0') == '1': _pwr_flags |= _PL.F_SMC_SENSE
_pwr_print = _pwr_env_int('NWOAS_PWR_PRINT')
if _pwr_print: _pwr_flags |= _PL.F_PRINT
_pwr_slc = os.environ.get('NWOAS_PWR_SLC', '0') == '1'
_pwr_live = os.environ.get('NWOAS_PWR_LIVE', '0') == '1'
_pwr_keys = [k for k in os.environ.get('NWOAS_PWR_SMC_KEYS', ','.join(_PL.DEFAULT_SMC_KEYS)).split(',') if k]
assert 1 <= len(_pwr_keys) <= 8 and all(len(k) == 4 for k in _pwr_keys), _pwr_keys
assert 0 <= _pwr_sec_hz <= 5000 and (_pwr_sec_hz == 0 or _pwr_sec_hz >= 10), _pwr_sec_hz
assert 0 <= _pwr_gap_us <= 62500 and 0 <= _pwr_print <= 3600

_pwr_dir = os.environ['NWOAS_LINK_DIR']
_pwr_out = os.path.join(_pwr_dir, 'hvstats.jsonl')
_pwr_buf = u.malloc(_PL.SIZE + 256)

_pwr_q = p.request(_PL.P_HV_NWOAS_PWR, _PL.OP_QUERY, 0, 0, 0, 0)
assert _pwr_q == (_PL.MAGIC << 32) | _PL.VERSION, 'HV has no S260 power interface: %#x' % _pwr_q


def _pwr_config(flags):
    r = p.request(_PL.P_HV_NWOAS_PWR, _PL.OP_CONFIG, flags, _pwr_sec_hz, _pwr_gap_us, _pwr_print)
    assert r == (1 << 32) | flags, 'HV refused S260 config flags=%#x ret=%#x' % (flags, r)
    return r


def _pwr_snapshot():
    n = p.request(_PL.P_HV_NWOAS_PWR, _PL.OP_SNAPSHOT, _pwr_buf, _PL.SIZE, 0, 0)
    assert n == _PL.SIZE, n
    return _PL.parse(iface.readmem(_pwr_buf, _PL.SIZE))


def _pwr_smc():
    if not _pwr_flags & _PL.F_SMC_SENSE:
        return None
    keys = b''.join(_PL.smc_key(k).to_bytes(4, 'little') for k in _pwr_keys)
    iface.writemem(_pwr_buf, keys)
    out = _pwr_buf + 64
    t0 = time.monotonic()
    ok = p.request(_PL.P_HV_NWOAS_PWR, _PL.OP_SMC_READ, _pwr_buf, len(_pwr_keys), out, 0)
    raw = iface.readmem(out, 8 * len(_pwr_keys))
    vals = [_PL.decode_smc(k, int.from_bytes(raw[8 * i:8 * i + 8], 'little')) for i, k in enumerate(_pwr_keys)]
    return {'ok': ok, 'ms': round((time.monotonic() - t0) * 1000, 1), 'values': vals}


def _pwr_mcc():
    n = p.request(_PL.P_HV_NWOAS_PWR, _PL.OP_MCC_STAT, _pwr_buf, 16, 0, 0)
    if n >= 1 << 63 or n == 0:
        return None
    raw = iface.readmem(_pwr_buf, 4 * n)
    w = [int.from_bytes(raw[4 * i:4 * i + 4], 'little') for i in range(n)]
    return [{'enable': '%#x' % w[i], 'status': '%#x' % w[i + 1]} for i in range(0, n, 2)]


_pwr_last = None


def _pwr_mark(label, job):
    global _pwr_last
    snap = _pwr_snapshot()
    smc = _pwr_smc()
    rec = {'label': label, 'job': job, 'host_time': time.time(), 'flags': '%#x' % _pwr_cur_flags,
           'snapshot': snap, 'smc': smc}
    with open(_pwr_out, 'a') as f:
        f.write(json.dumps(rec) + '\n')
    msg = 'NWOAS-PWR mark %s flags=%#x' % (label, _pwr_cur_flags)
    if _pwr_last is not None:
        d = _PL.delta(_pwr_last['snapshot'], snap)
        sl = ','.join('%.0f' % c['sleep_pct'] for c in d['cpu'])
        wfi = sum(c['wfi_traps'] for c in d['cpu'])
        fiq = sum(c['fiq'] for c in d['cpu'])
        n = d['nvme']
        msg += (' since %s: %.1fs wfi=%d sleep%%=[%s] fiq=%d nvme=%d lat=%.0fus depth=%.2f' %
                (_pwr_last['label'], d['seconds'], wfi, sl, fiq, n['ns1_cmds'], n['lat_mean_us'], n['depth_mean']))
    if smc:
        msg += ' smc=' + ','.join('%s:%s' % (v['key'], v.get('float', 'ERR')) for v in smc['values'])
    hv.log(msg)
    _pwr_last = rec


_pwr_cur_flags = _pwr_flags
_pwr_config(_pwr_flags)
_pwr_mcc_before = _pwr_mcc()
if _pwr_slc:
    _pwr_rc = p.request(_PL.P_HV_NWOAS_PWR, _PL.OP_SLC, 1, 0, 0, 0)
    hv.log('NWOAS-PWR SLC enable rc=%d before=%s after=%s' % (_pwr_rc - (1 << 64) if _pwr_rc >> 63 else _pwr_rc,
                                                              _pwr_mcc_before, _pwr_mcc()))
else:
    hv.log('NWOAS-PWR SLC (not enabled by S260) status=%s' % _pwr_mcc_before)
hv.log('NWOAS-PWR S260 config flags=%#x sectick_hz=%d tgap_us=%s print_s=%d smc_keys=%s live=%d' %
       (_pwr_flags, _pwr_sec_hz, _pwr_gap or 'default', _pwr_print, ','.join(_pwr_keys), _pwr_live))
_pwr_mark('boot', 0)

# file-delivery-s243 may have replaced hv._nwoas_link by its ArtifactLink wrapper (attribute 'base');
# LinkNamespace.io() calls self.receive() on the real object, so hook that one.
_pwr_link = hv._nwoas_link
while 'receive' not in type(_pwr_link).__dict__ and 'base' in vars(_pwr_link):
    _pwr_link = vars(_pwr_link)['base']
assert 'receive' in type(_pwr_link).__dict__, 'S260: no LinkNamespace found under hv._nwoas_link'
_pwr_orig_receive = _pwr_link.receive
_pwr_lines = {}


def _pwr_set(spec):
    global _pwr_cur_flags
    bits = {'wfi': _PL.F_WFI_SLEEP, 'sectick': _PL.F_SEC_TICK, 'tgap': _PL.F_TIMER_GAP}
    new = _pwr_cur_flags
    for item in spec.split():
        k, _, v = item.partition('=')
        if k not in bits or v not in ('0', '1'):
            hv.log('NWOAS-PWR NWSTAT-SET ignored %r' % item)
            return
        if bits[k] == _PL.F_SEC_TICK and not _pwr_sec_hz:
            hv.log('NWOAS-PWR NWSTAT-SET sectick needs NWOAS_PWR_SECTICK at launch')
            return
        new = (new | bits[k]) if v == '1' else (new & ~bits[k])
    assert not (new ^ _pwr_cur_flags) & ~_PL.LIVE_MASK
    _pwr_config(new)
    _pwr_cur_flags = new
    hv.log('NWOAS-PWR NWSTAT-SET flags=%#x' % new)


def _pwr_receive(data):
    ack = _pwr_link.ack
    ok = _pwr_orig_receive(data)
    if not ok or _pwr_link.ack == ack or len(data) < 64:
        return ok
    jid, kind, n = int.from_bytes(data[32:36], 'little'), int.from_bytes(data[36:40], 'little'), \
        int.from_bytes(data[40:44], 'little')
    if kind != 2:
        _pwr_lines.pop(jid, None)
        return ok
    try:
        text = _pwr_lines.get(jid, '') + bytes(data[64:64 + n]).decode('ascii', 'replace')
        *full, rest = text.replace('\r', '').split('\n')
        _pwr_lines[jid] = rest[-512:]
        for line in full:
            line = line.strip()
            if line.startswith('NWSTAT-MARK '):
                _pwr_mark(line[12:].strip()[:64], jid)
            elif line.startswith('NWSTAT-SET ') and _pwr_live:
                _pwr_set(line[11:].strip()[:128])
    except Exception as e:  # never break the transport because of measurement
        hv.log('NWOAS-PWR mark handling failed: %r' % (e,))
    return ok


_pwr_link.receive = _pwr_receive
hv.log('NWOAS-PWR S260 module ready: marks -> %s' % _pwr_out)
