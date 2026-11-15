#!/usr/bin/env python3
"""Module-level comparison of two FDs with the CodeView RSDS PDB-content hash masked.

LLD writes each image's RSDS GUID as <8-byte hash of the PDB contents>"LLD PDB.". The PDB records
compiler/linker executable paths and working directories, which are not part of the image's code or
data. This script decompresses every FV/section (fvdiff.py walker), zeroes the 8 hash bytes after every
"RSDS" in every leaf, and reports any leaf that still differs, plus where the raw differences lie.
usage: maskcmp.py A.fd B.fd
"""
import sys, json, re
sys.dont_write_bytecode = True
sys.path.insert(0, __import__('os').path.dirname(__file__))
import fvdiff
fvdiff.sha = lambda b: bytes(b)

def mask(b):
    b = bytearray(b)
    for m in re.finditer(rb'RSDS(.{8})LLD PDB\.', bytes(b), re.S):
        b[m.start(1):m.end(1)] = b'\0' * 8
    return bytes(b)

A = open(sys.argv[1], 'rb').read(); B = open(sys.argv[2], 'rb').read()
fa, fb = fvdiff.flat(fvdiff.walk_fd(A)), fvdiff.flat(fvdiff.walk_fd(B))
leaf = lambda k: not (k.endswith('#hdr') or k.endswith('#fvhdr') or k.endswith('[lzma]'))
raw_changed = [k for k in fa if k in fb and fa[k][0] != fb[k][0] and leaf(k)]
still = [k for k in raw_changed if mask(fa[k][0]) != mask(fb[k][0])]
rsds_only = [fa[k][2] or k.rsplit('/', 2)[-2] for k in raw_changed if k not in still]
lzma_masked_equal = all(mask(fa[k][0]) == mask(fb[k][0]) for k in fa if k.endswith('[lzma]') and k in fb)
print(json.dumps({'leaf_items': sum(1 for k in fa if leaf(k)),
                  'only_in_a': [k for k in fa if k not in fb], 'only_in_b': [k for k in fb if k not in fa],
                  'leaves_raw_different': len(raw_changed),
                  'leaves_equal_after_rsds_mask': len(raw_changed) - len(still),
                  'leaves_still_different': [{'item': k, 'ui': fa[k][2], 'a_len': len(fa[k][0]), 'b_len': len(fb[k][0])} for k in still],
                  'decompressed_volumes_equal_after_mask': lzma_masked_equal,
                  'rsds_only_modules': sorted(set(rsds_only))}, indent=1))
