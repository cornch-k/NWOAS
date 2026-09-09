#!/bin/bash
# S6-D27: ONLY display-commit variable vs D25 is iBoot swap op15/16/18
# instead of deprecated setSurface op1. A separately logged single bootstrap
# reboot must precede this script. PASS requires replies and sustained HPD/link.
# No kmutil, firmware change, PMGR reset, guest boot, or new DART mapping.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-swap-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import hashlib,json,os,signal,struct,subprocess,time
from pathlib import Path
from construct import Container
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils
from m1n1.hw.dart import DART
from m1n1.hw.asc import ASC
from m1n1.fw.afk.rbep import AFKRingBufEndpoint
from m1n1.fw.dcp.iboot import DCPIBootClient,IBootLayerInfo
from tahoe_afk_link import TahoeLink,TahoeIBoot,decode_packet

root=Path('/Volumes/X31/NWOAS'); prefix=os.environ['NWOAS_PROTOCOL_LOG']
class NewIBootEndpoint(AFKRingBufEndpoint):
    SHORT='iboot'; announcement=None
    def handle_ipc(self,data):
        assert self.announcement is None
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
def deadline(*a): raise TimeoutError('D27 deadline')
signal.signal(signal.SIGALRM,deadline); signal.alarm(90)
i=UartInterface(port+':115200'); i.dev.timeout=3; i.dev.write_timeout=3
try:
    i.cmd(i.REQ_NOP); i.reply(i.REQ_NOP); p=M1N1Proxy(i); base=p.get_base()
    assert i.readmem(base+binary.index(marker),len(marker))==marker
    u=ProxyUtils(p); assert u.adt.model=='Macmini9,1'
    assert b'mBoot-18000.121.3' in bytes(u.adt['/chosen'].firmware_version)
    dart=DART.from_adt(u,'/arm-io/dart-dcp'); asc=u.adt['/arm-io/dcp'].get_reg(0)[0]
    dcp=NewClient(u,asc,dart); dcp.dva_offset=u.adt['/arm-io/dcp'][0].asc_dram_mask
    ASC.boot(dcp); dcp.mgmt.start(); dcp.mgmt.wait_boot(30); dcp.start_ep(0x23)
    end=time.monotonic()+10
    while dcp.iboot.announcement is None and time.monotonic()<end: dcp.work()
    assert dcp.iboot.announcement is not None
    ep=dcp.iboot; ptr=(ep.txq.get_rptr(),ep.txq.get_wptr(),ep.rxq.get_rptr(),ep.rxq.get_wptr())
    assert ptr==(0,0,256,256),ptr
    link=TahoeLink(i,p,ptr,prefix,boot_base=base,shared=ep.iobuffer,
                   syslog_base=dcp.syslog.iobuffer,asc_base=asc)
    link.sequence=0; link.command_sequence=0; service=TahoeIBoot(link)
    hpd,nt,nc=service.getModeCount(); print('[FACT] HPD/counts',hpd,nt,nc); assert hpd
    timing=service.send_cmd(4,replen=4096); color=service.send_cmd(5,replen=4096)
    assert len(timing)==4+struct.unpack_from('<I',timing)[0]*24
    assert len(color)==4+struct.unpack_from('<I',color)[0]*24
    advertised=[timing[4+j*24:28+j*24] for j in range(nt)]
    t=next((x for fps in (60<<16,3928232) for x in advertised
            if struct.unpack('<6I',x)==(1,1280,720,fps,0,0)),None)
    assert t is not None, 'No 720p 60/59.94 flag0 timing'
    print('[FACT] Selected timing',struct.unpack('<6I',t))
    c=next(color[4+j*24:28+j*24] for j in range(nc) if struct.unpack_from('<IIIII',color,4+j*24)==(1,1,1,1,32))
    pa,dva,size=u.ba.video.base,0x13dc000,1280*720*4
    assert pa==u.adt['/vram'].reg[0].addr
    for path in ('/arm-io/dart-disp0','/arm-io/dart-dcp'):
        assert DART.from_adt(u,path).iotranslate(0,dva,size)==[(pa,size)]
    Path(prefix+'.vram-before.bin').write_bytes(i.readmem(pa,size))
    palette=[(255,255,255),(255,255,0),(0,255,255),(0,255,0),(255,0,255),(255,0,0),(0,0,255),(32,32,32)]
    row=b''.join(bytes((b,g,r,255))*160 for r,g,b in palette)
    i.writemem(pa,row*720); p.dc_cvac(pa,size)
    service.setPower(True); service.send_cmd(6,t+c)
    layer=Container(planes=[Container(addr=dva,stride=5120,addr_format=1),Container(),Container()],
                    plane_cnt=1,width=1280,height=720,surface_fmt=1,colorspace=2,eotf=1,transform=0)
    swap=service.send_cmd(15,replen=128)
    assert len(swap)==20,('swapBegin length',len(swap))
    swap_id=struct.unpack_from('<I',swap,12)[0]
    rect=struct.pack('<8I',1280,720,0,0,1280,720,0,0)
    set_layer=bytes(8)+IBootLayerInfo.build(layer)+bytes(8)+rect+bytes(4)
    assert len(set_layer)==216
    service.send_cmd(16,set_layer,replen=128); service.send_cmd(18,bytes(12),replen=128)
    print('[FACT] swapBegin/setLayer/end ACK, swap_id',swap_id)
    handoff=os.environ.get('NWOAS_SLEEP_HANDOFF')
    if handoff:
        if handoff == 'ap-first':
            print('[DESIGN] Immediate AP quiesce and IOP sleep before AFK shutdown')
            link.sleep_before_shutdown()
            print('[FACT] AP quiesce and IOP sleep ACK before AFK shutdown')
        else:
            use_close=handoff != 'no-close'
            print('[DESIGN] Immediate AFK shutdown and RTKit sleep handoff; close=',use_close)
            link.shutdown_and_sleep(send_close=use_close)
            print('[FACT] AFK shutdown, AP quiesce, and IOP sleep ACK; close=',use_close)
        p.clear32(asc+0x44,0x10)
        print('[FACT] DCP CPU stopped',hex(p.read32(asc+0x44)))
        ret=p.pmgr_reset(0,'DISP0_CPU0')
        print('[FACT] DISP0_CPU0 reset result',ret)
    else:
        end=time.monotonic()+15
        while time.monotonic()<end:
            assert not link.work(), 'Unexpected response outside command'
            time.sleep(.01)
    state=dict(boot_base=base,shared=ep.iobuffer,syslog_base=dcp.syslog.iobuffer,
               asc_base=asc,pointers=link.pointers(),sequence=link.sequence,
               command_sequence=link.command_sequence,swap_id=swap_id)
    Path(prefix+'.state.json').write_text(json.dumps(state,indent=2))
    print('[FACT] Final state',state)
finally:
    signal.alarm(0); i.dev.close()
PY
