"""s274 FwSnap: read the raw NwoaSnap<k>* dumps of one run and print what the firmware RAM compare shows.

    python3 -I analyze-s274.py <runs/s274-YYYYMMDD-HHMMSS>      (prefix of the -<knob>-snap<k>.txt files)

Per scan (driver side, see fw.cpp "s274 FwSnap"):
  NwoaSnap<k>      summary text
  NwoaSnap<k>A     u32 triples {offset from rambase, word before the escan, word at the verdict}
                   have3 (stall + re-create): only words equal before the escan and after the re-create
                   have2 (no stall / no re-create): every word that differs
  NwoaSnap<k>H<i>  u32 offset + 3 x 1024 bytes (before / verdict / after) around ring base hit i
  NwoaSnap<k>C<t>  "counters" iovar at moment t
Output: no network names or addresses (the driver masks them before the compare).
"""
import glob
import re
import struct
import sys
from collections import Counter

WIN = 1024
WW = 768


def load(path):
    vals = {}
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'\s*(NwoaSnap\d\w*)\s+REG_(SZ|BINARY)\s*(.*)$', line.rstrip('\r\n'))
        if m:
            vals[m.group(1)] = m.group(3).strip() if m.group(2) == 'SZ' else bytes.fromhex(m.group(3).strip())
    return vals


def main(prefix):
    scans = []                      # (knob, k, vals)
    for p in sorted(glob.glob(prefix + '-*-snap[0-9].txt')):
        m = re.search(r'-(\w+)-snap(\d)\.txt$', p)
        v = load(p)
        if v:
            scans.append((m.group(1), int(m.group(2)), v))
    if not scans:
        print('S274 ANALYZE no dumps for', prefix)
        return
    stuckA, aliveA = [], []
    for knob, k, v in scans:
        s = v.get('NwoaSnap%d' % k, '')
        a = v.get('NwoaSnap%dA' % k, b'')
        recs = [struct.unpack_from('<3I', a, i) for i in range(0, len(a) - 11, 12)]
        have3 = 'have3' in s
        print('S274 SCAN %s #%d: %s | A records read %d' % (knob, k, s, len(recs)))
        (stuckA if have3 else aliveA).append({o: (x, y) for o, x, y in recs})
        for h in range(6):
            w = v.get('NwoaSnap%dH%d' % (k, h))
            if not w or len(w) < 4 + 3 * WIN:
                continue
            st = struct.unpack_from('<I', w)[0]
            t = [w[4 + i * WIN:4 + (i + 1) * WIN] for i in range(3)]
            n = 0
            for o in range(0, WIN, 4):
                x, y, z = (struct.unpack_from('<I', b, o)[0] for b in t)
                if x != y or y != z:
                    if n < 40:
                        print('S274 HIT  %s #%d h%d +%05x: %08x -> %08x -> %08x%s' %
                              (knob, k, h, st + o, x, y, z, '  (back)' if x == z and x != y else ''))
                    n += 1
            print('S274 HIT  %s #%d h%d window +%05x..+%05x: %d words differ' % (knob, k, h, st, st + WIN, n))
        for j in range(14):                       # s274: windows around single state words
            w = v.get('NwoaSnap%dW%d' % (k, j))
            if not w or len(w) < 4 + 3 * WW:
                continue
            st = struct.unpack_from('<I', w)[0]
            t = [w[4 + i * WW:4 + (i + 1) * WW] for i in range(3)]
            print('S274 WIN  %s #%d w%d +%05x..+%05x (snapshot 1 = stalled; lines marked * differ between snapshots)' % (knob, k, j, st, st + WW))
            for o in range(0, WW, 16):
                row = [b[o:o + 16] for b in t]
                words = ' '.join('%08x' % struct.unpack_from('<I', row[1], q)[0] for q in range(0, 16, 4))
                if row[0] != row[1] or row[1] != row[2]:
                    d = ' '.join('%d:%08x>%08x>%08x' % (q // 4, *(struct.unpack_from('<I', r, q)[0] for r in row))
                                 for q in range(0, 16, 4) if len({r[q:q + 4] for r in row}) > 1)
                    print('S274 WIN   * +%05x: %s   | %s' % (st + o, words, d))
                else:
                    print('S274 WIN     +%05x: %s' % (st + o, words))
        c = [v.get('NwoaSnap%dC%d' % (k, t)) for t in range(3)]
        if c[0] and c[1]:
            d = []
            for o in range(0, min(len(c[0]), len(c[1])) - 3, 4):
                x, y = struct.unpack_from('<I', c[0], o)[0], struct.unpack_from('<I', c[1], o)[0]
                z = struct.unpack_from('<I', c[2], o)[0] if c[2] else y
                if x != y or y != z:
                    d.append('+%x:%d/%d' % (o, (y - x) & 0xFFFFFFFF, (z - y) & 0xFFFFFFFF))
            print('S274 CNT  %s #%d counters words changed (offset:scan delta/fix delta): %s' % (knob, k, ' '.join(d[:80])))
    if stuckA:
        cnt = Counter(o for d in stuckA for o in d)
        noise = set(o for d in aliveA for o in d)
        allof = sorted(o for o, n in cnt.items() if n == len(stuckA))
        print('S274 SUM  stalled scans %d, scans without the stall %d' % (len(stuckA), len(aliveA)))
        print('S274 SUM  words "changed by the scan and back after the re-create" in every stalled scan: %d; '
              'of these not changed in any scan without the stall: %d' % (len(allof), len([o for o in allof if o not in noise])))
        shown = 0
        for o in allof:
            if o in noise:
                continue
            vals = [d[o] for d in stuckA]
            same = len(set(vals)) == 1
            if shown < 200:
                print('S274 CAND +%05x: %s%s' % (o, ' | '.join('%08x>%08x' % v for v in vals[:4]), '  (same values every time)' if same else ''))
            shown += 1
    print('S274 ANALYZE DONE')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
