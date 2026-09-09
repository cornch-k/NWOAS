#!/bin/bash
# S6-D19: enumerate endpoint0x24 AV controller services on the exact live D18 boot.
# Based on public m1n1 experiments/dcp_iboot.py. No video/power commands yet.
# Zero reboot/reset; isolated extra host allocations and unused IOVAs.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-av-enumerate-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import os,signal,struct,time,json,subprocess
from pathlib import Path
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils
from m1n1.hw.dart import DART
from m1n1.fw.afk.rbep import AFKRingBufEndpoint
from m1n1.fw.dcp.iboot import DCPIBootClient
from m1n1.fw.asc.syslog import ASCSysLogEndpoint
from m1n1.fw.common import OSSerialize
from tahoe_afk_link import TahoeLink,decode_packet
root=Path('/Volumes/X31/NWOAS');prefix=os.environ['NWOAS_PROTOCOL_LOG']
state=json.loads((root/'nwoas_scripts/logs/tahoe-dcp-fresh-20260906-233709.c1iSXB.state.json').read_text())
class AVEndpoint(AFKRingBufEndpoint):
    SHORT='av'
    def __init__(self,*a,**k):super().__init__(*a,**k);self.announcements=[]
    def handle_ipc(self,data):
        Path(prefix+f'.announce{len(self.announcements)}.bin').write_bytes(struct.pack('<4sI',b'IOP ',len(data)-8)+data)
        packet=decode_packet(data)
        assert (packet['kind'],packet['category'])==(0x11,0)
        payload=packet.pop('payload')
        packet['name']=payload[:32].split(b'\0',1)[0].decode('ascii')
        packet['properties']=OSSerialize().parse(payload[32:])
        self.announcements.append(packet)
        print('[FACT] Announcement',packet,flush=True)
class Client(DCPIBootClient):
    ENDPOINTS={**DCPIBootClient.ENDPOINTS,0x24:AVEndpoint}
port='/dev/cu.usbmodemC07HL05SQ6NY1'
assert not subprocess.run(['/usr/sbin/lsof','-t',port],capture_output=True).stdout.strip()
def timeout(*a):raise TimeoutError('AV enumeration deadline')
signal.signal(signal.SIGALRM,timeout);signal.alarm(25)
i=UartInterface(port+':115200');i.dev.timeout=3;i.dev.write_timeout=3
try:
    i.cmd(i.REQ_NOP);i.reply(i.REQ_NOP);p=M1N1Proxy(i)
    link=TahoeLink(i,p,tuple(state['pointers']),prefix,**{k:state[k] for k in ('boot_base','shared','syslog_base','asc_base')})
    assert not link.work()
    u=ProxyUtils(p);u.malloc(0x2000000)
    dart=DART.from_adt(u,'/arm-io/dart-dcp',iova_range=(0x82000000,0x83000000))
    assert dart.iotranslate(0,0x82000000,0x10000)==[(None,0x10000)]
    dcp=Client(u,state['asc_base'],dart);dcp.dva_offset=u.adt['/arm-io/dcp'][0].asc_dram_mask
    syslog=ASCSysLogEndpoint(dcp,2)
    syslog.count,syslog.entrysize,syslog.iobuffer,syslog.started=63,128,state['syslog_base'],True
    syslog.iobuffer_dva=0xf80028000
    assert dart.iotranslate(0,0x80028000,160)==[(state['syslog_base'],160)]
    dcp.add_ep(2,syslog)
    dcp.start_ep(0x24)
    end=time.monotonic()+5
    while time.monotonic()<end:dcp.work();time.sleep(.001)
    ep=dcp.av
    assert ep.announcements, 'No AV announcements'
    result=dict(boot_base=state['boot_base'],shared=ep.iobuffer,syslog_base=state['syslog_base'],asc_base=state['asc_base'],
                endpoint=0x24,announcements=ep.announcements,pointers=(ep.txq.get_rptr(),ep.txq.get_wptr(),ep.rxq.get_rptr(),ep.rxq.get_wptr()))
    Path(prefix+'.state.json').write_text(json.dumps(result,indent=2))
    print('[FACT] AV state',result)
finally:signal.alarm(0);i.dev.close()
PY
