#!/bin/bash
# S269 / P39 chain (driver 2775 = 2774 + recording-only receipt Wom1PoolTimeline): mirrors gpupool-s266/run-2774-chain.sh.
#   1. boot A = boot-s265.sh with NWOAS_S265_POOL=0 (pool OFF, same install conditions as 2774) and the 2775
#      manifest; delivers the package (DJOB) -> D28; then I37 install 2775 + probe14 delivery (gpucov16).
#   2. boot B = boot-s265.sh, pool ON -> K45a (keep the 08:10 K43c trace) -> K45l (size ladder 1792..3072, staging
#      first = K43c order, old timeline values deleted) -> K45t (decode Wom1PoolTimeline*) -> K45b (probe14 big buffer
#      first) -> K45t -> K43c (ladder + 3.5 GiB fill3 + D3D11, regression) -> K41 K9 K10 K25b K21 K38g K38dg K41.
#   3. boot C = boot-s265.sh, pool ON again (KMD stop path through the pool window with 2775) -> K43b + K41.
#   bash gpupool-s269/run-2775-chain.sh <current launcher .out> <current .link>     (or: idle -)
# Hypervisor pinned to the K43c one (g254L: NWOAS_S262_HV=0, S260 off): no concurrent NVMe, and the S265 launcher
# has no Bluetooth. Back to the daily stack afterwards: bash bt-s268/run-s268.sh <last .out> <last .link>.
# SKIP_INSTALL=1: 2775 + probe14 already on Windows (package delivery refuses to overwrite, so a second full run
# stops at delivery): start at the pool-on boot. KIT / KIT2 override the kits. Every boot wait matches "S265 BOOT DONE link=" only.
# Rollback: agx-s248/jobs/R37-rollback-2774.cmd on any running boot, then W4.
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s269/runs; mkdir -p $R /tmp/claude-501
RES=$R/chain-2775-$(date +%Y%m%d-%H%M%S).txt
MANF=$ROOT/file-delivery-s243/manifests/agx-j274-2775.json
PKGJ=$ROOT/file-delivery-s243/jobs/agx-j274-2775-pkg.cmd
KITJ=$ROOT/file-delivery-s243/jobs/gpucov16-j274-2775.cmd
INST=$ROOT/agx-s248/jobs/I37-install-2775.cmd
KIT=${KIT:-"K45a K45l K45t K45b K45t K43c K41 K9 K10 K25b K21 K38g K38dg K41"}
KIT2=${KIT2:-"K43b K45t K41"}
export NWOAS_S262_HV=0; unset NWOAS_NVME_QD NWOAS_S260_HV
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
for f in $MANF $PKGJ $KITJ $INST $ROOT/agx-s248/jobs/D28-rearm.cmd $ROOT/agx-s248/jobs/R37-rollback-2774.cmd \
         $(for k in $KIT $KIT2; do ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1; done); do
  [ -f $f ] || { echo "STOP: missing $f (python3 -I file-delivery-s243/make-2775.py <build-p39 out dir>)"; exit 1; }
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
say "START 2775 chain old=$2 NWOAS_S262_HV=$NWOAS_S262_HV"
if [ "${SKIP_INSTALL:-0}" = 1 ]; then
  say "SKIP_INSTALL=1: no install, straight to the pool-on boot"; OUT=$1; L=$2
else
# 1. install under pool OFF
boot $1 $2 $MANF $PKGJ 0
job $L $LOG $OUT $INST || exit $?
grep -a -q "30.0.2775.0" $JOBLOG || { say "INSTALL FAIL: VC is not 30.0.2775.0 (rollback: R37)"; exit 9; }
job $L $LOG $OUT $KITJ || exit $?
grep -a -q "S243 DELIVER COMPLETE" $JOBLOG || { say "ABORT probe14 delivery"; exit 5; }
say "installed 2775 + probe14 (pool off boot)"
fi
# 2. pool ON boot with 2775
boot $OUT $L "" "" 1
[ "$POOL" = 1 ] || { say "POOL NOT ARMED (hypervisor lines above); stop here"; exit 8; }
sleep 60
kit $L $LOG $OUT $KIT || exit $?
say "restart: second pool boot (KMD stop path through the pool window, now with 2775)"
boot $OUT $L "" "" 1
kit $L $LOG $OUT $KIT2 || exit $?
say "HV EL2 exceptions in this boot: $(grep -a -c 'Exception taken from EL2' "$LOG.vuart")"
say "S269 CHAIN DONE link=$L out=$OUT pool=$POOL res=$RES"
