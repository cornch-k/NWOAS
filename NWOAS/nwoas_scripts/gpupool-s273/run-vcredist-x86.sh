#!/bin/bash
# S273b: x86 Visual C++ runtime for the 2777 x86 WOW64 UMD, then K50 again (2777 stays installed, no driver change).
#   one boot = boot-s265.sh, pool ON, manifest vcredist-x86.json, delivery vcredist-x86-deliver.cmd
#   -> K51 (read-only: x86 CRT already somewhere?) -> I40 (sha + Microsoft signature check, vc_redist.x86.exe
#   /install /quiet /norestart, SysWOW64 check) -> K50 (x86/x64/arm64; arm64 now also runs d3d11) -> K41.
#   Needs first: third-party/vc_redist.x86.exe and  python3 -I file-delivery-s243/make-vcredist-x86.py <sha256>
#   bash gpupool-s273/run-vcredist-x86.sh <current launcher .out> <current .link>
# NWGET refuses to overwrite: for a second run, delete C:\NWOAS\drop\vcredist-x86 first or set SKIP_DELIVER=1.
# Undo: Windows "Apps > Installed apps > Microsoft Visual C++ 2015-2022 Redistributable (x86)" (the user's call).
# Not run by the agent that wrote it (HARDWARE-UNVERIFIED).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; R=$ROOT/gpupool-s273/runs; mkdir -p $R /tmp/claude-501
RES=$R/vcredist-$(date +%Y%m%d-%H%M%S).txt
MANF=$ROOT/file-delivery-s243/manifests/vcredist-x86.json
DELJ=$ROOT/file-delivery-s243/jobs/vcredist-x86-deliver.cmd
INST=$ROOT/agx-s248/jobs/I40-vcredist-x86.cmd
KIT=${KIT:-"K50 K41"}
export NWOAS_S262_HV=0; unset NWOAS_NVME_QD NWOAS_S260_HV
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
for f in $MANF $DELJ $INST $ROOT/gpucov/jobs/K51-crt32-find.cmd $(for k in $KIT; do ls $ROOT/gpucov/jobs/$k-*.cmd 2>/dev/null | head -1; done); do
  [ -f $f ] || { echo "STOP: missing $f (python3 -I file-delivery-s243/make-vcredist-x86.py <sha256>)"; exit 1; }
  case $f in *.json) continue ;; esac
  n=$(python3 -I -c "import sys;s=open(sys.argv[1],encoding='ascii').read().replace('\r\n','\n').replace('\n','\r\n');print(len(s.encode('ascii')))" $f) || { echo "STOP: not ASCII $f"; exit 1; }
  [ "$n" -le 4032 ] || { echo "STOP: $f is $n B > 4032"; exit 1; }; done
grep -q '@SHA@' $INST && { echo "STOP: I40 still has the template placeholder"; exit 1; }
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
boot() { local T=$R/boot-$(date +%H%M%S).txt
  (NWOAS_S265_POOL=${5:-${NWOAS_S265_POOL:-1}} MAN=${3:-} DJOB=${4:-} nohup bash $ROOT/gpupool-s265/boot-s265.sh $1 $2 > $T 2>&1 &)
  waitline $T "S265 BOOT DONE link=|ABORT" 400; cat $T >> $RES
  BL=$(grep -a "S265 BOOT DONE link=" $T | tail -1); [ -n "$BL" ] || { say "BOOT FAIL ($T)"; exit 1; }
  L=$(echo "$BL" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); OUT=$(echo "$BL" | sed -n 's/.* out=\([^ ]*\).*/\1/p')
  LOG=$(echo "$BL" | sed -n 's/.* log=\([^ ]*\).*/\1/p'); POOL=$(echo "$BL" | sed -n 's/.* pool=\([01]\).*/\1/p')
  echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L pool=$POOL out=$OUT"; }
say "START vc_redist x86 chain old=$2"
if [ "${SKIP_DELIVER:-0}" = 1 ]; then boot $1 $2 "" "" 1; else boot $1 $2 $MANF $DELJ 1; fi
sleep 30
job $L $LOG $OUT $ROOT/gpucov/jobs/K51-crt32-find.cmd || exit $?
job $L $LOG $OUT $INST || exit $?
grep -a -q "I40 CHECK OK" $JOBLOG || { say "STOP: I40 refused the file (sha/signature); nothing installed"; exit 9; }
grep -a -q "I40 VERDICT PASS" $JOBLOG || { say "STOP: x86 CRT still not in SysWOW64 after I40"; exit 9; }
for k in $KIT; do job $L $LOG $OUT $(ls $ROOT/gpucov/jobs/$k-*.cmd | head -1) || exit $?; done
say "HV EL2 exceptions in this boot: $(grep -a -c 'Exception taken from EL2' "$LOG.vuart")"
grep -a "K50 VERDICT\|I40 RC\|I40 AFTER\|K51 X86 total" $RES | sed 's/^/SUMMARY /' >> $RES
say "S273B CHAIN DONE link=$L out=$OUT pool=$POOL res=$RES"
