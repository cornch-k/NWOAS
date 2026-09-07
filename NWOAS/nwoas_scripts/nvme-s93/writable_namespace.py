"""S96: namespace that permits Write/Flush only inside one verified LBA window.

Everything outside [write_first, write_last] stays exactly as ReadOnlyNamespace:
mutating opcodes are refused with WRITE_TO_RO before touching the backend.
The window is the WINTEST partition measured read-only in S95; any other range,
including partial overlap, is rejected with LBA_RANGE.
"""
import struct
from readonly_namespace import (ReadOnlyNamespace,Result,SUCCESS,INVALID_FIELD,INVALID_NSID,
                                LBA_RANGE,WRITE_TO_RO,READ_ERROR,BLOCK,MAX_TRANSFER)
from prp import resolve,InvalidPRP

WRITE_ERROR=0x280 # Media: Write Fault (mirrors READ_ERROR 0x281)
FLUSH=0x00
WRITE=0x01

class WindowWritableNamespace(ReadOnlyNamespace):
    def __init__(self,block_count,read_block,write_block,flush,write_first,write_last):
        super().__init__(block_count,read_block)
        if not (isinstance(write_first,int) and isinstance(write_last,int) and 0<=write_first<=write_last<block_count):
            raise ValueError('invalid write window')
        self.write_block=write_block;self.flush=flush
        self.write_first=write_first;self.write_last=write_last

    def admin(self,command):
        r=super().admin(command)
        if r.status==SUCCESS and command[0]==0x06 and (struct.unpack_from('<I',command,40)[0]&0xff)==0:
            d=bytearray(r.data);d[99]=0 # NSATTR: namespace no longer write-protected
            return Result(SUCCESS,bytes(d))
        return r

    def io(self,command,mem=None):
        fields=self._fields(command)
        if fields is None:return Result(INVALID_FIELD)
        opcode,nsid,cdw=fields
        if opcode not in (FLUSH,WRITE):return super().io(command)
        if nsid!=1:return Result(INVALID_NSID)
        if opcode==FLUSH:
            if any(cdw):return Result(INVALID_FIELD)
            try:self.flush()
            except (OSError,TimeoutError):return Result(WRITE_ERROR)
            return Result(SUCCESS)
        slba=cdw[0]|(cdw[1]<<32)
        nlb=(cdw[2]&0xffff)+1
        # ponytail: FUA/LR bits and cdw13 DSM access hints are accepted and ignored (no per-write flush).
        if cdw[2]&0x3fff0000 or cdw[3]&~0xff or any(cdw[4:]):return Result(INVALID_FIELD)
        if nlb*BLOCK>MAX_TRANSFER:return Result(INVALID_FIELD)
        if slba>=self.block_count or nlb>self.block_count-slba:return Result(LBA_RANGE)
        if slba<self.write_first or slba+nlb-1>self.write_last:return Result(WRITE_TO_RO)
        if mem is None:return Result(INVALID_FIELD)
        try:spans=resolve(*struct.unpack_from('<QQ',command,24),nlb*BLOCK,mem.contains,mem.read)
        except InvalidPRP:return Result(INVALID_FIELD)
        data=b''.join(mem.read(a,n) for a,n in spans)
        if len(data)!=nlb*BLOCK:return Result(INVALID_FIELD)
        assert self.write_first<=slba and slba+nlb-1<=self.write_last
        try:self.write_block(slba,data) # S97: one multi-block backend call
        except (OSError,TimeoutError):return Result(WRITE_ERROR)
        return Result(SUCCESS)
