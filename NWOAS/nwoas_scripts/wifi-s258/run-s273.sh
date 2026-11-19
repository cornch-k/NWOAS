#!/bin/bash
# s273 = nwoa4378.sys 1.0.1.13 = s272 + registry knob FwSnap (observation only, ring handling unchanged):
# for the first 6 scans made while connected the driver copies the firmware RAM before the escan, at the stuck /
# alive verdict (before any fix) and 0.3 s after the flowring re-create, and writes the differing words to
# NwoaSnap<k>* (connected BSSID, own MAC and network name overwritten with 0xEE before the compare).
# macOS 13.5 Wi-Fi firmware 18.20.383 as s258r..s272.  Same procedure as run-s272.sh (copy):
#   bash wifi-s258/run-s273.sh install <old .out> <old .link> [knobs] [observe_s]   (restart Windows, deliver, W1)
#   bash wifi-s258/run-s273.sh install idle - [knobs] [observe_s]
#   bash wifi-s258/run-s273.sh knobs <cur .out> <cur .link> [knobs] [observe_s]   (1.0.1.13 already installed)
# knobs: comma list of snap,home (default snap,home); observe_s per knob default 780 (300..1800).
#   snap = test scan every 120 s that leaves the channel (the stall), home = home channel only (no stall: control)
# Per knob: W41-s273-<k> (registry + device restart) -> R2 + W2s -> W19 ping -> W2s every 300 s -> W2s + W19 stop
# -> W43-s273-dump-0..5 (one job per scan, saved raw to runs/s273-<time>-<knob>-snap<k>.txt) -> R2; at the end
# (and on an abort while the queue works) W42-s273-reset.  The s258o re-create fallback stays on.
# Rollback to s272: W44-rollback-s273.cmd.  Offline reading: python3 -I wifi-s258/analyze-s273.py runs/s273-<time>
# Result: wifi-s258/runs/s273-<mode>-<time>.txt, last line "S273 DONE ..." or "S273 ABORT <reason>".
# Not run by the session that wrote it.  [build only] until it runs on the Mac mini.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/wifi-s258; J=$D/jobs; mkdir -p $D/runs
MODE=${1:-}; OLDOUT=${2:-}; OLDLINK=${3:-}; KNOBS=${4:-snap,home}; OBS=${5:-780}
case "$MODE" in install|knobs) ;; *) echo "usage: $0 install|knobs <.out> <.link> [knobs] [observe_s]"; exit 1 ;; esac
[ -n "$OLDOUT" ] && [ -n "$OLDLINK" ] || { echo "need <.out> <.link>"; exit 1; }
case "$OBS" in ''|*[!0-9]*) echo "observe_s must be seconds"; exit 1 ;; esac
[ "$OBS" -ge 300 ] && [ "$OBS" -le 1800 ] || { echo "observe_s must be 300..1800"; exit 1; }
for k in $(echo "$KNOBS" | tr ',' ' '); do [ -f $J/W41-s273-$k.cmd ] || { echo "unknown knob $k"; exit 1; }; done
N=wifi-j274-20261119-2337-s273fw
MAN=$ROOT/file-delivery-s243/manifests/$N.json
FW135=18.20.383
TS=$(date +%Y%m%d-%H%M%S); RES=$D/runs/s273-$MODE-$TS.txt; LAST=$D/runs/s273-$TS.lastjob.txt
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
  iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | head -c 600000 > $LAST
  { echo "----- job $j $(basename $f)"; head -c 20000 $LAST | san; echo; } >> $RES; }
abort() { say "ABORT: $*"
  if [ $STUCK = 0 ] && [ -n "$L" ] && [ -d "$L" ]; then
    [ $PING = 1 ] && { PING=0; job $J/W19-ping-stop.cmd; }
    job $J/W42-s273-reset.cmd && grep -a -E 'NwoaCfgS258o|S273 W42' $LAST | tee -a $RES
  fi
  rm -f $LAST; echo "S273 ABORT $*" | tee -a $RES; exit 1; }
KEYS='^ *(NwoaSnap[0-9] |NwoaScanFlow |NwoaS270 |NwoaCfgS258o |NwoaHostDisassocLog |NwoaCntTxStall |NwoaStallLog |NwoaFwVersion |NwoaMb |NwoaFlowState )'
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
  T=$D/runs/boot-s273-$(date +%H%M%S).txt
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
  job $J/W1-install-wifi-s273.cmd || abort "W1"
  grep -a -E 'PNPUTIL EXIT|DriverVersion|status=' $LAST | head -6 | tee -a $RES
  grep -a 'DriverVersion' $LAST | grep -q '1\.0\.1\.13' || abort "1.0.1.13 (s273) not installed (W1)"
fi

# ---------------- check the running driver
job $J/W2s-status-s273.cmd || abort "W2 check"
grep -a -E "$KEYS" $LAST | cut -c1-300 | tee -a $RES
grep -a 'NwoaFwVersion' $LAST | grep -q "version $FW135" || abort "firmware $FW135 not loaded (NwoaFwVersion)"
grep -a 'NwoaCfgS258o' $LAST | grep -q 'FwSnap=' || abort "the running driver is not s273 (no FwSnap in NwoaCfgS258o)"

