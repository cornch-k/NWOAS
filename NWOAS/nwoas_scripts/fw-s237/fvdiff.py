#!/usr/bin/env python3
"""Compare two UEFI FD images module by module (read-only).

Walks every firmware volume in each FD, every FFS file, every section, and decompresses
LZMA GUID-defined sections (EDK2 LzmaCompress = .lzma "alone" format) and nested FV images,
so a difference in a compressed DXE FV is attributed to the FFS file(s) / PE32 images inside it.

usage: fvdiff.py A.fd B.fd [--json out.json]
"""
import hashlib, json, lzma, struct, sys
from collections import OrderedDict

LZMA_GUID = '{ee4e5898-3914-4259-9d6e-dc7bd79403cf}'
FV_SIG = b'_FVH'
SEC_NAMES = {0x01: 'COMPRESSION', 0x02: 'GUID_DEFINED', 0x10: 'PE32', 0x11: 'PIC', 0x12: 'TE',
             0x13: 'DXE_DEPEX', 0x14: 'VERSION', 0x15: 'USER_INTERFACE', 0x17: 'FV_IMAGE',
             0x18: 'FREEFORM_SUBTYPE_GUID', 0x19: 'RAW', 0x1b: 'PEI_DEPEX', 0x1c: 'MM_DEPEX'}


def guid(b):
    a, b2, c = struct.unpack_from('<IHH', b, 0)
    d = b[8:16]
    return '{%08x-%04x-%04x-%s-%s}' % (a, b2, c, d[:2].hex(), d[2:].hex())


def sha(b):
    return hashlib.sha256(b).hexdigest()


def sections(buf, path, out, base):
    o = 0
    while o + 4 <= len(buf):
        size = int.from_bytes(buf[o:o + 3], 'little'); typ = buf[o + 3]; hl = 4
        if size == 0xffffff:
            size = struct.unpack_from('<I', buf, o + 4)[0]; hl = 8
        if size < hl or o + size > len(buf):
            break
        body = buf[o + hl:o + size]
        name = SEC_NAMES.get(typ, 'T%02x' % typ)
        p = '%s/%s@%x' % (path, name, o)
        if typ == 0x15:
            out['_ui'] = body.decode('utf-16-le', 'replace').rstrip('\0')
        if typ == 0x02:
            g = guid(body); doff = struct.unpack_from('<H', body, 16)[0] - hl
            data = body[doff:]
            if g == LZMA_GUID:
                raw = lzma.decompress(data, format=lzma.FORMAT_ALONE)
                out[p + '[lzma]'] = (sha(raw), len(raw), None)
                sections(raw, p + '[lzma]', out, None)
            else:
                sections(data, p, out, None)
        elif typ == 0x17:
            fv(body, p, out, None)
        else:
            out[p] = (sha(body), len(body), None if base is None else (base + o + hl, len(body)))
        o += (size + 3) & ~3


def fv(buf, path, out, base):
    if buf[40:44] != FV_SIG:
        return 0
    fvlen = struct.unpack_from('<Q', buf, 32)[0]; hlen = struct.unpack_from('<H', buf, 48)[0]
    ext = struct.unpack_from('<H', buf, 52)[0]
    o = hlen
    if ext:
        o = ext + struct.unpack_from('<I', buf, ext + 16)[0]
    o = (o + 7) & ~7
    while o + 24 <= min(len(buf), fvlen):
        hdr = buf[o:o + 24]
        if hdr == b'\xff' * 24:
            break
        g = guid(hdr); typ = hdr[18]; attr = hdr[19]
        size = int.from_bytes(hdr[20:23], 'little'); hl = 24
        if attr & 0x01:  # FFS_ATTRIB_LARGE_FILE
            size = struct.unpack_from('<Q', buf, o + 24)[0]; hl = 32
        if size < hl:
            break
        fpath = '%s/%s' % (path, g)
        sub = OrderedDict()
        if typ in (0x01, 0xf0):  # RAW / PAD
            sub['raw'] = (sha(buf[o + hl:o + size]), size - hl, None if base is None else (base + o + hl, size - hl))
        else:
            sections(buf[o + hl:o + size], '', sub, None if base is None else base + o + hl)
        ui = sub.pop('_ui', '')
        out[fpath] = {'type': typ, 'ui': ui, 'offset': None if base is None else base + o,
                      'size': size, 'header_sha256': sha(hdr), 'sections': sub}
        o = (o + size + 7) & ~7
    return fvlen


def walk_fd(fd):
    out = OrderedDict(); o = 0
    while o < len(fd):
        if fd[o + 40:o + 44] == FV_SIG:
            n = fv(fd[o:], 'FV@%x' % o, out, o)
            out['FV@%x' % o] = {'fv_header_sha256': sha(fd[o:o + struct.unpack_from('<H', fd, o + 48)[0]]),
                                'length': n}
            o += max(n, 0x1000)
        else:
            o += 0x1000
    return out


def flat(files, prefix='', ui_parent=''):
    r = {}
    for k, v in files.items():
        if isinstance(v, tuple):
            r[prefix + k] = (v[0], v[1], ui_parent)
        elif 'sections' in v:
            r[prefix + k + '#hdr'] = (v['header_sha256'], v['size'], v['ui'])
            r.update(flat(v['sections'], prefix + k, v['ui'] or ui_parent))
        else:
            r[prefix + k + '#fvhdr'] = (v['fv_header_sha256'], v['length'], ui_parent)
    return r


def main():
    A = open(sys.argv[1], 'rb').read(); B = open(sys.argv[2], 'rb').read()
    fa, fb = flat(walk_fd(A)), flat(walk_fd(B))
    changed = [k for k in fa if k in fb and fa[k][0] != fb[k][0]]
    only_a = [k for k in fa if k not in fb]; only_b = [k for k in fb if k not in fa]
    rng = []; i = 0; n = min(len(A), len(B))
    while i < n:
        if A[i] != B[i]:
            j = i
            while j < n and (A[j] != B[j] or (j + 64 < n and A[j:j + 64] != B[j:j + 64])):
                j += 1
            rng.append((i, j)); i = j
        else:
            i += 1
    res = {'a': sys.argv[1], 'b': sys.argv[2], 'a_sha256': sha(A), 'b_sha256': sha(B),
           'a_bytes': len(A), 'b_bytes': len(B),
           'differing_bytes': sum(1 for x, y in zip(A, B) if x != y) + abs(len(A) - len(B)),
           'differing_ranges': [[hex(x), hex(y), y - x] for x, y in rng],
           'leaf_items_compared': len(fa),
           'changed': [{'item': k, 'ui': fa[k][2], 'a_len': fa[k][1], 'b_len': fb[k][1]} for k in changed],
           'only_in_a': only_a, 'only_in_b': only_b}
    if '--json' in sys.argv:
        open(sys.argv[sys.argv.index('--json') + 1], 'w').write(json.dumps(res, indent=2) + '\n')
    print(json.dumps({k: v for k, v in res.items() if k != 'differing_ranges'} | {'n_ranges': len(rng)}, indent=1))


if __name__ == '__main__':
    main()
