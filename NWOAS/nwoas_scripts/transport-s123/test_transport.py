import json,struct,tempfile,unittest,zlib
from pathlib import Path
from transport import *
from test_controller import Memory

def cmd(op,ns=2,lba=128,n=1):
    b=bytearray(64);b[0]=op;struct.pack_into('<I',b,4,ns)
    struct.pack_into('<QQ',b,24,0x8000,0x9000)
    struct.pack_into('<III',b,40,lba,0,n-1)
    return bytes(b)

class Primary:
    def __init__(self):self.calls=[]
    def io(self,c,m=None):self.calls.append(c);return Result(SUCCESS,b'primary')
    def admin(self,c):return Result(SUCCESS,bytes(4096))

class Test(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.link=LinkNamespace(fat_image({'TEST.TXT':b'hello'}),self.tmp.name)
        self.primary=Primary();self.pair=NamespacePair(self.primary,self.link);self.mem=Memory()
    def packet(self,kind=4,job=0,seq=1,body=b'hello'):
        b=bytearray(4096);b[:16]=MAGIC;b[16:32]=self.link.token
        struct.pack_into('<6I',b,32,job,kind,len(body),zlib.crc32(body),seq,0);b[64:64+len(body)]=body
        return bytes(b)
    def send(self,b,lba=128,n=1):
        self.mem.write(0x8000,b);return self.pair.io(cmd(1,lba=lba,n=n),self.mem).status
    def test_ram_only_and_overlap_rejection(self):
        for lba,n in [(0,1),(127,2),(128,2),(129,1),(255,2),(8448,1)]:
            self.assertNotEqual(self.send(self.packet(),lba,n),0)
        self.assertEqual(self.send(self.packet()),0);self.assertEqual(self.primary.calls,[])
    def test_invalid_token_crc_sequence(self):
        for at in (0,16,44,48,100):
            b=bytearray(self.packet());b[at]^=1;self.assertNotEqual(self.send(bytes(b)),0)
        self.assertEqual(self.link.ack,0);self.assertFalse(self.primary.calls)
    def test_duplicate_idempotent_and_restart_rejected(self):
        b=self.packet();self.assertEqual(self.send(b),0);self.assertEqual(self.send(b),0)
        self.assertNotEqual(self.send(self.packet(seq=2)),0)
        events=Path(self.tmp.name,'events.jsonl').read_text().splitlines();self.assertEqual(len(events),1)
    def test_job_output_completion_no_replay(self):
        self.send(self.packet());Path(self.tmp.name,'job.json').write_text(json.dumps({'id':1,'script':'ver\r\n'}))
        f=self.link.frame();self.assertEqual(f[64:69],b'ver\r\n')
        self.assertEqual(self.send(self.packet(2,1,2,b'abc')),0)
        self.assertEqual(self.send(self.packet(3,1,3,b'complete')),0)
        self.assertEqual(Path(self.tmp.name,'job-1.log').read_bytes(),b'abc')
        self.assertEqual(struct.unpack_from('<I',self.link.frame(),36)[0],0)
        self.assertNotEqual(self.send(self.packet(2,1,4,b'abc')),0)
    def test_first_namespace_passthrough(self):
        c=cmd(2,ns=1);self.assertEqual(self.pair.io(c).data,b'primary');self.assertEqual(self.primary.calls,[c])
        self.assertEqual(self.pair.io(cmd(2,ns=3)).status,INVALID_NSID)
    def test_enumeration(self):
        c=bytearray(cmd(6,ns=0));struct.pack_into('<6I',c,40,2,0,0,0,0,0)
        self.assertEqual(struct.unpack_from('<III',self.pair.admin(c).data), (1,2,0))
        struct.pack_into('<I',c,4,1);self.assertEqual(struct.unpack_from('<II',self.pair.admin(c).data),(2,0))
        struct.pack_into('<I',c,40,0);struct.pack_into('<I',c,4,2)
        d=self.pair.admin(c).data;self.assertEqual(struct.unpack_from('<Q',d)[0],CAPACITY);self.assertEqual(d[130],12)
    def test_fat_geometry_and_payload(self):
        b=self.link.image;self.assertEqual(len(b),CAPACITY*4096)
        self.assertEqual(struct.unpack_from('<II',b,454),(256,8192))
        self.assertEqual(struct.unpack_from('<H',b,256*4096+11)[0],4096)
        self.assertEqual(b[269*4096:269*4096+5],b'hello')
    def test_mailbox_read_spanning(self):
        r=self.pair.io(cmd(2,lba=127,n=2));self.assertEqual(len(r.data),8192);self.assertEqual(r.data[4096:4112],MAGIC)
    def test_invalid_prp_no_receive(self):
        c=bytearray(cmd(1));struct.pack_into('<Q',c,24,0x700100000)
        self.assertNotEqual(self.pair.io(c,self.mem).status,0);self.assertEqual(self.link.ack,0)
    def test_filesystem_metadata_writes_are_ram_only(self):
        before=bytes(self.link.image[:256*4096]);data=b'M'*4096
        self.assertEqual(self.send(data,lba=265),0)
        self.assertEqual(self.link.read(265,1),data)
        self.assertEqual(bytes(self.link.image[:256*4096]),before)
        self.assertEqual(self.primary.calls,[]);self.assertEqual(self.link.ack,0)
    def test_partition_end_and_frame_untouched(self):
        self.assertEqual(self.send(b'Z'*4096,lba=CAPACITY-1),0)
        self.assertNotEqual(self.send(b'Z'*4096,lba=CAPACITY-1,n=2),0)
        self.assertEqual(self.link.frame()[:16],MAGIC);self.assertEqual(self.primary.calls,[])

if __name__=='__main__':unittest.main()
