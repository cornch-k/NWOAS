#!/bin/bash
# S6-D22: repeat read-only AV getPower ONLY after verified standard-open success.
# Public command: DCPAVControllerService.getPower, group8/cmd9.
# Exact post-D19 state, one OPEN report then one command; no reboot/modeset.
set -eu
ROOT=/Volumes/X31/NWOAS
export PYTHONDONTWRITEBYTECODE=1 M1N1_KEEP_BAUD=1
export PYTHONPATH="$ROOT/m1n1_windows/proxyclient:$ROOT/nwoas_scripts"
LOG=$(mktemp "$ROOT/nwoas_scripts/logs/tahoe-dcp-av-power-opened-$(date '+%Y%m%d-%H%M%S').XXXXXX")
export NWOAS_PROTOCOL_LOG="$LOG"
echo "Log: $LOG"
exec "$ROOT/m1n1_windows/.venv-hv/bin/python3" -u - > "$LOG" 2>&1 <<'PY'
import os,json,signal,struct
from pathlib import Path
from m1n1.proxy import UartInterface,M1N1Proxy
from tahoe_afk_link import TahoeLink
prefix=os.environ['NWOAS_PROTOCOL_LOG']
state=json.loads(Path('/Volumes/X31/NWOAS/nwoas_scripts/logs/tahoe-dcp-av-enumerate-20260906-233925.U0K1hN.state.json').read_text())
def timeout(*a):raise TimeoutError('AV query deadline')
signal.signal(signal.SIGALRM,timeout);signal.alarm(20)
i=UartInterface('/dev/cu.usbmodemC07HL05SQ6NY1:115200');i.dev.timeout=3;i.dev.write_timeout=3
try:
    i.cmd(i.REQ_NOP);i.reply(i.REQ_NOP);p=M1N1Proxy(i)
    link=TahoeLink(i,p,(640,640,1792,1792),prefix,**{k:state[k] for k in ('boot_base','shared','syslog_base','asc_base','endpoint')},interface=5,half_size=0x2000)
    link.sequence,link.command_sequence=3,2
    msg=struct.pack('<2xHIII48x',8,9,32,0x69706378)+bytes(32)
    response=link.command(msg,96)
    assert len(response)>=64
    group,cmd,length,magic=struct.unpack_from('<2xHIII',response)
    assert (group,cmd,length,magic)==(8,9,32,0x69706378)
    print('[FACT] AV power',struct.unpack_from('<I',response,80)[0])
    print('[FACT] raw response',response.hex())
    state.update(pointers=link.pointers(),sequence=link.sequence,command_sequence=link.command_sequence)
    Path(prefix+'.state.json').write_text(json.dumps(state,indent=2))
finally:signal.alarm(0);i.dev.close()
PY
