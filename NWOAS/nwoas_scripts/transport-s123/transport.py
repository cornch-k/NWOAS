"""S123: second NVMe namespace, entirely host RAM; no physical backend reference.

FAT16 delivery volume plus one unpartitioned mailbox sector. NS1 stays untouched.
Mailbox writes NEVER go to a physical disk. Invalid or overlapping writes fail.
"""
import json
import secrets
import struct
import zlib
from pathlib import Path
from readonly_namespace import ReadOnlyNamespace, Result, SUCCESS, INVALID_FIELD, INVALID_NSID, WRITE_TO_RO
from prp import resolve, InvalidPRP

BLOCK=4096
PORT=128
MAGIC=b'NWOAS-S123-LINK!'
CAPACITY=8448

def fat_image(files):
    """32 MiB FAT16 partition, 4K sectors; MBR start=256, mailbox outside it."""
    b=bytearray(CAPACITY*BLOCK)
    struct.pack_into('<I', b, 440, 0x53313233)
    b[446:462]=struct.pack('<B3sB3sII',0,b'\xfe\xff\xff',6,b'\xfe\xff\xff',256,8192)
    b[510:512]=b'\x55\xaa'
    boot=256*BLOCK
    b[boot:boot+11]=b'\xeb\x3c\x90NWOAS123'
    struct.pack_into('<HBHBHHBHHHII',b,boot+11,4096,1,1,2,512,8192,0xf8,4,63,255,256,0)
    b[boot+36:boot+39]=b'\x80\0\x29'
    struct.pack_into('<I',b,boot+39,0x53313233)
    b[boot+43:boot+62]=b'NWOASLINK  FAT16   '
    b[boot+510:boot+512]=b'\x55\xaa'
    fat=bytearray(4*BLOCK);struct.pack_into('<HH',fat,0,0xfff8,0xffff)
    root=(256+1+8)*BLOCK; data=(256+1+8+4)*BLOCK
    b[root:root+11]=b'NWOASLINK  ';b[root+11]=8
    cluster=2
    for ix,(name,content) in enumerate(files.items(),1):
        stem,ext=name.split('.')
        assert len(stem)<=8 and len(ext)<=3 and ix<512
        ent=root+ix*32;name83=(stem.ljust(8)+ext.ljust(3)).encode('ascii')
        b[ent:ent+11]=name83;b[ent+11]=0x21
        struct.pack_into('<H',b,ent+26,cluster)
        struct.pack_into('<I',b,ent+28,len(content))
        count=max(1,(len(content)+BLOCK-1)//BLOCK)
        assert cluster+count<8181
        at=data+(cluster-2)*BLOCK;b[at:at+len(content)]=content
        for c in range(cluster,cluster+count):struct.pack_into('<H',fat,2*c,0xffff if c==cluster+count-1 else c+1)
        cluster+=count
    b[(257)*BLOCK:261*BLOCK]=fat;b[261*BLOCK:265*BLOCK]=fat
    assert len(b)==CAPACITY*BLOCK
    return bytes(b)

class LinkNamespace(ReadOnlyNamespace):
    def __init__(self,image,directory):
        if len(image)!=CAPACITY*BLOCK:raise ValueError('bad image')
        self.image=bytearray(image);self.directory=Path(directory);self.directory.mkdir(parents=True,exist_ok=True)
        self.token=secrets.token_bytes(16);self.ack=0;self.last=None;self.job=None;self.connected=False;self.completed=set()
        super().__init__(CAPACITY,self.read)
        (self.directory/'session.json').write_text(json.dumps({'token':self.token.hex(),'port':PORT}))

    def frame(self):
        path=self.directory/'job.json'
        if path.exists():
            job=json.loads(path.read_text())
            text=job['script'].encode('ascii');jid=job['id']
            if not isinstance(jid,int) or not 1<=jid<2**32 or not 0<len(text)<=4032:raise ValueError('bad job')
            if self.job and (jid<self.job['id'] or (jid==self.job['id'] and job!=self.job)):raise ValueError('job ID reused')
            self.job=job
        pending=self.job and self.job['id'] not in self.completed
        body=self.job['script'].encode('ascii') if pending else b''
        out=bytearray(BLOCK);out[:16]=MAGIC;out[16:32]=self.token
        struct.pack_into('<6I',out,32,self.job['id'] if pending else 0,1 if pending else 0,len(body),zlib.crc32(body),self.ack,0)
        out[64:64+len(body)]=body
        return bytes(out)

    def read(self,lba,n):
        out=bytearray(self.image[lba*BLOCK:(lba+n)*BLOCK])
        if lba<=PORT<lba+n:out[(PORT-lba)*BLOCK:(PORT-lba+1)*BLOCK]=self.frame()
        return bytes(out)

    def receive(self,data):
        if len(data)!=BLOCK or data[:16]!=MAGIC or data[16:32]!=self.token:return False
        jid,kind,n,crc,seq,rc=struct.unpack_from('<6I',data,32)
        body=data[64:64+n]
        if n>4032 or zlib.crc32(body)!=crc or kind not in (2,3,4):return False
        if any(data[56:64]) or any(data[64+n:]):return False
        if kind==4:
            if jid!=0:return False
        elif self.job is None or jid!=self.job['id']:return False
        if seq==self.ack:return data==self.last
        if seq!=self.ack+1:return False
        if kind==4 and self.connected:return False # one worker per boot, no implicit task replay
        if kind!=4 and (not self.connected or jid in self.completed):return False
        if kind==2:
            with (self.directory/f'job-{jid}.log').open('ab') as f:f.write(body)
        else:
            with (self.directory/'events.jsonl').open('a') as f:f.write(json.dumps({'job':jid,'kind':kind,'exit':rc,'message':body.decode('utf-8','replace')})+'\n')
        self.ack=seq;self.last=data
        if kind==4:self.connected=True
        if kind==3:self.completed.add(jid)
        return True

    def io(self,command,mem=None):
        fields=self._fields(command)
        if fields is None:return Result(INVALID_FIELD)
        op,ns,cdw=fields
        if ns!=1:return Result(INVALID_NSID)
        if op==0:return Result(SUCCESS if not any(cdw) else INVALID_FIELD)
        if op!=1:return super().io(command,mem)
        lba=cdw[0]|(cdw[1]<<32);n=(cdw[2]&0xffff)+1
        if cdw[2]&0x3fff0000 or cdw[3]&~0xff or any(cdw[4:]) or n>16:return Result(INVALID_FIELD)
        mailbox=lba==PORT and n==1
        # S126: Windows FAT lazily updates access dates/directory metadata, even
        # for read-only files. A rejected flush can hold raw-disk mailbox I/O.
        # Accept partition writes in this RAM image only. MBR/gap stay immutable;
        # mailbox-straddling writes still fail. No physical backend is referenced.
        if not mailbox and not (256<=lba and lba+n<=CAPACITY):return Result(WRITE_TO_RO)
        if mem is None:return Result(INVALID_FIELD)
        try:
            spans=resolve(*struct.unpack_from('<QQ',command,24),n*BLOCK,mem.contains,mem.read)
            data=b''.join(mem.read(a,n) for a,n in spans)
        except InvalidPRP:return Result(INVALID_FIELD)
        if len(data)!=n*BLOCK:return Result(INVALID_FIELD)
        if mailbox:return Result(SUCCESS if self.receive(data) else INVALID_FIELD)
        self.image[lba*BLOCK:(lba+n)*BLOCK]=data
        return Result(SUCCESS)

class NamespacePair:
    def __init__(self,primary,link):self.primary,self.link=primary,link
    def admin(self,command):
        f=ReadOnlyNamespace._fields(command)
        if f is None:return Result(INVALID_FIELD)
        op,ns,cdw=f
        if op!=6 or cdw[0]>>8 or any(cdw[1:]):return Result(INVALID_FIELD)
        cns=cdw[0]&255
        if cns==2:
            b=bytearray(BLOCK);ids=[x for x in (1,2) if x>ns]
            for i,v in enumerate(ids):struct.pack_into('<I',b,4*i,v)
            return Result(SUCCESS,bytes(b))
        if cns==0 and ns==2:
            c=bytearray(command);struct.pack_into('<I',c,4,1)
            r=self.link.admin(bytes(c));b=bytearray(r.data)
            # S126: writable RAM FAT partition plus guarded raw mailbox.
            if r.status==SUCCESS:b[99]=0
            return Result(r.status,bytes(b))
        r=self.primary.admin(command)
        if cns==1 and r.status==SUCCESS:
            b=bytearray(r.data);struct.pack_into('<I',b,516,2);return Result(SUCCESS,bytes(b))
        return r
    def io(self,command,mem=None):
        if len(command)!=64:return Result(INVALID_FIELD)
        ns=struct.unpack_from('<I',command,4)[0]
        if ns==1:return self.primary.io(command,mem)
        if ns==2:
            c=bytearray(command);struct.pack_into('<I',c,4,1);return self.link.io(bytes(c),mem)
        return Result(INVALID_NSID)
