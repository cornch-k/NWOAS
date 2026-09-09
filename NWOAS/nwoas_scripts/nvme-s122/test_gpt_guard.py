import unittest,struct,zlib,uuid,os
from gpt_guard import GptGuard,GptViolation,BLOCK
RUN=os.path.join(os.path.dirname(__file__),'..','ans2-s87','run-20260907-160641')
ZONE=(53839104,59968511);NB=61279344
def load(l):
 with open(os.path.join(RUN,f'lba-{l}.bin'),'rb') as f:return f.read()
class Disk:
 def __init__(self):
  self.b={l:load(l) for l in (0,1,2,3,4,5,61279343)}
  ents=b''.join(self.b[l] for l in (2,3,4,5))
  for i in range(4):self.b[61279339+i]=ents[i*BLOCK:(i+1)*BLOCK]
  self.writes=[]
 def read(self,lba,n=1):return b''.join(self.b.get(l,bytes(BLOCK)) for l in range(lba,lba+n))
 def write(self,lba,data):self.writes.append(lba);self.b[lba]=bytes(data)
def entries_of(d):return bytearray(d.read(2,4))
def hdr_with(base,ents,cur,alt,ent_lba):
 h=bytearray(base);struct.pack_into('<QQ',h,24,cur,alt);struct.pack_into('<Q',h,72,ent_lba)
 struct.pack_into('<I',h,88,zlib.crc32(bytes(ents[:128*128])));struct.pack_into('<I',h,16,0)
 struct.pack_into('<I',h,16,zlib.crc32(bytes(h[:92])));return bytes(h)
def entry(type_guid,start,end,name='X'):
 e=bytearray(128);e[:16]=uuid.UUID(type_guid).bytes_le;e[16:32]=uuid.uuid4().bytes_le
 struct.pack_into('<QQQ',e,32,start,end,0);e[56:56+len(name)*2]=name.encode('utf-16le');return bytes(e)
