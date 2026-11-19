#!/bin/bash
# s272 = nwoa4378.sys 1.0.1.12 = s271 + registry knob DbMagic, the doorbell values of Broadcom's own PCIe host driver
# bcmdhd (AOSP kernel/google-modules/wlan/bcmdhd/bcm4389 dhd_msgbuf.c) instead of brcmfmac's constant 1:
#   bit 1: every H2D doorbell = DHD_WRPTR_UPDATE_H2D_DB_MAGIC 0xFF000000 | ring id << 16 | write index
#   bit 2: after draining a D2H completion ring, doorbell DHD_RDPTR_UPDATE_H2D_DB_MAGIC 0xDD000000 | id << 16 | read
#          index ("ring doorbell once n items are read to sync with dongle", dhd_prot_upd_read_idx, DMA indices)
# macOS 13.5 Wi-Fi firmware 18.20.383 as s258r..s271.  Same procedure as run-s271.sh (copy):
#   bash wifi-s258/run-s272.sh install <old .out> <old .link> [knobs] [observe_s]   (restart Windows, deliver, W1)
#   bash wifi-s258/run-s272.sh install idle - [knobs] [observe_s]
#   bash wifi-s258/run-s272.sh knobs <cur .out> <cur .link> [knobs] [observe_s]   (1.0.1.12 already installed)
# knobs: comma list of base,both,wr,rd (default base,both); observe_s per knob default 600 (300..1800).
# Per knob: W38-s272-<k> (registry + device restart, TestConnScanSec=120) -> R2 + W2 -> W19 ping -> W2 every 300 s ->
# W2 + W19 stop + R2; at the end (and on an abort while the queue works) W39-s272-reset.  The s258o re-create
# fallback (ScanFlowCheck=1) stays on in every knob.  Rollback to s271: W40-rollback-s272.cmd.
# Result: wifi-s258/runs/s272-<mode>-<time>.txt, last line "S272 DONE ..." or "S272 ABORT <reason>".
# Not run by the agent that wrote it.  [build only] until it runs on the Mac mini.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/wifi-s258; J=$D/jobs; mkdir -p $D/runs
MODE=${1:-}; OLDOUT=${2:-}; OLDLINK=${3:-}; KNOBS=${4:-base,both}; OBS=${5:-600}
case "$MODE" in install|knobs) ;; *) echo "usage: $0 install|knobs <.out> <.link> [knobs] [observe_s]"; exit 1 ;; esac
[ -n "$OLDOUT" ] && [ -n "$OLDLINK" ] || { echo "need <.out> <.link>"; exit 1; }
case "$OBS" in ''|*[!0-9]*) echo "observe_s must be seconds"; exit 1 ;; esac
[ "$OBS" -ge 300 ] && [ "$OBS" -le 1800 ] || { echo "observe_s must be 300..1800"; exit 1; }
for k in $(echo "$KNOBS" | tr ',' ' '); do [ -f $J/W38-s272-$k.cmd ] || { echo "unknown knob $k"; exit 1; }; done
N=wifi-j274-20261119-2253-s272
MAN=$ROOT/file-delivery-s243/manifests/$N.json
FW135=18.20.383
TS=$(date +%Y%m%d-%H%M%S); RES=$D/runs/s272-$MODE-$TS.txt; LAST=$D/runs/s272-$TS.lastjob.txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
# no network names / MACs in the result: drop SSID/BSSID/profile/name lines of netsh, mask MAC-shaped tokens
san() { grep -a -v -E 'SSID|BSSID|프로필|Profile *:|^ *이름 *:|^ *Name *:' | sed -E 's/([0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}/<mac>/g'; }
L=$OLDLINK; STUCK=0; PING=0
[ "$OLDOUT" = idle ] && L=""
job() { local f=$1 j i
  [ $STUCK = 0 ] || return 1
  python3 -I $ROOT/transport-s123/queue_job.py $L $f >/dev/null || { say "queue fail $(basename $f)"; return 1; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 600); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 3; done
  grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl || { STUCK=1; say "job $j $(basename $f) not finished after 30 min"; return 1; }
  iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | head -c 60000 > $LAST
  { echo "----- job $j $(basename $f)"; head -c 20000 $LAST | san; echo; } >> $RES; }
