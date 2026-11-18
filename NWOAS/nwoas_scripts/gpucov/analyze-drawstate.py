#!/usr/bin/env python3
"""Decode the D3D11 draw-state trace (2762 P26) from a gpucov run file.

Usage: python3 -I gpucov/analyze-drawstate.py gpucov/runs/<run>.txt

Reads the "K17 measure-d3d11-..." lines that the K17-drawstate-chunk jobs
print, joins every draw with the blend / rasterizer / depth-stencil objects
and shaders bound at that moment, groups draws that share the same state, and
flags states that make a draw produce no visible pixels.
"""
import collections
import sys

BLEND = {1: 'ZERO', 2: 'ONE', 3: 'SRC_COLOR', 4: 'INV_SRC_COLOR', 5: 'SRC_ALPHA', 6: 'INV_SRC_ALPHA',
         7: 'DEST_ALPHA', 8: 'INV_DEST_ALPHA', 9: 'DEST_COLOR', 10: 'INV_DEST_COLOR', 11: 'SRC_ALPHA_SAT',
         14: 'BLEND_FACTOR', 15: 'INV_BLEND_FACTOR', 16: 'SRC1_COLOR', 17: 'INV_SRC1_COLOR', 18: 'SRC1_ALPHA',
         19: 'INV_SRC1_ALPHA'}
OP = {1: 'ADD', 2: 'SUB', 3: 'REV_SUB', 4: 'MIN', 5: 'MAX'}
CULL = {1: 'NONE', 2: 'FRONT', 3: 'BACK'}
FILL = {2: 'WIREFRAME', 3: 'SOLID'}
CMP = {1: 'NEVER', 2: 'LESS', 3: 'EQUAL', 4: 'LESS_EQUAL', 5: 'GREATER', 6: 'NOT_EQUAL', 7: 'GREATER_EQUAL', 8: 'ALWAYS'}
KIND = {1: 'Draw', 2: 'DrawIndexed', 3: 'DrawInstanced', 4: 'DrawIndexedInstanced'}


