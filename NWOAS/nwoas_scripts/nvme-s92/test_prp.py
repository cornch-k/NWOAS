import unittest,struct
from prp import *
class PRPTests(unittest.TestCase):
 def setUp(self):self.reads=[]
 def valid(self,a,n):return 0x1000<=a and a+n<=0x30000
 def read(self,a,n):self.reads.append((a,n));return struct.pack('<QQ',0x5000,0x9000)[:n]
 def test_single(self):self.assertEqual(resolve(0x1ffc,0,4,self.valid,self.read),[(0x1ffc,4)])
 def test_unaligned_first_with_second(self):self.assertEqual(resolve(0x1ffc,0x4000,4096,self.valid,self.read),[(0x1ffc,4),(0x4000,4092)])
 def test_list(self):self.assertEqual(resolve(0x2000,0x3000,12288,self.valid,self.read),[(0x2000,4096),(0x5000,4096),(0x9000,4096)])
 def test_bad_descriptor_cannot_be_read(self):
  for a in [0,0x30000,0x690000000]:
   with self.assertRaises(InvalidPRP):resolve(0x2000,a,12288,self.valid,self.read)
  self.assertEqual(self.reads,[])
 def test_bad_data_pointer(self):
  with self.assertRaises(InvalidPRP):resolve(0x2000,0x3000,8193,self.valid,lambda a,n:struct.pack('<QQ',0x5000,0x690000000))
 def test_overflow_and_limit(self):
  for a,n in [(2**64-4,8),(0x1000,65537),(0x1000,0),(0x1001,4)]:
   with self.assertRaises(InvalidPRP):resolve(a,0,n,self.valid,self.read)
if __name__=='__main__':unittest.main()
