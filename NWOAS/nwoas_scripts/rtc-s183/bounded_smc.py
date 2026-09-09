# SPDX-License-Identifier: MIT
"""One bounded SMC CLKM read, using the existing m1n1 RTKit protocol.
No write-key opcode is exposed. Management start/quiesce messages change the
mailbox session, so this belongs only before guest launch, never in a live OS.
Proxy RPC timeouts remain a separate bound from these software deadlines.
"""
import time
from m1n1.fw.smc import SMCClient,SMCEndpoint,SMCInitialize,SMCReadKey,SMCError

class BoundedEndpoint(SMCEndpoint):
    def start(self):
        self.send(SMCInitialize(ID=0)); self.msgid+=1
        while self.shmem is None:self.asc.work()
    def cmd(self,cmd):
        if not isinstance(cmd,SMCReadKey):raise ValueError('only CLKM read command')
        cmd.ID=self.new_msgid();self.send(cmd)
        while cmd.ID in self.outstanding:self.asc.work()
        ret=self.ret.pop(cmd.ID)
        if ret.RESULT:raise SMCError(f'SMC read failed {ret}',ret)
        return ret
    def read_clkm(self,base,size):
        ret=self.cmd(SMCReadKey(KEY=int.from_bytes(b'CLKM','big'),SIZE=6))
        if ret.SIZE!=6:raise ValueError('CLKM reply not six bytes')
        if self.shmem is None or not base<=self.shmem<=base+size-6:
            raise ValueError('SMC reply buffer outside ADT SRAM region')
        self.asc.check_deadline()
        data=self.asc.iface.readmem(self.shmem,6)
        if len(data)!=6:raise ValueError('short CLKM read')
        return data

class BoundedSMC(SMCClient):
    ENDPOINTS={0x20:BoundedEndpoint}
    def __init__(self,u,base):
        super().__init__(u,base,None); self.clock=time.monotonic; self.deadline=0
    def set_budget(self,seconds):
        if not 0<seconds<=5:raise ValueError('SMC budget out of range')
        self.deadline=self.clock()+seconds
    def check_deadline(self):
        if self.clock()>=self.deadline:raise TimeoutError('SMC operation deadline')
    def work(self):
        self.check_deadline();return super().work()
    def send(self,msg0,msg1):
        self.check_deadline()
        while self.asc.INBOX_CTRL.reg.FULL:self.check_deadline()
        self.asc.INBOX0.val=msg0;self.asc.INBOX1.val=msg1
        while self.asc.INBOX_CTRL.reg.FULL:self.check_deadline()
