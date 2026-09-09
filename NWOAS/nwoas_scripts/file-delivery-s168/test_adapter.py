import sys,struct,tempfile,unittest,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'nvme-s124'),str(ROOT/'transport-s123')]
from transport import LinkNamespace,NamespacePair,fat_image,CAPACITY,PORT
from readonly_namespace import SUCCESS,INVALID_FIELD,WRITE_TO_RO,Result
from channel import REQ_MAGIC,SENTINEL
from adapter import ArtifactLink

def cmd(op,lba,n=1,ns=1,addr=0x10000):
 b=bytearray(64);b[0]=op;struct.pack_into('<I',b,4,ns);struct.pack_into('<Q',b,24,addr)
 struct.pack_into('<Q',b,40,lba);struct.pack_into('<I',b,48,n-1);return bytes(b)
def request(token):
 b=bytearray(4096);b[:16]=REQ_MAGIC;b[16:32]=token
 struct.pack_into('<IQI',b,32,1,SENTINEL,0);struct.pack_into('<I',b,48,zlib.crc32(b[:48]));return bytes(b)
class Memory:
 def __init__(self,data):self.data=data
 def contains(self,a,n):return 0x10000<=a and a+n<=0x10000+len(self.data)
 def read(self,a,n):return self.data[a-0x10000:a-0x10000+n]
class Primary:
 def __init__(self):self.calls=[]
 def io(self,c,m=None):self.calls.append(c);return Result(0)
class Tests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.base=LinkNamespace(fat_image({'TEST.TXT':b'abc'}),self.tmp.name)
  self.link=ArtifactLink(self.base,b'official-test-content');self.mem=Memory(request(self.base.token))
 def tearDown(self):self.tmp.cleanup()
 def test_request_and_bound_read_callback(self):
  self.assertEqual(self.link.io(cmd(1,127),self.mem).status,SUCCESS)
  self.assertEqual(self.link.io(cmd(2,129,16)).data,self.link.channel.response_window())
  self.assertEqual(self.base.read_block.__self__,self.base) # Existing bound callback deliberately unchanged.
 def test_port_fat_capacity_preserved(self):
  before=bytes(self.base.image);frame=self.base.frame()
  self.link.io(cmd(1,127),self.mem)
  self.assertEqual(bytes(self.base.image),before);self.assertEqual(self.base.frame(),frame)
  mixed=self.link.io(cmd(2,128,16));self.assertEqual(mixed.data[:4096],frame)
  self.assertEqual(mixed.data[4096:],self.link.channel.response_window()[:15*4096])
  self.assertEqual(self.link.block_count,CAPACITY)
  self.assertEqual(self.link.io(cmd(2,256,16)).data,self.base.io(cmd(2,256,16)).data)
 def test_reject_gap_straddles(self):
  for lba,n in [(126,2),(127,2),(129,1),(144,1),(255,2),(0,1)]:
   self.assertEqual(self.link.io(cmd(1,lba,n),self.mem).status,WRITE_TO_RO)
 def test_invalid_request_and_prp(self):
  self.assertEqual(self.link.io(cmd(1,127),Memory(bytes(4096))).status,INVALID_FIELD)
  self.assertEqual(self.link.io(cmd(1,127,addr=0),self.mem).status,INVALID_FIELD)
  b=bytearray(cmd(1,127));b[49]=1
  self.assertNotEqual(self.link.io(bytes(b),self.mem).status,SUCCESS)
  self.assertEqual(self.link.io(cmd(1,127),None).status,INVALID_FIELD)
 def test_namespace_dispatch_isolation(self):
  primary=Primary();pair=NamespacePair(primary,self.link)
  self.assertEqual(pair.io(cmd(1,127,ns=2),self.mem).status,SUCCESS)
  self.assertEqual(primary.calls,[])
  pair.io(cmd(2,10,ns=1));self.assertEqual(len(primary.calls),1)
 def test_original_partition_write_remains_ram_only(self):
  data=Memory(b'Z'*4096);self.assertEqual(self.link.io(cmd(1,300),data).status,SUCCESS)
  self.assertEqual(self.link.io(cmd(2,300)).data,b'Z'*4096)
if __name__=='__main__':unittest.main()
