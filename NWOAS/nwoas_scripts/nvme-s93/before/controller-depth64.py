"""S93 experimental read-only NVMe transport. No physical storage write API.
PCI segment1, INTx only; bounded contiguous queues and validated PRP DMA.
NVMe 1.3 register/command layouts: NVM Express spec and QEMU include/block/nvme.h.
"""
import struct
from dataclasses import dataclass
from readonly_namespace import Result, SUCCESS, INVALID_FIELD, INVALID_OPCODE
from prp import resolve, InvalidPRP

@dataclass
class SQ:
    base:int
    size:int
    cqid:int
    head:int=0
    tail:int=0

@dataclass
class CQ:
    base:int
    size:int
    ien:bool=True
    head:int=0
    tail:int=0
    phase:int=1
    pending:int=0

class Controller:
    MAX_Q=4
    MAX_DEPTH=64
    BAR_SIZE=0x4000
    def __init__(self,namespace,memory,irq,log=lambda x:None,bar=0x700100000):
        self.ns,self.mem,self.irq,self.log=namespace,memory,irq,log
        self.bar=bar
        self.config=bytearray(4096)
        struct.pack_into('<HH',self.config,0,0x1234,0x0010)
        self.config[8:12]=bytes([1,2,8,1]) # rev, NVMe prog-if, subclass, storage
        struct.pack_into('<HH',self.config,0x2c,0x1234,0x0010)
        self.config[0x3d]=1 # INTA, no MSI capability
        self.probe_low=self.probe_high=False
        self.command=0
        self.reset()

    def reset(self):
        self.cc=self.csts=self.aqa=self.asq=self.acq=self.mask=0
        self.sq={};self.cq={};self.features={};self.aer=[]
        self.irq(False)

    def update_irq(self):
        pending=any(q.ien and q.pending for q in self.cq.values())
        self.irq(bool(pending and not(self.mask&1) and not(self.command&0x400)))

    def pci_read(self,offset,width):
        n=width//8
        if offset<0 or offset+n>4096:return (1<<width)-1
        cfg=bytearray(self.config)
        pending=any(q.ien and q.pending for q in self.cq.values())
        struct.pack_into('<HH',cfg,4,self.command,8 if pending else 0)
        struct.pack_into('<II',cfg,0x10,(0xffffc004 if self.probe_low else (self.bar&0xffffffff)|4),0xffffffff if self.probe_high else self.bar>>32)
        return int.from_bytes(cfg[offset:offset+n],'little')

    def pci_write(self,offset,value,width):
        n=width//8
        if offset<=4<offset+n or offset==4:
            b=bytearray(struct.pack('<H',self.command))
            raw=value.to_bytes(n,'little')
            for i in range(n):
                if 4<=offset+i<6:b[offset+i-4]=raw[i]
            self.command=int.from_bytes(b,'little')&0x407
            self.update_irq()
        elif offset in (0x10,0x14) and width==32:
            probe=value==0xffffffff
            if offset==0x10:self.probe_low=probe
            else:self.probe_high=probe
            if not probe:
                expected=((self.bar&0xffffffff)|4) if offset==0x10 else self.bar>>32
                if value!=expected:self.log(f'BAR relocation unsupported off={offset:x} value={value:x} expected={expected:x}')

    def read(self,offset,width):
        b=bytearray(self.BAR_SIZE)
        # MQES63, CQR1, timeout10s, NVM command set, 4KiB pages.
        struct.pack_into('<Q',b,0,63|(1<<16)|(20<<24)|(1<<37))
        struct.pack_into('<I',b,8,0x10300)
        for off,val in ((0xc,self.mask),(0x10,self.mask),(0x14,self.cc),(0x1c,self.csts),(0x24,self.aqa)):
            struct.pack_into('<I',b,off,val)
        struct.pack_into('<QQ',b,0x28,self.asq,self.acq)
        if offset<0 or offset+width//8>len(b):return 0
        return int.from_bytes(b[offset:offset+width//8],'little')

    def fatal(self,why):
        self.csts|=2;self.log('CFS '+why);self.irq(False)

    def write(self,offset,value,width):
        if width not in (32,64) or offset&3:return
        value&=(1<<width)-1
        if offset>=0x1000:
            if width!=32 or offset>=0x1000+8*(self.MAX_Q+1):return
            qid=(offset-0x1000)//8
            if not(self.csts&1) or self.csts&2:return
            if offset&4:
                q=self.cq.get(qid)
                if q is None or value>=q.size:return self.fatal('invalid CQ head')
                consumed=(value-q.head)%q.size
                if consumed>q.pending:return self.fatal('CQ head advances beyond completions')
                q.head=value;q.pending-=consumed
                self.update_irq()
                for sid in list(self.sq):
                    if self.sq[sid].cqid==qid:self.process(sid)
            else:
                q=self.sq.get(qid)
                if q is None or value>=q.size:return self.fatal('invalid SQ tail')
                q.tail=value;self.process(qid)
            return
        if offset==0xc:self.mask|=value;self.update_irq()
        elif offset==0x10:self.mask&=~value;self.update_irq()
        elif offset==0x14:
            old=self.cc;self.cc=value
            if not(value&1):
                self.sq.clear();self.cq.clear();self.aer.clear();self.csts=0;self.update_irq()
            elif not(old&1):
                sizes=((self.aqa&0xfff)+1,((self.aqa>>16)&0xfff)+1)
                if ((value>>7)&0xf) or ((value>>4)&7) or ((value>>16)&0xf)!=6 or ((value>>20)&0xf)!=4:
                    return self.fatal('CC unsupported sizes/CSS/MPS')
                if not self.valid_queue(self.asq,sizes[0],64) or not self.valid_queue(self.acq,sizes[1],16):
                    return self.fatal(f'admin queue invalid ASQ={self.asq:x} ACQ={self.acq:x} sizes={sizes} containsSQ={self.mem.contains(self.asq,sizes[0]*64)} containsCQ={self.mem.contains(self.acq,sizes[1]*16)}')
                self.sq[0]=SQ(self.asq,sizes[0],0);self.cq[0]=CQ(self.acq,sizes[1]);self.csts=1
                self.log(f'EN admin SQ={self.asq:x}/{sizes[0]} CQ={self.acq:x}/{sizes[1]}')
            if (value>>14)&3:self.csts=(self.csts&~12)|8
        elif offset==0x24 and not(self.cc&1):self.aqa=value
        elif offset in (0x28,0x2c,0x30,0x34) and not(self.cc&1):
            name='asq' if offset<0x30 else 'acq'
            shift=32 if offset&4 else 0
            mask=((1<<width)-1)<<shift
            setattr(self,name,(getattr(self,name)&~mask)|(value<<shift))

    def valid_queue(self,base,depth,stride):
        return 2<=depth<=self.MAX_DEPTH and base!=0 and base%4096==0 and self.mem.contains(base,depth*stride)

    def admin(self,cmd):
        op=cmd[0];dw=struct.unpack_from('<6I',cmd,40);nsid=struct.unpack_from('<I',cmd,4)[0]
        if cmd[1] or struct.unpack_from('<Q',cmd,16)[0]:return Result(INVALID_FIELD)
        if op==6:return self.ns.admin(cmd)
        if op in (1,5):
            qid=dw[0]&0xffff;depth=(dw[0]>>16)+1;base=struct.unpack_from('<Q',cmd,24)[0]
            if nsid or not 1<=qid<=self.MAX_Q or not dw[1]&1:return Result(INVALID_FIELD)
            if not self.valid_queue(base,depth,16 if op==5 else 64):return Result(INVALID_FIELD)
            if op==5:
                if qid in self.cq or dw[1]>>16:return Result(INVALID_FIELD)
                self.cq[qid]=CQ(base,depth,bool(dw[1]&2))
            else:
                cqid=dw[1]>>16
                if qid in self.sq or cqid==0 or cqid not in self.cq:return Result(INVALID_FIELD)
                self.sq[qid]=SQ(base,depth,cqid)
            self.log(f'CREATE {"CQ" if op==5 else "SQ"} {qid} depth={depth} base={base:x}')
            return Result(SUCCESS)
        if op in (0,4):
            qid=dw[0]&0xffff;queues=self.sq if op==0 else self.cq
            if qid==0 or qid not in queues:return Result(INVALID_FIELD)
            if op==4 and any(q.cqid==qid for q in self.sq.values()):return Result(0x10c)
            del queues[qid];self.update_irq();return Result(SUCCESS)
        if op in (9,10):
            fid=dw[0]&0xff
            if fid==7:return Result(SUCCESS,result=(self.MAX_Q-1)|((self.MAX_Q-1)<<16))
            if fid not in (1,2,4,5,6,8,9,10,11):return Result(INVALID_FIELD)
            if op==9:self.features[fid]=dw[1]
            return Result(SUCCESS,result=self.features.get(fid,0))
        if op==2:
            lid=dw[0]&0xff;length=(((dw[0]>>16)|((dw[1]&0xffff)<<16))+1)*4
            if lid not in (1,2,3) or length>4096 or dw[2] or dw[3]:return Result(INVALID_FIELD)
            data=bytearray(length)
            if lid==2 and length>=4:data[1:3]=struct.pack('<H',300);data[3]=100
            if lid==3 and length>=16:data[0]=1;data[8:16]=b'S93     '
            return Result(SUCCESS,bytes(data))
        if op==0xc:
            if self.aer:return Result(0x105)
            self.aer.append(struct.unpack_from('<H',cmd,2)[0]);return None
        if op==8:return Result(SUCCESS,result=1) # synchronous commands already complete
        return Result(INVALID_OPCODE)

    def process(self,qid):
        sq=self.sq[qid];cq=self.cq[sq.cqid]
        # Leave one CQ slot unused; process again after guest acknowledges CQ head.
        for _ in range(self.MAX_DEPTH):
            if sq.head==sq.tail or cq.pending>=cq.size-1:return
            try:
                cmd=self.mem.read(sq.base+sq.head*64,64)
                if len(cmd)!=64:raise ValueError('short command')
                cid=struct.unpack_from('<H',cmd,2)[0]
                result=self.admin(cmd) if qid==0 else self.ns.io(cmd)
                sq.head=(sq.head+1)%sq.size
                if result is None:continue
                if result.data:
                    try:
                        spans=resolve(*struct.unpack_from('<QQ',cmd,24),len(result.data),self.mem.contains,self.mem.read)
                    except InvalidPRP:result=Result(INVALID_FIELD)
                    else:
                        at=0
                        for addr,n in spans:self.mem.write(addr,result.data[at:at+n]);at+=n
                entry=struct.pack('<IIHHHH',result.result,0,sq.head,qid,cid,(result.status<<1)|cq.phase)
                self.mem.write(cq.base+cq.tail*16,entry)
                cq.tail=(cq.tail+1)%cq.size;cq.pending+=1
                if cq.tail==0:cq.phase^=1
                self.log(f'CMD q={qid} op={cmd[0]:02x} cid={cid} status={result.status:x} bytes={len(result.data)}')
                self.update_irq()
            except (ValueError,OSError,TimeoutError) as exc:
                self.fatal(str(exc));return
