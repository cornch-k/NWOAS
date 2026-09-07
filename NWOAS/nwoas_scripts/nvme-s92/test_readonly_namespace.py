import unittest,struct
from readonly_namespace import *
def cmd(op=2,nsid=1,lba=0,count=1,cns=None):
 b=bytearray(64);b[0]=op;struct.pack_into('<I',b,4,nsid)
 if cns is not None:struct.pack_into('<I',b,40,cns)
 else:struct.pack_into('<QH',b,40,lba,count-1)
 return bytes(b)
class NamespaceTests(unittest.TestCase):
 def setUp(self):
  self.calls=[]
  def read(n):self.calls.append(n);return bytes([n])*4096
  self.ns=ReadOnlyNamespace(32,read)
 def test_all_nonread_opcodes_cannot_reach_backend(self):
  for op in range(256):
   if op!=2:self.assertNotEqual(self.ns.io(cmd(op=op)).status,0)
  self.assertEqual(self.calls,[])
 def test_last_block_and_bounds(self):
  self.assertEqual(self.ns.io(cmd(lba=31)).data,bytes([31])*4096)
  for lba,n in [(31,2),(32,1),(2**64-1,1)]:
   self.assertEqual(self.ns.io(cmd(lba=lba,count=n)).status,LBA_RANGE)
  self.assertEqual(self.calls,[31])
 def test_transfer_limit_and_invalid_namespace(self):
  self.assertEqual(self.ns.io(cmd(count=17)).status,INVALID_FIELD)
  self.assertEqual(self.ns.io(cmd(nsid=2)).status,INVALID_NSID)
  self.assertEqual(self.calls,[])
 def test_multiblock_exact(self):
  self.assertEqual(self.ns.io(cmd(lba=3,count=2)).data,bytes([3])*4096+bytes([4])*4096)
 def test_namespace_format_and_capacity(self):
  b=self.ns.admin(cmd(op=6,cns=0)).data
  self.assertEqual(len(b),4096);self.assertEqual(struct.unpack_from('<QQQ',b),(32,32,32));self.assertEqual(b[130],12)
 def test_identify_controller_and_active_list(self):
  self.assertEqual(len(self.ns.admin(cmd(op=6,nsid=0,cns=1)).data),4096)
  self.assertEqual(self.ns.admin(cmd(op=6,nsid=0,cns=2)).data[:8],b'\1\0\0\0\0\0\0\0')
 def test_bad_command_and_metadata(self):
  self.assertEqual(self.ns.io(b'').status,INVALID_FIELD)
  b=bytearray(cmd());b[16]=1;self.assertEqual(self.ns.io(b).status,INVALID_FIELD)
 def test_partial_read_failure_returns_no_data(self):
  ns=ReadOnlyNamespace(32,lambda n:b'X'*4096 if n==0 else b'bad')
  r=ns.io(cmd(count=2));self.assertEqual(r.status,READ_ERROR);self.assertEqual(r.data,b'')
if __name__=='__main__':unittest.main()
