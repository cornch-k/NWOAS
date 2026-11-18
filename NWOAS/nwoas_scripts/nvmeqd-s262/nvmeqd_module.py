# NWOAS S262 launcher module (run_guest.py -m, LAST: after power-s260/power_module.py).
# Only for the g262R HV (port-j274/gpu/hv/out-hv2/m1n1-g262R-agx-hv2-nvmeqd.bin, interface version 3).
# g262P refused every configuration (lifecycle_epoch); g262Q found no ANS line in the 5-cell
# /arm-io/ans "interrupts" and ran the engine tick-polled, which halved rand4k IOPS (hardware
# 2026-11-19).  g262R takes the 5th cell (590) and keeps the engine OFF when no line is usable.
# HARDWARE-UNVERIFIED.  Not run by the agent that wrote it.
#
# Environment (unset = off = the g260O synchronous NVMe path, one ANS command at a time):
#   NWOAS_NVME_QD=<n>        1..61: up to n ANS commands in flight (ANS tags 2..n+1)
#   NWOAS_NVME_QD_IRQ=0|1    default 1: completions by the ANS interrupt (the single AIC line of
#                            /arm-io/ans, routed to the NVMe IRQ CPU; I/O CQ 1 is re-created with
#                            interrupts enabled before the guest starts).  0 = doorbell/tick polling only
#   NWOAS_NVME_QD_STRICT=0|1 default 0.  1 = FUA / write-through writes run alone (no other command
#                            in flight before or after), i.e. exactly the old ordering for those
#   NWOAS_NVME_QD_KICK=0|1   default 1: IPI the NVMe IRQ CPU when completions were posted elsewhere
#   NWOAS_NVME_QD_POLL=0|1   default 0.  g262R: without a usable ANS interrupt (or with QD_IRQ=0) the
#                            engine stays OFF = exactly the QD-off path.  1 = run it tick-polled anyway
#                            (experiment only; that mode halved rand4k IOPS on 2026-11-19)
# Each job output line "NWSTAT-MARK <label>" (seen by power_module.py) also appends one QD counter
# snapshot to <NWOAS_LINK_DIR>/nvmeqd.jsonl; nvmeqd-s262/nvmeqd_report.py makes the table.
import json, os, struct, time

QD_SIG = 0x53323632  # "S262"
QD_FIELDS = (
    'sig_version', 'depth', 'flags', 'irq',
    'submitted', 'completed', 'posted', 'posted_immediate', 'discarded',
    'reads', 'writes', 'flushes', 'chain_flushes', 'link_cmds', 'refused',
    'done_sqdb', 'done_cqdb', 'done_tick', 'done_irq', 'done_drain',
    'irqs', 'irq_empty', 'storm', 'kicks',
    'reap_calls', 'reap_batch_max',
    'inflight_max', 'inflight_sum', 'inflight_samples',
    'stall_tags', 'stall_cq', 'stall_barrier', 'stall_link',
    'drains', 'drain_ticks_max', 'drain_fail',
    'timeouts', 'faults', 'dev_errors',
    'dev_ticks_sum', 'dev_ticks_max', 'lat_ticks_sum', 'lat_ticks_max', 'lat_count',
    'eng_active', 'eng_depth', 'eng_irq', 'eng_faulted', 'eng_inflight', 'eng_alloc',
    'eng_bad_tag', 'eng_inval_fail', 'eng_stash_max', 'eng_sync_cmds', 'eng_sync_drain_waits',
    'busy_now', 'cntfrq',
)
assert len(QD_FIELDS) == 57
QD_SIZE = 8 * len(QD_FIELDS)
F_IRQ, F_STRICT, F_NOKICK, F_POLL = 1, 2, 4, 8

_qd_sig = p.nwoas_nvme_fastpath(26)
assert _qd_sig >> 32 == QD_SIG and (_qd_sig >> 24) & 0xff == 3, \
    'S262: this HV is not g262R (action 26 = %#x); use m1n1-g262R-agx-hv2-nvmeqd.bin' % _qd_sig
