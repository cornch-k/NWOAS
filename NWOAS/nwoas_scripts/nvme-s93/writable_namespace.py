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
    def __init__(self,block_count,read_block,write_block,flush,write_first,write_last,gpt_guard=None,direct=None):
        super().__init__(block_count,read_block)
        if not (isinstance(write_first,int) and isinstance(write_last,int) and 0<=write_first<=write_last<block_count):
            raise ValueError('invalid write window')
        self.write_block=write_block;self.flush=flush
        self.write_first=write_first;self.write_last=write_last
        self.gpt=gpt_guard # S98: optional validated partition-table writes
        # S101: direct(write, lba, n, prp1, prp2) -> True when the device DMA'd guest RAM itself.
        # False means "not handled" and the copy path runs instead. Zone/GPT checks stay here.
        self.direct=direct

    def _try_direct(self,write,slba,nlb,command):
        if self.direct is None:return False
        prp1,prp2=struct.unpack_from('<QQ',command,24)
        if prp1&(BLOCK-1) or (nlb>=2 and prp2&(BLOCK-1)):return False
        if self.gpt is not None and (any(self.gpt.is_gpt(l) for l in range(slba,slba+nlb)) or
                                     (not write and any(l in self.gpt.shadow for l in range(slba,slba+nlb)))):return False
        return bool(self.direct(write,slba,nlb,prp1,prp2))

    def io(self,command,mem=None):
        fields=self._fields(command)
        if fields is not None and fields[0]==0x02 and fields[1]==1 and mem is not None:
            cdw=fields[2];slba=cdw[0]|(cdw[1]<<32);nlb=(cdw[2]&0xffff)+1
            if not(cdw[2]&0x3fff0000 or any(cdw[3:])) and nlb*BLOCK<=MAX_TRANSFER and slba<self.block_count and nlb<=self.block_count-slba:
                if self._try_direct(False,slba,nlb,command):return Result(SUCCESS)
        r=self._io(command,mem)
        if self.gpt is not None and command[0]==0x02 and r.status==SUCCESS and r.data:
            cdw=struct.unpack_from('<III',command,40);slba=cdw[0]|(cdw[1]<<32)
            return Result(SUCCESS,self.gpt.read_overlay(slba,len(r.data)//BLOCK,r.data))
        return r

    def _gpt_write(self,slba,nlb,data):
        from gpt_guard import GptViolation
        if not all(self.gpt.is_gpt(l) for l in range(slba,slba+nlb)):return Result(WRITE_TO_RO) # no GPT+data straddle
        try:
            for i in range(nlb):self.gpt.stage(slba+i,data[i*BLOCK:(i+1)*BLOCK])
        except GptViolation:return Result(WRITE_TO_RO)
        except (OSError,TimeoutError):return Result(WRITE_ERROR)
        return Result(SUCCESS)

    def admin(self,command):
        r=super().admin(command)
        if r.status==SUCCESS and command[0]==0x06 and (struct.unpack_from('<I',command,40)[0]&0xff)==0:
            d=bytearray(r.data);d[99]=0 # NSATTR: namespace no longer write-protected
            return Result(SUCCESS,bytes(d))
        return r

    def _io(self,command,mem=None):
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
        gpt_write=self.gpt is not None and self.gpt.is_gpt(slba)
        if not gpt_write and (slba<self.write_first or slba+nlb-1>self.write_last):return Result(WRITE_TO_RO)
        if mem is None:return Result(INVALID_FIELD)
        if not gpt_write and self._try_direct(True,slba,nlb,command):return Result(SUCCESS)
        try:spans=resolve(*struct.unpack_from('<QQ',command,24),nlb*BLOCK,mem.contains,mem.read)
        except InvalidPRP:return Result(INVALID_FIELD)
        data=b''.join(mem.read(a,n) for a,n in spans)
        if len(data)!=nlb*BLOCK:return Result(INVALID_FIELD)
        if gpt_write:return self._gpt_write(slba,nlb,data)
        assert self.write_first<=slba and slba+nlb-1<=self.write_last
        try:self.write_block(slba,data) # S97: one multi-block backend call
        except (OSError,TimeoutError):return Result(WRITE_ERROR)
        return Result(SUCCESS)
