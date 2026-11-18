#!/usr/bin/env python3
"""S260: tables from <LOG>.link/hvstats.jsonl (written by power_module.py at each NWSTAT-MARK).
usage: python3 -I hvstats_report.py <LOG.link | hvstats.jsonl> [--all]
One row per interval between consecutive marks (default: only intervals that start at a
'<name>-begin' mark and end at the matching '<name>-end')."""
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import nwoas_pwr_layout as PL  # noqa: E402


def load(path):
    p = Path(path)
    if p.is_dir():
        p = p / 'hvstats.jsonl'
    return [json.loads(line) for line in p.read_text().splitlines() if line.strip()]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recs = load(argv[1])
    show_all = '--all' in argv
    rows = []
    for a, b in zip(recs, recs[1:]):
        name = a['label'][:-6] if a['label'].endswith('-begin') else None
        if not show_all and not (name and b['label'] == name + '-end'):
            continue
        d = PL.delta(a['snapshot'], b['snapshot'])
        rows.append((name or '%s..%s' % (a['label'], b['label']), a, b, d))
    if not rows:
        print('no begin/end mark pairs (use --all)')
        return 1
    print('| phase | flags | s | WFI traps/s | sleep % CPU0..7 | FIQ/s | tick FIQ/s | IRQ/s | sync/s | MMIO/s | '
          'timer inj/s | timer gated/s | route-masked skips/s | IPI sent/s | lock wait % | host proxy % |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for name, a, b, d in rows:
        s = d['seconds'] or 1
        tot = lambda k: sum(c[k] for c in d['cpu'])
        print('| %s | %s | %.1f | %.0f | %s | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.2f | %.2f |' % (
            name, b['flags'], s, tot('wfi_traps') / s, ' '.join('%.0f' % c['sleep_pct'] for c in d['cpu']),
            tot('fiq') / s, tot('fiq_tick') / s, tot('irq') / s, tot('sync') / s, tot('sync_dabort') / s,
            tot('timer_inj') / s, tot('timer_gated') / s, tot('wfi_skip_route') / s, tot('ipi_sent') / s,
            sum(c['bhl_wait_pct'] for c in d['cpu']) / len(d['cpu']), d['proxy_pct']))
    print()
    print('| phase | wake/s by IPI tick vtimer irq other (all CPUs) | IPI received/s CPU0..7 | NVMe wake IPIs/s | '
          'no-sleep for NVMe/s |')
    print('|---|---|---|---|---|')
    for name, a, b, d in rows:
        s = d['seconds'] or 1
        tot = lambda k: sum(c.get(k, 0) for c in d['cpu'])
        print('| %s | %s | %s | %.0f | %.0f |' % (
            name, ' '.join('%.0f' % (tot(k) / s) for k in ('wake_ipi', 'wake_tick', 'wake_vtimer', 'wake_irq', 'wake_other')),
            ' '.join('%.0f' % (c.get('ipi_rx', 0) / s) for c in d['cpu']), tot('nvme_kicks') / s, tot('wfi_skip_nvme') / s))
    print()
    print('| phase | NS1 cmds | read MB/s | write MB/s | mean latency us | boot max latency us | mean device us | '
          'mean depth | doorbell/tick cmds | latency histogram %s |' % ' '.join(PL.HIST_EDGES_US))
    print('|---|---|---|---|---|---|---|---|---|---|')
    for name, a, b, d in rows:
        n = d['nvme']
        print('| %s | %d | %.1f | %.1f | %.1f | %.1f | %.1f | %.2f | %d/%d | %s |' % (
            name, n['ns1_cmds'], n['read_MBps'], n['write_MBps'], n['lat_mean_us'], n['lat_max_us'],
            n['exec_mean_us'], n['depth_mean'], n['doorbell_cmds'], n['tick_cmds'],
            ' '.join(str(n['lat_hist%d' % i]) for i in range(6))))
    if any(b.get('smc') for _, _, b, _ in rows):
        print()
        keys = [v['key'] for v in next(b['smc'] for _, _, b, _ in rows if b.get('smc'))['values']]
        print('| phase (value at end mark) | ' + ' | '.join('%s (%s)' % (k, PL.SMC_LABELS.get(k, 'raw')) for k in keys) + ' |')
        print('|---|' + '---|' * len(keys))
        for name, a, b, d in rows:
            vals = {v['key']: v for v in (b.get('smc') or {}).get('values', [])}
            print('| %s | ' % name + ' | '.join(str(vals.get(k, {}).get('float', 'ERR')) for k in keys) + ' |')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