QD_REFUSE_SIG = 0x51444e4f
QD_OFF_SIG = 0x51444f46
QD_OFF = {1: 'NWOAS_NVME_QD_IRQ=0 without NWOAS_NVME_QD_POLL=1', 2: 'ADT has no /arm-io/ans',
          3: '/arm-io/ans has no interrupts property', 4: '/arm-io/ans interrupts cell count is not 1 or 5',
          5: '/arm-io/ans interrupt-parent is not the AIC', 6: 'ANS line out of the AIC range',
          7: '5th /arm-io/ans cell repeats a mailbox line'}
QD_REFUSE = {1: 'engine already active', 2: 'depth out of range', 3: 'unknown flag bits',
             4: 'guest I/O queue already armed', 5: 'fast path faulted', 6: 'NS2 host round trip busy',
             7: 'guest I/O already executed', 8: 'guest I/O SQ/CQ already registered',
             9: 'guest enabled the controller', 10: 'ANS engine setup failed'}
QD_SETUP = {1: 'ANS not initialized', 2: 'engine already active', 3: 'depth out of range',
            4: 'PRP memory allocation failed', 5: 'I/O queue re-create with IRQ enable failed'}


def _qd_env01(name, default):
    v = os.environ.get(name, '').strip()
    if not v:
        return default
    assert v in ('0', '1'), '%s must be 0 or 1, not %r' % (name, v)
    return v == '1'


_qd_depth = int(os.environ.get('NWOAS_NVME_QD', '0').strip() or '0', 0)
assert 0 <= _qd_depth <= 61, 'NWOAS_NVME_QD must be 0..61, not %d' % _qd_depth
_qd_flags = ((F_IRQ if _qd_env01('NWOAS_NVME_QD_IRQ', True) else 0) |
             (F_STRICT if _qd_env01('NWOAS_NVME_QD_STRICT', False) else 0) |
             (0 if _qd_env01('NWOAS_NVME_QD_KICK', True) else F_NOKICK) |
             (F_POLL if _qd_env01('NWOAS_NVME_QD_POLL', False) else 0))

_qd_engine = False
if _qd_depth:
    _qd_ret = p.nwoas_nvme_fastpath(24, _qd_depth, _qd_flags)
    if not _qd_ret >> 63 and _qd_ret >> 32 == QD_OFF_SIG:
        # g262R: nothing was set up in the HV; the I/O path is the QD-off path.
        _r, _cells = _qd_ret & 0xff, (_qd_ret >> 16) & 0xff
        hv.log('[S262] NVMe QD engine OFF (asked QD=%d flags=%#x): reason %d: %s (/arm-io/ans cells=%d) '
               '-> synchronous path, same cost as QD off' % (_qd_depth, _qd_flags, _r, QD_OFF.get(_r, '?'),
                                                               _cells))
    elif not _qd_ret >> 63:
        _r, _sub = _qd_ret & 0xff, (_qd_ret >> 8) & 0xff
        _why = (QD_REFUSE.get(_r, '?') + ('' if _r != 10 else ': ' + QD_SETUP.get(_sub, '?'))
                if _qd_ret >> 32 == QD_REFUSE_SIG else 'unknown answer')
        raise AssertionError('S262: HV refused NVMe QD=%d flags=%#x (ret=%#x reason=%d sub=%d: %s)' %
                             (_qd_depth, _qd_flags, _qd_ret, _r, _sub, _why))
    else:
        _qd_engine = True
        _qd_irq = (_qd_ret >> 32) & 0x7fffffff
        _qd_acc = _qd_ret & 0xff
        _qd_cells, _qd_cell = (_qd_ret >> 16) & 0xff, (_qd_ret >> 24) & 0x7f
        assert (_qd_ret >> 8) & 0xff == _qd_depth, _qd_ret
        if _qd_irq:
            hv.log('[S262] NVMe QD engine ON: QD=%d flags=%#x (asked %#x) ANS irq=%d '
                   '(ADT /arm-io/ans interrupts cell %d of %d)' % (_qd_depth, _qd_acc, _qd_flags, _qd_irq,
                                                                   _qd_cell, _qd_cells))
        else:
            hv.log('[S262] NVMe QD engine ON, POLLING ONLY (NWOAS_NVME_QD_POLL=1 experiment): QD=%d '
                   'flags=%#x (asked %#x)' % (_qd_depth, _qd_acc, _qd_flags))
