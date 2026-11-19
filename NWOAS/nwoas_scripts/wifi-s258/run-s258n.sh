#!/bin/bash
# s258n (1.0.1.5) runs.  Every run restarts Windows (the stall pattern was "3 Windows recoveries per boot").
#   bash wifi-s258/run-s258n.sh base  <old .out> <old .link> [observe_s]   deliver + in-place install (W1), all s258n defaults
#                                                                         (ConnectedScan=1, TX stall watchdog on), W19 ping on
#   bash wifi-s258/run-s258n.sh scan0 <old .out> <old .link> [observe_s]   ConnectedScan=0 for the whole boot (firmware scan while
#                                                                         connected as s258m): stall onset vs scan, watchdog recovery
# scan0 needs s258n installed already (a base run first); the switch is set before the restart and put back at the end.
# Each run: W17 ETW start after boot/W1, W19 ping (gateway every second, looked up until found), W2 every 300 s,
# W18 (ETW stop + masked text) after the first W2 that shows a host disconnect (3@ / 8@) or a TX stall
# (NwoaCntTxStall > 0), else at the end; then W19 stop and W15 (WLAN events + connection profile v4/v6).
# Never W9 (uninstall).  Result: wifi-s258/runs/s258n-<mode>-<time>.txt
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/wifi-s258; J=$D/jobs; mkdir -p $D/runs
MODE=${1:-}; OLDOUT=${2:-}; OLDLINK=${3:-}; OBS=${4:-1200}
case "$MODE" in base|scan0) ;; *) echo "usage: $0 base|scan0 <old .out> <old .link> [observe_s]"; exit 1 ;; esac
[ -n "$OLDOUT" ] && [ -n "$OLDLINK" ] || { echo "need <old .out> <old .link>"; exit 1; }
N=wifi-j274-20261119-0546-s258n
MAN=$ROOT/file-delivery-s243/manifests/$N.json
RES=$D/runs/s258n-$MODE-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
job() { local LK=$1 f=$2 j; python3 -I $ROOT/transport-s123/queue_job.py $LK $f >/dev/null || { say "queue fail $f"; return 1; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $LK/job.json)
  for i in $(seq 1 600); do grep -q "\"job\": $j, \"kind\": 3" $LK/events.jsonl && break; sleep 3; done
  iconv -c -f CP949 -t UTF-8 $LK/job-$j.log | tr -d '\r' | head -c 60000 > $D/runs/last-job.txt; cat $D/runs/last-job.txt >> $RES; }
say "MODE=$MODE observe=${OBS}s"
KEYS='NwoaHostDisassocLog|NwoaCntTx |NwoaCntTxStatus|NwoaCntRx |NwoaNowMs|NwoaPool|NwoaRingIdx|NwoaStallLog|NwoaCntTxStall|NwoaCntStall|NwoaCntScanSkipped|NwoaConnScan|NwoaScanDiag|NwoaDiscDiag|NwoaMb|NwoaEvDiag|NwoaEvAll|NwoaFwPm|NwoaCfgS258n|NwoaConsoleLive|NwoaConsoleBytes|NwoaFwCnt|NwoaCntPmKick'

# ---- switch for the whole next boot (scan0), on the Windows that runs now
SW_ON=""; SW_OFF=""
[ $MODE = scan0 ] && { SW_ON=$J/W23-connscan-0.cmd; SW_OFF=$J/W23-connscan-1.cmd; }
if [ $MODE != base ]; then
  job $OLDLINK $J/W2-status.cmd || exit 1
  grep -a -q '1\.0\.1\.5' $D/runs/last-job.txt || { say "ABORT: s258n (1.0.1.5) is not the running driver; do a base run first"; exit 1; }
  job $OLDLINK $SW_ON || exit 1; grep -a 'NwoaCfgS258n' $D/runs/last-job.txt | tee -a $RES
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
  job $L $J/W1-install-wifi-s258n.cmd || exit 1; say "W1 done"
  grep -a -E 'PNPUTIL EXIT|DriverVersion|status=' $D/runs/last-job.txt | head -6 | tee -a $RES
fi
job $L $J/W17-etw-wlan-start-s258n.cmd; say "W17 done"
grep -a -E '^W17 (NOW|PROV|NOPROV|WLANSVC|KERNEL|PROVIDERS|LOGMAN|SCEN)' $D/runs/last-job.txt | head -20 | tee -a $RES
job $L $J/W19-ping-start-s258n.cmd; say "W19 start"; grep -a '^W19' $D/runs/last-job.txt | head -4 | tee -a $RES

# ---- observe
ETW=1
for n in $(seq 1 $((OBS/300))); do sleep 300; job $L $J/W2-status.cmd; say "W2 #$n"
  grep -a -E "$KEYS" $D/runs/last-job.txt | cut -c1-900 | tee -a $RES
  if [ $ETW = 1 ] && { grep -a 'NwoaHostDisassocLog' $D/runs/last-job.txt | grep -q -E '(^|[^0-9])[38]@' ||
       grep -a 'NwoaCntTxStall ' $D/runs/last-job.txt | grep -q -v -E '0x0$'; }; then
    job $L $J/W18-etw-wlan-stop-s258n.cmd; ETW=0; say "W18 done (after the first host disconnect / TX stall)"
    grep -a -E '^W18 (NOW|LINES|PROVLINES|CAP|PRINTED|MASK|ETL|TEXT|NO_|CONVERT|NETSH)' $D/runs/last-job.txt | head -30 | tee -a $RES; fi
done
if [ $ETW = 1 ]; then job $L $J/W18-etw-wlan-stop-s258n.cmd; say "W18 done (end, no host disconnect / stall seen)"
  grep -a -E '^W18 (NOW|LINES|PROVLINES|CAP|PRINTED|MASK|ETL|TEXT|NO_|CONVERT|NETSH)' $D/runs/last-job.txt | head -30 | tee -a $RES; fi
job $L $J/W19-ping-stop.cmd; say "W19 stop"; grep -a '^W19' $D/runs/last-job.txt | head -80 | tee -a $RES
job $L $J/W15-wlan-events.cmd; say "W15 done"
grep -a -E "W15 (COUNT3H|EV|NIC|PROF)|NwoaHostDisassocLog|NwoaCntStats" $D/runs/last-job.txt | cut -c1-200 | head -60 | tee -a $RES
if [ -n "$SW_OFF" ]; then job $L $SW_OFF; say "switch back to ConnectedScan=1"; grep -a 'NwoaCfgS258n' $D/runs/last-job.txt | tee -a $RES; fi
# pass check (numbers only)
H=$(grep -a 'NwoaHostDisassocLog' $RES | tail -1); RX=$(grep -a 'NwoaCntRx ' $RES | awk '{print $3}' | tr '\n' ' ')
say "CHECK host3=$(echo "$H" | grep -o -E '(^|[^0-9])3@' | wc -l | tr -d ' ') host8=$(echo "$H" | grep -o -E '(^|[^0-9])8@' | wc -l | tr -d ' ') rx=[$RX] $(grep -a '^W19 END' $RES | tail -1) $(grep -a 'W15 PROF' $RES | tail -1)"
say "S258N $MODE DONE link=$L"
