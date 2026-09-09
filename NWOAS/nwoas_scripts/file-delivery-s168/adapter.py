"""Wrap an existing NS2 RAM link, retaining all old command guards and callbacks."""
import struct
from channel import HostArtifactChannel, BLOCK
from readonly_namespace import Result, SUCCESS, INVALID_FIELD, INVALID_NSID
from prp import resolve, InvalidPRP

class ArtifactLink:
    def __init__(self, base, artifact):
        self.base=base
        self.channel=HostArtifactChannel(base.token).register(artifact)

    def __getattr__(self, name):
        return getattr(self.base,name)

    def io(self, command, mem=None):
        f=self.base._fields(command)
        if f is None:return Result(INVALID_FIELD)
        op,ns,cdw=f
        if ns!=1:return Result(INVALID_NSID)
        lba=cdw[0] | (cdw[1]<<32); n=(cdw[2]&0xffff)+1
        if op==1 and self.channel.is_request_write(lba,n):
            # Identical field/PRP checks to the old single-block mailbox path.
            if cdw[2]&0x3fff0000 or cdw[3]&~0xff or any(cdw[4:]) or mem is None:
                return Result(INVALID_FIELD)
            try:
                spans=resolve(*struct.unpack_from('<QQ',command,24),BLOCK,mem.contains,mem.read)
                data=b''.join(mem.read(a,size) for a,size in spans)
            except InvalidPRP:return Result(INVALID_FIELD)
            return Result(SUCCESS if self.channel.handle_request(data) else INVALID_FIELD)
        result=self.base.io(command,mem)
        if op==2 and result.status==SUCCESS and self.channel.intersects_window(lba,n):
            return Result(SUCCESS,self.channel.overlay(lba,n,result.data),result.result)
        return result