else:
    hv.log('[S262] NVMe QD off (g260O synchronous path)')

_qd_buf = u.malloc(QD_SIZE + 64)
_qd_out = os.path.join(os.environ['NWOAS_LINK_DIR'], 'nvmeqd.jsonl')


def _qd_snapshot():
    n = p.nwoas_nvme_fastpath(25, _qd_buf, QD_SIZE)
    assert n == QD_SIZE, n
    return dict(zip(QD_FIELDS, struct.unpack('<%dQ' % len(QD_FIELDS), iface.readmem(_qd_buf, QD_SIZE))))


_qd_last = None


def _qd_mark(label, job):
    global _qd_last
    s = _qd_snapshot()
    rec = {'label': label, 'job': job, 'host_time': time.time(), 'snapshot': s}
    with open(_qd_out, 'a') as f:
        f.write(json.dumps(rec) + '\n')
    msg = '[S262] mark %s depth=%d busy=%d faults=%d timeouts=%d' % (
        label, s['depth'], s['busy_now'], s['faults'], s['timeouts'])
    if _qd_last is not None:
        a = _qd_last['snapshot']
        d = {k: s[k] - a[k] for k in QD_FIELDS}
        frq = s['cntfrq'] or 24000000
        msg += (' since %s: done=%d irq=%d(empty %d) kicks=%d inflight_mean=%.2f max=%d '
                'stall tag/cq/barrier/link=%d/%d/%d/%d chain=%d lat=%.0fus dev=%.0fus' % (
                    _qd_last['label'], d['completed'], d['irqs'], d['irq_empty'], d['kicks'],
                    d['inflight_sum'] / d['inflight_samples'] if d['inflight_samples'] else 0,
                    s['inflight_max'], d['stall_tags'], d['stall_cq'], d['stall_barrier'],
                    d['stall_link'], d['chain_flushes'],
                    d['lat_ticks_sum'] * 1e6 / frq / d['lat_count'] if d['lat_count'] else 0,
                    d['dev_ticks_sum'] * 1e6 / frq / d['completed'] if d['completed'] else 0))
    hv.log(msg)
    _qd_last = rec


_qd_mark('boot', 0)

# power_module.py's _pwr_receive looks _pwr_mark up in ITS OWN module globals at call time.
# HV.run_script() runs each -m script with runpy.run_path(init_globals=shell_locals), i.e. in a
# fresh dict, so rebinding _pwr_mark here (g262Q) never reached _pwr_receive: nvmeqd.jsonl had
# only the boot mark on 2026-11-19.  Replace it in the dict _pwr_receive really uses.
assert '_pwr_receive' in globals(), 'S262: load power-s260/power_module.py before nvmeqd_module.py'
_qd_pwr_ns = _pwr_receive.__globals__
_qd_pwr_mark = _qd_pwr_ns['_pwr_mark']


def _qd_pwr_and_qd_mark(label, job):
    _qd_pwr_mark(label, job)
    try:
        _qd_mark(label, job)
    except Exception as e:  # never break the transport because of measurement
        hv.log('[S262] mark handling failed: %r' % (e,))


_qd_pwr_ns['_pwr_mark'] = _qd_pwr_and_qd_mark


hv.log('[S262] nvmeqd module ready (engine %s): marks -> %s' % ('ON' if _qd_engine else 'OFF', _qd_out))
