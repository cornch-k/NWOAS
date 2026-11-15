#!/bin/bash
# S236 validation orchestrator: [pre-jobs] -> restart -> 3 x U3 fresh-boot cycles -> truncatememory 4 GiB
# -> 3 x U3 cycles -> revert truncatememory -> final session kept alive.
# usage: validate.sh <launcher.sh> <tag> [pre-job.cmd ...]
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
LAUNCHER=$1; TAG=$2; shift 2
RES=$ROOT/usb-s236/results-$TAG.txt
U3=$ROOT/usb-s232/U3-fresh-copy.cmd
W4=$ROOT/layout-s231/jobs/W4-restart.cmd
CUR=$(ls -t $ROOT/logs/launcher-*.out | head -1)
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
linkdir() { echo "$(sed -n 's/^Log: //p' $CUR).link"; }
wait_worker() {
  local L; for i in $(seq 1 240); do L=$(linkdir)
    grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && return 0
    grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: session ended before worker ($CUR)"; exit 2; }
    sleep 5; done; say "ABORT: worker timeout"; exit 2; }
wait_idle() {
  local L=$(linkdir) j; [ -f $L/job.json ] || return 0
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && return 0; sleep 5; done
  say "ABORT: previous job $j never completed"; exit 3; }
run_job() { # $1 job file, $2 grep pattern for the result line
  local L j; wait_worker; wait_idle; L=$(linkdir)
  python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || { say "ABORT: queue $1"; exit 3; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBOUT=$(tr -d '\r' < $L/job-$j.log | grep -a -E "$2" | tr '\n' ' ')
}
relaunch() { # $1 suffix
  for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $CUR && break; sleep 2; done
  grep -q "LAUNCHER EXIT" $CUR || { say "ABORT: launcher did not exit"; exit 4; }
  for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
  sleep 4
  CUR=$ROOT/logs/launcher-$TAG-$(date "+%Y%m%d-%H%M%S")-$1.out
  (caffeinate -dims bash $ROOT/$LAUNCHER > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
  sleep 10
}
cycles() { # $1 n, $2 label, $3 job that ends the phase (must restart Windows)
  for c in $(seq 1 $1); do
    run_job $U3 "^SHA256|RESULT|NO USB"
    say "$2 cycle $c: $JOBOUT"
    if [ $c -lt $1 ]; then run_job $W4 "RESTART"; else run_job $3 "RESTART|truncatememory|MB|badmemorylist"; say "$2 end job: $JOBOUT"; fi
    relaunch "$2-c$c"
  done
}
say "START launcher=$LAUNCHER tag=$TAG cur=$CUR"
for pre in "$@"; do run_job $pre "RC=|badmemorylist|COMPLETE|0x"; say "pre $(basename $pre): ${JOBOUT:0:400}"; done
run_job $W4 "RESTART"; relaunch p0
cycles 3 normal $ROOT/usb-s232/T1-truncatememory.cmd
cycles 3 trunc4g $ROOT/usb-s232/T2-revert-truncate.cmd
run_job $ROOT/usb-s236/B0-state.cmd "truncatememory|badmemorylist|Total Physical|COMPLETE"
say "final state: ${JOBOUT:0:600}"
say "VALIDATION SEQUENCE DONE"
wait
