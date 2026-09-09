"""S98: validated GPT/MBR writes.

Windows may rewrite the partition table (diskpart/Setup) as long as the resulting
table keeps every protected Apple entry byte-identical and confines every other
entry to the Windows zone. Writes to GPT blocks are staged in a shadow; once the
staged primary (or backup) set is self-consistent (signature + header CRC +
entries CRC) the invariants are checked and the set is committed or discarded.
Reads of staged blocks return the shadow so the guest sees its own writes.
"""
import struct,zlib
BLOCK=4096
GPT_SIG=b'EFI PART'

class GptViolation(Exception):pass

def _hdr(b):
    if b[:8]!=GPT_SIG:return None
    size,crc=struct.unpack_from('<II',b,12)
    if not 92<=size<=BLOCK:return None
    raw=bytearray(b[:size]);raw[16:20]=b'\0'*4
    if zlib.crc32(raw)!=crc:return None
    cur,alt,first,last=struct.unpack_from('<QQQQ',b,24)
    guid=b[56:72];ent,cnt,esz,ecrc=struct.unpack_from('<QIII',b,72)
    return dict(cur=cur,alt=alt,first=first,last=last,guid=guid,ent=ent,cnt=cnt,esz=esz,ecrc=ecrc)

class GptGuard:
    def __init__(self,read_block,write_block,disk_blocks,zone_first,zone_last,protected_entries):
        """read_block(lba,n)->bytes; write_block(lba,data) writes anywhere (GPT blocks only reach here).
        protected_entries: raw 128-byte GPT entries that must survive unchanged."""
        self.read=read_block;self.write=write_block;self.nblocks=disk_blocks
        self.zone=(zone_first,zone_last);self.protected=[bytes(e) for e in protected_entries]
        assert all(len(e)==128 and e[:16]!=b'\0'*16 for e in self.protected)
        h=_hdr(self.read(1,1))
        if h is None:raise GptViolation('primary GPT header invalid at init')
        self.ref=h;self.ent_blocks=(h['cnt']*h['esz']+BLOCK-1)//BLOCK
        bh=_hdr(self.read(h['alt'],1))
        if bh is None or bh['ent']+self.ent_blocks>h['alt']:raise GptViolation('backup GPT header invalid at init')
        self.sets={'primary':(1,h['ent']),'backup':(h['alt'],bh['ent'])}
        self.gpt_lbas={0}
        for hl,el in self.sets.values():self.gpt_lbas|={hl}|set(range(el,el+self.ent_blocks))
        self.shadow={}
        self.check_table(self.read(h['ent'],self.ent_blocks),h) # current on-disk table must already satisfy invariants

    def is_gpt(self,lba):return lba in self.gpt_lbas

    def check_table(self,entries,h):
        if (h['guid'],h['first'],h['last'],h['cnt'],h['esz'])!=(self.ref['guid'],self.ref['first'],self.ref['last'],self.ref['cnt'],self.ref['esz']):
            raise GptViolation('header geometry/disk GUID changed')
        if zlib.crc32(entries[:h['cnt']*h['esz']])!=h['ecrc']:raise GptViolation('entries CRC mismatch')
        seen=set()
        for i in range(h['cnt']):
            e=entries[i*h['esz']:(i+1)*h['esz']][:128]
            if e[:16]==b'\0'*16:continue
            if e in self.protected:
                if e in seen:raise GptViolation(f'duplicate protected entry in slot {i+1}')
                seen.add(e);continue
            if any(e[16:32]==p[16:32] for p in self.protected):raise GptViolation(f'protected partition modified in slot {i+1}')
            s,en=struct.unpack_from('<QQ',e,32)
            if not (self.zone[0]<=s<=en<=self.zone[1]):raise GptViolation(f'slot {i+1} outside Windows zone: {s}-{en}')
        if seen!=set(self.protected):raise GptViolation('protected partition missing')

    def read_overlay(self,lba,n,data):
        if not self.shadow:return data
        d=bytearray(data)
        for i in range(n):
            if lba+i in self.shadow:d[i*BLOCK:(i+1)*BLOCK]=self.shadow[lba+i]
        return bytes(d)

    def stage(self,lba,data):
        """Stage one GPT-block write. Raises GptViolation to reject (shadow for that set dropped)."""
        assert len(data)==BLOCK and lba in self.gpt_lbas
        if lba==0:
            if data[510:512]!=b'\x55\xaa' or data[446+4]!=0xEE:raise GptViolation('MBR not protective')
            self.write(0,data);return
        self.shadow[lba]=bytes(data)
        for name,(hl,el) in self.sets.items():
            blocks=list(range(el,el+self.ent_blocks))+[hl] # header last = commit point
            if lba not in blocks:continue
            cur=lambda l:self.shadow.get(l) or self.read(l,1)
            h=_hdr(cur(hl))
            if h is None or h['cur']!=hl or h['ent']!=el or h['alt']!=self.sets['backup' if name=='primary' else 'primary'][0]:
                if h is not None:self._drop(blocks);raise GptViolation(f'{name} header points elsewhere')
                return # header not yet consistent; keep staging
            entries=b''.join(cur(l) for l in range(el,el+self.ent_blocks))
            if zlib.crc32(entries[:h['cnt']*h['esz']])!=h['ecrc']:return # entries not yet complete
            try:self.check_table(entries,h)
            except GptViolation:self._drop(blocks);raise
            for l in blocks:
                if l in self.shadow:self.write(l,self.shadow[l]);del self.shadow[l] # keep staged on write failure
            return

    def _drop(self,blocks):
        for l in blocks:self.shadow.pop(l,None)
