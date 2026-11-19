#!/bin/bash
# NWOAS 4-1 rehearsal, part 2 (PLAN-4-1-disk-boot.md 7장): the FULL S268 daily stack on the D4 that the
# stage1 rehearsal left running (iBoot -> D4 -> MacBook chainload stage1 -> stage1 NVMe up/read/shut down ->
# ESP m1n1/boot.bin = D4), WITHOUT a reboot in between, then the SSD checks.
#   bash standalone-s264/stage1/run-s268-after-stage1.sh          PREPARED, NOT RUN BY THE AGENT THAT WROTE IT
# = bt-s268/run-s268.sh with the "idle" branch's reboot.py removed (that reboot would throw away exactly the
#   state under test) and no W4 branch, + bt-s268/run-s268-regress.sh section 2 (Q1S -> safety -> Q2 -> safety
#   -> R3 chkdsk /scan + disk events since boot).  run-s268.sh and run-s268-regress.sh are not modified.
# Last line: "ST1-S268 DONE | qd=ON | q1s=PASS | q2=PASS | safety=ok/ok | chkdsk_rc=0 | diskevt=0 | VERDICT=PASS"
#            (any other value -> VERDICT=CHECK) or "ST1-S268 ABORT <reason>".
# HARDWARE-UNVERIFIED.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
J=$ROOT/bt-s261/jobs
JW=$ROOT/wifi-s259/jobs
JQ=$ROOT/nvmeqd-s262/jobs
JR=$ROOT/bt-s268/jobs
mkdir -p $ROOT/standalone-s264/runs
RES=$ROOT/standalone-s264/runs/s268-after-stage1-$(date "+%Y%m%d-%H%M%S").txt
LAST=$RES.lastjob.txt
HVPAT='\[s259\]|\[s261\]|\[S262\]|NVMe QD engine|NWOAS-PWR|Traceback|AssertionError|RuntimeError'
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
abort() { say "ABORT: $*"; rm -f $LAST; echo "ST1-S268 ABORT $*" | tee -a $RES; exit 1; }
jobwait() { local L=$1 j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBLOG=$L/job-$j.log; }
runjob() { python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || abort "queue $1"; jobwait $L
  iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | head -c 60000 | tee -a $RES; }
