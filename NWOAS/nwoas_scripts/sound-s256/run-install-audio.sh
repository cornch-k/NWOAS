#!/bin/bash
# S256: restart the running Windows into agxdcp-s256 (S256 audio payload + sound-s256/audio_module.py)
# with the audio delivery manifest -> deliver the NWAU0001 package + test WAV -> D28 (GPU re-arm, as
# agx-s248/run-reboot-arm-dcp256.sh) -> A1 install -> A2 status [-> A3 play when the 3rd arg is "play"].
# Style of agx-s248/run-install-2757.sh.  Usage:
#   bash run-install-audio.sh <current launcher .out> <current LOG.link> [play]
#   bash run-install-audio.sh idle - [play]      (Windows not running, Mac mini idle at the D4 proxy)
# Not run by the agent that wrote it.  Build success is not working audio.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
PKG=${NWAU_PKG:-snd-j274-20261118-1131}
A1JOB=${NWAU_A1:-A1-install-audio.cmd}
MANIFEST=$ROOT/file-delivery-s243/manifests/$PKG.json
DELIVER=$ROOT/file-delivery-s243/jobs/$PKG.cmd
J=$ROOT/sound-s256/jobs
mkdir -p $ROOT/sound-s256/runs
RES=$ROOT/sound-s256/runs/install-audio-$(date "+%Y%m%d-%H%M%S").txt
W4=$ROOT/layout-s231/jobs/W4-restart.cmd
OLDOUT=$1; OLDLINK=$2; PLAY=${3:-}
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
jobwait() { local L=$1 j; j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 480); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 5; done
  JOBLOG=$L/job-$j.log; }
runjob() { python3 -I $ROOT/transport-s123/queue_job.py $L $1 >/dev/null || { say "ABORT queue $1"; exit 6; }; jobwait $L
  iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | head -c 60000 | tee -a $RES; }
# the manifest re-checks size+sha at launch; check here too so a stale package stops before the restart
python3 -I -B -c "import sys, json; sys.path.insert(0, sys.argv[2]); import s243_manifest as m; m.load(json.load(open(sys.argv[1])))" \
  $MANIFEST $ROOT/file-delivery-s243 || { echo "ABORT manifest does not match the files"; exit 7; }
say "START old=$OLDLINK pkg=$PKG"
if [ "$OLDOUT" = idle ]; then
  # Windows is not running: the Mac mini sits at the D4 proxy.  m1n1 is restarted first so UEFI
  # starts from a clean state (as after the Wi-Fi proxy runs).
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
CUR=$ROOT/logs/launcher-agxdcp256-audio-$(date "+%Y%m%d-%H%M%S").out
(NWOAS_DELIVER_MANIFEST=$MANIFEST caffeinate -dims bash $ROOT/agxdcp-s256-guest-test.sh > $CUR 2>&1; echo "LAUNCHER EXIT $?" >> $CUR) &
say "launched $CUR"
L=""
for i in $(seq 1 240); do LOG=$(sed -n "s/^Log: //p" $CUR | head -1); [ -n "$LOG" ] && L=$LOG.link
  [ -n "$L" ] && grep -q '"kind": 4' $L/events.jsonl 2>/dev/null && break
  grep -q "LAUNCHER EXIT" $CUR && { say "ABORT: launcher exited before worker"; tail -20 $CUR | tee -a $RES; exit 2; }
  sleep 5; done
grep -q '"kind": 4' $L/events.jsonl 2>/dev/null || { say "ABORT worker timeout"; exit 2; }
say "worker connected link=$L"
say "audio module: $(grep -a -E '\[S256-audio\] (READY|NOT READY|FAIL)' ${LOG} ${CUR} 2>/dev/null | head -2 | tr '\n' ' ')"
python3 -I $ROOT/transport-s123/queue_job.py $L $DELIVER >/dev/null; jobwait $L
say "deliver: $(iconv -f CP949 -t UTF-8 $JOBLOG | tr -d '\r' | grep -a -E 'READBACK|EXIT|FAIL' | tr '\n' ' ')"
grep -a -q "READBACK MATCH" $JOBLOG || { say "ABORT delivery"; wait; exit 5; }
grep -a -q "READBACK MISMATCH" $JOBLOG && { say "ABORT delivery mismatch"; wait; exit 5; }
runjob $ROOT/agx-s248/jobs/D28-rearm.cmd > /dev/null; say "D28 done"
runjob $J/$A1JOB
runjob $J/A2-status.cmd
if [ "$PLAY" = play ]; then runjob $J/A3-play.cmd; fi
say "AUDIO INSTALL DONE link=$L out=$CUR"
wait
