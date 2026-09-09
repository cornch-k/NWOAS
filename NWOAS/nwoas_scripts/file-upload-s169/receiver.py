"""S169 fixed-artifact receive channel. No arbitrary guest paths or disk access.

64KiB writes at LBA160..175; 4KiB read-only ACK at176. Existing job port128,
S168 download129..144 and FAT256+ untouched. Files remain private until exact
size/SHA verification; partials are retained on error, never silently replaced.
"""
import hashlib,os,struct,zlib
from pathlib import Path
BLOCK=4096;WINDOW=65536;HEADER=128;CAP=WINDOW-HEADER
WRITE_LBA=160;ACK_LBA=176
MAGIC=b'NWOAS-S169-UPLD!';ACK_MAGIC=b'NWOAS-S169-UACK!'
assert len(MAGIC)==len(ACK_MAGIC)==16
BEGIN,DATA,END=0,1,2
class Receiver:
 def __init__(self,token,size,sha256,destination):
  if len(token)!=16 or size<1:raise ValueError('invalid registration')
  self.token=bytes(token);self.size=size;self.digest=bytes.fromhex(sha256)
  if len(self.digest)!=32:raise ValueError('invalid digest')
  self.dest=Path(destination);self.part=self.dest.with_name(self.dest.name+'.part')
  self.file=None;self.received=0;self.hash=hashlib.sha256();self.seq=0
  self.last=None;self.done=False;self.failed=False;self.ack=self._ack()
 def _ack(self):
  b=bytearray(BLOCK);b[:16]=ACK_MAGIC;b[16:32]=self.token
  struct.pack_into('<IIQQ',b,32,self.seq,2 if self.failed else 1 if self.done else 0,self.received,self.size)
  b[64:96]=self.digest;struct.pack_into('<I',b,124,zlib.crc32(b[:124]));return bytes(b)
 def close(self):
  if self.file is not None:self.file.close();self.file=None
 def accept(self,b):
  if len(b)!=WINDOW or b[:16]!=MAGIC or b[16:32]!=self.token:return False
  seq,kind,offset,size,n,crc=struct.unpack_from('<IIQQII',b,32)
  if b[64:96]!=self.digest or any(b[96:124]) or zlib.crc32(b[:124])!=struct.unpack_from('<I',b,124)[0]:return False
  if n>CAP or any(b[HEADER+n:]) or zlib.crc32(b[HEADER:HEADER+n])!=crc:return False
  if size!=self.size or not seq:return False
  signature=hashlib.sha256(b).digest()
  if seq==self.seq:return signature==self.last
  if self.failed or self.done or seq!=self.seq+1 or offset!=self.received:return False
  if kind==BEGIN:
   if self.seq or n or offset:return False
  elif kind==DATA:
   if self.file is None or n!=min(CAP,self.size-self.received) or not n:return False
  elif kind==END:
   if self.file is None or n or self.received!=self.size:return False
  else:return False
  try:
   if kind==BEGIN:
    if self.dest.exists() or self.dest.is_symlink():return False
    fd=os.open(self.part,os.O_WRONLY|os.O_CREAT|os.O_EXCL|getattr(os,'O_NOFOLLOW',0),0o600)
    self.file=os.fdopen(fd,'wb')
   elif kind==DATA:
    payload=b[HEADER:HEADER+n]
    if self.file.write(payload)!=n:raise OSError('short backup write')
    self.hash.update(payload);self.received+=n
   else:
    if self.hash.digest()!=self.digest:raise ValueError('full artifact SHA256 mismatch')
    # Durability fsync is performed by the host verifier after guest completion,
    # outside the synchronous NVMe rendezvous to avoid blocking its interrupt path.
    self.file.flush();self.close()
    # Atomic no-clobber final publication; partial is retained if link fails.
    os.link(self.part,self.dest);self.part.unlink();self.done=True
  except (OSError,ValueError):
   self.failed=True;self.close();self.ack=self._ack();return False
  self.seq=seq;self.last=signature;self.ack=self._ack();return True
 def overlay(self,lba,n,original):
  if len(original)!=n*BLOCK or n<1:raise ValueError('invalid read shape')
  if not lba<=ACK_LBA<lba+n:return original
  b=bytearray(original);at=(ACK_LBA-lba)*BLOCK;b[at:at+BLOCK]=self.ack;return bytes(b)