abort() { say "ABORT: $*"
  if [ $STUCK = 0 ] && [ -n "$L" ] && [ -d "$L" ]; then
    [ $PING = 1 ] && { PING=0; job $J/W19-ping-stop.cmd; }
    job $J/W39-s272-reset.cmd && grep -a -E 'NwoaCfgS258o|S272 W39' $LAST | tee -a $RES
  fi
  rm -f $LAST; echo "S272 ABORT $*" | tee -a $RES; exit 1; }
KEYS='^ *(NwoaScanFlow |NwoaS270 |NwoaCfgS258o |NwoaHostDisassocLog |NwoaCntTxStall |NwoaStallLog |NwoaFwVersion |NwoaMb |NwoaFlowState )'
say "MODE=$MODE knobs=$KNOBS observe=${OBS}s out=$OLDOUT link=$OLDLINK"
if [ -n "$L" ]; then
  [ -d "$L" ] && [ -f "$L/events.jsonl" ] || { L=""; abort "link $OLDLINK missing"; }
  grep -q '"kind": 4' $L/events.jsonl || { L=""; abort "worker never connected on $OLDLINK"; }
elif [ $MODE = knobs ]; then
  abort "knobs mode needs the running Windows' .out and .link"
fi

# ---------------- install: restart Windows (S268 daily stack) with the s272 manifest, deliver, W1
if [ $MODE = install ]; then
  [ "$OLDOUT" = idle ] || [ -f "$OLDOUT" ] || abort "old .out $OLDOUT missing"
  python3 -I -c "import sys,json; sys.path.insert(0,sys.argv[1]); import s243_manifest as m; m.load(json.load(open(sys.argv[2])))" \
    $ROOT/file-delivery-s243 $MAN 2>&1 | tail -2 | tee -a $RES
  python3 -I -c "import sys,json; sys.path.insert(0,sys.argv[1]); import s243_manifest as m; m.load(json.load(open(sys.argv[2])))" \
    $ROOT/file-delivery-s243 $MAN >/dev/null 2>&1 || abort "manifest files do not match"
  T=$D/runs/boot-s272-$(date +%H%M%S).txt
  (env NWOAS_AUDIO_PROT=moderate NWOAS_DELIVER_MANIFEST=$MAN nohup bash $ROOT/bt-s268/run-s268.sh $OLDOUT $OLDLINK > $T 2>&1 &)
  for i in $(seq 1 300); do grep -a -q -E "S268 DONE|ABORT" $T && break; sleep 5; done
  head -c 40000 $T | san >> $RES; r=$(grep -a "S268 DONE" $T | tail -1)
  san < $T > $T.tmp && mv $T.tmp $T                                # the boot log keeps no network names either
  [ -n "$r" ] || { L=""; abort "BOOT FAIL (see $T)"; }
  L=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L"
  job $ROOT/file-delivery-s243/jobs/$N.cmd || abort "deliver driver"
  grep -a -q 'DELIVER COMPLETE' $LAST || abort "DELIVER (driver) not complete"
  job $ROOT/file-delivery-s243/jobs/$N-scripts.cmd || abort "deliver scripts"
  grep -a -q 'DELIVER COMPLETE' $LAST || abort "DELIVER (scripts) not complete"
  job $J/W1-install-wifi-s272.cmd || abort "W1"
  grep -a -E 'PNPUTIL EXIT|DriverVersion|status=' $LAST | head -6 | tee -a $RES
  grep -a 'DriverVersion' $LAST | grep -q '1\.0\.1\.12' || abort "1.0.1.12 (s272) not installed (W1)"
fi

# ---------------- check the running driver
job $J/W2-status.cmd || abort "W2 check"
grep -a -E "$KEYS" $LAST | cut -c1-300 | tee -a $RES
grep -a 'NwoaFwVersion' $LAST | grep -q "version $FW135" || abort "firmware $FW135 not loaded (NwoaFwVersion)"
grep -a 'NwoaCfgS258o' $LAST | grep -q 'DbMagic=' || abort "the running driver is not s272 (no DbMagic in NwoaCfgS258o)"

