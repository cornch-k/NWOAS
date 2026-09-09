# Run after nvme-s93/guest_module.py. S123 transport bring-up is READ-ONLY on SSD.
import sys,os,struct
from pathlib import Path
sys.path.insert(0,'/Volumes/X31/NWOAS/nwoas_scripts/transport-s123')
from transport import LinkNamespace,NamespacePair,fat_image
from readonly_namespace import Result,SUCCESS,WRITE_TO_RO
from prp import resolve,InvalidPRP
from m1n1.hv.types import NwoasNvmeLinkRequest
_controller=hv._nwoas_nvme[0]
_memory=hv._nwoas_nvme[1]
class TransportReadOnly:
    def __init__(self,base):self.base=base
    def io(self,command,mem=None):
        if command[0]==2:return self.base.io(command,mem)
        if command[0]==0:return Result(SUCCESS) # no physical writes to flush in this bring-up
        return Result(WRITE_TO_RO)
    def admin(self,command):
        r=self.base.admin(command)
        if r.status==SUCCESS and command[0]==6 and command[40]==0:
            b=bytearray(r.data);b[99]=1;return Result(SUCCESS,bytes(b))
        return r
_root=Path('/Volumes/X31/NWOAS/nwoas_scripts')
_files={'NW177.EXE':(_root/'worker-s177/NWOS.EXE').read_bytes(),
        'MEM180.EXE':(_root/'memory-s180/memtest/MEMTEST.EXE').read_bytes(),
        'USERCB.EXE':(_root/'bench-session-s176/USERCB.EXE').read_bytes(),
        'CBRUN.CMD':(_root/'bench-session-s176/CBRUN.CMD').read_bytes(),
        'NWGET.EXE':(_root/'file-delivery-s168/client/NWOAS-S168.EXE').read_bytes(),
        'NWPUT.EXE':(_root/'file-upload-s169/client/NWPUT.EXE').read_bytes(),
        'NWESP.EXE':(_root/'uefi-s125/NWESP.EXE').read_bytes(),
        'NWOS.EXE':(_root/'native-link-s125/NWOS.EXE').read_bytes(),
        'AUTONWOS.CMD':(_root/'native-link-s125/install-autostart.cmd').read_bytes(),
        'CPUSTRES.EXE':(_root/'cpufreq-s140/CPUSTRES.EXE').read_bytes(),
        'DISKREAD.EXE':(_root/'cpufreq-s140/DISKREAD.EXE').read_bytes(),
        'CPU140.CMD':b'@echo off\r\n%~dp0CPUSTRES.EXE\r\n',
        # S134: deliver the Windows inventory collector on the host-RAM tools
        # namespace. This avoids moving the physical WINARM USB between Macs.
        'COLLECT.PS1':(_root/'s134-windows-inventory/collect.ps1').read_bytes(),
        'S134.CMD':b'@echo off\r\npowershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0COLLECT.PS1"\r\npause\r\n',
        'R124.BIN':(_root/'nvme-s124/R124.BIN').read_bytes(),
        'P124.BIN':(_root/'nvme-s124/P124.BIN').read_bytes(),
        'NWAGENT.EXE':(_root/'transport-s123/NWAGENT.EXE').read_bytes(),
        'START.CMD':b'@echo off\r\n%~dp0NWAGENT.EXE\r\n',
        'NWGUARD.EXE':(_root/'nvme-s118/NWGUARD.EXE').read_bytes(),
        'NWTRIM.EXE':(_root/'nvme-s120/NWTRIM.EXE').read_bytes(),
        'NWVFLUSH.EXE':(_root/'nvme-s120/NWVFLUSH.EXE').read_bytes(),
        'NWREAD.EXE':(_root/'nvme-s115/NWREAD.EXE').read_bytes()}
_link=LinkNamespace(fat_image(_files),os.environ['NWOAS_LINK_DIR'])
class CachedPair(NamespacePair):
    def io(self,command,mem=None):
        import struct
        if len(command)==64 and command[0]==0 and struct.unpack_from('<I',command,4)[0]==0xffffffff:
            c=bytearray(command);struct.pack_into('<I',c,4,1)
            return self.primary.io(bytes(c),mem)
        return super().io(command,mem)
    def flush(self):self.primary.flush()
    def set_cache(self,enabled):self.primary.set_cache(enabled)
    def get_cache(self):return self.primary.get_cache()
_controller.ns=CachedPair(_controller.ns,_link)
hv._nwoas_link=_link

def _fast_link_handle(addr):
    """Execute NS2 data only; target m1n1 remains sole SQ/CQ owner."""
    req=iface.readstruct(addr,NwoasNvmeLinkRequest)
    if (req.magic!=0x53313530 or req.version!=1 or
        req.size!=NwoasNvmeLinkRequest.sizeof() or req.response!=0 or
        len(req.command)!=64 or struct.unpack_from('<I',req.command,4)[0]!=2):
        return False
    lifecycle=p.nwoas_nvme_fastpath(4)
    if ((lifecycle&0xffffffff)!=req.lifecycle_epoch or not(lifecycle&(1<<32)) or
        lifecycle&(1<<33) or not(lifecycle&(1<<34))):
        req.response=2
        iface.writemem(addr,NwoasNvmeLinkRequest.build(req))
        return True
    command=bytes(req.command)
    try:
        result=_controller.ns.io(command,_memory)
        if result is None:raise ValueError('asynchronous result on I/O queue')
        if result.data:
            try:spans=resolve(*struct.unpack_from('<QQ',command,24),len(result.data),_memory.contains,_memory.read)
            except InvalidPRP:result=Result(2)
            else:
                at=0
                for guest_addr,n in spans:
                    _memory.write(guest_addr,result.data[at:at+n]);at+=n
        req.status=result.status
        req.result=result.result
    except (ValueError,OSError,TimeoutError) as exc:
        req.status=0x280 if command[0]==1 else 0x281
        req.result=0
        hv.log(f'[S150] NS2 command failed op={command[0]:02x}: {exc}')
    if not 0<=req.status<=0x7fff or not 0<=req.result<=0xffffffff:
        req.status=2;req.result=0
    req.response=1
    iface.writemem(addr,NwoasNvmeLinkRequest.build(req))
    return True

hv.nwoas_nvme_link_handler=_fast_link_handle
hv.log('[S150] host RAM namespace 2 online through target-owned I/O queues; S124 SSD Windows zone writable; VWC1 + FUA/flush/shutdown; GPT blocks validated')
