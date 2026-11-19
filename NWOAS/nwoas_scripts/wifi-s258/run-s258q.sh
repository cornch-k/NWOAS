#!/bin/bash
# s258q (1.0.1.8) runs.  Every run restarts Windows (as s258o/p).
#   bash wifi-s258/run-s258q.sh shapes   <old .out> <old .link> [observe_s]  deliver + in-place install (W1, all
#        defaults), then W29: ShapeCycle=1 + TestConnScanSec=120 (device restart).  While connected the driver
#        makes a test scan every 120 s and rotates its shape after each verdict: 0 base (directed own SSID,
#        active, full list), 1 foreign (one non-home channel), 2 homelong (home channel, 3000 ms active dwell),
#        3 wild (brcmfmac wildcard form), 4 passive (full list), 5 chunks (3 channels per escan, 2 s gaps).
#        Windows scans while connected are counted as "win".  The s258o fix (doorbell, flowring re-create)
#        stays the safety net.  W19 ping on.  Default observe 1800 s.
#   bash wifi-s258/run-s258q.sh reshapes <old .out> <old .link> [observe_s]  s258q installed already: W29 on the
#        running Windows, then restart Windows and the same observation.
# Each run: W17 ETW start, W19 ping (gateway every second), W2 every 300 s, W18 after the first W2 that shows a host
# disconnect (3@ / 8@) or a TX stall (NwoaCntTxStall > 0), else at the end; W19 stop, W15, W29 off, then the CHECK
# lines (one per shape: scans, alive, stuck).  Never W9 (uninstall).  Result: wifi-s258/runs/s258q-<mode>-<time>.txt
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/wifi-s258; J=$D/jobs; mkdir -p $D/runs
MODE=${1:-}; OLDOUT=${2:-}; OLDLINK=${3:-}; OBS=${4:-1800}
case "$MODE" in shapes|reshapes) ;; *) echo "usage: $0 shapes|reshapes <old .out> <old .link> [observe_s]"; exit 1 ;; esac
[ -n "$OLDOUT" ] && [ -n "$OLDLINK" ] || { echo "need <old .out> <old .link>"; exit 1; }
N=wifi-j274-20261119-1020-s258q
MAN=$ROOT/file-delivery-s243/manifests/$N.json
RES=$D/runs/s258q-$MODE-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
job() { local LK=$1 f=$2 j; python3 -I $ROOT/transport-s123/queue_job.py $LK $f >/dev/null || { say "queue fail $f"; return 1; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $LK/job.json)
  for i in $(seq 1 600); do grep -q "\"job\": $j, \"kind\": 3" $LK/events.jsonl && break; sleep 3; done
  iconv -c -f CP949 -t UTF-8 $LK/job-$j.log | tr -d '\r' | head -c 60000 > $D/runs/last-job.txt; cat $D/runs/last-job.txt >> $RES; }
say "MODE=$MODE observe=${OBS}s"
KEYS='NwoaHostDisassocLog|NwoaCntTx |NwoaCntTxStatus|NwoaCntRx |NwoaNowMs|NwoaPool|NwoaRingIdx|NwoaStallLog|NwoaCntTxStall|NwoaCntStall|NwoaConnScan|NwoaScanDiag|NwoaDiscDiag|NwoaCfgS258o|NwoaScanFlow|NwoaFwIdxNow|NwoaTcmIdxPtr|NwoaCntDbell|NwoaCntFlowDbell|NwoaMb|NwoaEvAll|NwoaFwPm|NwoaConsoleLive|NwoaFwCnt|NwoaCfgS258p|NwoaCand|NwoaInitIovSt|NwoaEscanRequest|NwoaCfgS258q|NwoaShape|NwoaEv124'

# ---- reshapes: switch for the whole next boot, on the Windows that runs now
SW_OFF=""
if [ $MODE = reshapes ]; then
  job $OLDLINK $J/W2-status.cmd || exit 1
  grep -a -q 'NwoaCfgS258q' $D/runs/last-job.txt || { say "ABORT: s258q (1.0.1.8) is not the running driver; do a shapes run first"; exit 1; }
  job $OLDLINK $J/W29-shapes-on.cmd || exit 1; grep -a -E 'NwoaCfgS258q|NwoaShape ' $D/runs/last-job.txt | tee -a $RES
  SW_OFF=$J/W29-shapes-off.cmd
fi

# ---- restart Windows (scans: with the delivery manifest)
T=$D/runs/boot-$(date +%H%M%S).txt
if [ $MODE = shapes ]; then
  (env NWOAS_AUDIO_PROT=moderate NWOAS_DELIVER_MANIFEST=$MAN nohup bash $ROOT/wifi-s259/run-s259.sh $OLDOUT $OLDLINK > $T 2>&1 &)
else
  (env NWOAS_AUDIO_PROT=moderate nohup bash $ROOT/wifi-s259/run-s259.sh $OLDOUT $OLDLINK > $T 2>&1 &)
