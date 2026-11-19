#!/bin/bash
# s258r = the s258q driver binary (nwoa4378.sys 1.0.1.8, byte for byte) + the macOS 13.5 (22G74) Wi-Fi firmware
# (atlantisb-X3.trx 18.20.383.15.7.8.150, the build Asahi Linux users on the Mac mini M1 run), INF DriverVer 1.0.1.9.
# Question: does the TX flowring stall after a connected scan depend on the firmware build (H2)?
# Every run restarts Windows (as s258q).
#   bash wifi-s258/run-s258r.sh shapes  <old .out> <old .link> [observe_s]  deliver + in-place install (W1, all
#        defaults, FirmwareDir removed), check NwoaFwVersion = 18.20.383, then W29: ShapeCycle=1 + TestConnScanSec=120
#        (shapes 0..5 as in s258q).  Default observe 1800 s.
#   bash wifi-s258/run-s258r.sh base    <old .out> <old .link> [observe_s]  deliver + install as above, then W30:
#        TestConnScanSec=120 with shape 0 (base) only.  Quick comparison with the s258q base row.  Default 900 s.
#   bash wifi-s258/run-s258r.sh reshapes|rebase <old .out> <old .link> [observe_s]  s258r installed already: W29 / W30
#        on the running Windows, then restart Windows and the same observation (no delivery: the drop files exist).
# Each run: W17 ETW start, W19 ping (gateway every second), W2 every 300 s, W18 after the first W2 that shows a host
# disconnect (3@ / 8@) or a TX stall (NwoaCntTxStall > 0), else at the end; W19 stop, W15, W29 off, then the CHECK
# lines.  Never W9 (uninstall).  Roll back to the s258q firmware: W31-rollback-s258q-fw.cmd (see RESULT-s258r-fw135.md).
# Result: wifi-s258/runs/s258r-<mode>-<time>.txt
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts; D=$ROOT/wifi-s258; J=$D/jobs; mkdir -p $D/runs
MODE=${1:-}; OLDOUT=${2:-}; OLDLINK=${3:-}
case "$MODE" in shapes|reshapes) OBS=${4:-1800}; SW_ON=$J/W29-shapes-on.cmd ;; base|rebase) OBS=${4:-900}; SW_ON=$J/W30-base-on.cmd ;;
  *) echo "usage: $0 shapes|base|reshapes|rebase <old .out> <old .link> [observe_s]"; exit 1 ;; esac
case "$MODE" in shapes|base) INSTALL=1 ;; *) INSTALL=0 ;; esac
FW135=18.20.383
[ -n "$OLDOUT" ] && [ -n "$OLDLINK" ] || { echo "need <old .out> <old .link>"; exit 1; }
N=wifi-j274-20261119-1056-s258r
MAN=$ROOT/file-delivery-s243/manifests/$N.json
RES=$D/runs/s258r-$MODE-$(date +%Y%m%d-%H%M%S).txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
job() { local LK=$1 f=$2 j; python3 -I $ROOT/transport-s123/queue_job.py $LK $f >/dev/null || { say "queue fail $f"; return 1; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $LK/job.json)
  for i in $(seq 1 600); do grep -q "\"job\": $j, \"kind\": 3" $LK/events.jsonl && break; sleep 3; done
  iconv -c -f CP949 -t UTF-8 $LK/job-$j.log | tr -d '\r' | head -c 60000 > $D/runs/last-job.txt; cat $D/runs/last-job.txt >> $RES; }
say "MODE=$MODE observe=${OBS}s"
KEYS='NwoaHostDisassocLog|NwoaCntTx |NwoaCntTxStatus|NwoaCntRx |NwoaNowMs|NwoaPool|NwoaRingIdx|NwoaStallLog|NwoaCntTxStall|NwoaCntStall|NwoaConnScan|NwoaScanDiag|NwoaDiscDiag|NwoaCfgS258o|NwoaScanFlow|NwoaFwIdxNow|NwoaTcmIdxPtr|NwoaCntDbell|NwoaCntFlowDbell|NwoaMb|NwoaEvAll|NwoaFwPm|NwoaConsoleLive|NwoaFwCnt|NwoaCfgS258p|NwoaCand|NwoaInitIovSt|NwoaEscanRequest|NwoaCfgS258q|NwoaShape|NwoaEv124|NwoaFwVersion'

