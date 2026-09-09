#!/bin/bash
# S6-D24: ONLY added prerequisite is the successful public AV wakeDisplay.
# Reapply existing 720p mode/surface, then service BOTH active rings for 15sec.
# No reboot/reset/allocation/framebuffer remapping or firmware change.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-av-display-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import os,json,signal,struct,time
from pathlib import Path
from construct import Container
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.fw.dcp.iboot import IBootLayerInfo
from tahoe_afk_link import TahoeLink,TahoeIBoot,decode_packet
root=Path('/Volumes/X31/NWOAS');prefix=os.environ['NWOAS_PROTOCOL_LOG']
ib=json.loads((root/'nwoas_scripts/logs/tahoe-dcp-fresh-20260906-233709.c1iSXB.state.json').read_text())
av=json.loads((root/'nwoas_scripts/logs/tahoe-dcp-av-wake-20260906-234416.7kUjzO.state.json').read_text())
def timeout(*a):raise TimeoutError('AV display deadline')
signal.signal(signal.SIGALRM,timeout);signal.alarm(45)
i=UartInterface('/dev/cu.usbmodemC07HL05SQ6NY1:115200');i.dev.timeout=3;i.dev.write_timeout=3
try:
    i.cmd(i.REQ_NOP);i.reply(i.REQ_NOP);p=M1N1Proxy(i)
    links=[]
    for state,ep,iface,half in ((ib,0x23,3,0x4000),(av,0x24,5,0x2000)):
        assert p.get_base()==state['boot_base']
        ptr=tuple(p.read32(state['shared']+x) for x in (128,256,half+128,half+256))
        assert ptr[:2]==tuple(state['pointers'][:2]), 'Unexpected producer'
        link=TahoeLink(i,p,ptr,prefix+f'.ep{ep:x}',**{k:state[k] for k in ('boot_base','shared','syslog_base','asc_base')},endpoint=ep,interface=iface,half_size=half)
        link.sequence,link.command_sequence=state['sequence'],state['command_sequence']
        assert not link.work()
        links.append(link)
    link,avlink=links
    service=TahoeIBoot(link)
    print('[FACT] HPD after AV wake',service.getModeCount())
    def saved_list(index):
        path=root/f'nwoas_scripts/logs/tahoe-dcp-fresh-20260906-233709.c1iSXB.msg{index}.bin'
        return decode_packet(path.read_bytes()[8:])['payload'][16:]
    timing,color=saved_list(2),saved_list(3)
    t=next(timing[4+j*24:28+j*24] for j in range(struct.unpack_from('<I',timing)[0]) if struct.unpack_from('<6I',timing,4+j*24)==(1,1280,720,60<<16,0,0))
    c=next(color[4+j*24:28+j*24] for j in range(struct.unpack_from('<I',color)[0]) if struct.unpack_from('<IIIII',color,4+j*24)==(1,1,1,1,32))
    service.send_cmd(6,t+c)
    layer=Container(planes=[Container(addr=0x13dc000,stride=5120,addr_format=1),Container(),Container()],
                    plane_cnt=1,width=1280,height=720,surface_fmt=1,colorspace=2,eotf=1,transform=0)
    service.send_cmd(1,IBootLayerInfo.build(layer)+bytes(8))
    print('[FACT] Mode/surface ACK after AV wake')
    end=time.monotonic()+15
    while time.monotonic()<end:
        for entry in links:assert not entry.work()
        time.sleep(.01)
    for state,entry in zip((ib,av),links):
        state.update(pointers=entry.pointers(),sequence=entry.sequence,command_sequence=entry.command_sequence)
    Path(prefix+'.state.json').write_text(json.dumps(dict(iboot=ib,av=av),indent=2))
    print('[FACT] State saved; physical output remains separately observed')
finally:signal.alarm(0);i.dev.close()
PY
