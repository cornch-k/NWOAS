# Run after nvme-s93/guest_module.py. S123 transport bring-up is READ-ONLY on SSD.
import sys,os
from pathlib import Path
sys.path.insert(0,'/Volumes/X31/NWOAS/nwoas_scripts/transport-s123')
from transport import LinkNamespace,NamespacePair,fat_image
from readonly_namespace import Result,SUCCESS,WRITE_TO_RO
_controller=hv._nwoas_nvme[0]
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
_files={'NWESP.EXE':(_root/'uefi-s125/NWESP.EXE').read_bytes(),
        'NWOS.EXE':(_root/'native-link-s125/NWOS.EXE').read_bytes(),
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
hv.log('[S123] host RAM namespace 2 online; S124 SSD Windows zone writable; VWC1 + FUA/flush/shutdown; GPT writes rejected')