# ---- reshapes: switch for the whole next boot, on the Windows that runs now
SW_OFF=""
if [ $INSTALL = 0 ]; then
  job $OLDLINK $J/W2-status.cmd || exit 1
  grep -a 'NwoaFwVersion' $D/runs/last-job.txt | tee -a $RES
  grep -a 'NwoaFwVersion' $D/runs/last-job.txt | grep -q "version $FW135" && grep -a -q 'NwoaCfgS258q' $D/runs/last-job.txt ||
    { say "ABORT: s258r (s258q driver + firmware $FW135) is not running; do a shapes or base run first"; exit 1; }
  job $OLDLINK $SW_ON || exit 1; grep -a -E 'NwoaCfgS258q|NwoaShape |NwoaFwVersion' $D/runs/last-job.txt | tee -a $RES
  SW_OFF=$J/W29-shapes-off.cmd
fi

# ---- restart Windows (scans: with the delivery manifest)
T=$D/runs/boot-$(date +%H%M%S).txt
if [ $INSTALL = 1 ]; then
  (env NWOAS_AUDIO_PROT=moderate NWOAS_DELIVER_MANIFEST=$MAN nohup bash $ROOT/wifi-s259/run-s259.sh $OLDOUT $OLDLINK > $T 2>&1 &)
else
  (env NWOAS_AUDIO_PROT=moderate nohup bash $ROOT/wifi-s259/run-s259.sh $OLDOUT $OLDLINK > $T 2>&1 &)
fi
for i in $(seq 1 240); do grep -a -q -E "S259 DONE|ABORT" $T && break; sleep 5; done
cat $T >> $RES; r=$(grep -a "S259 DONE" $T | tail -1); [ -n "$r" ] || { say "BOOT FAIL"; exit 1; }
L=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p'); echo $L > /tmp/claude-501/curlink.txt; say "booted link=$L"

if [ $INSTALL = 1 ]; then
  job $L $ROOT/file-delivery-s243/jobs/$N.cmd || exit 1
  grep -a -q 'DELIVER COMPLETE' $D/runs/last-job.txt || { say "DELIVER (driver) not complete"; exit 1; }
  job $L $ROOT/file-delivery-s243/jobs/$N-scripts.cmd || exit 1
  grep -a -q 'DELIVER COMPLETE' $D/runs/last-job.txt || { say "DELIVER (scripts) not complete"; exit 1; }
  job $L $J/W1-install-wifi-s258r.cmd || exit 1; say "W1 done"
  grep -a -E 'PNPUTIL EXIT|DriverVersion|DriverInfPath|status=|NwoaFwVersion' $D/runs/last-job.txt | head -8 | tee -a $RES
  grep -a 'DriverVersion' $D/runs/last-job.txt | grep -q '1\.0\.1\.9' || { say "ABORT: 1.0.1.9 (s258r) not installed (W1)"; exit 1; }
  job $L $J/W2-status.cmd || exit 1
  grep -a 'NwoaFwVersion' $D/runs/last-job.txt | tee -a $RES
  grep -a 'NwoaFwVersion' $D/runs/last-job.txt | grep -q "version $FW135" || { say "ABORT: firmware $FW135 not loaded (NwoaFwVersion)"; exit 1; }
  job $L $SW_ON || exit 1; say "switches on ($(basename $SW_ON))"
  grep -a -E 'NwoaCfgS258q|NwoaShape |NwoaFwVersion|RESTART EXIT' $D/runs/last-job.txt | tee -a $RES
  SW_OFF=$J/W29-shapes-off.cmd
