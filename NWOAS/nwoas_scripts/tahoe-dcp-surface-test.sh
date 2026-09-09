#!/bin/bash
# S6-D10 continuation: mode was sent; collect pending mode ACK, then set surface once.
# PASS stages: command replies; visible pattern still requires observation.
# Post-power state only. Zero reboots/reset/allocations/remapping. VRAM already staged.
# No Windows guest or boot-argument changes in this experiment.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-surface-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import os,signal,struct,subprocess
from pathlib import Path
from construct import Container
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils
from m1n1.hw.dart import DART
from m1n1.fw.dcp.iboot import IBootLayerInfo
from tahoe_afk_link import TahoeLink,TahoeIBoot,decode_packet
root=Path('/Volumes/X31/NWOAS')
def list_payload(number):
    frame=(root/f'nwoas_scripts/logs/tahoe-dcp-list-20260906-231314.Gn6G8b.msg{number}.bin').read_bytes()
    return decode_packet(frame[8:])['payload'][16:]
timings,colors=list_payload(2),list_payload(3)
nt,nc=struct.unpack_from('<I',timings)[0],struct.unpack_from('<I',colors)[0]
assert len(timings)==4+nt*24 and len(colors)==4+nc*24
t=next(timings[4+j*24:28+j*24] for j in range(nt) if struct.unpack_from('<IIII',timings,4+j*24)==(1,1920,1080,60<<16))
c=next(colors[4+j*24:28+j*24] for j in range(nc) if struct.unpack_from('<IIIII',colors,4+j*24)==(1,1,1,1,32))
port='/dev/cu.usbmodemC07HL05SQ6NY1'
assert not subprocess.run(['/usr/sbin/lsof','-t',port],capture_output=True).stdout.strip()
def deadline(*a):raise TimeoutError('Pattern experiment deadline')
signal.signal(signal.SIGALRM,deadline);signal.alarm(50)
i=UartInterface(port+':115200');i.dev.timeout=3;i.dev.write_timeout=3
try:
    i.cmd(i.REQ_NOP);i.reply(i.REQ_NOP);p=M1N1Proxy(i)
    link=TahoeLink(i,p,(1024,1024,2816,2944),os.environ['NWOAS_PROTOCOL_LOG'])
    link.sequence,link.command_sequence=7,6
    u=ProxyUtils(p)
    pa,dva,size=0xbe3f60000,0x13dc000,1920*1080*4
    assert u.ba.video.base==pa
    assert u.adt['/vram'].reg[0].size>=size
    for path in ('/arm-io/dart-disp0','/arm-io/dart-dcp'):
        dart=DART.from_adt(u,path)
        assert dart.iotranslate(0,dva,size)==[(pa,size)], 'Framebuffer mapping differs'
    service=TahoeIBoot(link)
    pending=link.work()
    assert len(pending)==1
    reply=pending[0]
    assert (reply['interface'],reply['kind'],reply['category'])==(3,192,2)
    assert reply['payload']==struct.pack('<BBHI',0,5,0,0), reply
    print('[FACT] Previously sent 1080p60 mode ACK received; not resent')
    layer=Container(planes=[Container(addr=dva,stride=1920*4,addr_format=1),Container(),Container()],
                    plane_cnt=1,width=1920,height=1080,surface_fmt=1,colorspace=2,eotf=1,transform=0)
    service.send_cmd(1,IBootLayerInfo.build(layer))
    print('[FACT] Color-bar surface command ACK; physical image not yet observed')
    print('[FACT] pointers',link.pointers())
finally:
    signal.alarm(0);i.dev.close()
PY