fi
for i in $(seq 1 240); do grep -a -q -E "S259 DONE|ABORT" $T && break; sleep 5; done
cat $T >> $RES; r=$(grep -a "S259 DONE" $T | tail -1); [ -n "$r" ] || { say "BOOT FAIL"; exit 1; }
L=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L"

if [ $MODE = shapes ]; then
  job $L $ROOT/file-delivery-s243/jobs/$N.cmd || exit 1
  grep -a -q 'DELIVER COMPLETE' $D/runs/last-job.txt || { say "DELIVER (driver) not complete"; exit 1; }
  job $L $ROOT/file-delivery-s243/jobs/$N-scripts.cmd || exit 1
  grep -a -q 'DELIVER COMPLETE' $D/runs/last-job.txt || { say "DELIVER (scripts) not complete"; exit 1; }
  job $L $J/W1-install-wifi-s258q.cmd || exit 1; say "W1 done"
  grep -a -E 'PNPUTIL EXIT|DriverVersion|status=' $D/runs/last-job.txt | head -6 | tee -a $RES
  grep -a -q '1\.0\.1\.8' $D/runs/last-job.txt || { say "ABORT: 1.0.1.8 not installed (W1)"; exit 1; }
  job $L $J/W29-shapes-on.cmd || exit 1; say "W29 done (ShapeCycle=1, TestConnScanSec=120)"
  grep -a -E 'NwoaCfgS258q|NwoaShape |RESTART EXIT' $D/runs/last-job.txt | tee -a $RES
  SW_OFF=$J/W29-shapes-off.cmd
fi
job $L $J/W17-etw-wlan-start-s258q.cmd; say "W17 done"
grep -a -E '^W17 (NOW|PROV|NOPROV|WLANSVC|KERNEL|PROVIDERS|LOGMAN|SCEN)' $D/runs/last-job.txt | head -20 | tee -a $RES
job $L $J/W19-ping-start-s258q.cmd; say "W19 start"; grep -a '^W19' $D/runs/last-job.txt | head -4 | tee -a $RES

# ---- observe
ETW=1
for n in $(seq 1 $((OBS/300))); do sleep 300; job $L $J/W2-status.cmd; say "W2 #$n"
  grep -a -E "$KEYS" $D/runs/last-job.txt | cut -c1-1600 | tee -a $RES
  if [ $ETW = 1 ] && { grep -a 'NwoaHostDisassocLog' $D/runs/last-job.txt | grep -q -E '(^|[^0-9])[38]@' ||
       grep -a 'NwoaCntTxStall ' $D/runs/last-job.txt | grep -q -v -E '0x0$'; }; then
    job $L $J/W18-etw-wlan-stop-s258q.cmd; ETW=0; say "W18 done (after the first host disconnect / TX stall)"
    grep -a -E '^W18 (NOW|LINES|PROVLINES|CAP|PRINTED|MASK|ETL|TEXT|NO_|CONVERT|NETSH)' $D/runs/last-job.txt | head -30 | tee -a $RES; fi
done
if [ $ETW = 1 ]; then job $L $J/W18-etw-wlan-stop-s258q.cmd; say "W18 done (end, no host disconnect / stall seen)"
  grep -a -E '^W18 (NOW|LINES|PROVLINES|CAP|PRINTED|MASK|ETL|TEXT|NO_|CONVERT|NETSH)' $D/runs/last-job.txt | head -30 | tee -a $RES; fi
say "W19 stop"; job $L $J/W19-ping-stop.cmd; grep -a '^W19' $D/runs/last-job.txt | head -120 | tee -a $RES
job $L $J/W15-wlan-events.cmd; say "W15 done"
grep -a -E "W15 (COUNT3H|EV|NIC|PROF)|NwoaHostDisassocLog|NwoaCntStats" $D/runs/last-job.txt | cut -c1-200 | head -60 | tee -a $RES
if [ -n "$SW_OFF" ]; then job $L $SW_OFF; say "test switches back to defaults (next device start)"; grep -a 'NwoaCfgS258q' $D/runs/last-job.txt | tee -a $RES; fi

# ---- pass check (numbers only).  Ping times are ms since boot (W19), scan times ms of interrupt time (driver):
# the two clocks differ by about a second, so a ping loss counts as "in a scan" within 3 s of a scan window.
python3 -I - "$RES" "$MODE" <<'EOF' | tee -a $RES
import re, sys
res, mode = sys.argv[1], sys.argv[2]
lines = open(res, encoding='utf-8', errors='replace').read().splitlines()
def last(key):
    v = None
    for l in lines:
        m = re.match(r'^\s*' + key + r'\s+REG_(?:SZ|DWORD)\s*(.*)$', l)
        if m: v = m.group(1).strip()
    return v
