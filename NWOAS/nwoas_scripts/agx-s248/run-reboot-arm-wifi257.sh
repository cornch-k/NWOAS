#!/bin/bash
# S257: restart Windows -> Wi-Fi stage 2 (wifi-s257/run.sh --run --stage ${STAGE:-fw}) on the idle D4 proxy
# -> m1n1 reboot (tools/reboot.py, like wifi-s240/hw/run-hw.sh) -> relaunch agxdcp-s256 -> D28 re-arm.
# Usage: bash agx-s248/run-reboot-arm-wifi257.sh <old launcher .out> <old .link> <tag>   [STAGE=ids|chip|otp|fw|scan]
# Based on run-reboot-arm-beep255.sh (D4-proxy step) and run-reboot-arm-dcp256.sh (relaunch).
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
OLDOUT=$1; OLDLINK=$2; TAG=$3; STAGE=${STAGE:-fw}
RES=$ROOT/agx-s248/runs/reboot-$TAG.txt
W4=$ROOT/layout-s231/jobs/W4-restart.cmd
M1=$ROOT/../m1n1_windows
D1=/dev/cu.usbmodemC07HL05SQ6NY1; D3=/dev/cu.usbmodemC07HL05SQ6NY3
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
jobwait() { local L=$1 j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBLOG=$L/job-$j.log; }
say "START old=$OLDLINK stage=$STAGE"
python3 -I $ROOT/transport-s123/queue_job.py $OLDLINK $W4 >/dev/null || { say "ABORT queue W4"; exit 3; }
for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $OLDOUT && break; sleep 2; done
grep -q "LAUNCHER EXIT" $OLDOUT || { say "ABORT: old launcher did not exit"; exit 4; }
for i in $(seq 1 90); do [ -e $D1 ] && [ -e $D3 ] && break; sleep 2; done
sleep 6
/usr/sbin/lsof $D1 $D3 >/dev/null 2>&1 && { say "ABORT serial busy"; exit 5; }
mkdir -p $ROOT/wifi-s257/runs
WLOG=$ROOT/wifi-s257/runs/console-$TAG-$(date "+%Y%m%d-%H%M%S").txt
M1N1DEVICE=$D1 M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=$M1/proxyclient "$M1/.venv-hv/bin/python3" -u $ROOT/recovery-s126/proxy_ready.py > $WLOG 2>&1 \
  || { say "ABORT proxy not ready (see $WLOG)"; exit 6; }
bash $ROOT/wifi-s257/run.sh --run --stage $STAGE >> $WLOG 2>&1; WRC=$?
grep -a -E "^\[s240\]|^\[s257\]|verdict|STOP|Error|Traceback" $WLOG | grep -v "fw-console" | tail -90 | cut -c1-240 | tee -a $RES
grep -a "fw-console" $WLOG | tail -8 | cut -c1-240 | tee -a $RES
say "WIFI rc=$WRC log=$WLOG"
if [ $WRC -eq 4 ]; then say "STOP: Wi-Fi power-down NOT confirmed. Power-cycle the Mac mini by hand; not relaunching."; exit 7; fi
sleep 2
say "rebooting m1n1"
M1N1DEVICE=$D1 M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1 "$M1/.venv-hv/bin/python3" -I -B $M1/proxyclient/tools/reboot.py >> $RES 2>&1 || say "reboot.py rc=$?"
sleep 15
for i in $(seq 1 90); do [ -e $D1 ] && [ -e $D3 ] && break; sleep 2; done
sleep 4
CUR=$ROOT/logs/launcher-agxdcp256-$TAG-$(date "+%Y%m%d-%H%M%S").out
(caffeinate -dims bash $ROOT/agxdcp-s256-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
say "launched $CUR"
L=""
for i in $(seq 1 240); do LOG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LOG" ] && L=$LOG.link
  [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
  grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: launcher exited before worker"; exit 2; }
  sleep 5; done
say "worker connected link=$L"
python3 -I $ROOT/transport-s123/queue_job.py $L $ROOT/agx-s248/jobs/D28-rearm.cmd >/dev/null; jobwait $L
iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | grep -a -E "DEV|Wom1|DWM" | tee -a $RES
say "ARMED DONE link=$L out=$CUR"
wait