def words(line):
    parts = line.split()
    # name hr=0x... v0 v1 ...
    return parts[0], [int(x, 16) for x in parts[2:]]


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def main(path):
    blends, rasters, dsas, sigs, draws = {}, {}, {}, {}, []
    for raw in open(path, encoding='utf-8', errors='replace'):
        raw = raw.strip()
        if not raw.startswith('K17 measure-d3d11-'):
            continue
        name, v = words(raw[4:])
        if name == 'measure-d3d11-blend':
            blends[v[0]] = v
        elif name == 'measure-d3d11-raster':
            rasters[v[0]] = v
        elif name == 'measure-d3d11-dsa':
            dsas[v[0]] = v
        elif name == 'measure-d3d11-shader-sig':
            sigs[v[1]] = v
        elif name == 'measure-d3d11-drawstate':
            draws.append(v)
    print('objects: blend=%d raster=%d dsa=%d shaders=%d draws=%d' %
          (len(blends), len(rasters), len(dsas), len(sigs), len(draws)))
    for h, b in sorted(blends.items()):
        print('BLEND %08x a2c=%d indep=%d rt0 enable=%d logic=%d rgb=%s*src %s %s*dst alpha=%s %s %s logicop=%d mask=0x%x rt1-7 masks=%07x'
              % (h, b[1], b[2], b[3], b[4], BLEND.get(b[5], b[5]), OP.get(b[7], b[7]), BLEND.get(b[6], b[6]),
                 BLEND.get(b[8], b[8]), OP.get(b[10], b[10]), BLEND.get(b[9], b[9]), b[11], b[12], b[13]))
    for h, r in sorted(rasters.items()):
        print('RASTER %08x fill=%s cull=%s ccw=%d bias=%d clip=%d scissor=%d msaa=%d aaline=%d forced=%d cons=%d'
              % (h, FILL.get(r[1], r[1]), CULL.get(r[2], r[2]), r[3], s32(r[4]), r[5], r[6], r[7], r[8], r[9], r[10]))
    for h, d in sorted(dsas.items()):
        print('DSA %08x depth=%d write=%d func=%s stencil=%d front=%d back=%d rmask=%02x wmask=%02x front_func=%s pass=%d'
              % (h, d[1], d[2], CMP.get(d[3], d[3]), d[4], d[5], d[6], d[7], d[8], CMP.get(d[12], d[12]), d[11]))
    for h, s in sorted(sigs.items()):
        stage = 'VS' if s[0] == 0 else 'PS' if s[0] == 4 else str(s[0])
        ins = ['sv%d:r%d:%x' % (e >> 16, (e >> 8) & 0xff, e & 0xf) for e in s[6:6 + min(s[4], 10)]]
        outs = ['sv%d:r%d:%x' % (e >> 16, (e >> 8) & 0xff, e & 0xf) for e in s[16:16 + min(s[5], 10)]]
        print('SHADER %s %08x dwords=%d version=%08x in=%s out=%s' % (stage, h, s[2], s[3], ' '.join(ins), ' '.join(outs)))

    groups = collections.OrderedDict()
    for d in draws:
        key = tuple(d[4:9]) + tuple(d[9:19]) + tuple(d[19:23]) + tuple(d[23:27]) + tuple(d[27:29])
        g = groups.setdefault(key, {'n': 0, 'first': d[0], 'kinds': collections.Counter(), 'count': 0})
        g['n'] += 1
        g['kinds'][KIND.get(d[1], d[1])] += 1
        g['count'] += d[2] * max(d[3], 1)
    print('\nDRAW GROUPS (same bound state), first draw sequence, draws, vertices*instances:')
    for key, g in groups.items():
        blend, smask, raster, dsa, sref = key[0:5]
        vpx, vpy, vpw, vph, nvp, scl, sct, scr, scb, nsc = [s32(x) for x in key[5:15]]
        nrt, rtfmt, rtw, zfmt = key[15:19]
        vs, ps, srv0, prim = key[19:23]
        fbw, fbh = key[23:25]
        flags = []
        b = blends.get(blend)
        mask = b[12] if b else 0xf
        if blend and not b:
            flags.append('blend-unknown')
        if mask == 0:
            flags.append('RT0-WRITE-MASK-0')
        if b and b[3] and b[5] == 1 and b[6] == 2:
            flags.append('BLEND-ZERO-ONE(no change)')
        if smask == 0:
            flags.append('SAMPLE-MASK-0')
        r = rasters.get(raster)
        if r and r[6] and (scr <= scl or scb <= sct or nsc == 0):
            flags.append('SCISSOR-EMPTY(scissor on)')
        if r and r[2] == 2:
            flags.append('cull-front')
        if nvp == 0 or vpw <= 0 or vph <= 0:
            flags.append('VIEWPORT-EMPTY')
        if fbw and (vpx >= fbw or vpy >= fbh):
            flags.append('VIEWPORT-OFF-TARGET')
        if nrt == 0:
            flags.append('NO-COLOUR-TARGET')
        if not ps:
            flags.append('ps-unknown')
        z = dsas.get(dsa)
        if z and z[1] and z[3] == 1:
            flags.append('DEPTH-NEVER')
        if z and z[4]:
            flags.append('stencil')
        print('G first=%d draws=%d verts=%d kinds=%s blend=%08x(mask %x) raster=%08x dsa=%08x ref=%d vp=%d,%d %dx%d(n%d) '
              'sc=%d,%d-%d,%d(n%d) rt=%d fmt=%d w=%d z=%d fb=%dx%d vs=%08x ps=%08x srv0=%d prim=%d %s'
              % (g['first'], g['n'], g['count'], dict(g['kinds']), blend, mask, raster, dsa, sref, vpx, vpy, vpw, vph,
                 nvp, scl, sct, scr, scb, nsc, nrt, rtfmt, rtw, zfmt, fbw, fbh, vs, ps, srv0, prim,
                 ' '.join(flags) if flags else 'ok'))


if __name__ == '__main__':
    main(sys.argv[1])
