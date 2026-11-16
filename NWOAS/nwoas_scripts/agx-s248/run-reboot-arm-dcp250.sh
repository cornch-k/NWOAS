#!/bin/bash
# S250: restart Windows -> (no guest) chainload g250E -> dcp_swap_test.py -> reboot to D4 -> agxmed1g Windows + arm.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
NS=$ROOT/nwoas_scripts
OLDOUT=$1; OLDLINK=$2
RES=$NS/agx-s248/runs/dcp250.txt
W4=$NS/layout-s231/jobs/W4-restart.cmd
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
say "START old=$OLDLINK"
python3 -I $NS/transport-s123/queue_job.py $OLDLINK $W4 >/dev/null || { say "ABORT queue W4"; exit 3; }
for i in $(seq 1 200); do grep -q "LAUNCHER EXIT" $OLDOUT && break; sleep 2; done
grep -q "LAUNCHER EXIT" $OLDOUT || { say "ABORT: old launcher did not exit"; exit 4; }
for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 4
M1N1=$ROOT/m1n1_windows
PY=$M1N1/.venv-hv/bin/python3
S244=$NS/display-s244
HV=$ROOT/port-j274/gpu/hv/out-hv2/m1n1-g250E-agx-hv2-dcpasync.bin
LOG=$NS/logs/dcp-s250-$(date '+%Y%m%d-%H%M%S').log
export M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH=$M1N1/proxyclient:$NS
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = 9693cc6ac4a50c8428e58a1ddfd21011188e026d8ec4cd3dab6216dbeec0c0ee ] || { say "ABORT hv sha"; exit 1; }
"$PY" -u "$NS/recovery-s126/proxy_ready.py" >> "$LOG" 2>&1
PROBE_RC=0; "$PY" -B -u "$S244/probe_fw135_s244.py" > "$LOG.probe" 2>&1 || PROBE_RC=$?
[ "$PROBE_RC" -eq 0 ] || { say "ABORT probe $PROBE_RC"; exit 1; }
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$LOG" 2>&1
grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$LOG" || { say "ABORT chainload"; exit 2; }
say "chainloaded g250E, test start log=$LOG"
caffeinate -dims "$PY" -u "$ROOT/port-j274/gpu/hv/tools-hv2/dcp_swap_test.py" --out "$LOG.json" >> "$LOG" 2>&1; TRC=$?
say "test rc=$TRC"
timeout 60 "$PY" -u "$M1N1/proxyclient/tools/reboot.py" >> "$LOG" 2>&1; say "reboot rc=$?"
for i in $(seq 1 90); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && break; sleep 2; done
sleep 4
CUR=$NS/logs/launcher-agxmed1g-after250-$(date "+%Y%m%d-%H%M%S").out
(caffeinate -dims bash $NS/agxmed1g-s249-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
say "windows launched $CUR"
L=""
for i in $(seq 1 240); do LG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LG" ] && L=$LG.link
  [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
  grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: launcher exited before worker"; exit 5; }
  sleep 5; done
python3 -I $NS/transport-s123/queue_job.py $L $NS/agx-s248/jobs/D28-rearm.cmd >/dev/null
j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
iconv -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | grep -a -E "DEV|Wom1|DWM" | tee -a $RES
say "ALL DONE link=$L"
wait
