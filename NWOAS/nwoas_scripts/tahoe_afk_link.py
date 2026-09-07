"""Experimental Tahoe AFK link with verified live boot/ring state checks.

Wire-layout hypothesis validated by D8 getModeCount response. This is not a
general EPIC replacement. No firmware implementation is included here.
"""
import struct
import time
from pathlib import Path
from types import SimpleNamespace
from m1n1.fw.afk.rbep import AFKRingBuf
from m1n1.fw.dcp.iboot import DCPIBootService


def decode_packet(data):
    if len(data) < 32 or data[:8] != bytes(8):
        raise ValueError('Unsupported queue header or truncated packet')
    seq, reserved, interface, size = struct.unpack_from('<BBHI', data, 8)
    if size != len(data)-16 or size < 16:
        raise ValueError('Fragmented/truncated message not supported')
    stamp, kind, category, flags = struct.unpack_from('<QBBB',data,16)
    return dict(sequence=seq, reserved=reserved, interface=interface,
                stamp=stamp, kind=kind, category=category, flags=flags,
                payload=data[32:])


class TahoeLink:
    ASC = 0x231c00000
    SHARED = 0x80d224000

    def __init__(self, iface, proxy, expected, prefix, *, boot_base=0x803a84000,
                 shared=0x80d224000, syslog_base=0x80d220000, asc_base=0x231c00000,
                 endpoint=0x23, interface=3, half_size=0x4000):
        self.iface, self.p, self.prefix = iface, proxy, Path(prefix)
        self.SHARED, self.ASC, self.syslog_base = shared, asc_base, syslog_base
        self.endpoint,self.interface=endpoint,interface
        assert proxy.get_base() == boot_base, 'Boot changed; cannot reattach'
        ep = SimpleNamespace(iface=iface)
        self.tx = AFKRingBuf(ep,self.SHARED,half_size)
        self.rx = AFKRingBuf(ep,self.SHARED+half_size,half_size)
        for ring in (self.tx,self.rx):
            assert ring.block_size == 128
            ring.rptr,ring.wptr = ring.get_rptr(),ring.get_wptr()
        assert self.pointers() == expected, 'Unexpected ring state'
        assert self.p.read32(self.ASC+0x48)&3 == 1, 'DCP not running'
        self.sequence, self.command_sequence, self.frame_count = 2,1,0
        self.control_ack = None
        self.expected_control = None
        self.reports=[]

    def pointers(self):
        return (self.tx.get_rptr(),self.tx.get_wptr(),self.rx.get_rptr(),self.rx.get_wptr())

    def mailbox_send(self,msg,ep):
        assert not self.p.read32(self.ASC+0x8110)&(1<<16), 'Mailbox full'
        self.p.write64(self.ASC+0x8800,msg)
        self.p.write64(self.ASC+0x8808,ep)

    def syslog(self,msg):
        assert (msg>>52)&15 == 5
        index = msg&255
        assert index < 63
        data = self.iface.readmem(self.syslog_base+index*160,160)
        context=data[8:32].split(b'\0',1)[0].decode('ascii',errors='replace')
        line=data[32:].split(b'\0',1)[0].decode('ascii',errors='replace')
        print('[DCP]',context,line,flush=True)
        self.mailbox_send(msg,2)

    def work(self):
        while not self.p.read32(self.ASC+0x8114)&(1<<17):
            msg=self.p.read64(self.ASC+0x8830)
            ep=self.p.read64(self.ASC+0x8838)&255
            if ep == 2 and (msg>>52)&15 == 5:
                self.syslog(msg)
            elif ep in (0x23,0x24) and msg>>48 == 0x85:
                # Both active test rings are polled separately; this doorbell
                # does not consume the other endpoint's shared queue data.
                pass
            elif ep == 8 and msg>>56 == 2:
                # Log only, matching upstream RTKit unknown-OSLog behavior.
                print('[OSLOG notification]',hex(msg),flush=True)
            elif self.expected_control is not None and (ep,msg)==self.expected_control:
                self.control_ack=(ep,msg)
                print('[FACT] Control ACK',hex(ep),hex(msg),flush=True)
            else:
                raise RuntimeError(f'Unrecognized mailbox captured: {ep:#x}/{msg:#x}')
        packets=[]
        for data in self.rx.read():
            self.frame_count+=1
            frame=struct.pack('<4sI',b'IOP ',len(data)-8)+data
            Path(str(self.prefix)+f'.msg{self.frame_count}.bin').write_bytes(frame)
            packet=decode_packet(data)
            if packet['category']==0:
                self.reports.append(packet)
                print('[FACT] Async report',packet['interface'],hex(packet['kind']),
                      'bytes',len(packet['payload']),flush=True)
            else:
                packets.append(packet)
        return packets

    def quiesce(self):
        # Existing public AFK/RTKit stop sequence, with bounded ACK waits.
        for ep,request,response in ((self.endpoint,0xc0<<48,0xc1<<48),
                                     (0,(0xb<<52)|0x10,(0xb<<52)|0x10),
                                     (0,(6<<52)|0x10,(7<<52)|0x10)):
            self.expected_control=(ep,response)
            self.control_ack=None
            self.mailbox_send(request,ep)
            end=time.monotonic()+5
            while self.control_ack is None and time.monotonic()<end:
                assert not self.work(), 'Unexpected data during quiesce'
                time.sleep(.005)
            assert self.control_ack==self.expected_control, 'Quiesce ACK timeout'
            self.expected_control=None

    def send_report(self, kind, payload=b'', flags=1):
        """Send one Tahoe AFK interface report and return after it is queued."""
        assert 0 <= kind <= 0xff and 0 <= flags <= 0xff
        self.work()
        body=struct.pack('<QBBB5x',0,kind,0,flags)+payload
        packet=bytes(8)+struct.pack('<BBHI',self.sequence&255,0,self.interface,len(body))+body
        self.sequence+=1
        wptr=self.tx.write(packet)
        self.mailbox_send((0xa2<<48)|wptr,self.endpoint)

    def shutdown_and_sleep(self, send_close=False):
        # m1n1's DCP shutdown path stops RBEP without an interface CLOSE.
        # Keep CLOSE optional for the separately recorded D26/D29 tests.
        if send_close:
            self.send_report(0x13, bytes(4))
            end=time.monotonic()+.25
            while time.monotonic()<end:
                assert not self.work(), 'Unexpected response to close report'
                time.sleep(.005)
        for ep,request,response in ((self.endpoint,0xc0<<48,0xc1<<48),
                                     (0,(0xb<<52)|0x10,(0xb<<52)|0x10),
                                     (0,(6<<52)|0x01,(7<<52)|0x01)):
            self.expected_control=(ep,response)
            self.control_ack=None
            self.mailbox_send(request,ep)
            end=time.monotonic()+5
            while self.control_ack is None and time.monotonic()<end:
                assert not self.work(), 'Unexpected data during sleep sequence'
                time.sleep(.005)
            assert self.control_ack==self.expected_control, 'Sleep ACK timeout'
            self.expected_control=None

    def close_and_sleep(self):
        self.shutdown_and_sleep(send_close=True)

    def sleep_before_shutdown(self):
        # Tahoe experiment: switch RTKit power while the display interface is
        # still alive, avoiding the IOAV teardown caused by RBEP shutdown.
        for request,response in (((0xb<<52)|0x10,(0xb<<52)|0x10),
                                  ((6<<52)|0x01,(7<<52)|0x01)):
            self.expected_control=(0,response)
            self.control_ack=None
            self.mailbox_send(request,0)
            end=time.monotonic()+5
            while self.control_ack is None and time.monotonic()<end:
                assert not self.work(), 'Unexpected data during AP-first sleep'
                time.sleep(.005)
            assert self.control_ack==self.expected_control, 'AP-first sleep ACK timeout'
            self.expected_control=None

    def command(self, payload, max_response):
        self.work()
        cs=self.command_sequence&255
        self.command_sequence+=1
        body=struct.pack('<QBBB5x',0,0xc0,1,0)+struct.pack('<BBHI',0,cs,0,max_response)+payload
        packet=bytes(8)+struct.pack('<BBHI',self.sequence&255,0,self.interface,len(body))+body
        self.sequence+=1
        wptr=self.tx.write(packet)
        self.mailbox_send((0xa2<<48)|wptr,self.endpoint)
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            for response in self.work():
                assert (response['interface'],response['kind'],response['category'])==(self.interface,0xc0,2), response
                data=response['payload']
                assert len(data)>=8
                flags,seq,reserved,status=struct.unpack_from('<BBHI',data)
                assert flags&1 == 0 and seq==cs, 'Unexpected response header'
                assert status==0, f'DCP command status={status:#x}'
                assert len(data)-8<=max_response
                return data[8:]
            time.sleep(.005)
        raise TimeoutError('One command sent; no matching response in 10 seconds')


class TahoeIBoot(DCPIBootService):
    def __init__(self,link):
        self.link=link

    def send_cmd(self,op,data=b'',replen=None):
        request=struct.pack('<IIII',op,16+len(data),0,0)+data
        response=self.link.command(request,(replen if replen is not None else 4096)+8)
        # Successful setters may have only an AFK response header (D10 op2).
        if not response and op in (1,2,6,16,18):
            return b''
        assert len(response)>=8
        rcmd,rlen=struct.unpack_from('<II',response)
        assert rcmd==op, 'Unexpected iBoot response opcode'
        # Tahoe swapBegin advertises its 0x11c response-buffer extent in
        # rlen while returning only the 20-byte SwapInfo payload in-band.
        if op == 15:
            assert rlen >= len(response), 'Truncated swapBegin response header'
        else:
            assert rlen == len(response), 'Unexpected iBoot response length'
        return response[8:]
