#!/bin/bash
# S6-D18: clean single-process reproduction of the measured Tahoe wire protocol.
# Hypothesis: old legacy request / process reattachment contaminated D6 state.
# One separately logged bootstrap reboot precedes this script, no reboot here.
# PASS: reply validation + sustained output; physical observation required.
# No kmutil, firmware changes, PMGR resets, guest boot or new DART framebuffer maps.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-fresh-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import os,signal,struct,time,hashlib,json,subprocess
from pathlib import Path
from construct import Container
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils
from m1n1.hw.dart import DART
from m1n1.hw.asc import ASC
from m1n1.fw.afk.rbep import AFKRingBufEndpoint
from m1n1.fw.dcp.iboot import DCPIBootClient,IBootLayerInfo
from tahoe_afk_link import TahoeLink,TahoeIBoot,decode_packet
root=Path('/Volumes/X31/NWOAS')
prefix=os.environ['NWOAS_PROTOCOL_LOG']
class NewIBootEndpoint(AFKRingBufEndpoint):
    SHORT='iboot'
    announcement=None
    def handle_ipc(self,data):
        assert self.announcement is None, 'Unexpected packet during startup'
        packet=decode_packet(data)
        assert (packet['interface'],packet['kind'],packet['category'])==(3,0x11,0)
        assert packet['payload'][:32].split(b'\0',1)[0]==b'disp0-service'
        self.announcement=packet
        Path(prefix+'.announce.bin').write_bytes(struct.pack('<4sI',b'IOP ',len(data)-8)+data)
class NewClient(DCPIBootClient):
    ENDPOINTS={**DCPIBootClient.ENDPOINTS,0x23:NewIBootEndpoint}
port='/dev/cu.usbmodemC07HL05SQ6NY1'
assert not subprocess.run(['/usr/sbin/lsof','-t',port],capture_output=True).stdout.strip()
binary=(root/'experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin').read_bytes()
assert hashlib.md5(binary).hexdigest()=='7bec0225edc101857be3b7f195dd7357'
marker=b'NWOAS-DCP-DEFER: automatic display init deferred to host'
def deadline(*a):raise TimeoutError('Fresh experiment deadline')
signal.signal(signal.SIGALRM,deadline);signal.alarm(90)
i=UartInterface(port+':115200');i.dev.timeout=3;i.dev.write_timeout=3
try:
    i.cmd(i.REQ_NOP);i.reply(i.REQ_NOP);p=M1N1Proxy(i);base=p.get_base()
    assert i.readmem(base+binary.index(marker),len(marker))==marker
    u=ProxyUtils(p)
    assert u.adt.model=='Macmini9,1'
    assert b'mBoot-18000.121.3' in bytes(u.adt['/chosen'].firmware_version)
    dart=DART.from_adt(u,'/arm-io/dart-dcp')
    asc=u.adt['/arm-io/dcp'].get_reg(0)[0]
    dcp=NewClient(u,asc,dart);dcp.dva_offset=u.adt['/arm-io/dcp'][0].asc_dram_mask
    ASC.boot(dcp);dcp.mgmt.start();dcp.mgmt.wait_boot(30)
    dcp.start_ep(0x23)
    end=time.monotonic()+10
    while dcp.iboot.announcement is None and time.monotonic()<end:dcp.work()
    assert dcp.iboot.announcement is not None
    ep=dcp.iboot
    assert dcp.syslog.entrysize==128 and dcp.syslog.count==63
    ptr=(ep.txq.get_rptr(),ep.txq.get_wptr(),ep.rxq.get_rptr(),ep.rxq.get_wptr())
    assert ptr==(0,0,256,256),ptr
    link=TahoeLink(i,p,ptr,prefix,boot_base=base,shared=ep.iobuffer,
                   syslog_base=dcp.syslog.iobuffer,asc_base=asc)
    state=dict(boot_base=base,shared=ep.iobuffer,syslog_base=dcp.syslog.iobuffer,asc_base=asc)
    print('[FACT] Fresh state',state)
    Path(prefix+'.state.json').write_text(json.dumps(state,indent=2))
    body=struct.pack('<QBBB5x',0,0x12,0,1)
    wptr=link.tx.write(bytes(8)+struct.pack('<BBHI',0,0,3,len(body))+body)
    link.mailbox_send((0xa2<<48)|wptr,0x23)
    link.sequence,link.command_sequence=1,0
    service=TahoeIBoot(link)
    print('[FACT] HPD/counts',service.getModeCount())
    timing=service.send_cmd(4,replen=4096)
    color=service.send_cmd(5,replen=4096)
    nt,nc=struct.unpack_from('<I',timing)[0],struct.unpack_from('<I',color)[0]
    assert len(timing)==4+nt*24 and len(color)==4+nc*24
    t=next(timing[4+j*24:28+j*24] for j in range(nt) if struct.unpack_from('<6I',timing,4+j*24)==(1,1280,720,60<<16,0,0))
    c=next(color[4+j*24:28+j*24] for j in range(nc) if struct.unpack_from('<IIIII',color,4+j*24)==(1,1,1,1,32))
    pa,dva,size=u.ba.video.base,0x13dc000,1280*720*4
    assert pa==u.adt['/vram'].reg[0].addr and u.adt['/vram'].reg[0].size>=size
    for path in ('/arm-io/dart-disp0','/arm-io/dart-dcp'):
        d=DART.from_adt(u,path)
        assert d.iotranslate(0,dva,size)==[(pa,size)]
    Path(prefix+'.vram-before.bin').write_bytes(i.readmem(pa,size))
    palette=[(255,255,255),(255,255,0),(0,255,255),(0,255,0),(255,0,255),(255,0,0),(0,0,255),(32,32,32)]
    row=b''.join(bytes((b,g,r,255))*160 for r,g,b in palette)
    i.writemem(pa,row*720);p.dc_cvac(pa,size)
    service.setPower(True)
    service.send_cmd(6,t+c)
    layer=Container(planes=[Container(addr=dva,stride=1280*4,addr_format=1),Container(),Container()],
                    plane_cnt=1,width=1280,height=720,surface_fmt=1,colorspace=2,eotf=1,transform=0)
    service.send_cmd(1,IBootLayerInfo.build(layer)+bytes(8))
    print('[FACT] Fresh 720p mode/surface ACK; observing messages for 15 seconds')
    end=time.monotonic()+15
    while time.monotonic()<end:
        assert not link.work(), 'Unexpected response outside command'
        time.sleep(.01)
    state.update(pointers=link.pointers(),sequence=link.sequence,command_sequence=link.command_sequence)
    Path(prefix+'.state.json').write_text(json.dumps(state,indent=2))
    print('[FACT] Final state',state)
finally:
    signal.alarm(0);i.dev.close()
PY