host = last('NwoaHostDisassocLog') or ''
h3 = len(re.findall(r'(?:^|[^0-9])3@', host)); h8 = len(re.findall(r'(?:^|[^0-9])8@', host))
stall = int(last('NwoaCntTxStall') or '0x0', 16)
slog = last('NwoaStallLog') or ''
sf = last('NwoaScanFlow') or ''
sfl = last('NwoaScanFlowLog') or ''
d = dict(re.findall(r'(\w+)=(\d+)', sf))
conn = int(d.get('conn', 0))
wins = [(int(a), int(b)) for a, b in re.findall(r'\bs(\d+) e(\d+) ', sfl)]
i0 = max([i for i, l in enumerate(lines) if l.endswith(' W19 stop')] or [0])
chg = [(int(t), s) for t, s in re.findall(r'^W19 CHG t_ms=(\d+) (\S+)', '\n'.join(lines[i0:]), re.M)]
end = re.findall(r'^W19 END t_ms=(\d+) ok=(\d+) bad=(\d+)', '\n'.join(lines[i0:]), re.M)
ok, bad, tend = (int(end[-1][1]), int(end[-1][2]), int(end[-1][0])) if end else (0, 0, 0)
inscan = 0
for k, (t, s) in enumerate(chg):
    if s == 'Success': continue
    t2 = chg[k + 1][0] if k + 1 < len(chg) else tend
    n = max(1, round((t2 - t) / 1000))
    if any(a - 3000 <= t <= b + 3000 or a - 3000 <= t2 <= b + 3000 or (t <= a and t2 >= b) for a, b in wins): inscan += n
loss = 100.0 * bad / (ok + bad) if ok + bad else 100.0
prof = ([l for l in lines if 'W15 PROF' in l and 'CHECK' not in l] or [''])[-1]
v4 = 'v4=Internet' in prof
crit = [('conn_scans>=8', conn >= 8), ('no_stall', stall == 0 and not slog.strip()), ('host_disc=0', h3 == 0 and h8 == 0),
        ('ping_loss<1%', loss < 1.0), ('v4=Internet', v4)]
print('CHECK mode=%s conn_scans=%d test_scans=%s sf alive=%s stuck=%s dbfix=%s refresh=%s refreshfail=%s idle=%s '
      'stall=%d stalllog=%s host3=%d host8=%d ping ok=%d bad=%d loss=%.2f%% bad_near_scans~%d %s' % (
      mode, conn, d.get('test', '?'), d.get('alive', '?'), d.get('stuck', '?'), d.get('dbfix', '?'), d.get('refresh', '?'),
      d.get('refreshfail', '?'), d.get('idle', '?'), stall, 'empty' if not slog.strip() else 'NOT_EMPTY', h3, h8, ok, bad,
      loss, inscan, prof.strip()))
ss = last('NwoaShapeStats') or ''
print('CHECK SHAPE now: ' + (last('NwoaShape') or ''))
print('CHECK EV124: ' + (last('NwoaEv124') or ''))
print('CHECK TABLE shape       scans alive stuck idle gone err dbfix refresh abort gapstuck fallback ms_mean/max ev124/ev120  -> reading')
for part in ss.split(' | '):
    m = re.match(r'q(\d+) (\S+) n(\d+) a(\d+) s(\d+) i(\d+) g(\d+) e(\d+) db(\d+) rf(\d+) ab(\d+) gs(\d+) fb(\d+) ms(\d+)/(\d+) x(\d+)/(\d+)', part.strip())
    if not m: continue
    k, name = m.group(1), m.group(2)
    n, a, st, i, g, e, db, rf, ab, gs, fb, ms, mx, x1, x2 = (int(v) for v in m.groups()[2:])
    if n == 0 and e == 0 and ab == 0: rd = 'not tested'
    elif e and n == 0: rd = 'escan refused'
    elif ab and n == 0: rd = 'sequence aborted only'
    elif a and not st: rd = 'firmware resumed by itself'
    elif st and not a: rd = 'stuck every time'
    elif a and st: rd = 'mixed'
    else: rd = 'no judgement (idle/gone)'
    if db: rd += ', one more doorbell freed it %d time(s)' % db
    if gs: rd += ', items not taken in a gap %d time(s)' % gs
    if fb: rd += ', fell back to base %d time(s)' % fb
    print('CHECK TABLE q%s %-10s %5d %5d %5d %4d %4d %3d %5d %7d %5d %8d %8d %6d/%-6d %5d/%-5d -> %s' % (
        k, name, n, a, st, i, g, e, db, rf, ab, gs, fb, ms, mx, x1, x2, rd))
print('CHECK RESULT ' + ' '.join(('%s=%s' % (n, 'ok' if v else 'FAIL')) for n, v in crit) +
      (' -> PASS' if all(v for _, v in crit) else ' -> NOT PASS'))
EOF
say "S258Q $MODE DONE link=$L"
