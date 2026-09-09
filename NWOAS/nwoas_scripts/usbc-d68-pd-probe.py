#!/usr/bin/env python3
"""D68: read target non-debug HPM PD state in fresh proxy; no PD register writes.
One caller-owned bootstrap reboot, 115200, no retries. I2C transport writes
FIFO/control registers only. Assert Macmini9,1 and ADT hpm1 address 0x3f.
PASS means readable PD state, not USB input success. No firmware flashing.
"""
import os, signal
os.environ['M1N1_KEEP_BAUD']='1'
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils
from m1n1.hw.i2c import I2C

def deadline(*args):
    raise TimeoutError('D68 bounded PD read expired')
signal.signal(signal.SIGALRM, deadline)
signal.alarm(45)
iface=UartInterface('/dev/cu.usbmodemC07HL05SQ6NY1:115200')
iface.dev.timeout=3
iface.dev.write_timeout=3
try:
    p=M1N1Proxy(iface)
    u=ProxyUtils(p)
    assert u.adt.model=='Macmini9,1'
    candidates=[]
    for node in u.adt['/arm-io/i2c0'].walk_tree():
        if node.name=='hpm1':
            raw=getattr(node,'hpm_iic_addr',None)
            print('[D68] hpm1 address property:',repr(raw),flush=True)
            if isinstance(raw,bytes): addr=raw[0]
            elif isinstance(raw,int): addr=raw
            else: raise RuntimeError('unrecognized HPM address property')
            candidates.append(addr)
    assert candidates==[0x3f], ('non-debug HPM mapping uncertain',candidates)
    ret=p.pmgr_adt_clocks_enable('/arm-io/i2c0')
    print('[D68] I2C power enable:',ret,flush=True)
    assert ret==0
    bus=I2C(u,'/arm-io/i2c0')
    for name,reg,n in [('mode',3,4),('status',0x1a,4),('power-state',0x20,1),
                       ('sysconfig',0x28,17),('control',0x29,5),
                       ('power-status',0x3f,2),('pd-status',0x40,4)]:
        raw=bus.read_reg(0x3f,reg,n+1)
        assert len(raw)==n+1 and 0 < raw[0] <= 64, ('invalid block',name,raw.hex())
        print('[D68]',name,'reported_len',raw[0], 'data',raw[1:1+min(raw[0],n)].hex(),flush=True)
finally:
    signal.alarm(0)
    iface.dev.close()
