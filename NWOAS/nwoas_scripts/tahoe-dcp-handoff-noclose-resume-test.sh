#!/bin/bash
# S6-D30 list-resume: the prior run stopped after read-only enumeration because
# its exact 720p60 selector was absent. Reattach to that exact three-command
# state, recheck HPD, use the advertised 1280x720@59.94 flag0 entry, then
# mode+swap and the no-CLOSE handoff. No reboot or repeated list commands.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-handoff-noclose-resume-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import json,os,signal,struct,subprocess,time
from pathlib import Path
from construct import Container
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils
from m1n1.hw.dart import DART
from m1n1.fw.dcp.iboot import IBootLayerInfo
from tahoe_afk_link import TahoeLink,TahoeIBoot,decode_packet

root=Path('/Volumes/X31/NWOAS'); prefix=os.environ['NWOAS_PROTOCOL_LOG']
source=root/'nwoas_scripts/logs/tahoe-dcp-swap-20260907-002528.otJ9ds'
port='/dev/cu.usbmodemC07HL05SQ6NY1'; base=0x803c9c000
shared=0x80d43c000; syslog=0x80d438000; asc=0x231c00000
expected=(384,384,2560,2560)
assert not subprocess.run(['/usr/sbin/lsof','-t',port],capture_output=True).stdout.strip()
def response_payload(path):
    packet=decode_packet(Path(path).read_bytes()[8:])
    return packet['payload'][16:]
timing=response_payload(str(source)+'.msg2.bin')
color=response_payload(str(source)+'.msg3.bin')
nt,nc=struct.unpack_from('<I',timing)[0],struct.unpack_from('<I',color)[0]
t=next(timing[4+j*24:28+j*24] for j in range(nt)
       if struct.unpack_from('<6I',timing,4+j*24)==(1,1280,720,3928232,0,0))
c=next(color[4+j*24:28+j*24] for j in range(nc)
       if struct.unpack_from('<IIIII',color,4+j*24)==(1,1,1,1,32))
def deadline(*a): raise TimeoutError('D30 resume deadline')
signal.signal(signal.SIGALRM,deadline); signal.alarm(40)
i=UartInterface(port+':115200'); i.dev.timeout=3; i.dev.write_timeout=3
try:
    i.cmd(i.REQ_NOP); i.reply(i.REQ_NOP); p=M1N1Proxy(i); u=ProxyUtils(p)
    link=TahoeLink(i,p,expected,prefix,boot_base=base,shared=shared,
                   syslog_base=syslog,asc_base=asc)
    link.sequence=3; link.command_sequence=3; service=TahoeIBoot(link)
    hpd,live_nt,live_nc=service.getModeCount()
    print('[FACT] Live HPD/counts',hpd,live_nt,live_nc); assert (hpd,live_nt,live_nc)==(True,nt,nc)
    pa,dva,size=u.ba.video.base,0x13dc000,1280*720*4
    for path in ('/arm-io/dart-disp0','/arm-io/dart-dcp'):
        assert DART.from_adt(u,path).iotranslate(0,dva,size)==[(pa,size)]
    Path(prefix+'.vram-before.bin').write_bytes(i.readmem(pa,size))
    palette=[(255,255,255),(255,255,0),(0,255,255),(0,255,0),(255,0,255),(255,0,0),(0,0,255),(32,32,32)]
    row=b''.join(bytes((b,g,r,255))*160 for r,g,b in palette)
    i.writemem(pa,row*720); p.dc_cvac(pa,size)
    service.setPower(True); service.send_cmd(6,t+c)
    layer=Container(planes=[Container(addr=dva,stride=5120,addr_format=1),Container(),Container()],
                    plane_cnt=1,width=1280,height=720,surface_fmt=1,colorspace=2,eotf=1,transform=0)
    swap=service.send_cmd(15,replen=128); assert len(swap)==20
    swap_id=struct.unpack_from('<I',swap,12)[0]
    rect=struct.pack('<8I',1280,720,0,0,1280,720,0,0)
    set_layer=bytes(8)+IBootLayerInfo.build(layer)+bytes(8)+rect+bytes(4)
    service.send_cmd(16,set_layer,replen=128); service.send_cmd(18,bytes(12),replen=128)
    print('[FACT] 59.94 mode and swap ACK, swap_id',swap_id)
    link.shutdown_and_sleep(send_close=False)
    print('[FACT] AFK shutdown, AP quiesce, IOP sleep ACK')
    p.clear32(asc+0x44,0x10); print('[FACT] DCP CPU stopped',hex(p.read32(asc+0x44)))
    ret=p.pmgr_reset(0,'DISP0_CPU0'); print('[FACT] DISP0_CPU0 reset result',ret)
    state=dict(boot_base=base,shared=shared,syslog_base=syslog,asc_base=asc,
               pointers=link.pointers(),sequence=link.sequence,
               command_sequence=link.command_sequence,swap_id=swap_id)
    Path(prefix+'.state.json').write_text(json.dumps(state,indent=2))
finally:
    signal.alarm(0); i.dev.close()
PY
