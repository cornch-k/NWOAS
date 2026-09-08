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
_files={'NWAGENT.EXE':(_root/'transport-s123/NWAGENT.EXE').read_bytes(),
        'START.CMD':b'@echo off\r\n%~dp0NWAGENT.EXE\r\n',
        'NWGUARD.EXE':(_root/'nvme-s118/NWGUARD.EXE').read_bytes(),
        'NWREAD.EXE':(_root/'nvme-s115/NWREAD.EXE').read_bytes()}
_link=LinkNamespace(fat_image(_files),os.environ['NWOAS_LINK_DIR'])
_controller.ns=NamespacePair(TransportReadOnly(_controller.ns),_link)
hv._nwoas_link=_link
hv.log('[S123] host RAM namespace 2 online; mini SSD namespace 1 READ ONLY; no installation tasks armed by host')
