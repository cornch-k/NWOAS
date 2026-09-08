"""S92: transport-independent, strictly read-only NVMe namespace engine.

This does not expose PCI/MMIO/interrupts to Windows. It is the command layer for
that future adapter. Backend API has only read_block(lba), no write/format/flush.
Status values follow NVMe (unshifted SC/SCT, before CQ phase/DNR packing).
"""
from dataclasses import dataclass
import struct

SUCCESS=0
INVALID_OPCODE=1
INVALID_FIELD=2
INVALID_NSID=0x0b
LBA_RANGE=0x80
WRITE_TO_RO=0x182
READ_ERROR=0x281
BLOCK=4096
MAX_TRANSFER=65536

@dataclass(frozen=True)
class Result:
    status:int
    data:bytes=b''
    result:int=0

class ReadOnlyNamespace:
    def __init__(self, block_count, read_block):
        if not isinstance(block_count,int) or not 1<=block_count<2**40:
            raise ValueError('invalid namespace capacity')
        self.block_count=block_count
        self.read_block=read_block

    @staticmethod
    def _fields(command):
        if len(command)!=64:
            return None
        opcode,flags,cid,nsid=struct.unpack_from('<BBHI',command)
        # Only unfused PRP commands; no SGL or external metadata yet.
        if flags or struct.unpack_from('<Q',command,16)[0]:
            return None
        return opcode,nsid,struct.unpack_from('<IIIIII',command,40)

    def admin(self,command):
        fields=self._fields(command)
        if fields is None:return Result(INVALID_FIELD)
        opcode,nsid,cdw=fields
        if opcode!=0x06:return Result(INVALID_OPCODE)
        cns=cdw[0]&0xff
        if cdw[0]>>8 or any(cdw[1:]):return Result(INVALID_FIELD)
        data=bytearray(BLOCK)
        if cns==0:
            if nsid!=1:return Result(INVALID_NSID)
            struct.pack_into('<QQQ',data,0,self.block_count,self.block_count,self.block_count)
            data[25]=0 # one LBA format, zero-based count
            data[26]=0 # format0, no extended metadata
            data[130]=12 # LBADS of LBAF0 (4096-byte logical sector)
            data[99]=1 # namespace write-protection state
        elif cns==1:
            if nsid not in (0,0xffffffff):return Result(INVALID_NSID)
            struct.pack_into('<HH',data,0,0x1234,0x1234) # synthetic research identity
            data[4:24]=b'NWOAS-RO-00000000001'.ljust(20,b' ')[:20]
            model=b'NWOAS ANS2 READ ONLY BRIDGE'
            data[24:64]=model.ljust(40,b' ')
            data[64:72]=b'S92     '
            data[77]=4 # MDTS=64KiB with4KiB minimum pages
            struct.pack_into('<H',data,78,1) # controller ID
            struct.pack_into('<I',data,80,0x00010300) # NVMe1.3
            data[512]=0x66 # SQ entry size64 bytes
            data[513]=0x44 # CQ entry size16 bytes
            struct.pack_into('<I',data,516,1) # one namespace
        elif cns==2:
            if nsid==0:struct.pack_into('<I',data,0,1)
        else:return Result(INVALID_FIELD)
        assert len(data)==BLOCK
        return Result(SUCCESS,bytes(data))

    def io(self,command,mem=None):
        fields=self._fields(command)
        if fields is None:return Result(INVALID_FIELD)
        opcode,nsid,cdw=fields
        if nsid!=1:return Result(INVALID_NSID)
        # Explicitly reject mutating NVM commands without consulting the backend.
        if opcode in (0x01,0x04,0x08,0x09,0x0d,0x11,0x15,0x19,0x79):
            return Result(WRITE_TO_RO)
        if opcode!=0x02:return Result(INVALID_OPCODE)
        slba=cdw[0]|(cdw[1]<<32)
        nlb=(cdw[2]&0xffff)+1
        # Accept only FUA/LR hints; metadata/protection/directives unsupported.
        if cdw[2]&0x3fff0000 or any(cdw[3:]):return Result(INVALID_FIELD)
        if nlb*BLOCK>MAX_TRANSFER:return Result(INVALID_FIELD)
        if slba>=self.block_count or nlb>self.block_count-slba:return Result(LBA_RANGE)
        # S97: one backend call per command (read_block(lba, n) returns n*BLOCK bytes).
        try:b=self.read_block(slba,nlb)
        except (OSError,TimeoutError):return Result(READ_ERROR)
        if not isinstance(b,bytes) or len(b)!=nlb*BLOCK:return Result(READ_ERROR)
        return Result(SUCCESS,b)
