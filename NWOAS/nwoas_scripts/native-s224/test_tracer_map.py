"""Actual m1n1 DictRangeMap replacement semantics; import helpers only."""
from m1n1.utils import DictRangeMap,irange
m=DictRangeMap();z=irange(0x700000000,0x100000)
a=object();b=object();m[z,'same']=a;m[z,'same']=b
assert m[0x700000000,'same'] is b and m[0x7000fffff,'same'] is b
assert len(m[0x700000000])==1
print('PASS: same-range/name tracer replaces its entry, no callback stacking')
