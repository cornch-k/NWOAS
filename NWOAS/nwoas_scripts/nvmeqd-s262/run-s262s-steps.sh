#!/bin/bash
# S262S hardware steps (RESULT-g262S.md): same HV g262R, storage tools run at normal I/O priority through
# NWIOP.EXE (Q1S) next to the old low-priority Q1 in the same boot.  Stops at the first failure and boots back
# to the old one-at-a-time path.   bash nvmeqd-s262/run-s262s-steps.sh <old .out> <old .link>
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/nvmeqd-s262; mkdir -p $D/runs
RES=$D/runs/s262s-steps-$(date +%Y%m%d-%H%M%S).txt
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
q1s() { job $D/jobs/Q1S-io-integ-normal.cmd || return 1
  echo "$JL" | grep -a -E 'IOPRIO|H2|IOPS|MB/s|INTEG|integ_rc|io_rc' | head -40 | tee -a $RES
  echo "$JL" | grep -a -q 'IOPRIO child pid=.* process=2(normal) thread=2(normal)' || { say "Q1S: I/O priority 2 not applied"; return 1; }
  echo "$JL" | grep -a -q 'INTEG PASS' && echo "$JL" | grep -a -q 'io_rc=0 integ_rc=0' || { say "Q1S INTEG/io not PASS"; return 1; }; safety; }
say "START"
boot NWOAS_S262_HV=1 NWOAS_NVME_QD=32 NWOAS_DELIVER_MANIFEST=$D/manifests/s262s-nwiop.json || back bootA
job $D/jobs/Q0S-deliver-nwiop.cmd; echo "$JL" | grep -a -q 'READBACK MATCH NWIOP.EXE' || back Q0S
say "STEP A1 QD=32, Q1 (agent I/O priority, expected low)"; q1 || back stepA1
say "STEP A2 QD=32, Q1S (I/O priority normal)"; q1s || back stepA2
say "STEP B QD off, Q1S"; boot NWOAS_S262_HV=1 || back bootB; q1s || back stepB
say "STEP C QD=8, Q1S"; boot NWOAS_S262_HV=1 NWOAS_NVME_QD=8 || back bootC; q1s || back stepC
say "STEP D QD=32 + NVME_SHUT, Q1S -> restart -> Q2"
boot NWOAS_S262_HV=1 NWOAS_NVME_QD=32 NWOAS_PWR_NVME_SHUT=1 || back bootD; q1s || back stepD-q1s
boot NWOAS_S262_HV=1 NWOAS_NVME_QD=32 NWOAS_PWR_NVME_SHUT=1 || back bootD2
job $D/jobs/Q2-verify.cmd; echo "$JL" | grep -a -E 'INTEG|verify_rc' | tee -a $RES
echo "$JL" | grep -a -q 'verify_rc=0' || back stepD-q2; safety || back stepD-safety
job $D/jobs/Q4-chkdsk.cmd; echo "$JL" | grep -a -E 'chkdsk_rc|DISKEVT|BOOT153|problem|error' | head -20 | tee -a $RES
DE=$(echo "$JL" | sed -n 's/^DISKEVT_COUNT \([0-9]*\).*/\1/p' | tail -1)
echo "$JL" | grep -a -q 'chkdsk_rc=0' || { say "STEP Q4 FAIL: chkdsk_rc not 0"; back Q4-chkdsk; }
[ "$DE" = 0 ] || { say "STEP Q4 FAIL: disk error events DISKEVT_COUNT=${DE:-missing}"; back Q4-diskevt; }
say "ALL S262S STEPS DONE link=$L out=$OUT"