fi
job $L $J/W17-etw-wlan-start-s258r.cmd; say "W17 done"
grep -a -E '^W17 (NOW|PROV|NOPROV|WLANSVC|KERNEL|PROVIDERS|LOGMAN|SCEN)' $D/runs/last-job.txt | head -20 | tee -a $RES
job $L $J/W19-ping-start-s258r.cmd; say "W19 start"; grep -a '^W19' $D/runs/last-job.txt | head -4 | tee -a $RES

# ---- observe
ETW=1
for n in $(seq 1 $((OBS/300))); do sleep 300; job $L $J/W2-status.cmd; say "W2 #$n"
  grep -a -E "$KEYS" $D/runs/last-job.txt | cut -c1-1600 | tee -a $RES
  if [ $ETW = 1 ] && { grep -a 'NwoaHostDisassocLog' $D/runs/last-job.txt | grep -q -E '(^|[^0-9])[38]@' ||
       grep -a 'NwoaCntTxStall ' $D/runs/last-job.txt | grep -q -v -E '0x0$'; }; then
    job $L $J/W18-etw-wlan-stop-s258r.cmd; ETW=0; say "W18 done (after the first host disconnect / TX stall)"
    grep -a -E '^W18 (NOW|LINES|PROVLINES|CAP|PRINTED|MASK|ETL|TEXT|NO_|CONVERT|NETSH)' $D/runs/last-job.txt | head -30 | tee -a $RES; fi
done
if [ $ETW = 1 ]; then job $L $J/W18-etw-wlan-stop-s258r.cmd; say "W18 done (end, no host disconnect / stall seen)"
  grep -a -E '^W18 (NOW|LINES|PROVLINES|CAP|PRINTED|MASK|ETL|TEXT|NO_|CONVERT|NETSH)' $D/runs/last-job.txt | head -30 | tee -a $RES; fi
say "W19 stop"; job $L $J/W19-ping-stop.cmd; grep -a '^W19' $D/runs/last-job.txt | head -120 | tee -a $RES
job $L $J/W15-wlan-events.cmd; say "W15 done"
grep -a -E "W15 (COUNT3H|EV|NIC|PROF)|NwoaHostDisassocLog|NwoaCntStats" $D/runs/last-job.txt | cut -c1-200 | head -60 | tee -a $RES
if [ -n "$SW_OFF" ]; then job $L $SW_OFF; say "test switches back to defaults (next device start)"; grep -a 'NwoaCfgS258q' $D/runs/last-job.txt | tee -a $RES; fi

# ---- pass check (numbers only).  Ping times are ms since boot (W19), scan times ms of interrupt time (driver):
# the two clocks differ by about a second, so a ping loss counts as "in a scan" within 3 s of a scan window.
python3 -I - "$RES" "$MODE" "$OBS" <<'EOF' | tee -a $RES
import re, sys
res, mode, obs = sys.argv[1], sys.argv[2], int(sys.argv[3])
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
need = 8 if mode in ('shapes', 'reshapes') else max(3, obs // 120 - 2)
crit = [('conn_scans>=%d' % need, conn >= need), ('fw=18.20.383', '18.20.383' in (last('NwoaFwVersion') or '')), ('no_stall', stall == 0 and not slog.strip()), ('host_disc=0', h3 == 0 and h8 == 0),
        ('ping_loss<1%', loss < 1.0), ('v4=Internet', v4)]
print('CHECK mode=%s conn_scans=%d test_scans=%s sf alive=%s stuck=%s dbfix=%s refresh=%s refreshfail=%s idle=%s '
      'stall=%d stalllog=%s host3=%d host8=%d ping ok=%d bad=%d loss=%.2f%% bad_near_scans~%d %s' % (
      mode, conn, d.get('test', '?'), d.get('alive', '?'), d.get('stuck', '?'), d.get('dbfix', '?'), d.get('refresh', '?'),
      d.get('refreshfail', '?'), d.get('idle', '?'), stall, 'empty' if not slog.strip() else 'NOT_EMPTY', h3, h8, ok, bad,
      loss, inscan, prof.strip()))
ss = last('NwoaShapeStats') or ''
print('CHECK FW: ' + (last('NwoaFwVersion') or 'none'))
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
say "S258R $MODE DONE link=$L"