ESP='c12a7328-f81f-11d2-ba4b-00a0c93ec93b';MSR='e3c9e316-0b5c-4db8-817d-f92df00215ae';DATA='ebd0a0a2-b9e5-4433-87c0-68b6b72699c7'
class Test(unittest.TestCase):
 def setUp(self):
  self.d=Disk();ents=entries_of(self.d)
  self.prot=[bytes(ents[i*128:(i+1)*128]) for i in (0,1,3)]
  self.g=GptGuard(self.d.read,self.d.write,NB,*ZONE,self.prot)
 def commit_primary(self,ents):
  h=hdr_with(self.d.b[1],ents,1,61279343,2)
  for i in range(4):self.g.stage(2+i,bytes(ents[i*BLOCK:(i+1)*BLOCK]))
  self.g.stage(1,h)
 def test_windows_style_repartition_commits(self):
  ents=entries_of(self.d);z=ZONE[0]
  ents[2*128:3*128]=entry(ESP,z,z+25599,'EFI system partition')
  ents[4*128:5*128]=entry(MSR,z+25600,z+29695,'Microsoft reserved partition')
  ents[5*128:6*128]=entry(DATA,z+29696,ZONE[1],'Basic data partition')
  self.assertEqual(self.d.writes,[])
  self.commit_primary(ents)
  self.assertEqual(sorted(self.d.writes),[1,2,3,4,5]);self.assertEqual(self.g.shadow,{})
  self.assertEqual(self.d.read(2,4),bytes(ents))
 def test_partial_stage_reads_shadow_and_writes_nothing(self):
  ents=entries_of(self.d);ents[2*128:3*128]=entry(DATA,ZONE[0],ZONE[1])
  self.g.stage(2,bytes(ents[:BLOCK]))
  self.assertEqual(self.d.writes,[]);self.assertEqual(self.g.read_overlay(2,1,self.d.read(2))[:BLOCK],bytes(ents[:BLOCK]))
  self.assertNotEqual(self.d.read(2),bytes(ents[:BLOCK]))
 def test_apfs_entry_change_rejected(self):
  ents=entries_of(self.d);struct.pack_into('<Q',ents,1*128+40,53838942+4096)
  with self.assertRaises(GptViolation):self.commit_primary(ents)
  self.assertEqual(self.d.writes,[]);self.assertEqual(self.g.shadow,{})
 def test_protected_deleted_rejected(self):
  ents=entries_of(self.d);ents[3*128:4*128]=bytes(128)
  with self.assertRaises(GptViolation):self.commit_primary(ents)
  self.assertEqual(self.d.writes,[])
 def test_entry_outside_zone_rejected(self):
  for s,e in ((ZONE[0]-1,ZONE[1]),(ZONE[0],ZONE[1]+1),(6,100),(59968630,61279338)):
   ents=entries_of(self.d);ents[2*128:3*128]=entry(DATA,s,e)
   with self.assertRaises(GptViolation):self.commit_primary(ents)
  self.assertEqual(self.d.writes,[])
 def test_header_geometry_change_rejected(self):
  ents=entries_of(self.d);h=bytearray(hdr_with(self.d.b[1],ents,1,61279343,2))
  struct.pack_into('<Q',h,40,7);struct.pack_into('<I',h,16,0);struct.pack_into('<I',h,16,zlib.crc32(bytes(h[:92])))
  with self.assertRaises(GptViolation):self.g.stage(1,bytes(h))
  self.assertEqual(self.d.writes,[])
 def test_backup_set_commits_independently(self):
  ents=entries_of(self.d);ents[2*128:3*128]=entry(DATA,ZONE[0],ZONE[1])
  bh=hdr_with(self.d.b[61279343],ents,61279343,1,61279339)
  self.g.stage(61279343,bh)
  for i in range(4):self.g.stage(61279339+i,bytes(ents[i*BLOCK:(i+1)*BLOCK]))
  self.assertEqual(sorted(self.d.writes),[61279339,61279340,61279341,61279342,61279343])
 def test_mbr(self):
  m=bytearray(self.d.b[0]);self.g.stage(0,bytes(m));self.assertEqual(self.d.writes,[0])
  m[450]=0x07
  with self.assertRaises(GptViolation):self.g.stage(0,bytes(m))
 def test_init_refuses_when_disk_already_violates(self):
  d=Disk();ents=entries_of(d);ents[2*128:3*128]=entry(DATA,6,100);d.b[2]=bytes(ents[:BLOCK])
  h=hdr_with(d.b[1],ents,1,61279343,2);d.b[1]=h
  with self.assertRaises(GptViolation):GptGuard(d.read,d.write,NB,*ZONE,self.prot)
if __name__=='__main__':unittest.main()
class TestDup(unittest.TestCase):
 def test_duplicate_protected_rejected(self):
  d=Disk();ents=entries_of(d);prot=[bytes(ents[i*128:(i+1)*128]) for i in (0,1,3)]
  g=GptGuard(d.read,d.write,NB,*ZONE,prot)
  ents[3*128:4*128]=prot[0] # RecoveryOS slot overwritten by an ISC copy
  h=hdr_with(d.b[1],ents,1,61279343,2)
  for i in range(4):g.stage(2+i,bytes(ents[i*BLOCK:(i+1)*BLOCK]))
  with self.assertRaises(GptViolation):g.stage(1,h)
  self.assertEqual(d.writes,[])
 def test_commit_write_failure_keeps_shadow(self):
  d=Disk();ents=entries_of(d);prot=[bytes(ents[i*128:(i+1)*128]) for i in (0,1,3)]
  calls=[]
  def w(l,b):
   calls.append(l)
   if len(calls)==3:raise OSError('ans2')
   d.write(l,b)
  g=GptGuard(d.read,w,NB,*ZONE,prot);ents[2*128:3*128]=entry(DATA,ZONE[0],ZONE[1])
  for i in range(4):g.stage(2+i,bytes(ents[i*BLOCK:(i+1)*BLOCK]))
  with self.assertRaises(OSError):g.stage(1,hdr_with(d.b[1],ents,1,61279343,2))
  self.assertEqual(calls,[2,3,4]);self.assertIn(4,g.shadow);self.assertIn(1,g.shadow);self.assertNotIn(1,d.writes)
