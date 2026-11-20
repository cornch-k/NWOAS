#!/bin/bash
# S273 / P42 chain (driver 2777 = 2776 + the x86 WOW64 UMD in SysWOW64, UserModeDriverNameWow).
#   1. boot A = boot-s265.sh, pool OFF, 2777 manifest; package delivery -> D28 -> I39 install 2777
#      (AppleAgxRenderAdmission.arm64xwow.inf) + probe15 arm64/x64/x86 delivery (gpucov18).
#   2. boot B = pool ON -> K50 (x86/x64/arm64 UMD: umd draw11 d3d11 [d3d12]) -> K42 (x64 regression, probe10)
#      -> K43c K41 K9 K10 K25b K21 K38g K38dg K41.
#   3. boot C = pool ON again (KMD stop path) -> K43b K50 K41.
#   bash gpupool-s273/run-2777-chain.sh <current launcher .out> <current .link>     (or: idle -)
# Hypervisor pinned to g254L (NWOAS_S262_HV=0); the S265 launcher has no Bluetooth.
# SKIP_INSTALL=1: 2777 + probe15 already on Windows. KIT / KIT2 override the kits.
# Rollback: agx-s248/jobs/R39-rollback-2776.cmd then W4.  Back to the daily stack: bash bt-s268/run-s268.sh.
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s273/runs; mkdir -p $R /tmp/claude-501
RES=$R/chain-2777-$(date +%Y%m%d-%H%M%S).txt
MANF=$ROOT/file-delivery-s243/manifests/agx-j274-2777.json
PKGJ=$ROOT/file-delivery-s243/jobs/agx-j274-2777-pkg.cmd
KITJ=$ROOT/file-delivery-s243/jobs/gpucov18-j274-2777.cmd
INST=$ROOT/agx-s248/jobs/I39-install-2777.cmd
KIT=${KIT:-"K50 K42 K43c K41 K9 K10 K25b K21 K38g K38dg K41"}
KIT2=${KIT2:-"K43b K50 K41"}
export NWOAS_S262_HV=0; unset NWOAS_NVME_QD NWOAS_S260_HV
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
for f in $MANF $PKGJ $KITJ $INST $ROOT/agx-s248/jobs/D28-rearm.cmd $ROOT/agx-s248/jobs/R39-rollback-2776.cmd \
         $(for k in $KIT $KIT2; do ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1; done); do
  [ -f $f ] || { echo "STOP: missing $f (python3 -I file-delivery-s243/make-2777.py <build-p42 out dir>)"; exit 1; }
  case $f in *.json) continue ;; esac
  n=$(python3 -I -c "import sys;s=open(sys.argv[1],encoding='ascii').read().replace('\r\n','\n').replace('\n','\r\n');print(len(s.encode('ascii')))" $f) || { echo "STOP: not ASCII $f"; exit 1; }
  [ "$n" -le 4032 ] || { echo "STOP: $f is $n B > 4032"; exit 1; }; done
[ $(echo $KIT $KIT2 | wc -w) -eq $(for k in $KIT $KIT2; do ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1; done | wc -l) ] || { echo "STOP: a KIT job file is missing"; exit 1; }
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
    job $L $LOG $OUT $f || return $?; done; }
boot() { local T=$R/boot-$(date +%H%M%S).txt
  (NWOAS_S265_POOL=${5:-${NWOAS_S265_POOL:-1}} MAN=${3:-} DJOB=${4:-} nohup bash $ROOT/gpupool-s265/boot-s265.sh $1 $2 > $T 2>&1 &)
  waitline $T "S265 BOOT DONE link=|ABORT" 400; cat $T >> $RES
  BL=$(grep -a "S265 BOOT DONE link=" $T | tail -1); [ -n "$BL" ] || { say "BOOT FAIL ($T)"; exit 1; }
  L=$(echo "$BL" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); OUT=$(echo "$BL" | sed -n 's/.* out=\([^ ]*\).*/\1/p')
  LOG=$(echo "$BL" | sed -n 's/.* log=\([^ ]*\).*/\1/p'); POOL=$(echo "$BL" | sed -n 's/.* pool=\([01]\).*/\1/p')
  echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L pool=$POOL out=$OUT"; }
say "START 2777 chain old=$2 NWOAS_S262_HV=$NWOAS_S262_HV"
if [ "${SKIP_INSTALL:-0}" = 1 ]; then
  say "SKIP_INSTALL=1: no install, straight to the pool-on boot"; OUT=$1; L=$2
else
# 1. install under pool OFF
boot $1 $2 $MANF $PKGJ 0
job $L $LOG $OUT $INST || exit $?
grep -a -q "30.0.2777.0" $JOBLOG || { say "INSTALL FAIL: VC is not 30.0.2777.0 (rollback: R39)"; exit 9; }
grep -a -q "SYSWOW64 AppleAgxRenderAdmissionUmd32.dll [0-9]" $JOBLOG || say "NOTE: SysWOW64 Umd32 not reported by I39"
job $L $LOG $OUT $KITJ || exit $?
grep -a -q "S243 DELIVER COMPLETE" $JOBLOG || { say "ABORT probe15 delivery"; exit 5; }
say "installed 2777 + probe15 (pool off boot)"
fi
# 2. pool ON boot with 2777
boot $OUT $L "" "" 1
[ "$POOL" = 1 ] || { say "POOL NOT ARMED (hypervisor lines above); stop here"; exit 8; }
sleep 60
kit $L $LOG $OUT $KIT || exit $?
say "restart: second pool boot (KMD stop path through the pool window, now with 2777)"
boot $OUT $L "" "" 1
kit $L $LOG $OUT $KIT2 || exit $?
say "HV EL2 exceptions in this boot: $(grep -a -c 'Exception taken from EL2' "$LOG.vuart")"
say "S273 CHAIN DONE link=$L out=$OUT pool=$POOL res=$RES"
