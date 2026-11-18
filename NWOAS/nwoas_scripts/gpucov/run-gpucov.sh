#!/bin/bash
# GPU coverage kit runner (host side). Nothing here touches Wi-Fi/BT/sound/power files.
#   bash gpucov/run-gpucov.sh deliver <old launcher .out> <old .link>
#       restart Windows -> $LAUNCHER (default agxdcp-s256) with the 2758+gpucov manifest ->
#       deliver ONLY the kit to C:\NWOAS\gpucov (driver unchanged) -> run the kit
#   bash gpucov/run-gpucov.sh run <link>
#       run the kit on a session that is already up (e.g. after run-install-2758.sh)
#   MAN=<manifest> DJOB=<delivery job> to deliver another kit file set (e.g. probe3)
#   KIT="K6 K2 K4" to choose jobs (default: K6 K1 K2 K4 K5 K7; K3 fill only when named)
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
J=$ROOT/gpucov/jobs
RES=$ROOT/gpucov/runs/gpucov-$(date "+%Y%m%d-%H%M%S").txt
LAUNCHER=${LAUNCHER:-agxdcp-s256-guest-test.sh}
KIT=${KIT:-"K6 K1 K2 K4 K5 K7"}
MAN=${MAN:-$ROOT/file-delivery-s243/manifests/agx-j274-2758-gpucov.json}
DJOB=${DJOB:-$ROOT/file-delivery-s243/jobs/gpucov-j274-20261118.cmd}
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
jobwait() { local L=$1 j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBLOG=$L/job-$j.log; }
runjob() { python3 -I $ROOT/transport-s123/queue_job.py $1 $2 >/dev/null || { say "ABORT queue $2"; exit 3; }; jobwait $1
  iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | head -c 400000 | tee -a $RES; }
MODE=${1:-}
if [ "$MODE" = deliver ]; then
  OLDOUT=$2; OLDLINK=$3
  say "START deliver old=$OLDLINK launcher=$LAUNCHER"
  python3 -I $ROOT/transport-s123/queue_job.py $OLDLINK $ROOT/layout-s231/jobs/W4-restart.cmd >/dev/null || { say "ABORT queue W4"; exit 3; }
  for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $OLDOUT && break; sleep 2; done
  grep -q "LAUNCHER EXIT" $OLDOUT || { say "ABORT: old launcher did not exit"; exit 4; }
  for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
  sleep 4
  LTAG=$(echo $LAUNCHER | sed -n "s/^agxdcp-s\([0-9]*\).*/\1/p")
  CUR=$ROOT/logs/launcher-agxdcp${LTAG}-gpucov-$(date "+%Y%m%d-%H%M%S").out
  (NWOAS_DELIVER_MANIFEST=$MAN caffeinate -dims bash $ROOT/$LAUNCHER > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
  say "launched $CUR"
  L=""
  for i in $(seq 1 240); do LOG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LOG" ] && L=$LOG.link
    [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
    grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: launcher exited before worker"; tail -20 $CUR | tee -a $RES; exit 2; }
    sleep 5; done
  grep -q '"kind": 4' $L/events.jsonl 2>/dev/null || { say "ABORT worker timeout"; exit 2; }
  say "worker connected link=$L"
  runjob $L $DJOB
  grep -a -q "S243 DELIVER COMPLETE" $JOBLOG || { say "ABORT kit delivery"; exit 5; }
  say "D28 rearm before the kit (G3Armed + GPU restart, as after every relaunch)"
  runjob $L $ROOT/agx-s248/jobs/D28-rearm.cmd
elif [ "$MODE" = run ]; then
  L=$2
else
  echo "usage: $0 deliver <old.out> <old.link> | run <link>"; exit 1
fi
for k in $KIT; do f=$(ls $J/$k-*.cmd | head -1); say "== $k $(basename $f)"; runjob $L $f; done
say "KIT DONE link=$L"
