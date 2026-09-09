import tempfile,struct,zlib,hashlib,unittest
from pathlib import Path
from receiver import *
def frame(token,blob,seq,kind,offset,payload=b''):
 b=bytearray(WINDOW);b[:16]=MAGIC;b[16:32]=token
 struct.pack_into('<IIQQII',b,32,seq,kind,offset,len(blob),len(payload),zlib.crc32(payload));b[64:96]=hashlib.sha256(blob).digest()
 struct.pack_into('<I',b,124,zlib.crc32(b[:124]));b[128:128+len(payload)]=payload;return bytes(b)
class Tests(unittest.TestCase):
 def setUp(self):
  self.t=tempfile.TemporaryDirectory();self.blob=bytes(range(256))*400;self.token=b'T'*16
  self.r=Receiver(self.token,len(self.blob),hashlib.sha256(self.blob).hexdigest(),Path(self.t.name)/'install.swm')
 def tearDown(self):self.r.close();self.t.cleanup()
 def f(self,seq,kind,offset,payload=b''):return frame(self.token,self.blob,seq,kind,offset,payload)
 def test_roundtrip_and_retry(self):
  begin=self.f(1,BEGIN,0);self.assertTrue(self.r.accept(begin));self.assertTrue(self.r.accept(begin))
  off=0;seq=2
  while off<len(self.blob):
   b=self.blob[off:off+CAP];msg=self.f(seq,DATA,off,b);self.assertTrue(self.r.accept(msg));self.assertTrue(self.r.accept(msg));off+=len(b);seq+=1
  self.assertTrue(self.r.accept(self.f(seq,END,off)));self.assertTrue(self.r.done)
  self.assertEqual(self.r.dest.read_bytes(),self.blob);self.assertFalse(self.r.part.exists())
  self.assertTrue(self.r.accept(self.f(seq,END,off)))
 def test_reject_shape_crc_and_stale(self):
  self.assertFalse(self.r.accept(self.f(2,BEGIN,0)));self.assertEqual(self.r.seq,0)
  self.assertTrue(self.r.accept(self.f(1,BEGIN,0)))
  b=bytearray(self.f(2,DATA,0,self.blob[:CAP]));b[200]^=1;self.assertFalse(self.r.accept(bytes(b)))
  self.assertFalse(self.r.accept(self.f(2,DATA,1,self.blob[:CAP])))
  self.assertFalse(self.r.accept(self.f(1,DATA,0,self.blob[:CAP])))
  self.assertEqual(self.r.received,0)
 def test_no_clobber(self):
  self.r.dest.write_bytes(b'keep');self.assertFalse(self.r.accept(self.f(1,BEGIN,0)));self.assertEqual(self.r.dest.read_bytes(),b'keep')
 def test_existing_partial(self):
  self.r.part.write_bytes(b'keep');self.assertFalse(self.r.accept(self.f(1,BEGIN,0)));self.assertEqual(self.r.part.read_bytes(),b'keep')
 def test_no_file_before_valid_begin(self):
  self.assertFalse(self.r.accept(self.f(1,DATA,0,self.blob[:CAP])));self.assertFalse(self.r.part.exists())
 def test_final_hash_mismatch_preserves_partial(self):
  self.assertTrue(self.r.accept(self.f(1,BEGIN,0)))
  off=0;seq=2
  while off<len(self.blob):
   chunk=bytes([0])*min(CAP,len(self.blob)-off)
   self.assertTrue(self.r.accept(self.f(seq,DATA,off,chunk)));off+=len(chunk);seq+=1
  self.assertFalse(self.r.accept(self.f(seq,END,off)));self.assertTrue(self.r.failed);self.assertFalse(self.r.dest.exists());self.assertTrue(self.r.part.exists())
 def test_overlay_boundaries(self):
  b=b'Z'*(3*BLOCK);out=self.r.overlay(175,3,b);self.assertEqual(out[:BLOCK],b[:BLOCK]);self.assertEqual(out[BLOCK:2*BLOCK],self.r.ack);self.assertEqual(out[2*BLOCK:],b[2*BLOCK:])
if __name__=='__main__':unittest.main()
