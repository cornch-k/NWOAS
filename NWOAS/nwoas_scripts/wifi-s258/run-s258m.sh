#!/bin/bash
# s258m (1.0.1.4) runs.  Every run restarts Windows (the disconnect pattern is "3 cycles per boot").
#   bash wifi-s258/run-s258m.sh base <old .out> <old .link> [observe_s]   deliver + in-place install (W1), both H switches 0
#   bash wifi-s258/run-s258m.sh h1   <old .out> <old .link> [observe_s]   ScanReportConnectedOnly=1 for the whole boot
#   bash wifi-s258/run-s258m.sh h3   <old .out> <old .link> [observe_s]   NoDisassocIndOnHostDisconnect=1 for the whole boot
#   bash wifi-s258/run-s258m.sh h2   <old .out> <old .link> [observe_s]   gateway ping every second (W19) during the run
#   bash wifi-s258/run-s258m.sh fixoff <old .out> <old .link> [observe_s] BssAgeInfo=0 LinkInfoReal=0 (s258l BSS/link reports)
# h1/h2/h3/fixoff need s258m installed already (a base run first).  The switch is set on the running Windows
# before the restart, so the driver reads it at boot; it is put back to 0 at the end (W20-0 / W21-0).
# Each run: W17 ETW start right after boot/W1, W2 every 300 s, W18 (ETW stop + filtered text) right
# after the first W2 that shows a host disconnect (else at the end), W15 WLAN events last.
# Never W9 (uninstall).  Result: wifi-s258/runs/s258m-<mode>-<time>.txt
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/wifi-s258; J=$D/jobs; mkdir -p $D/runs
MODE=${1:-}; OLDOUT=${2:-}; OLDLINK=${3:-}; OBS=${4:-1200}
case "$MODE" in base|h1|h2|h3|fixoff) ;; *) echo "usage: $0 base|h1|h2|h3|fixoff <old .out> <old .link> [observe_s]"; exit 1 ;; esac
[ -n "$OLDOUT" ] && [ -n "$OLDLINK" ] || { echo "need <old .out> <old .link>"; exit 1; }
N=wifi-j274-20261119-0429-s258m
MAN=$ROOT/file-delivery-s243/manifests/$N.json
RES=$D/runs/s258m-$MODE-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
job() { local LK=$1 f=$2 j; python3 -I $ROOT/transport-s123/queue_job.py $LK $f >/dev/null || { say "queue fail $f"; return 1; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $LK/job.json)
  for i in $(seq 1 600); do grep -q "\"job\": $j, \"kind\": 3" $LK/events.jsonl && break; sleep 3; done
  iconv -c -f CP949 -t UTF-8 $LK/job-$j.log | tr -d '\r' | head -c 60000 > $D/runs/last-job.txt; cat $D/runs/last-job.txt >> $RES; }
say "MODE=$MODE observe=${OBS}s"

# ---- switch for the whole next boot (h1/h3), on the Windows that runs now
SW_ON=""; SW_OFF=""
[ $MODE = h1 ] && { SW_ON=$J/W20-h1-scanconnonly-1.cmd; SW_OFF=$J/W20-h1-scanconnonly-0.cmd; }
[ $MODE = h3 ] && { SW_ON=$J/W21-h3-nodisassoc-1.cmd; SW_OFF=$J/W21-h3-nodisassoc-0.cmd; }
[ $MODE = fixoff ] && { SW_ON=$J/W22-fixes-off.cmd; SW_OFF=$J/W22-fixes-on.cmd; }
if [ $MODE != base ]; then
  job $OLDLINK $J/W2-status.cmd || exit 1
  grep -a -q '1\.0\.1\.4' $D/runs/last-job.txt || { say "ABORT: s258m (1.0.1.4) is not the running driver; do a base run first"; exit 1; }
  if [ -n "$SW_ON" ]; then job $OLDLINK $SW_ON || exit 1; grep -a 'NwoaCfgS258m' $D/runs/last-job.txt | tee -a $RES; fi
fi

# ---- restart Windows (base: with the delivery manifest)
T=$D/runs/boot-$(date +%H%M%S).txt
if [ $MODE = base ]; then
  (env NWOAS_AUDIO_PROT=moderate NWOAS_DELIVER_MANIFEST=$MAN nohup bash $ROOT/wifi-s259/run-s259.sh $OLDOUT $OLDLINK > $T 2>&1 &)
