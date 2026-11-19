#!/bin/bash
# S267e chain (replaces run-k43d-chain.sh: its K43d job is 5,990 B, over the 4,032-byte mailbox in
# transport-s123/queue_job.py, so that chain would stop at "ABORT queue K43d"; and probe13 settle= waits on this
# process's DXGI usage, which is already back after each device close, so its (c) leg repeated K43c).
# NO new driver: 2774 (P38) stays installed.
#   boot = gpupool-s265/boot-s265.sh, pool ON, the K43c hypervisor (g254L: NWOAS_S262_HV=0, NWOAS_S260_HV unset,
#          NVMe one at a time, as at 08:08), probe13 delivered during the boot only if missing (gpucov15s) -> D28
#   -> K43u  (read-only: failure anchors of the 08:10 K43c UMD trace C:\NWOAS\gpucov\k43c-umd.log)
#   -> K43e1 (runs: a 2304 alone | b 2048,2304 one process | c each size its own process 20 s apart | d 3072,2304)
#   -> K43e2 K43e3 (extract a,b / c,d) -> K43e4 (verdict SIZE/HISTORY/NOREPRO/MIXED, receipts now) -> K41
#   bash gpupool-s267/run-k43e-chain.sh <current launcher .out> <current .link>     (or: idle -)
# Side effects: W4 restarts the running Windows; this boot has NO Bluetooth (S259 payload) and NO concurrent NVMe.
# Back to the daily stack afterwards: bash bt-s268/run-s268.sh <S265 .out> <S265 .link> (both printed at the end).
# KIT overrides the job list. Every boot wait matches "S265 BOOT DONE link=" only.
# Stops if the installed driver is not 30.0.2774.0 (checked on K43u). Nothing is installed; nothing to roll back.
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s267/runs; mkdir -p $R /tmp/claude-501
RES=$R/chain-k43e-$(date +%Y%m%d-%H%M%S).txt
MANF=$ROOT/file-delivery-s243/manifests/gpucov-probe13.json
KITJ=$ROOT/file-delivery-s243/jobs/gpucov15s-probe13-ifmissing.cmd
KIT=${KIT:-"K43u K43e1 K43e2 K43e3 K43e4 K41"}
export NWOAS_S262_HV=0; unset NWOAS_NVME_QD NWOAS_S260_HV
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
# mailbox check before anything restarts: every job this chain queues must fit queue_job.py's 4,032 B
for f in $MANF $KITJ $ROOT/agx-s248/jobs/D28-rearm.cmd $(for k in $KIT; do ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1; done); do
  [ -f $f ] || { echo "STOP: missing $f"; exit 1; }
  case $f in *.json) continue ;; esac
  n=$(python3 -I -c "import sys;s=open(sys.argv[1],encoding='ascii').read().replace('\r\n','\n').replace('\n','\r\n');print(len(s.encode('ascii')))" $f) || { echo "STOP: not ASCII $f"; exit 1; }
  [ "$n" -le 4032 ] || { echo "STOP: $f is $n B > 4032"; exit 1; }; done
[ $(echo $KIT | wc -w) -eq $(for k in $KIT; do ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1; done | wc -l) ] || { echo "STOP: a KIT job file is missing"; exit 1; }
waitline() { for i in $(seq 1 $3); do grep -a -q -E "$2" $1 && return 0; sleep 5; done; return 1; }
job() { local L=$1 LOG=$2 OUT=$3 f=$4 j
  say "== $(basename $f)"
  python3 -I $ROOT/transport-s123/queue_job.py $L $f >/dev/null || { say "ABORT queue $(basename $f)"; return 3; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break
    if grep -a -q -E 'Exception taken from EL2|Unhandled exception' "$LOG.vuart" || grep -q "LAUNCHER EXIT" $OUT; then
      say "ABORT hypervisor/launcher gone during $(basename $f)"; grep -a -n -B3 -A12 'Exception taken from EL2' "$LOG.vuart" | head -40 | tee -a $RES; return 7; fi
    sleep 5; done
  JOBLOG=$L/job-$j.log
  iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | head -c 400000 | tee -a $RES; }
kit() { local L=$1 LOG=$2 OUT=$3 k f; shift 3
  for k in "$@"; do f=$(ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1); [ -n "$f" ] || { say "no job $k"; continue; }
    job $L $LOG $OUT $f || return $?
    if [ $k = K43u ] && ! grep -a -q "K43u VC .*30.0.2774.0" $JOBLOG; then say "STOP: installed driver is not 30.0.2774.0"; return 9; fi
  done; }
boot() { local T=$R/boot-$(date +%H%M%S).txt
  (NWOAS_S265_POOL=${5:-${NWOAS_S265_POOL:-1}} MAN=${3:-} DJOB=${4:-} nohup bash $ROOT/gpupool-s265/boot-s265.sh $1 $2 > $T 2>&1 &)
  waitline $T "S265 BOOT DONE link=|ABORT" 400; cat $T >> $RES
  BL=$(grep -a "S265 BOOT DONE link=" $T | tail -1); [ -n "$BL" ] || { say "BOOT FAIL ($T)"; exit 1; }
  L=$(echo "$BL" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); OUT=$(echo "$BL" | sed -n 's/.* out=\([^ ]*\).*/\1/p')
  LOG=$(echo "$BL" | sed -n 's/.* log=\([^ ]*\).*/\1/p'); POOL=$(echo "$BL" | sed -n 's/.* pool=\([01]\).*/\1/p')
  echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L pool=$POOL out=$OUT"; }
say "START k43e chain (2774 stays installed) old=$2 KIT=$KIT NWOAS_S262_HV=$NWOAS_S262_HV"
boot $1 $2 $MANF $KITJ 1
[ "$POOL" = 1 ] || { say "POOL NOT ARMED (hypervisor lines above); stop here"; exit 8; }
grep -a -q "S243 DELIVER COMPLETE" $RES || { say "ABORT probe13 delivery (no S243 DELIVER COMPLETE in the boot log)"; exit 5; }
grep -a -q "m1n1-g254L" "$LOG" || say "NOTE: hypervisor is not g254L (K43c ran on g254L)"
sleep 60
kit $L $LOG $OUT $KIT || exit $?
say "HV EL2 exceptions in this boot: $(grep -a -c 'Exception taken from EL2' "$LOG.vuart")"
say "S267E CHAIN DONE link=$L out=$OUT pool=$POOL res=$RES"
