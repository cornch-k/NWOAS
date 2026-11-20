"""Small disassembler for raw SM4/SM5 shader tokens (version token first), as dumped by
APPLE_AGX_D3D_SHADER_DUMP (<stage>-<hash>.bin). Read-only helper, not a full disassembler.
    python3 -I dxbc_tokens_dis.py FILE.bin [...]"""
import re, struct, sys
from pathlib import Path
hdr = (Path(__file__).resolve().parents[3] / 'port-j274/gpu/driver-p15/drivers/apple-agx/mesa/dxbc/dxbc_parse.h').read_text()
body = hdr[hdr.index('enum dxbc_opcode {'):]; body = body[:body.index('};')]
body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
OPS = [n.strip().split('=')[0].strip().replace('DXBC_OP_', '').lower() for n in body.split('{', 1)[1].split(',') if n.strip()]
OT = {0: 'r', 1: 'v', 2: 'o', 3: 'x', 4: 'imm', 5: 'immd', 6: 's', 7: 't', 8: 'cb', 9: 'icb', 10: 'label', 11: 'vPrim', 12: 'oDepth', 13: 'null', 14: 'rast', 15: 'oMask'}
def operand(w, i):
    t = w[i]; i += 1
    nc = t & 3; sel = (t >> 2) & 3; typ = (t >> 12) & 0xff; dim = (t >> 20) & 3; ext = t >> 31
    mod = ''
    while ext:
        e = w[i]; i += 1; ext = e >> 31
        if (e & 0x3f) == 1: mod = {0: '', 1: '-', 2: 'abs ', 3: '-abs '}[(e >> 6) & 3]
    comps = ''
    if nc == 2:
        if sel == 0: comps = '.' + ''.join('xyzw'[k] for k in range(4) if (t >> (4 + k)) & 1)
        elif sel == 1: comps = '.' + ''.join('xyzw'[(t >> (4 + 2 * k)) & 3] for k in range(4))
        else: comps = '.' + 'xyzw'[(t >> 4) & 3]
    name = OT.get(typ, 'T%d' % typ)
    if typ == 4:
        n = 4 if nc == 2 else 1
        vals = []
        for k in range(n):
            f = struct.unpack('<f', struct.pack('<I', w[i + k]))[0]
            vals.append(('%g' % f) if (abs(f) > 1e-6 and abs(f) < 1e9) or w[i + k] == 0 else '0x%08x' % w[i + k])
        return mod + 'l(' + ','.join(vals) + ')', i + n
    idx = []
    for d in range(dim):
        rep = (t >> (22 + 3 * d)) & 7
        if rep == 0: idx.append(str(w[i])); i += 1
        elif rep == 2: s, i = operand(w, i); idx.append('[' + s + ']')
        elif rep == 3: c = w[i]; i += 1; s, i = operand(w, i); idx.append('[%s+%d]' % (s, c))
        else: idx.append('?rep%d' % rep); i += 2
    return mod + name + ('[' + ']['.join(idx) + ']' if typ in (8, 9) else ''.join(idx)) + comps, i
def dis(path):
    b = Path(path).read_bytes(); w = struct.unpack('<%dI' % (len(b) // 4), b[:len(b) // 4 * 4])
    kind = {0: 'ps', 1: 'vs', 2: 'gs', 3: 'hs', 4: 'ds', 5: 'cs'}.get(w[0] >> 16, '?')
    print('== %s  %s_%d_%d  dwords=%d' % (Path(path).name, kind, (w[0] >> 4) & 15, w[0] & 15, w[1]))
    i = 2
    while i < min(len(w), w[1]):
        t = w[i]; op = t & 0x7ff; ln = (t >> 24) & 0x7f
        name = OPS[op] if op < len(OPS) else 'op%d' % op
        if op == 53:  # customdata
            ln = w[i + 1]; print('   customdata class=%d dwords=%d' % (t >> 11, ln)); i += max(ln, 2); continue
        if ln == 0: print('   ?? zero length at', i); break
        j = i + 1; e = t >> 31
        while e: e = w[j] >> 31; j += 1
        args = []
        try:
            if name.startswith('dcl_'):
                if name in ('dcl_temps', 'dcl_global_flags', 'dcl_max_output_vertex_count'): args = [str(w[j])] if j < i + ln else []
                else:
                    while j < i + ln:
                        if name in ('dcl_input_ps', 'dcl_input', 'dcl_output', 'dcl_resource', 'dcl_sampler', 'dcl_constant_buffer') and len(args) == 0:
                            s, j = operand(w, j); args.append(s)
                        else: args.append('0x%x' % w[j]); j += 1
                    if name == 'dcl_input_ps': args.append('interp=%d' % ((t >> 11) & 15))
            else:
                while j < i + ln: s, j = operand(w, j); args.append(s)
        except Exception as ex: args.append('<decode %s>' % ex)
        sat = '_sat' if (t >> 13) & 1 and not name.startswith('dcl_') else ''
        print('   %-14s %s' % (name + sat, ', '.join(args)))
        i += ln
for f in sys.argv[1:]: dis(f)
