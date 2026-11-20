#!/bin/bash
# S274 / P43 chain (driver 2778 = 2777 + Wom1PoolCpuVisibleTopDown default ON; registry 0 = off).
# Order today: run gpupool-s273/run-vcredist-x86.sh on 2777 first (x86 CRT into SysWOW64), then this chain.
#   1. boot A = boot-s265.sh, pool OFF, 2778 manifest; package delivery -> D28 -> I41 install 2778 (deletes the
#      TopDown registry value so the new default applies).
#   2. boot B = pool ON -> K48 (8 ladders 1792..3072; registry value must be absent) -> K45t (CPU-visible placement
#      must reach the top of the pool) -> K43c K50 K38g K38dg K41.
#   3. boot C = pool ON again -> K48 -> K45t -> K41.
#   bash gpupool-s274/run-2778-chain.sh <current launcher .out> <current .link>     (or: idle -)
# Pass: 16/16 K48 legs PASS, BOOT B/C PLACEMENT => OK (this boot's ladder pids only, all above 2,000 MiB), K50 x86/x64/arm64 PASS (x86 needs the CRT),
#   K43c PASS, K38g/K38dg fatal 0, K41 dwm 0, EL2 exceptions 0.
# Rollback: agx-s248/jobs/R40-rollback-2777.cmd then W4.  SKIP_INSTALL=1: 2778 already installed.
# First hardware run with the user present (new driver build). Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s274/runs; mkdir -p $R /tmp/claude-501
RES=$R/chain-2778-$(date +%Y%m%d-%H%M%S).txt
MANF=$ROOT/file-delivery-s243/manifests/agx-j274-2778.json
PKGJ=$ROOT/file-delivery-s243/jobs/agx-j274-2778-pkg.cmd
INST=$ROOT/agx-s248/jobs/I41-install-2778.cmd
KIT=${KIT:-"K43c K50 K38g K38dg K41"}
KIT2=${KIT2:-"K41"}
export NWOAS_S262_HV=0; unset NWOAS_NVME_QD NWOAS_S260_HV
for f in $MANF $PKGJ $INST $ROOT/agx-s248/jobs/D28-rearm.cmd $ROOT/agx-s248/jobs/R40-rollback-2777.cmd \
         $(for k in K48 K45t $KIT $KIT2; do ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1; done); do
  [ -f $f ] || { echo "STOP: missing $f (python3 -I file-delivery-s243/make-2778.py <build-p43 out dir>)"; exit 1; }
  case $f in *.json) continue ;; esac
  n=$(python3 -I -c "import sys;s=open(sys.argv[1],encoding='ascii').read().replace('\r\n','\n').replace('\n','\r\n');print(len(s.encode('ascii')))" $f) || { echo "STOP: not ASCII $f"; exit 1; }
  [ "$n" -le 4032 ] || { echo "STOP: $f is $n B > 4032"; exit 1; }; done
[ $(echo K48 K45t $KIT $KIT2 | wc -w) -eq $(for k in K48 K45t $KIT $KIT2; do ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1; done | wc -l) ] || { echo "STOP: a KIT job file is missing"; exit 1; }
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
  (NWOAS_S265_POOL=${5:-${NWOAS_S265_POOL:-1}} MAN=${3:-} DJOB=${4:-} nohup bash $ROOT/gpupool-s265/boot-s265.sh $1 $2 > $T 2>&1 &)
  waitline $T "S265 BOOT DONE link=|ABORT" 400; cat $T >> $RES
  BL=$(grep -a "S265 BOOT DONE link=" $T | tail -1); [ -n "$BL" ] || { say "BOOT FAIL ($T)"; exit 1; }
  L=$(echo "$BL" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); OUT=$(echo "$BL" | sed -n 's/.* out=\([^ ]*\).*/\1/p')
  LOG=$(echo "$BL" | sed -n 's/.* log=\([^ ]*\).*/\1/p'); POOL=$(echo "$BL" | sed -n 's/.* pool=\([01]\).*/\1/p')
  echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L pool=$POOL out=$OUT"; }