else
  (env NWOAS_AUDIO_PROT=moderate nohup bash $ROOT/wifi-s259/run-s259.sh $OLDOUT $OLDLINK > $T 2>&1 &)
fi
for i in $(seq 1 240); do grep -a -q -E "S259 DONE|ABORT" $T && break; sleep 5; done
cat $T >> $RES; r=$(grep -a "S259 DONE" $T | tail -1); [ -n "$r" ] || { say "BOOT FAIL"; exit 1; }
L=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L"

if [ $MODE = base ]; then
  job $L $ROOT/file-delivery-s243/jobs/$N.cmd || exit 1
  grep -a -q 'DELIVER COMPLETE' $D/runs/last-job.txt || { say "DELIVER (driver) not complete"; exit 1; }
  job $L $ROOT/file-delivery-s243/jobs/$N-scripts.cmd || exit 1
  grep -a -q 'DELIVER COMPLETE' $D/runs/last-job.txt || { say "DELIVER (scripts) not complete"; exit 1; }
  job $L $J/W1-install-wifi-s258m.cmd || exit 1; say "W1 done"
  grep -a -E 'PNPUTIL EXIT|DriverVersion|status=' $D/runs/last-job.txt | head -6 | tee -a $RES
fi
job $L $J/W17-etw-wlan-start.cmd; say "W17 done"
grep -a -E '^W17 (NOW|PROV|NOPROV|WLANSVC|KERNEL|PROVIDERS|LOGMAN|SCEN)' $D/runs/last-job.txt | head -20 | tee -a $RES
if [ $MODE = h2 ]; then job $L $J/W19-ping-start.cmd; say "W19 start"; grep -a '^W19' $D/runs/last-job.txt | head -4 | tee -a $RES; fi

# ---- observe
ETW=1
for n in $(seq 1 $((OBS/300))); do sleep 300; job $L $J/W2-status.cmd; say "W2 #$n"
  grep -a -E 'NwoaHostDisassocLog|NwoaScanDiag|NwoaDiscDiag|NwoaConnDiag|NwoaKeyDiag|NwoaCfgS258m|NwoaLinkBwMhz|NwoaLinkRateKbps|NwoaWmmNegotiated|NwoaCntScanSynth|NwoaCntDisassocIndSkipped|NwoaCntStats' $D/runs/last-job.txt | cut -c1-600 | tee -a $RES
  if [ $ETW = 1 ] && grep -a 'NwoaHostDisassocLog' $D/runs/last-job.txt | grep -q -E '(^|[^0-9])3@'; then
    job $L $J/W18-etw-wlan-stop.cmd; ETW=0; say "W18 done (after the first host disconnect)"
    grep -a -E '^W18 (NOW|LINES|PROVLINES|CAP|PRINTED|MASK|ETL|TEXT|NO_|CONVERT|NETSH)' $D/runs/last-job.txt | head -30 | tee -a $RES; fi
done
if [ $ETW = 1 ]; then job $L $J/W18-etw-wlan-stop.cmd; say "W18 done (end, no host disconnect seen)"
  grep -a -E '^W18 (NOW|LINES|PROVLINES|CAP|PRINTED|MASK|ETL|TEXT|NO_|CONVERT|NETSH)' $D/runs/last-job.txt | head -30 | tee -a $RES; fi
if [ $MODE = h2 ]; then job $L $J/W19-ping-stop.cmd; say "W19 stop"; grep -a '^W19' $D/runs/last-job.txt | head -80 | tee -a $RES; fi
job $L $J/W15-wlan-events.cmd; say "W15 done"
grep -a -E "W15 (COUNT3H|EV|NIC|PROF)|NwoaHostDisassocLog|NwoaCntStats" $D/runs/last-job.txt | cut -c1-200 | head -60 | tee -a $RES
if [ -n "$SW_OFF" ]; then job $L $SW_OFF; say "switch back to 0"; grep -a 'NwoaCfgS258m' $D/runs/last-job.txt | tee -a $RES; fi
say "S258M $MODE DONE link=$L"
