"""Bounded4KiB NVMe PRP resolver for a future64KiB read-only transport.
Caller must supply a RAM-range validator; never read descriptors from MMIO.
Long/chained PRP lists are deliberately unsupported in this first adapter.
"""
import struct
PAGE=4096
LIMIT=65536
class InvalidPRP(ValueError):pass

def resolve(prp1,prp2,length,ram_contains,read_memory):
    if not 1<=length<=LIMIT or not 0<prp1<2**64 or prp1&3:
        raise InvalidPRP('invalid PRP1/length')
    def checked(addr,size):
        if not 0<addr<2**64 or size<1 or addr+size>2**64 or not ram_contains(addr,size):
            raise InvalidPRP('PRP outside validated guest RAM')
        return addr,size
    first=min(length,PAGE-(prp1&(PAGE-1)))
    spans=[checked(prp1,first)];left=length-first
    if not left:return spans
    if not prp2 or prp2&(PAGE-1):raise InvalidPRP('PRP2 must be page aligned')
    if left<=PAGE:
        spans.append(checked(prp2,left));return spans
    pages=(left+PAGE-1)//PAGE
    checked(prp2,pages*8)
    raw=read_memory(prp2,pages*8)
    if len(raw)!=pages*8:raise InvalidPRP('short PRP list')
    for addr in struct.unpack('<'+'Q'*pages,raw):
        if addr&(PAGE-1):raise InvalidPRP('unaligned data page')
        n=min(left,PAGE);spans.append(checked(addr,n));left-=n
    assert left==0
    return spans
