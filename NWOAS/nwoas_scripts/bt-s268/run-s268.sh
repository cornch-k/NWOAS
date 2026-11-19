#!/bin/bash
# S268: start Windows with agxdcp-s268-guest-test.sh (S261 Wi-Fi + Bluetooth payload + bt_module.py on the
# S262 default stack: g262R HV, power_module.py, nvmeqd_module.py, NWOAS_NVME_QD=32) -> D28 (GPU re-arm)
# -> B0 Bluetooth/Wi-Fi/USB-A status job -> W1 Wi-Fi PCI status job.  Copy of bt-s261/run-s261.sh.
#   bash run-s268.sh <current launcher .out> <current LOG.link>   (Windows running: W4 restart first)
#   bash run-s268.sh idle -                                      (Mac mini idle at the D4 proxy)
# Environment passed through to the launcher: NWOAS_BT=0 (Wi-Fi exactly as S259, BT hidden), NWOAS_WIFI=0,
# NWOAS_S262_HV=0 / NWOAS_NVME_QD=<n> (agxdcp-s268 header), NWOAS_DELIVER_MANIFEST=<...json>.
# Not run by the agent that wrote it.  HARDWARE-UNVERIFIED.  A device in Device Manager is not working Bluetooth.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
J=$ROOT/bt-s261/jobs
JW=$ROOT/wifi-s259/jobs
mkdir -p $ROOT/bt-s268/runs
RES=$ROOT/bt-s268/runs/run-s268-$(date "+%Y%m%d-%H%M%S").txt
W4=$ROOT/layout-s231/jobs/W4-restart.cmd
OLDOUT=$1; OLDLINK=$2
HVPAT='\[s259\]|\[s261\]|\[S262\]|NVMe QD engine|NWOAS-PWR|Traceback|AssertionError|RuntimeError'
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
jobwait() { local L=$1 j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBLOG=$L/job-$j.log; }
runjob() { python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || { say "ABORT queue $1"; exit 6; }; jobwait $L
  iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | head -c 60000 | tee -a $RES; }
say "START old=$OLDLINK NWOAS_WIFI=${NWOAS_WIFI:-1} NWOAS_BT=${NWOAS_BT:-1} NWOAS_S262_HV=${NWOAS_S262_HV-<unset>} NWOAS_NVME_QD=${NWOAS_NVME_QD-<unset>} MANIFEST=${NWOAS_DELIVER_MANIFEST:-}"
if [ "$OLDOUT" = idle ]; then
  M1=/Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows
  M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1 \
    "$M1/.venv-hv/bin/python3" -I -B $M1/proxyclient/tools/reboot.py >> $RES 2>&1 || say "reboot.py rc=$?"
  for i in $(seq 1 30); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] || break; sleep 1; done
  say "idle start: m1n1 rebooted (proxy device gone after ${i}s)"
else
  python3 -I $ROOT/transport-s123/queue_job.py $OLDLINK $W4 >/dev/null || { say "ABORT queue W4"; exit 3; }
  for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $OLDOUT && break; sleep 2; done
  grep -q "LAUNCHER EXIT" $OLDOUT || { say "ABORT: old launcher did not exit"; exit 4; }
  say "old launcher exited"
fi
for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 4
CUR=$ROOT/logs/launcher-agxdcp268-bt-$(date "+%Y%m%d-%H%M%S").out
(PYTHONDONTWRITEBYTECODE=1 caffeinate -dims bash $ROOT/agxdcp-s268-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
say "launched $CUR"
L=""; LOG=""
for i in $(seq 1 240); do LOG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LOG" ] && L=$LOG.link
  [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
  grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: launcher exited before worker"; tail -20 $CUR | tee -a $RES
    grep -a -E "$HVPAT" $LOG 2>/dev/null | head -80 | tee -a $RES; exit 2; }
  sleep 5; done
grep -q '"kind": 4' $L/events.jsonl 2>/dev/null || { say "ABORT worker timeout"; grep -a -E "$HVPAT" $LOG | head -100 | tee -a $RES; exit 2; }
say "worker connected link=$L"
say "s259/s261/S262 hypervisor lines:"
grep -a -E '\[s259\] (BOOT|CMD|READY|FAIL|OFF|power off)|\[s261\] (BOOT|CMD|READY|FAIL|OFF|ensure|reset|fn1)|\[S262\] (NVMe QD|mark|nvmeqd module)|NVMe QD engine|NWOAS-PWR S260' $LOG | head -80 | tee -a $RES
grep -a -c '\[spi-en\] irq=695' $LOG | sed 's/^/spi-en 695 count: /' | tee -a $RES
runjob $ROOT/agx-s248/jobs/D28-rearm.cmd > /dev/null; say "D28 done"
runjob $J/B0-bt-acpi-status.cmd
runjob $JW/W1-wifi-pci-status.cmd
say "S268 DONE link=$L out=$CUR log=$LOG"
wait