# ---------------- knob cycle
for k in $(echo "$KNOBS" | tr ',' ' '); do
  say "== knob $k"
  job $J/W38-s272-$k.cmd || abort "W38 $k"
  grep -a -E 'NwoaCfgS258o|NwoaS270|NwoaStage|DriverVersion|RESTART EXIT' $LAST | cut -c1-300 | sed "s/^/KNOB $k /" | tee -a $RES
  grep -a 'DriverVersion' $LAST | grep -q '1\.0\.1\.12' || abort "knob $k: driver is not 1.0.1.12"
  case $k in base) want='FlowSpare=0 DbLegacy=0 dar=0 db=a20 hr=a24 DbMagic=0' ;; both) want='DbLegacy=0 dar=0 db=a20 hr=a24 DbMagic=3' ;;
    wr) want='DbLegacy=0 dar=0 db=a20 hr=a24 DbMagic=1' ;; rd) want='DbLegacy=0 dar=0 db=a20 hr=a24 DbMagic=2' ;; esac
  if ! grep -a 'NwoaCfgS258o' $LAST | grep -q -F "$want"; then      # driver slow to write its diag: once more
    sleep 30; job $J/W2-status.cmd || abort "W2 knob check $k"
    grep -a 'NwoaCfgS258o' $LAST | cut -c1-300 | sed "s/^/KNOB $k /" | tee -a $RES
    grep -a 'NwoaCfgS258o' $LAST | grep -q -F "$want" || abort "knob $k not applied (want '$want')"
  fi
  sleep 30                                                     # reconnect after the device restart
  job $ROOT/bt-s268/jobs/R2-wlan-since-boot.cmd || abort "R2 start $k"; grep -a '^R2 ' $LAST | sed "s/^/R2S $k /" >> $RES
  job $J/W2-status.cmd || abort "W2 start $k"
  if ! grep -a -E '^ *NwoaFlowState ' $LAST | grep -q '0x2$'; then         # link / flowring not up yet: once more
    sleep 30; job $J/W2-status.cmd || abort "W2 start $k"; fi
  grep -a -E "$KEYS" $LAST | cut -c1-600 | sed "s/^/SNAP0 $k /" >> $RES
  if ! grep -a -E '^ *NwoaFlowState ' $LAST | grep -q '0x2$'; then
    say "knob $k: TX flowring not open 60 s after the restart (create refused or no link) -> next knob"
    grep -a -E '^ *NwoaFlowState |^ *NwoaFlowringCreate ' $LAST | tee -a $RES; echo "KNOBFAIL $k flowring-not-open" >> $RES
    job $ROOT/bt-s268/jobs/R2-wlan-since-boot.cmd || abort "R2 end $k"; grep -a '^R2 ' $LAST | sed "s/^/R2E $k /" >> $RES
    continue
  fi
  job $J/W19-ping-start-s272.cmd || abort "W19 start $k"; grep -a '^W19\|S258 W19 FAIL' $LAST | head -3 | tee -a $RES
  grep -a -q 'W19 CREATE rc=0' $LAST || abort "W19 ping not started ($k)"
  PING=1; T0=$(date +%s); n=0
  while [ $(( $(date +%s) - T0 )) -lt $OBS ]; do
    left=$(( OBS - ($(date +%s) - T0) )); [ $left -gt 300 ] && sleep 300 || sleep $left
    n=$((n+1)); job $J/W2-status.cmd || abort "W2 #$n $k"; say "$k W2 #$n ($(( $(date +%s) - T0 )) s)"
    grep -a -E '^ *(NwoaScanFlow |NwoaS270 |NwoaHostDisassocLog )' $LAST | cut -c1-300 | tee -a $RES
  done
  grep -a -E "$KEYS" $LAST | cut -c1-600 | sed "s/^/SNAP1 $k /" >> $RES
  job $J/W19-ping-stop.cmd || abort "W19 stop $k"; PING=0
  grep -a -E '^W19 (START|GW|CHG|LINES|CAP)' $LAST | head -200 | sed "s/^/PING $k /" >> $RES
  grep -a -E '^W19 END' $LAST | tail -1 | sed "s/^/PING $k /" | tee -a $RES
  job $ROOT/bt-s268/jobs/R2-wlan-since-boot.cmd || abort "R2 end $k"; grep -a '^R2 ' $LAST | sed "s/^/R2E $k /" | tee -a $RES
done

