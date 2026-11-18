# NWOAS S262 launcher module (run_guest.py -m, LAST: after power-s260/power_module.py).
# Only for the g262Q HV (port-j274/gpu/hv/out-hv2/m1n1-g262Q-agx-hv2-nvmeqd.bin).  g262P refused every
# configuration (lifecycle_epoch is already 1 after S230 module.py's action 16), see README.
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
F_IRQ, F_STRICT, F_NOKICK = 1, 2, 4

_qd_sig = p.nwoas_nvme_fastpath(26)
assert _qd_sig >> 32 == QD_SIG and (_qd_sig >> 24) & 0xff == 2, \
    'S262: this HV has no usable concurrent NVMe path (action 26 = %#x); use the g262Q image' % _qd_sig
QD_REFUSE_SIG = 0x51444e4f
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
             (0 if _qd_env01('NWOAS_NVME_QD_KICK', True) else F_NOKICK))

if _qd_depth:
    _qd_ret = p.nwoas_nvme_fastpath(24, _qd_depth, _qd_flags)
    if not _qd_ret >> 63:
        _r, _sub = _qd_ret & 0xff, (_qd_ret >> 8) & 0xff
        _why = (QD_REFUSE.get(_r, '?') + ('' if _r != 10 else ': ' + QD_SETUP.get(_sub, '?'))
                if _qd_ret >> 32 == QD_REFUSE_SIG else 'unknown answer')
        raise AssertionError('S262: HV refused NVMe QD=%d flags=%#x (ret=%#x reason=%d sub=%d: %s)' %
                             (_qd_depth, _qd_flags, _qd_ret, _r, _sub, _why))
    _qd_irq = (_qd_ret >> 32) & 0x7fffffff
    _qd_acc = _qd_ret & 0xff
    assert (_qd_ret >> 8) & 0xff == _qd_depth, _qd_ret
    if (_qd_flags & F_IRQ) and not (_qd_acc & F_IRQ):
        hv.log('[S262] WARNING: no usable ANS interrupt in the ADT -> polling only')
    hv.log('[S262] NVMe QD=%d flags=%#x (asked %#x) ANS irq=%d' % (_qd_depth, _qd_acc, _qd_flags, _qd_irq))
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

# power_module.py's _pwr_receive looks _pwr_mark up in this shared namespace at call time.
assert '_pwr_mark' in globals(), 'S262: load power-s260/power_module.py before nvmeqd_module.py'
_qd_pwr_mark = _pwr_mark


def _pwr_mark(label, job):  # noqa: F811
    _qd_pwr_mark(label, job)
    try:
        _qd_mark(label, job)
    except Exception as e:  # never break the transport because of measurement
        hv.log('[S262] mark handling failed: %r' % (e,))


hv.log('[S262] nvmeqd module ready: marks -> %s' % _qd_out)
