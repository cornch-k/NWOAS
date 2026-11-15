#!/usr/bin/env python3
"""S237 diff report: payload regions vs S223, and module-level FD comparison (read-only).

Writes out/diff-report.json and prints a short text summary.
"""
import hashlib, json, re, sys
from pathlib import Path
sys.dont_write_bytecode = True
D = Path(__file__).resolve().parent
sys.path.insert(0, str(D))
import fvdiff  # noqa: E402

S = D.parent
S223 = S / 'loader-s223/m1n1-payload-s223-fd-tail.bin'
S216FD = S / 'firmware-s216/source-built.fd'
REPRO_FD = D / 'out/repro-x31/fd-repro.fd'
REPRO_PAY = D / 'out/repro-x31/m1n1-payload-s237-repro-s223.bin'
S237FD = D / 'out/s237/fd-s237.fd'
S237PAY = D / 'out/m1n1-payload-s237-bridgeresv.bin'
REGIONS = [('guest m1n1 prefix (S215)', 0, 0x20c000), ('padded J274 DTB (S221)', 0x20c000, 0x21c000),
           ('Project Mu FD', 0x21c000, 0x21c000 + 0x1d88000), ('0xff FD tail (S223)', 0x21c000 + 0x1d88000, 0x201c000)]


def sha(b):
    return hashlib.sha256(b).hexdigest()


def ranges(a, b, base=0, gap=64):
    out = []; i = 0; n = min(len(a), len(b))
    while i < n:
        if a[i] != b[i]:
            j = i; last = i
            while j < n and j <= last + gap:
                if a[j] != b[j]:
                    last = j
                j += 1
            out.append((base + i, base + last + 1)); i = last + 1
        else:
            i += 1
    return out


def mask(b):
    b = bytearray(b)
    for m in re.finditer(rb'RSDS(.{8})LLD PDB\.', bytes(b), re.S):
        b[m.start(1):m.end(1)] = b'\0' * 8
    return bytes(b)


def leaves(fd):
    fvdiff.sha = lambda x: bytes(x)
    return fvdiff.flat(fvdiff.walk_fd(fd))


def module_cmp(a, b):
    fa, fb = leaves(a), leaves(b)
    leaf = lambda k: not (k.endswith('#hdr') or k.endswith('#fvhdr') or k.endswith('[lzma]'))
    res = {'only_in_a': [k for k in fa if k not in fb], 'only_in_b': [k for k in fb if k not in fa],
           'leaves': sum(1 for k in fa if leaf(k)), 'raw_changed': [], 'changed_after_rsds_mask': []}
    for k in fa:
        if k in fb and leaf(k) and fa[k][0] != fb[k][0]:
            e = {'item': k, 'ui': fa[k][2], 'a_len': len(fa[k][0]), 'b_len': len(fb[k][0])}
            res['raw_changed'].append(e)
            if mask(fa[k][0]) != mask(fb[k][0]):
                r = ranges(mask(fa[k][0]), mask(fb[k][0]), 0, 16)
                e2 = dict(e, ranges_in_image=[[hex(x), hex(y)] for x, y in r[:40]], n_ranges=len(r))
                res['changed_after_rsds_mask'].append(e2)
    return res


p223, p237 = S223.read_bytes(), S237PAY.read_bytes()
rep = {'s223_payload_sha256': sha(p223), 's237_payload_sha256': sha(p237),
       'bytes': [len(p223), len(p237)], 'regions': []}
for name, lo, hi in REGIONS:
    a, b = p223[lo:hi], p237[lo:hi]
    r = ranges(a, b, lo)
    rep['regions'].append({'region': name, 'start': hex(lo), 'end': hex(hi), 'identical': a == b,
                           'differing_bytes': sum(1 for x, y in zip(a, b) if x != y),
                           'first_diff': hex(r[0][0]) if r else None, 'last_diff': hex(r[-1][1] - 1) if r else None,
                           'n_ranges_gap64': len(r)})
rep['repro_payload_vs_s223'] = {'sha256': sha(REPRO_PAY.read_bytes()),
                                'identical': REPRO_PAY.read_bytes() == p223}
rep['modules_s216fd_vs_repro'] = module_cmp(S216FD.read_bytes(), REPRO_FD.read_bytes())
rep['modules_repro_vs_s237'] = module_cmp(REPRO_FD.read_bytes(), S237FD.read_bytes())
rep['modules_s216fd_vs_s237'] = module_cmp(S216FD.read_bytes(), S237FD.read_bytes())
(D / 'out/diff-report.json').write_text(json.dumps(rep, indent=2) + '\n')
for r in rep['regions']:
    print('%-28s %s..%s identical=%s differing_bytes=%d' % (r['region'], r['start'], r['end'], r['identical'], r['differing_bytes']))
for k in ('modules_s216fd_vs_repro', 'modules_repro_vs_s237', 'modules_s216fd_vs_s237'):
    m = rep[k]
    print(k, 'leaves=%d raw_changed=%d changed_after_rsds_mask=%s only=%d/%d' % (
        m['leaves'], len(m['raw_changed']), [(e['ui'] or e['item'], e['a_len'], e['b_len'], e['n_ranges']) for e in m['changed_after_rsds_mask']],
        len(m['only_in_a']), len(m['only_in_b'])))