# K48 + K45t with the checks: registry value absent (2778 default on), and CPU-visible placement at the top
legs() { local b=$1 got top pids v
  kit $L $LOG $OUT K48 || exit $?
  got=$(iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | sed -n 's/^K48 TopDown=\([0-9]*\) .*/\1/p' | tail -1)
  [ -z "$got" ] || say "NOTE: boot $b registry TopDown=$got (2778 default needs it absent; 0 = off)"
  say "BOOT $b LEGS fail=$(iconv -c -f CP949 -t UTF-8 $JOBLOG | grep -a -c 'K48 LEG [0-9] mode=.*FAIL') of 8"
  # pids of this boot's 8 ladder processes: only their placement entries count (the "first" record can be from an older boot)
  pids=$(iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | sed -n 's/^K48 LEG [0-9] mode=[^ ]* pid=\([0-9][0-9]*\) .*/\1/p' | tr '\n' ' ')
  kit $L $LOG $OUT K45t || exit $?
  top=$(iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | grep -a ' U .*cpuvis=1 .*seg=3 ' | awk -v P="$pids" 'BEGIN{n=split(P,q," ");for(i=1;i<=n;i++)ok[q[i]]=1}
    {pid="";pb="";for(i=1;i<=NF;i++){if($i ~ /^pid=/)pid=substr($i,5); if($i ~ /^pool_base_mib=/)pb=substr($i,15)} if(pid in ok){ if(pb+0>2000) c++; else d++ }} END{print c+0, d+0}')
  v=BAD; [ "${top% *}" -gt 0 ] && [ "${top#* }" -eq 0 ] && v=OK
  say "BOOT $b PLACEMENT fresh_top=${top% *} fresh_low=${top#* } pids=[$pids] => $v (OK = entries of this boot's ladder processes exist and all sit above 2,000 MiB)"; }
say "START 2778 chain old=$2 NWOAS_S262_HV=$NWOAS_S262_HV"
if [ "${SKIP_INSTALL:-0}" = 1 ]; then
  say "SKIP_INSTALL=1: no install, straight to the pool-on boot"; OUT=$1; L=$2
else
boot $1 $2 $MANF $PKGJ 0
job $L $LOG $OUT $INST || exit $?
grep -a -q "30.0.2778.0" $JOBLOG || { say "INSTALL FAIL: VC is not 30.0.2778.0 (rollback: R40)"; exit 9; }
grep -a -q "CRT32 vcruntime140.dll MISSING" $JOBLOG && say "NOTE: x86 CRT missing - K50 x86 will FAIL (run gpupool-s273/run-vcredist-x86.sh first)"
say "installed 2778 (pool off boot)"
fi
boot $OUT $L "" "" 1
[ "$POOL" = 1 ] || { say "POOL NOT ARMED (hypervisor lines above); stop here"; exit 8; }
sleep 60
legs B
kit $L $LOG $OUT $KIT || exit $?
say "restart: second pool boot with 2778"
boot $OUT $L "" "" 1
[ "$POOL" = 1 ] || { say "POOL NOT ARMED (second boot); stop here"; exit 8; }
sleep 60
legs C
kit $L $LOG $OUT $KIT2 || exit $?
say "HV EL2 exceptions in this boot: $(grep -a -c 'Exception taken from EL2' "$LOG.vuart")"
SUM=$(grep -a "BOOT [BC] LEGS\|BOOT [BC] PLACEMENT\|K50 VERDICT\|K43c.*PASS\|K43c.*FAIL\|NOTE:" $RES | head -200); echo "$SUM" | sed 's/^/SUMMARY /' >> $RES
say "S274 CHAIN DONE link=$L out=$OUT pool=$POOL res=$RES"
