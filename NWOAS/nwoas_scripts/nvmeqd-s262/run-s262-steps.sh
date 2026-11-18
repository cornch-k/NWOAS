#!/bin/bash
# S262 hardware steps 0-4 (README): each boot via wifi-s259/run-s259.sh with the S262 switches, stop at the first
# failure and boot back to the old one-at-a-time path.   bash nvmeqd-s262/run-s262-steps.sh <old .out> <old .link>
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/nvmeqd-s262; mkdir -p $D/runs
RES=$D/runs/steps-$(date +%Y%m%d-%H%M%S).txt
OUT=$1; L=$2
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
boot() { # env assignments as args; run-s259.sh keeps running (it waits on the launcher), so poll its output
  local T=$D/runs/boot-$(date +%H%M%S).txt r
  (env NWOAS_AUDIO_PROT=moderate "$@" nohup bash $ROOT/wifi-s259/run-s259.sh $OUT $L > $T 2>&1 &)
  for i in $(seq 1 240); do grep -a -q -E "S259 DONE|ABORT" $T && break; sleep 5; done
  cat $T >> $RES; r=$(grep -a "S259 DONE" $T | tail -1)
  [ -n "$r" ] || { say "BOOT FAIL ($*)"; return 1; }
  L=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); OUT=$(echo "$r" | sed -n 's/.* out=\([^ ]*\).*/\1/p'); LOG=$(echo "$r" | sed -n 's/.* log=\([^ ]*\).*/\1/p')
  echo $L > /tmp/claude-501/curlink.txt; say "booted ($*) link=$L"
  grep -a -E 'S262|nvme: S262' $LOG | head -8 | tee -a $RES; }
job() { python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || { say "queue fail $1"; return 1; }
  local j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 600); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 3; done
  JL=$(iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r'); echo "$JL" | head -c 20000 >> $RES; }
safety() { local bad=0
  grep -a -q -E 'NWOAS-S262 NVMe QD fault|REFUSED write outside' $LOG && { say "SAFETY: HV fault/refused line"; bad=1; }
  grep -a -E '\[S262\] NVMe QD (engine|off)' $LOG | tail -1 | sed 's/^/ENGINE: /' | tee -a $RES
  # g262R: the QD snapshot must come from the job marks, not only the boot mark (g262Q's hook never fired,
  # so its "safety counters all 0" was read from the boot snapshot only).
  local nrec; nrec=$(cat $L/nvmeqd.jsonl 2>/dev/null | grep -c .)
  [ "$nrec" -ge 2 ] || { say "SAFETY: nvmeqd.jsonl has $nrec record(s): job marks not recorded"; bad=1; }
  if [ -s $L/nvmeqd.jsonl ]; then python3 -I $D/nvmeqd_report.py $L/nvmeqd.jsonl | tee -a $RES | tail -4
    python3 -I - $L/nvmeqd.jsonl <<'PY' || bad=1
import json,sys
r=[json.loads(l) for l in open(sys.argv[1]) if l.strip()][-1]['snapshot']
k=('faults','timeouts','dev_errors','drain_fail','eng_bad_tag','eng_inval_fail','storm','eng_faulted')
b={x:r.get(x,0) for x in k if r.get(x,0)}
print('SAFETY_COUNTERS', b or 'all 0'); sys.exit(1 if b else 0)
PY
  fi; return $bad; }
q1() { job $D/jobs/Q1-io-integ.cmd || return 1; echo "$JL" | grep -a -E 'H2|IOPS|MB/s|INTEG|integ_rc' | head -30 | tee -a $RES
  echo "$JL" | grep -a -q 'INTEG PASS' && echo "$JL" | grep -a -q 'integ_rc=0' || { say "Q1 INTEG not PASS"; return 1; }; safety; }
back() { say "STOP at $1 -> boot old path"; boot NWOAS_S262_HV= ; exit 1; }
say "START"
if [ "${3:-}" = attached ]; then LOG=${L%.link}; echo $L > /tmp/claude-501/curlink.txt; say "attached to running S262 boot link=$L"
else boot NWOAS_S262_HV=1 NWOAS_DELIVER_MANIFEST=$D/manifests/s262-nwqd.json || back boot0; fi
job $D/jobs/Q0-deliver-nwqd.cmd; echo "$JL" | grep -a -q 'READBACK MATCH NWQD.EXE' || back Q0
say "STEP1 (S262 HV, QD off)"; q1 || back step1
say "STEP2 QD=8"; boot NWOAS_S262_HV=1 NWOAS_NVME_QD=8 || back boot2; q1 || back step2
say "STEP3 QD=32"; boot NWOAS_S262_HV=1 NWOAS_NVME_QD=32 || back boot3; q1 || back step3
say "STEP4 QD=32 + NVME_SHUT, 3 restarts"
boot NWOAS_S262_HV=1 NWOAS_NVME_QD=32 NWOAS_PWR_NVME_SHUT=1 || back boot4
for n in 1 2 3; do q1 || back step4-q1-$n
  boot NWOAS_S262_HV=1 NWOAS_NVME_QD=32 NWOAS_PWR_NVME_SHUT=1 || back boot4-$n
  job $D/jobs/Q2-verify.cmd; echo "$JL" | grep -a -E 'INTEG|verify_rc' | tee -a $RES
  echo "$JL" | grep -a -q 'verify_rc=0' || back step4-q2-$n; safety || back step4-safety-$n; done
job $D/jobs/Q4-chkdsk.cmd; echo "$JL" | grep -a -E 'chkdsk_rc|DISKEVT|BOOT153|문제|problem|error' | head -20 | tee -a $RES
# g262R: DISKEVT_COUNT counts disk/storport/Ntfs error events only (Kernel-Boot 153 = VBS status, BOOT153_COUNT)
DE=$(echo "$JL" | sed -n 's/^DISKEVT_COUNT \([0-9]*\).*/\1/p' | tail -1)
echo "$JL" | grep -a -q 'chkdsk_rc=0' || { say "STEP Q4 FAIL: chkdsk_rc not 0"; back Q4-chkdsk; }
[ "$DE" = 0 ] || { say "STEP Q4 FAIL: disk error events DISKEVT_COUNT=${DE:-missing}"; back Q4-diskevt; }
say "ALL STEPS DONE link=$L out=$OUT"
