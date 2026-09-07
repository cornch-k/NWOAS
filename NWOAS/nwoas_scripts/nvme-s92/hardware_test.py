#!/usr/bin/env python3
"""S92 command-layer hardware test, idle S90/J274 only, GPT metadata only.
No guest, PCI emulation or disk mutation. Not a Windows driver test.
"""
import json,struct,zlib,signal,subprocess,datetime,hashlib
from pathlib import Path
from readonly_namespace import ReadOnlyNamespace,SUCCESS,WRITE_TO_RO
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
r=Path(__file__).resolve().parent
proof=json.loads(sorted((r.parent/'ans2-s87').glob('run-*/metadata.json'))[-1].read_text())
assert proof['status']=='PRIMARY_GPT_AND_BACKUP_HEADER_CRC_PASS'
blocks=proof['disk_bytes']//4096
port='/dev/cu.usbmodemC07HL05SQ6NY1';owners=subprocess.run(['lsof','-t',port],capture_output=True);assert owners.returncode==1 and not owners.stdout and not owners.stderr
signal.signal(signal.SIGALRM,lambda *_:(_ for _ in ()).throw(TimeoutError('S92 deadline')));signal.alarm(30)
i=UartInterface();p=M1N1Proxy(i);bootstrap_port(i,p);u=ProxyUtils(p)
assert 'j274' in str(u.adt.compatible).lower()
assert p.nvme_init();buf=u.memalign(0x4000,0x10000);reads=[]
def read(lba):
 assert lba in [0,1,2,3,4,5,blocks-1],'metadata-only test backend'
 signal.alarm(15)
 if not p.nvme_read(1,lba,buf):raise OSError('ANS2 read failed')
 reads.append(lba);return i.readmem(buf,4096)
def command(op,nsid=1,lba=0,count=1,cns=None):
 b=bytearray(64);b[0]=op;struct.pack_into('<I',b,4,nsid)
 if cns is None:struct.pack_into('<QH',b,40,lba,count-1)
 else:struct.pack_into('<I',b,40,cns)
 return bytes(b)
try:
 ns=ReadOnlyNamespace(blocks,read)
 info=ns.admin(command(6,cns=0));assert info.status==SUCCESS
 assert struct.unpack_from('<Q',info.data)[0]==blocks
 for op in [1,4,8,9,0x0d,0x11,0x15,0x19,0x79]:assert ns.io(command(op)).status==WRITE_TO_RO
 assert not reads
 result=ns.io(command(2,lba=0,count=2));assert result.status==SUCCESS
 mbr,h=result.data[:4096],result.data[4096:]
 assert mbr[510:512]==b'\x55\xaa' and h[:8]==b'EFI PART'
 size,crc=struct.unpack_from('<II',h,12);assert 92<=size<=4096
 chk=bytearray(h[:size]);chk[16:20]=b'\0'*4;assert zlib.crc32(chk)==crc
 assert h[56:72].hex()==__import__('uuid').UUID(proof['gpt']['disk_guid']).bytes_le.hex()
 backup=ns.io(command(2,lba=blocks-1));assert backup.status==SUCCESS and backup.data[:8]==b'EFI PART'
 report={'experiment':'S92','result':'COMMAND_LAYER_REAL_GPT_READ_PASS','reads':reads,'namespace_blocks':blocks,'disk_bytes':blocks*4096,'gpt_header_sha256':hashlib.sha256(h).hexdigest(),'write_requests_rejected_without_backend_access':True,'windows_driver_test':False,'pci_mmio_interrupts_connected':False}
 print(json.dumps(report,indent=2),flush=True)
 (r/('hardware-result-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'.json')).write_text(json.dumps(report,indent=2)+'\n')
finally:
 signal.alarm(30);p.nvme_shutdown();signal.alarm(0);i.dev.close()
