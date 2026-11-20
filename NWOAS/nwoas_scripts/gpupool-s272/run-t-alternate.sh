#!/bin/bash
# S272T: same-boot-free control for the P40 T switch (Wom1PoolCpuVisibleTopDown, read only at KMD start; the
# mid-session GPU restart leaves the device in CM_PROB_FAILED_POST_START, so T cannot be toggled inside a boot).
# Driver 2776 stays installed (no install, no new build). For every value t of SEQ (default "0 1 0 1"):
#   W4 restart -> pool-ON boot (boot-s265.sh, g254L) -> K48 (8 ladders 1792..3072, env switches off) -> K45t
#   -> [last T=1 boot only: regressions K43c K41 K9 K10 K25b K21 K38g K38dg K41]
#   -> K47t (next boot T=1) or K47z (next boot T=0) -> K41.  The last boot ends with K47z (registry cleared).
#   bash gpupool-s272/run-t-alternate.sh <current launcher .out> <current .link>     (or: idle -)
# T is accepted only if every K48 leg in every T=1 boot passes while at least one T=0 boot has a failing leg.
# Back to the daily stack: bash bt-s268/run-s268.sh <last .out> <last .link>.  Rollback of the driver: R38.
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s272/runs; mkdir -p $R /tmp/claude-501
RES=$R/talt-2776-$(date +%Y%m%d-%H%M%S).txt
MANF=$ROOT/file-delivery-s243/manifests/agx-j274-2776.json
PKGJ=$ROOT/file-delivery-s243/jobs/agx-j274-2776-pkg.cmd
INST=$ROOT/agx-s248/jobs/I38-install-2776.cmd
SEQ=${SEQ:-"0 1 0 1"}   # TopDown state the KMD reads at each pool-on boot start
REG=${REG:-"K43c K41 K9 K10 K25b K21 K38g K38dg K41"}   # regressions, run in the last T=1 boot
KIT="K48 K45t K47t K47z K41 $REG"
KIT2=""
export NWOAS_S262_HV=0; unset NWOAS_NVME_QD NWOAS_S260_HV
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
for f in $MANF $PKGJ $INST $ROOT/agx-s248/jobs/D28-rearm.cmd $ROOT/agx-s248/jobs/R38-rollback-2775.cmd \
         $(for k in $KIT $KIT2; do ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1; done); do
  [ -f $f ] || { echo "STOP: missing $f (python3 -I file-delivery-s243/make-2776.py <build-p40 out dir>)"; exit 1; }
  case $f in *.json) continue ;; esac
  n=$(python3 -I -c "import sys;s=open(sys.argv[1],encoding='ascii').read().replace('\r\n','\n').replace('\n','\r\n');print(len(s.encode('ascii')))" $f) || { echo "STOP: not ASCII $f"; exit 1; }
  [ "$n" -le 4032 ] || { echo "STOP: $f is $n B > 4032"; exit 1; }; done
[ $(echo $KIT $KIT2 | wc -w) -eq $(for k in $KIT $KIT2; do ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1; done | wc -l) ] || { echo "STOP: a KIT job file is missing"; exit 1; }
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
say "START T alternating-boot chain (2776 stays installed) old=$2 SEQ=$SEQ"
# The value must be in the registry before a boot starts: the first boot of SEQ must be 0 (K47z cleared it at
# 01:37 and nothing set it since); every later value is written at the end of the boot before it.
first=$(echo $SEQ | awk '{print $1}')
[ "$first" = 0 ] || { say "STOP: SEQ must start with 0 (registry is cleared now)"; exit 1; }
n=$(echo $SEQ | wc -w); bi=0; lastT1=0
for t in $SEQ; do bi=$((bi+1)); [ $t = 1 ] && lastT1=$bi; done
OUT=$1; L=$2; bi=0
# 11-20 fix: the boot counter was 'i', which waitline/job overwrote (no 'local i'), so 'next' was always empty
# -> every boot ended in K47z and no boot ever ran with T=1 (09:18-09:56 run). Counter is now 'bi', helpers use local i,
# and the chain stops if the value K48 reads at a boot differs from the expected one, or K47t did not write 1.
for t in $SEQ; do bi=$((bi+1))
  boot $OUT $L "" "" 1
  [ "$POOL" = 1 ] || { say "POOL NOT ARMED (hypervisor lines above); stop here"; exit 8; }
  say "BOOT $bi of $n: expected TopDown=$t at this KMD start"
  sleep 60
  kit $L $LOG $OUT K48 || exit $?
  got=$(iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | sed -n 's/^K48 TopDown=\([0-9]*\) .*/\1/p' | tail -1); [ -n "$got" ] || got=0
  [ "$got" = "$t" ] || { say "STOP: boot $bi expected TopDown=$t but K48 read '$got' (registry); results of this boot are not T=$t evidence"; kit $L $LOG $OUT K47z; exit 9; }
  say "BOOT $bi CHECK TopDown read=$got expected=$t OK"
  kit $L $LOG $OUT K45t || exit $?
  if [ $bi = $lastT1 ]; then say "regressions in the last T=1 boot"; kit $L $LOG $OUT $REG || exit $?; fi
  next=$(echo $SEQ | awk -v k=$((bi+1)) '{print $k}')
  if [ "$next" = 1 ]; then kit $L $LOG $OUT K47t || exit $?
    iconv -c -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | grep -q '^K47t TopDown=1$' || { say "STOP: K47t did not write TopDown=1"; kit $L $LOG $OUT K47z; exit 9; }
    say "BOOT $bi -> next boot TopDown=1 written"
  else kit $L $LOG $OUT K47z || exit $?; say "BOOT $bi -> next boot TopDown cleared"; fi
  kit $L $LOG $OUT K41 || exit $?
done
say "HV EL2 exceptions in the last boot: $(grep -a -c 'Exception taken from EL2' "$LOG.vuart")"
SUM=$(grep -a "K48 TopDown=\|K48 LEG [0-9] mode" $RES); echo "$SUM" | sed 's/^/SUMMARY /' >> $RES
say "S272T CHAIN DONE link=$L out=$OUT pool=$POOL res=$RES (TopDown cleared for the next boot)"