# ---------------- knob cycle
for k in $(echo "$KNOBS" | tr ',' ' '); do
  say "== knob $k"
  job $J/W41-s273-$k.cmd || abort "W41 $k"
  grep -a -E 'NwoaCfgS258o|NwoaS270|NwoaStage|DriverVersion|RESTART EXIT' $LAST | cut -c1-300 | sed "s/^/KNOB $k /" | tee -a $RES
  grep -a 'DriverVersion' $LAST | grep -q '1\.0\.1\.13' || abort "knob $k: driver is not 1.0.1.13"
  want='FwSnap=1'
  if ! grep -a 'NwoaCfgS258o' $LAST | grep -q -F "$want"; then      # driver slow to write its diag: once more
    sleep 30; job $J/W2s-status-s273.cmd || abort "W2 knob check $k"
    grep -a 'NwoaCfgS258o' $LAST | cut -c1-300 | sed "s/^/KNOB $k /" | tee -a $RES
    grep -a 'NwoaCfgS258o' $LAST | grep -q -F "$want" || abort "knob $k not applied (want '$want')"
  fi
  sleep 30                                                     # reconnect after the device restart
  job $ROOT/bt-s268/jobs/R2-wlan-since-boot.cmd || abort "R2 start $k"; grep -a '^R2 ' $LAST | sed "s/^/R2S $k /" >> $RES
  job $J/W2s-status-s273.cmd || abort "W2 start $k"
  if ! grep -a -E '^ *NwoaFlowState ' $LAST | grep -q '0x2$'; then         # link / flowring not up yet: once more
    sleep 30; job $J/W2s-status-s273.cmd || abort "W2 start $k"; fi
  grep -a -E "$KEYS" $LAST | cut -c1-600 | sed "s/^/SNAP0 $k /" >> $RES
  if ! grep -a -E '^ *NwoaFlowState ' $LAST | grep -q '0x2$'; then
    say "knob $k: TX flowring not open 60 s after the restart (create refused or no link) -> next knob"
    grep -a -E '^ *NwoaFlowState |^ *NwoaFlowringCreate ' $LAST | tee -a $RES; echo "KNOBFAIL $k flowring-not-open" >> $RES
    job $ROOT/bt-s268/jobs/R2-wlan-since-boot.cmd || abort "R2 end $k"; grep -a '^R2 ' $LAST | sed "s/^/R2E $k /" >> $RES
    continue
  fi
  job $J/W19-ping-start-s273.cmd || abort "W19 start $k"; grep -a '^W19\|S258 W19 FAIL' $LAST | head -3 | tee -a $RES
  grep -a -q 'W19 CREATE rc=0' $LAST || abort "W19 ping not started ($k)"
  PING=1; T0=$(date +%s); n=0
  while [ $(( $(date +%s) - T0 )) -lt $OBS ]; do
    left=$(( OBS - ($(date +%s) - T0) )); [ $left -gt 300 ] && sleep 300 || sleep $left
    n=$((n+1)); job $J/W2s-status-s273.cmd || abort "W2 #$n $k"; say "$k W2 #$n ($(( $(date +%s) - T0 )) s)"
    grep -a -E '^ *(NwoaSnap[0-9] |NwoaScanFlow |NwoaHostDisassocLog )' $LAST | cut -c1-300 | tee -a $RES
  done
  grep -a -E "$KEYS" $LAST | cut -c1-600 | sed "s/^/SNAP1 $k /" >> $RES
  for q in 0 1 2 3 4 5; do                                    # raw NwoaSnap<q>* of this knob, one file per scan
    job $J/W43-s273-dump-$q.cmd || abort "W43 dump $q $k"
    grep -a -E '^ *NwoaSnap' $LAST > $D/runs/s273-$TS-$k-snap$q.txt; say "$k dump $q: $(wc -c < $D/runs/s273-$TS-$k-snap$q.txt | tr -d ' ') bytes $(grep -a -E '^ *NwoaSnap[0-9] ' $LAST | cut -c1-300)"
    [ -s $D/runs/s273-$TS-$k-snap$q.txt ] || rm -f $D/runs/s273-$TS-$k-snap$q.txt
  done
  job $J/W19-ping-stop.cmd || abort "W19 stop $k"; PING=0
  grep -a -E '^W19 (START|GW|CHG|LINES|CAP)' $LAST | head -200 | sed "s/^/PING $k /" >> $RES
  grep -a -E '^W19 END' $LAST | tail -1 | sed "s/^/PING $k /" | tee -a $RES
  job $ROOT/bt-s268/jobs/R2-wlan-since-boot.cmd || abort "R2 end $k"; grep -a '^R2 ' $LAST | sed "s/^/R2E $k /" | tee -a $RES
done

# ---------------- reset to defaults (1.0.1.12 with every knob 0 = s258r behaviour)
job $J/W42-s273-reset.cmd || abort "W39 reset"; grep -a -E 'NwoaCfgS258o|RESTART EXIT' $LAST | tee -a $RES
grep -a 'NwoaCfgS258o' $LAST | grep -q -F 'FwSnap=0' || say "WARN reset not confirmed in NwoaCfgS258o"
line=""
for k in $(echo "$KNOBS" | tr ',' ' '); do
  sf=$(grep -a "^SNAP1 $k  *NwoaScanFlow " $RES | tail -1 | sed -n 's/.*\(alive=[0-9]*\) \(stuck=[0-9]*\).*\(refresh=[0-9]*\).*/\1 \2 \3/p')
  line="$line | $k: $sf dumps=$(ls $D/runs/s273-$TS-$k-snap*.txt 2>/dev/null | wc -l | tr -d ' ')"
done
python3 -I $D/analyze-s273.py $D/runs/s273-$TS 2>&1 | head -c 60000 | tee -a $RES
echo "S273 DONE$line" | tee -a $RES
rm -f $LAST