say "START (no reboot: the D4 proxy left by run-stage1-rehearsal.sh) NWOAS_WIFI=${NWOAS_WIFI:-1} NWOAS_BT=${NWOAS_BT:-1} NWOAS_S262_HV=${NWOAS_S262_HV-<unset>} NWOAS_NVME_QD=${NWOAS_NVME_QD-<unset>}"
[ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] || abort "D4 proxy devices missing (rehearsal did not leave D4 running)"
CUR=$ROOT/logs/launcher-agxdcp268-st1-$(date "+%Y%m%d-%H%M%S").out
(PYTHONDONTWRITEBYTECODE=1 caffeinate -dims bash $ROOT/agxdcp-s268-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
say "launched $CUR"
L=""; LOG=""
for i in $(seq 1 240); do LOG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LOG" ] && L=$LOG.link
  [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
  grep -q "LAUNCHER EXIT" $CUR && { tail -20 $CUR | tee -a $RES
    grep -a -E "$HVPAT" $LOG 2>/dev/null | head -80 | tee -a $RES; abort "launcher exited before worker"; }
  sleep 5; done
grep -q '"kind": 4' $L/events.jsonl 2>/dev/null || { grep -a -E "$HVPAT" $LOG | head -100 | tee -a $RES; abort "worker timeout"; }
say "worker connected link=$L"
grep -a -E '\[s259\] (BOOT|CMD|READY|FAIL|OFF|power off)|\[s261\] (BOOT|CMD|READY|FAIL|OFF|ensure|reset|fn1)|\[S262\] (NVMe QD|mark|nvmeqd module)|NVMe QD engine|NWOAS-PWR S260' $LOG | head -80 | tee -a $RES
QD=OFF; grep -a -q '^\[S262\] NVMe QD engine ON' $LOG && QD=ON; say "NVMe QD engine: $QD"
runjob $ROOT/agx-s248/jobs/D28-rearm.cmd > /dev/null; say "D28 done"
runjob $J/B0-bt-acpi-status.cmd
runjob $JW/W1-wifi-pci-status.cmd
say "S268 DONE link=$L out=$CUR log=$LOG"

# ---- SSD (copy of bt-s268/run-s268-regress.sh section 2)
STUCK=0
job() { local f=$1 j i
  [ $STUCK = 0 ] || return 1
  python3 -I $ROOT/transport-s123/queue_job.py $L $f >/dev/null || { say "queue fail $(basename $f)"; return 1; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 600); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 3; done
  grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl || { STUCK=1; say "job $j $(basename $f) not finished after 30 min"; return 1; }
  iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | head -c 60000 > $LAST
  { echo "----- job $j $(basename $f)"; head -c 20000 $LAST; echo; } >> $RES; }
nrecs() { [ -f $L/nvmeqd.jsonl ] && awk 'NF{n++} END{print n+0}' $L/nvmeqd.jsonl || echo 0; }
QREC0=$(nrecs)
safety() { local bad=0 nrec
  cat "$LOG" "$LOG.vuart" 2>/dev/null | grep -a -q -E 'NWOAS-S262 NVMe QD fault|REFUSED write outside|NWOAS-S262 ANS tag .* timed out' &&
    { say "SAFETY: HV fault/refused/timeout line"; bad=1; }
  grep -a -E '\[S262\] NVMe QD (engine|off)' "$LOG" | tail -1 | sed 's/^/ENGINE: /' | tee -a $RES
  nrec=$(nrecs)
  [ "$nrec" -gt "$QREC0" ] || { say "SAFETY: no new nvmeqd.jsonl record ($nrec, was $QREC0): job marks not recorded"; bad=1; }
  if [ -s $L/nvmeqd.jsonl ]; then python3 -I $ROOT/nvmeqd-s262/nvmeqd_report.py $L/nvmeqd.jsonl 2>&1 | head -c 8000 >> $RES
    python3 -I - $L/nvmeqd.jsonl <<'PY' 2>&1 | tee -a $RES || bad=1
import json,sys
r=[json.loads(l) for l in open(sys.argv[1]) if l.strip()][-1]['snapshot']
k=('faults','timeouts','dev_errors','drain_fail','eng_bad_tag','eng_inval_fail','storm','eng_faulted')
b={x:r.get(x,0) for x in k if r.get(x,0)}
print('SAFETY_COUNTERS', b or 'all 0'); sys.exit(1 if b else 0)
PY
  fi; [ $bad = 0 ] && echo "SAFETY OK" >> $RES || echo "SAFETY BAD" >> $RES; return $bad; }
say "== SSD"
job $JQ/Q1S-io-integ-normal.cmd || abort "Q1S"
grep -a -E 'IOPRIO|H2|IOPS|MB/s|INTEG|integ_rc|io_rc|FAIL' $LAST | head -40 | tee -a $RES
Q1S=FAIL; grep -a -q 'IOPRIO child pid=.* process=2(normal) thread=2(normal)' $LAST && grep -a -q 'INTEG PASS' $LAST &&
  grep -a -q 'io_rc=0 integ_rc=0' $LAST && Q1S=PASS
say "Q1S $Q1S"; S1=ok; safety || S1=BAD
job $JQ/Q2-verify.cmd || abort "Q2"; grep -a -E 'INTEG|verify_rc|FAIL' $LAST | head -10 | tee -a $RES
Q2=FAIL; grep -a -q 'verify_rc=0' $LAST && Q2=PASS; say "Q2 $Q2"; S2=ok; safety || S2=BAD
job $JR/R3-chkdsk-diskevt-boot.cmd || abort "R3"
grep -a -E 'chkdsk_rc|DISKEVT|BOOT153|R3 SINCE' $LAST | head -40 | tee -a $RES
CHK=$(sed -n 's/^S268R R3 chkdsk_rc=\([0-9]*\).*/\1/p' $LAST | tail -1); DE=$(sed -n 's/^DISKEVT_COUNT \([0-9]*\).*/\1/p' $LAST | tail -1)
V=PASS
[ $QD = ON ] && [ $Q1S = PASS ] && [ $Q2 = PASS ] && [ $S1 = ok ] && [ $S2 = ok ] && [ "${CHK:-?}" = 0 ] && [ "${DE:-?}" = 0 ] || V=CHECK
rm -f $LAST
echo "ST1-S268 DONE | qd=$QD | q1s=$Q1S | q2=$Q2 | safety=$S1/$S2 | chkdsk_rc=${CHK:-?} | diskevt=${DE:-?} | VERDICT=$V" | tee -a $RES
wait