# ---------------- reset to defaults (1.0.1.12 with every knob 0 = s258r behaviour)
job $J/W39-s272-reset.cmd || abort "W39 reset"; grep -a -E 'NwoaCfgS258o|RESTART EXIT' $LAST | tee -a $RES
grep -a 'NwoaCfgS258o' $LAST | grep -q -F 'FlowSpare=0 DbLegacy=0 dar=0 db=a20 hr=a24 DbMagic=0' || say "WARN reset not confirmed in NwoaCfgS258o"

# ---------------- summary (driver counters restart at 0 with every W38 device restart: SNAP1 = that knob only)
python3 -I - "$RES" "$KNOBS" <<'EOF' | tee -a $RES
import re, sys
res, knobs = sys.argv[1], sys.argv[2].split(',')
t = open(res, encoding='utf-8', errors='replace').read()
def val(tag, k, key):
    m = re.findall(r'^' + tag + ' ' + k + r' +' + key + r'\s+REG_(?:SZ|DWORD)\s*(.*)$', t, re.M)
    return m[-1].strip() if m else ''
def kv(s):
    return {a: int(b, 0) for a, b in re.findall(r'(\w+)=(-?(?:0x)?[0-9a-fA-F]+)\b', s) if re.fullmatch(r'-?(0x[0-9a-fA-F]+|\d+)', b)}
def r2(tag, k, i):
    m = re.findall(r'^' + tag + ' ' + k + r' R2 COUNT ' + str(i) + r' (\d+)', t, re.M)
    return int(m[-1]) if m else None
print('S272 TABLE knob   tests conn alive stuck dbfix refresh rfail | spare alive/stuck/noframe/openfail | mback | '
      'ping sent lost% nogw_s | disc8003 host3/8 | v4 end')
line = []
for k in knobs:
    if re.search(r'^KNOBFAIL ' + k + ' ', t, re.M):
        print('S272 TABLE %-6s flowring not open after the knob restart (create refused / no link): no verdict' % k)
        line.append('%s:NOT_OPEN' % k)
        continue
    sf = kv(val('SNAP1', k, 'NwoaScanFlow')); s7 = kv(val('SNAP1', k, 'NwoaS270'))
    host = val('SNAP1', k, 'NwoaHostDisassocLog')
    h3 = len(re.findall(r'(?:^|[^0-9])3@', host)); h8 = len(re.findall(r'(?:^|[^0-9])8@', host))
    a, b = r2('R2S', k, 8003), r2('R2E', k, 8003)
    disc = (b - a) if a is not None and b is not None else -1
    end = re.findall(r'^PING ' + k + r' W19 END t_ms=(\d+) ok=(\d+) bad=(\d+)', t, re.M)
    ok, bad, tend = (int(end[-1][1]), int(end[-1][2]), int(end[-1][0])) if end else (0, 0, 0)
    chg = [(int(x), s) for x, s in re.findall(r'^PING ' + k + r' W19 CHG t_ms=(\d+) (\S+)', t, re.M)]
    nogw = 0
    for i, (x, s) in enumerate(chg):
        if s == 'NOGW':
            nogw += round(((chg[i + 1][0] if i + 1 < len(chg) else tend) - x) / 1000)
    loss = 100.0 * bad / (ok + bad) if ok + bad else 100.0
    prof = re.findall(r'^R2E ' + k + r' R2 PROF (v4=\S+)', t, re.M)
    v4 = prof[-1] if prof else 'v4=?'
    print('S272 TABLE %-6s %5d %4d %5d %5d %5d %7d %5d | spare %d/%d/%d/%d | %5d | %5d %5.2f %4d | %3d %d/%d | %s' % (
        k, sf.get('test', -1), sf.get('conn', -1), sf.get('alive', -1), sf.get('stuck', -1), sf.get('dbfix', -1),
        sf.get('refresh', -1), sf.get('refreshfail', -1), s7.get('alive', -1), s7.get('stuck', -1),
        s7.get('noframe', -1), s7.get('openfail', -1), s7.get('mback', -1), ok + bad, loss, nogw, disc, h3, h8, v4))
    line.append('%s:alive%d/stuck%d/ref%d/loss%.1f%%/disc%d/%s' % (k, sf.get('alive', -1), sf.get('stuck', -1),
                sf.get('refresh', -1), loss, disc, v4.replace('v4=', '')))
print('S272 READ: a knob that removes the stall shows stuck 0 and alive >= 3 where base shows stuck >= 3')
print('S272 DONE | ' + ' | '.join(line))
EOF
rm -f $LAST
