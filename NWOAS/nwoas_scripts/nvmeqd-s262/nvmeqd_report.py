#!/usr/bin/env python3
"""S262: table of the concurrent-NVMe counters between NWSTAT-MARK lines.

    python3 -I nvmeqd_report.py <NWOAS_LINK_DIR>/nvmeqd.jsonl

One row per interval (mark A -> mark B).  The last block lists the absolute safety
counters, which must all be 0 for a pass (faults, timeouts, dev_errors, drain_fail,
eng_bad_tag, storm)."""
import json
import sys

SAFETY = ('faults', 'timeouts', 'dev_errors', 'drain_fail', 'eng_bad_tag', 'eng_inval_fail', 'storm',
          'eng_faulted')


def main(path):
    recs = [json.loads(l) for l in open(path) if l.strip()]
    if not recs:
        print('no records')
        return 1
    print('| interval | s | completed | per s | inflight mean | irqs | irq empty | kicks | '
          'stall tag/cq/barrier/link | flush | FUA flush | lat mean (us) | dev mean (us) |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for a, b in zip(recs, recs[1:]):
        x, y = a['snapshot'], b['snapshot']
        d = {k: y[k] - x[k] for k in y}
        frq = y['cntfrq'] or 24000000
        sec = b['host_time'] - a['host_time']
        print('| %s -> %s | %.1f | %d | %.0f | %.2f | %d | %d | %d | %d/%d/%d/%d | %d | %d | %.0f | %.0f |' % (
            a['label'], b['label'], sec, d['completed'], d['completed'] / sec if sec > 0 else 0,
            d['inflight_sum'] / d['inflight_samples'] if d['inflight_samples'] else 0,
            d['irqs'], d['irq_empty'], d['kicks'], d['stall_tags'], d['stall_cq'], d['stall_barrier'],
            d['stall_link'], d['flushes'], d['chain_flushes'],
            d['lat_ticks_sum'] * 1e6 / frq / d['lat_count'] if d['lat_count'] else 0,
            d['dev_ticks_sum'] * 1e6 / frq / d['completed'] if d['completed'] else 0))
    last = recs[-1]['snapshot']
    print()
    print('depth=%d flags=%#x irq=%d inflight_max=%d busy_now=%d' % (
        last['depth'], last['flags'], last['irq'], last['inflight_max'], last['busy_now']))
    bad = {k: last[k] for k in SAFETY if last[k]}
    print('safety counters: ' + ('all 0' if not bad else ', '.join('%s=%d' % kv for kv in bad.items())))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
