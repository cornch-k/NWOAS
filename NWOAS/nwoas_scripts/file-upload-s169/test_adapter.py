import sys,struct,tempfile,unittest,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'nvme-s124'),str(ROOT/'transport-s123')]
from transport import LinkNamespace,fat_image
from readonly_namespace import SUCCESS,WRITE_TO_RO,INVALID_FIELD
from receiver import Receiver,BLOCK
from upload_adapter import UploadLink
from test_receiver import frame,BEGIN

def cmd(op,lba,n=1):
 b=bytearray(64);b[0]=op;struct.pack_into('<I',b,4,1);struct.pack_into('<QQ',b,24,0x20000,0x10000);struct.pack_into('<Q',b,40,lba);struct.pack_into('<I',b,48,n-1);return bytes(b)
class Memory:
 def __init__(self,b):
  self.b=bytearray(0x20000);self.b[0x10000:0x20000]=b
  for i in range(15):struct.pack_into('<Q',self.b,i*8,0x21000+4096*i)
 def contains(self,a,n):return a>=0x10000 and a+n<=0x30000
 def read(self,a,n):return bytes(self.b[a-0x10000:a-0x10000+n])
class Tests(unittest.TestCase):
 def setUp(self):
  self.t=tempfile.TemporaryDirectory();p=Path(self.t.name)
  self.base=LinkNamespace(fat_image({'TEST.TXT':b'ok'}),p/'link');self.blob=b'test'
  self.r=Receiver(self.base.token,4,hashlib.sha256(self.blob).hexdigest(),p/'backup');self.link=UploadLink(self.base,self.r)
  self.mem=Memory(frame(self.base.token,self.blob,1,BEGIN,0))
 def tearDown(self):self.r.close();self.t.cleanup()
 def test_exact_64k_prp_request(self):
  self.assertEqual(self.link.io(cmd(1,160,16),self.mem).status,SUCCESS)
  self.assertEqual(self.link.io(cmd(2,176)).data,self.r.ack)
 def test_guard_unchanged(self):
  for l,n in [(159,16),(160,1),(160,15),(160,17),(176,1),(127,1),(128,2)]:
   self.assertNotEqual(self.link.io(cmd(1,l,n),self.mem).status,SUCCESS)
 def test_original_reads_preserved(self):
  for l,n in [(0,1),(127,2),(129,16),(256,16)]:self.assertEqual(self.link.io(cmd(2,l,n)).data,self.base.io(cmd(2,l,n)).data)
 def test_bad_prp_no_file(self):
  c=bytearray(cmd(1,160,16));struct.pack_into('<Q',c,32,0)
  self.assertEqual(self.link.io(bytes(c),self.mem).status,INVALID_FIELD);self.assertFalse(self.r.part.exists())
if __name__=='__main__':unittest.main()
