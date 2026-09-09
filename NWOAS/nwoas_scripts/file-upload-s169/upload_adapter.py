import struct
from readonly_namespace import Result,SUCCESS,INVALID_FIELD,INVALID_NSID
from prp import resolve,InvalidPRP
from receiver import WRITE_LBA,ACK_LBA,WINDOW
class UploadLink:
 def __init__(self,base,receiver):self.base,self.receiver=base,receiver
 def __getattr__(self,name):return getattr(self.base,name)
 def io(self,command,mem=None):
  f=self.base._fields(command)
  if f is None:return Result(INVALID_FIELD)
  op,ns,cdw=f
  if ns!=1:return Result(INVALID_NSID)
  lba=cdw[0]|(cdw[1]<<32);n=(cdw[2]&0xffff)+1
  if op==1 and lba==WRITE_LBA and n==16:
   if cdw[2]&0x3fff0000 or cdw[3]&~0xff or any(cdw[4:]) or mem is None:return Result(INVALID_FIELD)
   try:
    spans=resolve(*struct.unpack_from('<QQ',command,24),WINDOW,mem.contains,mem.read)
    b=b''.join(mem.read(a,z) for a,z in spans)
   except InvalidPRP:return Result(INVALID_FIELD)
   return Result(SUCCESS if self.receiver.accept(b) else INVALID_FIELD)
  result=self.base.io(command,mem)
  if op==2 and result.status==SUCCESS and lba<=ACK_LBA<lba+n:
   return Result(SUCCESS,self.receiver.overlay(lba,n,result.data),result.result)
  return result
