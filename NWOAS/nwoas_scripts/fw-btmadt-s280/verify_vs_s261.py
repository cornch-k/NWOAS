#!/usr/bin/env python3
"""S280 host check: against the S261 build tree, (1) the only source difference is MADT_Static.aslc (+ the
build-generated Conf/.AutoGenIdFile.txt), (2) of all compiled ACPI tables (*.acpi, *.aml) only MADT differs, in
exactly 4 bytes = EfficiencyClass 0 -> 1 of GICC MPIDR 0x10100..0x10103, (3) that MADT equals the S260 one.
usage: python3 -B verify_vs_s261.py      (reads the trees only)"""
from pathlib import Path
import filecmp, sys
R = Path('/Users/angigyeom/Desktop/DEV/NWOAS')
A, B, C = R / 'fw-bt-s261-mu', R / 'fw-btmadt-s280-mu', R / 'fw-power-s260-mu'
MREL = 'MacMini2020-AARCH64/DEBUG_CLANGPDB/AARCH64/T810XFamilyPkg/AcpiTables/CPUAcpiTables/OUTPUT/MADT_Static.acpi'
SKIP = {'Build', '.git', 'venv', '__pycache__'}
ok = True
def walk(d, rel=''):
    c = filecmp.dircmp(d[0], d[1], ignore=list(SKIP))
    out = [rel + n for n in c.diff_files + c.left_only + c.right_only]
    for s in c.common_dirs:
        out += walk((Path(d[0]) / s, Path(d[1]) / s), rel + s + '/')
    return out
src = sorted(walk((A, B)))
want = ['Conf/.AutoGenIdFile.txt', 'Silicon/Apple/T810XFamilyPkg/AcpiTables/MADT_Static.aslc']
print('source differences:', src); ok &= src == want
tab = lambda r: {str(q.relative_to(r)): q.read_bytes() for q in r.rglob('*') if q.is_file() and q.suffix in ('.acpi', '.aml')}
ta, tb = tab(A / 'Build'), tab(B / 'Build')
diff = [k for k in ta if ta[k] != tb.get(k)]
print('ACPI tables:', len(ta), 'differing:', diff); ok &= set(ta) == set(tb) and diff == [MREL]
a, b = ta[MREL], tb[MREL]
pos = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
eff = []
at, L = 44, int.from_bytes(b[4:8], 'little')
while at + 2 <= L and b[at + 1]:
    if b[at] == 0x0b:
        eff.append((int.from_bytes(b[at + 68:at + 76], 'little'), a[at + 76], b[at + 76], at + 76))
    at += b[at + 1]
print('GICC (mpidr, s261, s280):', [(hex(m), x, y) for m, x, y, _ in eff])
ok &= len(a) == len(b) and pos == [o for m, x, y, o in eff if m >= 0x10100]
ok &= all((x, y) == ((0, 1) if m >= 0x10100 else (0, 0)) for m, x, y, _ in eff) and len(eff) == 8
ok &= (C / 'Build' / MREL).read_bytes() == b
print('MADT == S260 MADT:', (C / 'Build' / MREL).read_bytes() == b)
print('S280 VERIFY:', 'PASS' if ok else 'FAIL'); sys.exit(0 if ok else 1)
