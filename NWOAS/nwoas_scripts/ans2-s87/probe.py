#!/usr/bin/env python3
"""S87: J274-only namespace1 GPT metadata reads from an idle m1n1 proxy.
Initializes controller/queues, never sends storage writes/format/flush commands.
Requires no running guest. Validates header and entry CRC before reporting layout.
Raw blocks remain local; stdout shows metadata only. Never resizes APFS.
"""
from pathlib import Path
import datetime,hashlib,json,os,signal,struct,subprocess,uuid,zlib
PORT='/dev/cu.usbmodemC07HL05SQ6NY1'
r=subprocess.run(['lsof','-t',PORT],capture_output=True)
assert r.returncode==1 and not r.stdout and not r.stderr,'STOP: serial owned or uncertain'
out=Path(__file__).resolve().parent/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));out.mkdir()
print('OUTPUT',out,flush=True)
def deadline(*_):raise TimeoutError('S87 operation deadline; do not auto-reboot')
signal.signal(signal.SIGALRM,deadline);signal.alarm(60)
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
iface=UartInterface();p=M1N1Proxy(iface,debug=False);bootstrap_port(iface,p);u=ProxyUtils(p)
compat=u.adt.compatible
assert 'j274' in str(compat).lower(),f'STOP wrong target: {compat}'
meta={'experiment':os.environ.get('NWOAS_ANS_EXPERIMENT','S87'),'target':str(compat),'writes_to_storage':False,'logical_block_bytes':4096,'namespace':1,'adt':{}}
for path in ['/arm-io/ans','/arm-io/sart-ans']:
 n=u.adt[path];regs=[]
 for j in range(len(n.reg)):
  try: regs.append([hex(x) for x in n.get_reg(j)])
  except Exception:break
 meta['adt'][path]={'compatible':str(n.compatible),'registers':regs,'interrupts':str(getattr(n,'interrupts',None))}
(out/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
print(json.dumps(meta),flush=True)
assert p.nvme_init(),'STOP: NVMe init failed'
signal.alarm(30)
buf=u.memalign(0x4000,0x10000)
def read(lba):
 signal.alarm(15)
 assert 0<=lba<2**40
 assert p.nvme_read(1,lba,buf),f'NVMe read failed LBA{lba}'
 b=iface.readmem(buf,4096)
 (out/f'lba-{lba}.bin').write_bytes(b)
 return b
mbr=read(0);assert mbr[510:512]==b'\x55\xaa','STOP: MBR signature missing'
h=read(1);assert h[:8]==b'EFI PART','STOP: GPT signature missing at 4K LBA1'
def header(b):
 size,crc=struct.unpack_from('<II',b,12);assert 92<=size<=4096
 raw=bytearray(b[:size]);raw[16:20]=b'\0'*4
 assert zlib.crc32(raw)==crc,'GPT header CRC mismatch'
 cur,backup,first,last=struct.unpack_from('<QQQQ',b,24)
 entries,count,esize,ecrc=struct.unpack_from('<QIII',b,72)
 assert 1<=count<=1024 and 128<=esize<=1024 and count*esize<=1024*1024
 return dict(current_lba=cur,backup_lba=backup,first_usable=first,last_usable=last,disk_guid=str(uuid.UUID(bytes_le=b[56:72])),entries_lba=entries,entry_count=count,entry_bytes=esize,entries_crc=ecrc)
g=header(h);assert g['current_lba']==1 and g['backup_lba']>g['last_usable']
nbytes=g['entry_count']*g['entry_bytes'];assert 2<=g['entries_lba'] and g['entries_lba']+(nbytes+4095)//4096<=g['first_usable']
arr=b''.join(read(g['entries_lba']+j) for j in range((nbytes+4095)//4096))[:nbytes]
assert zlib.crc32(arr)==g['entries_crc'],'GPT entries CRC mismatch'
parts=[]
for i in range(g['entry_count']):
 e=arr[i*g['entry_bytes']:(i+1)*g['entry_bytes']]
 if e[:16]==b'\0'*16:continue
 start,end,attrs=struct.unpack_from('<QQQ',e,32);assert g['first_usable']<=start<=end<=g['last_usable']
 parts.append(dict(index=i+1,type_guid=str(uuid.UUID(bytes_le=e[:16])),guid=str(uuid.UUID(bytes_le=e[16:32])),start_lba=start,end_lba=end,bytes=(end-start+1)*4096,name=e[56:128].decode('utf-16le').rstrip('\0'),attributes=hex(attrs)))
ordered=sorted(parts,key=lambda x:x['start_lba'])
assert all(a['end_lba']<b['start_lba'] for a,b in zip(ordered,ordered[1:]))
back=header(read(g['backup_lba']));assert back['backup_lba']==1 and back['current_lba']==g['backup_lba'] and back['disk_guid']==g['disk_guid'] and back['entries_crc']==g['entries_crc']
meta.update(status='PRIMARY_GPT_AND_BACKUP_HEADER_CRC_PASS',gpt=g,partitions=parts,disk_bytes=(g['backup_lba']+1)*4096)
(out/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
print(json.dumps(meta,indent=2),flush=True)
signal.alarm(30);p.nvme_shutdown();signal.alarm(0);iface.dev.close()
print(meta['experiment']+' PASS; namespace metadata only; controller shut down.',flush=True)
