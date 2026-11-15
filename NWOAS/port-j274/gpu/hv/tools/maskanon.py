"""Classify bindiff.py function diffs: 'anon-only' if every changed line differs
only in unresolved data addresses (@?0x...), else 'real'."""
import re, sys
from collections import Counter
txt = open(sys.argv[1]).read().splitlines()
blocks = {}; cur = None
for l in txt:
    m = re.match(r'^== (\S+): size (0x[0-9a-f]+) -> (0x[0-9a-f]+)', l)
    if m:
        cur = m.group(1); blocks[cur] = {'size': (m.group(2), m.group(3)), '-': [], '+': []}; continue
    if l.startswith('== '):
        cur = None; continue
    if cur and re.match(r'^   [-+]', l) and not l.startswith('   ---') and not l.startswith('   +++'):
        blocks[cur][l[3]].append(l[4:])
mask = lambda s: re.sub(r'@?\?0x[0-9a-f]+', '@?X', s)
anon, real = [], []
for n, b in blocks.items():
    if b['size'][0] == b['size'][1] and Counter(map(mask, b['-'])) == Counter(map(mask, b['+'])):
        anon.append(n)
    else:
        real.append(n)
addrs = Counter()
for l in txt:
    pass
print(f'functions with diffs: {len(blocks)}; anon-address-only: {len(anon)}; other: {len(real)}')
for n in real:
    b = blocks[n]
    print(f'== {n}: size {b["size"][0]} -> {b["size"][1]}')
    rm, ad = Counter(map(mask, b['-'])), Counter(map(mask, b['+']))
    for k, v in (rm - ad).items(): print(f'   - x{v} {k}')
    for k, v in (ad - rm).items(): print(f'   + x{v} {k}')
