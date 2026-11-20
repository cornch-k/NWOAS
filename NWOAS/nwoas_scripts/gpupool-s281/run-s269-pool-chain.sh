#!/bin/bash
# S281: judgement chain for the daily launcher WITH the GPU pool (S269 = S268 + pool window, driver 2780, no install).
#   A. on the S269 boot that is running now (no restart): hypervisor pool lines -> K70 (status) -> K48 (8 ladders)
#      -> K45t (placement) -> K59 K58 K57 K56 -> K52e (Edge default vs GPU raster, er6-*) -> K53e (export)
#      -> K43c K50 K38g K38dg K9 K10 K25b K41.
#   B. restart through bt-s268/run-s269.sh (W4): the old boot's HV console must show no EL2 exception at shutdown
#      (the KMD stop path runs through the pool window); new boot must arm the pool again -> K70 -> K48 -> K45t -> K41.
#   bash gpupool-s281/run-s269-pool-chain.sh <current S269 launcher .out> <current .link>
#   ONLY_A=1: stop after part A (no restart).
# Pass: pool lines present and pool=1 in both boots; K70 DEV OK, Wom1StartStage 12 / Status 0; BOOT A/B LEGS fail 0 or 1 of 8
#   and PLACEMENT => OK; K59 64/0, 64/0, 18/46; K58 10/0 x2; K57 17/1; K56 12/0 x3; K52e VERDICT => PASS; K43c PASS;
#   K50 x86/x64/arm64 PASS; K38g/K38dg fatal 0; K9 23/23 x4, K10 13/13 x2 + present 4/4, K25b 17/17 x2; K41 dwm 0;
#   EL2 exceptions 0 in both boots and at the shutdown; run-s269.sh's own Bluetooth / Wi-Fi / NVMe lines as on S268.
# Turning the pool off again: NWOAS_S269_POOL=0 bash bt-s268/run-s269.sh <out> <link> (= S268).  No driver or registry change here.
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s281/runs; mkdir -p $R /tmp/claude-501
RES=$R/chain-s269-$(date +%Y%m%d-%H%M%S).txt
KIT=${KIT:-"K59 K58 K57 K56 K52e K53e K43c K50 K38g K38dg K9 K10 K25b K41"}
[ $# -ge 2 ] || { echo "usage: $0 <current S269 launcher .out> <current .link>"; exit 1; }
for k in K70 K48 K45t $KIT; do f=$(ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1); [ -n "$f" ] || { echo "STOP: no job $k"; exit 1; }
  n=$(python3 -I -c "import sys;s=open(sys.argv[1],encoding='ascii').read().replace('\r\n','\n').replace('\n','\r\n');print(len(s.encode('ascii')))" $f) || { echo "STOP: not ASCII $f"; exit 1; }
  [ "$n" -le 4032 ] || { echo "STOP: $f is $n B > 4032"; exit 1; }; done
[ -f $ROOT/bt-s268/run-s269.sh ] || { echo "STOP: bt-s268/run-s269.sh missing"; exit 1; }
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
waitline() { local i; for i in $(seq 1 $3); do grep -a -q -E "$2" $1 && return 0; sleep 5; done; return 1; }
job() { local L=$1 LOG=$2 OUT=$3 f=$4 j i
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
    job $L $LOG $OUT $f || return $?; done; }
boot() { local T=$R/boot-$(date +%H%M%S).txt
  (nohup bash $ROOT/bt-s268/run-s269.sh $1 $2 > $T 2>&1 &)
  waitline $T "S269 DONE link=|ABORT" 500; cat $T >> $RES
  grep -a -q "OLD BOOT HV EXCEPTION" $T && say "NOTE: the previous boot shows an EL2 exception at shutdown"
  BL=$(grep -a "S269 DONE link=" $T | tail -1); [ -n "$BL" ] || { say "BOOT FAIL ($T)"; exit 1; }
  L=$(echo "$BL" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); OUT=$(echo "$BL" | sed -n 's/.* out=\([^ ]*\).*/\1/p')
  LOG=$(echo "$BL" | sed -n 's/.* log=\([^ ]*\).*/\1/p'); POOL=$(echo "$BL" | sed -n 's/.* pool=\([01]\).*/\1/p')
  echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L pool=$POOL out=$OUT ($(echo "$BL" | sed -n 's/.*\(el2_exc=[0-9]*\).*/\1/p'))"; }
poolarmed() { POOL=0; grep -a -q 'MEDIATE armed: .* pool=1 ' "$LOG.vuart" && grep -a -q 'pool window v1 IPA=0xd00000000 bytes=0x100000000' "$LOG.vuart" && POOL=1
  grep -a -E 'NWOAS-AGX: (pool window|MEDIATE armed|install REFUSED|local reserve)' "$LOG.vuart" | head -6 | tee -a $RES; say "pool armed=$POOL"; }
# K48 + K45t with the checks: registry value absent (2778 default on), and CPU-visible placement at the top
legs() { local b=$1 got top pids v
  kit $L $LOG $OUT K48 || exit $?
  got=$(iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | sed -n 's/^K48 TopDown=\([0-9]*\) .*/\1/p' | tail -1)
  [ -z "$got" ] || say "NOTE: boot $b registry TopDown=$got (default-on needs it absent; 0 = off)"
  say "BOOT $b LEGS fail=$(iconv -c -f CP949 -t UTF-8 $JOBLOG | grep -a -c 'K48 LEG [0-9] mode=.*FAIL') of 8"
  # pids of this boot's 8 ladder processes: only their placement entries count (the "first" record can be from an older boot)
  pids=$(iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | sed -n 's/^K48 LEG [0-9] mode=[^ ]* pid=\([0-9][0-9]*\) .*/\1/p' | tr '\n' ' ')
  kit $L $LOG $OUT K45t || exit $?
  top=$(iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | grep -a ' U .*cpuvis=1 .*seg=3 ' | awk -v P="$pids" 'BEGIN{n=split(P,q," ");for(i=1;i<=n;i++)ok[q[i]]=1}
    {pid="";pb="";for(i=1;i<=NF;i++){if($i ~ /^pid=/)pid=substr($i,5); if($i ~ /^pool_base_mib=/)pb=substr($i,15)} if(pid in ok){ if(pb+0>2000) c++; else d++ }} END{print c+0, d+0}')
  v=BAD; [ "${top% *}" -gt 0 ] && [ "${top#* }" -eq 0 ] && v=OK
  say "BOOT $b PLACEMENT fresh_top=${top% *} fresh_low=${top#* } pids=[$pids] => $v (OK = entries of this boot's ladder processes exist and all sit above 2,000 MiB)"; }
say "START S269 pool chain out=$1 link=$2"
OUT=$1; L=$2; LOG=$(sed -n "s/^Log: //p" $OUT | head -1)
[ -n "$LOG" ] && [ -d "$L" ] || { say "STOP: cannot read the log path from $OUT or $L is not a directory"; exit 1; }
poolarmed
[ "$POOL" = 1 ] || { say "POOL NOT ARMED on the running boot; stop here"; exit 8; }
kit $L $LOG $OUT K70 || exit $?
legs A
kit $L $LOG $OUT $KIT || exit $?
say "HV EL2 exceptions in boot A: $(grep -a -c 'Exception taken from EL2' "$LOG.vuart")"
if [ "${ONLY_A:-0}" != 1 ]; then
  say "restart through run-s269.sh"
  boot $OUT $L
  [ "$POOL" = 1 ] || { say "POOL NOT ARMED after the restart; stop here"; exit 8; }
  sleep 60
  kit $L $LOG $OUT K70 || exit $?
  legs B
  kit $L $LOG $OUT K41 || exit $?
  say "HV EL2 exceptions in boot B: $(grep -a -c 'Exception taken from EL2' "$LOG.vuart")"
fi
SUM=$(grep -a "pool armed=\|BOOT [AB] LEGS\|BOOT [AB] PLACEMENT\|K70 DEV\|K70 REG Wom1Start\|K70 RAM\|K50 VERDICT\|K43c VERDICT\|K5[6789].*SUMMARY\|K52e VERDICT\|K52e AB \|psfile case=.*FAIL\|psvary case=.*FAIL\|K53e ZIP\|EL2 exceptions in boot\|NOTE:" $RES | head -200); echo "$SUM" | sed 's/^/SUMMARY /' >> $RES
say "S281 CHAIN DONE link=$L out=$OUT pool=$POOL res=$RES"
