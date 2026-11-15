#!/bin/bash
# Restart current S242 Windows -> mini comes back at idle m1n1 proxy -> S240 Wi-Fi probe --run -> m1n1 reboot -> relaunch S242.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
RES=$ROOT/wifi-s240/hw/results.txt
W4=$ROOT/layout-s231/jobs/W4-restart.cmd
CUR=$(ls -t $ROOT/logs/launcher-*.out | head -1)
D1=/dev/cu.usbmodemC07HL05SQ6NY1; D3=/dev/cu.usbmodemC07HL05SQ6NY3
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
L=$(sed -n 's/^Log: //p' $CUR).link
say "START cur=$CUR"
if ! grep -q "LAUNCHER EXIT" $CUR; then
  if [ -f $L/job.json ]; then j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
    for i in $(seq 1 120); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done; fi
  python3 -I $ROOT/transport-s123/queue_job.py $L $W4 || { say "ABORT queue W4"; exit 3; }
  for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $CUR && break; sleep 2; done
fi
grep -q "LAUNCHER EXIT" $CUR || { say "ABORT launcher did not exit"; exit 4; }
for i in $(seq 1 90); do [ -e $D1 ] && [ -e $D3 ] && break; sleep 2; done
sleep 6
/usr/sbin/lsof $D1 $D3 2>/dev/null && { say "ABORT serial busy"; exit 5; }
say "m1n1 idle; running S240 probe"
bash $ROOT/wifi-s240/run.sh --run > $ROOT/wifi-s240/hw/probe-console.txt 2>&1; rc=$?
say "probe rc=$rc"
grep -a -E "^\[s240\]|verdict|PASS|FAIL|STOP|Error|Traceback" $ROOT/wifi-s240/hw/probe-console.txt | tail -40 | cut -c1-220 | tee -a $RES
sleep 2
say "rebooting m1n1"
M1N1DEVICE=$D1 M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1 /Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows/.venv-hv/bin/python3 -I -B /Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows/proxyclient/tools/reboot.py >> $RES 2>&1 || say "reboot.py rc=$?"
sleep 15
for i in $(seq 1 90); do [ -e $D1 ] && [ -e $D3 ] && break; sleep 2; done
sleep 4
CUR=$ROOT/logs/launcher-s242-$(date "+%Y%m%d-%H%M%S")-after-wifi.out
(caffeinate -dims bash $ROOT/display-s242-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
say "relaunched S242 -> $CUR"
for i in $(seq 1 240); do L=$(sed -n 's/^Log: //p' $CUR 2>/dev/null).link; grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && { say "S242 worker connected"; break; }; grep -q "LAUNCHER EXIT" $CUR && { say "S242 launcher exited early"; break; }; sleep 5; done
say "S240 HW DONE"
wait
