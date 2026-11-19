#!/bin/bash
# S265: start Windows with agxdcp-s265-guest-test.sh (S259 stack + the AGX pool window at 0xd00000000) ->
# optional kit delivery (MAN=<manifest> DJOB=<delivery job>) -> D28 (G3Armed + GPU device restart; with the
# pool on, D28 is the first run of the KMD stop path through the pool window, the 11-16 g247B crash site) ->
# prints the hypervisor pool lines and "S265 BOOT DONE link=... out=... log=... pool=<0|1>".
#   bash gpupool-s265/boot-s265.sh <current launcher .out> <current LOG.link>   (Windows running: W4 first)
#   bash gpupool-s265/boot-s265.sh idle -                                      (Mac mini idle at the D4 proxy)
# NWOAS_S265_POOL=0 in the environment = the same launcher with the pool off (S259 configuration).
# Every job wait also stops on "LAUNCHER EXIT" or an EL2 exception in the HV console (no 40-minute hang).
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
R=$ROOT/gpupool-s265/runs; mkdir -p $R
RES=$R/boot-s265-$(date "+%Y%m%d-%H%M%S").txt
W4=$ROOT/layout-s231/jobs/W4-restart.cmd
OLDOUT=$1; OLDLINK=$2
MAN=${MAN:-}; DJOB=${DJOB:-}
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
hvdead() { grep -a -q -E 'Exception taken from EL2|Unhandled exception' "$LOG.vuart" 2>/dev/null || grep -q "LAUNCHER EXIT" $CUR; }
runjob() { python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || { say "ABORT queue $1"; exit 6; }
  local j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break
    hvdead && { say "ABORT hypervisor/launcher gone during $(basename $1)"; grep -a -n -B3 -A12 'Exception taken from EL2' "$LOG.vuart" | head -40 | tee -a $RES; tail -5 $CUR | tee -a $RES; exit 7; }
    sleep 5; done
  JOBLOG=$L/job-$j.log; iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | head -c 60000 | tee -a $RES; }
say "START old=$OLDLINK NWOAS_S265_POOL=${NWOAS_S265_POOL:-1} MAN=${MAN:-none}"
if [ "$OLDOUT" = idle ]; then
  M1=/Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows
  M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1 \
    "$M1/.venv-hv/bin/python3" -I -B $M1/proxyclient/tools/reboot.py >> $RES 2>&1 || say "reboot.py rc=$?"
  for i in $(seq 1 30); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] || break; sleep 1; done
  say "idle start: m1n1 rebooted (proxy device gone after ${i}s)"
else
  python3 -I $ROOT/transport-s123/queue_job.py $OLDLINK $W4 >/dev/null || { say "ABORT queue W4"; exit 3; }
  for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $OLDOUT && break; sleep 2; done
  grep -q "LAUNCHER EXIT" $OLDOUT || { say "ABORT: old launcher did not exit"; exit 4; }
  OLDLOG=$(sed -n "s/^Log: //p" $OLDOUT | head -1)
  # the shutdown of a pool-on boot is the KMD stop path too
  if [ -n "$OLDLOG" ] && grep -a -q -E 'Exception taken from EL2|Unhandled exception' "$OLDLOG.vuart" 2>/dev/null; then
    say "OLD BOOT HV EXCEPTION at shutdown (see $OLDLOG.vuart)"; grep -a -n -B3 -A12 'Exception taken from EL2' "$OLDLOG.vuart" | head -40 | tee -a $RES
  else say "old launcher exited, no EL2 exception in its HV console"; fi
fi
for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 4
CUR=$ROOT/logs/launcher-agxdcp265-pool-$(date "+%Y%m%d-%H%M%S").out
if [ -n "$MAN" ]; then
  (NWOAS_DELIVER_MANIFEST=$MAN PYTHONDONTWRITEBYTECODE=1 caffeinate -dims bash $ROOT/agxdcp-s265-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
else
  (PYTHONDONTWRITEBYTECODE=1 caffeinate -dims bash $ROOT/agxdcp-s265-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
fi
say "launched $CUR"
L=""; LOG=""
for i in $(seq 1 240); do LOG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LOG" ] && L=$LOG.link
  [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
  grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: launcher exited before worker"; tail -20 $CUR | tee -a $RES
    grep -a -E 'NWOAS-AGX|Exception' "$LOG.vuart" 2>/dev/null | head -30 | tee -a $RES; exit 2; }
  sleep 5; done
grep -q '"kind": 4' $L/events.jsonl 2>/dev/null || { say "ABORT worker timeout"; exit 2; }
say "worker connected link=$L"
say "hypervisor AGX lines:"; grep -a -E 'NWOAS-AGX: (pool window|GPU broker table|MEDIATE armed|install REFUSED|local reserve)' "$LOG.vuart" | head -12 | tee -a $RES
POOL=0; grep -a -q 'MEDIATE armed: .* pool=1 ' "$LOG.vuart" && grep -a -q 'pool window v1 IPA=0xd00000000 bytes=0x100000000' "$LOG.vuart" && POOL=1
say "pool armed=$POOL"
if [ -n "$DJOB" ]; then runjob $DJOB; grep -a -q "S243 DELIVER COMPLETE" $JOBLOG || { say "ABORT kit delivery"; exit 5; }; fi
runjob $ROOT/agx-s248/jobs/D28-rearm.cmd
say "D28 done; HV EL2 exceptions so far: $(grep -a -c 'Exception taken from EL2' "$LOG.vuart")"
say "S265 BOOT DONE link=$L out=$CUR log=$LOG pool=$POOL"
wait
