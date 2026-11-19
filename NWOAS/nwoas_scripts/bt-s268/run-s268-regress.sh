#!/bin/bash
# S268R: regression checks with Bluetooth ON, on the Windows that ALREADY RUNS under agxdcp-s268-guest-test.sh.
#   bash bt-s268/run-s268-regress.sh <launcher .out> <LOG.link> <LOG> [wifi_window_s, default 780, min 720]
# No reboot, no W4, no B7/B9, no driver install, no registry knob changes, no delivery.  Read-only status jobs plus
# the S262 storage jobs (Q1S writes/reads the 384 MiB C:\NWOAS\s262\integ.bin pattern file; it is left in place).
#  1. Wi-Fi: R1 driver version (must be 1.0.1.9 = s258r) -> W2 + R2 (start) -> W10 -> W19 ping start (s258r script)
#     -> W2 every 240 s over the window -> W19 stop -> W15 -> W10 -> R2 (end).  Counts from the driver registry
#     (NwoaScanFlow conn/stuck/refresh = flow ring recreated, NwoaCntTxStall, NwoaHostDisassocLog 3@/8@) and from
#     WLAN-AutoConfig 8003 since boot (start/end difference).
#  2. SSD: Q1S (NWIOP normal priority: 4 KiB random QD1/8/32 + 1 MiB seq reads, then NWQD integ) -> safety ->
#     Q2 verify (same boot) -> safety -> R3 chkdsk /scan + provider-aware disk events since boot.  No NVME_SHUT restart.
#  3. B2 Bluetooth status.
# One job at a time (waits for "job": N, "kind": 3).  Result: bt-s268/runs/regress-<time>.txt, last line
# "S268R DONE | ..." or "S268R ABORT <reason>".  Not run by the agent that wrote it.  HARDWARE-UNVERIFIED.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
OUT=${1:-}; L=${2:-}; LOG=${3:-}; WIN=${4:-780}
D=$ROOT/bt-s268; JR=$D/jobs; JW=$ROOT/wifi-s258/jobs; JQ=$ROOT/nvmeqd-s262/jobs; JB=$ROOT/bt-s261/jobs
mkdir -p $D/runs
TS=$(date +%Y%m%d-%H%M%S); RES=$D/runs/regress-$TS.txt; LAST=$D/runs/regress-$TS.lastjob.txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
PING=0; STUCK=0
abort() { say "ABORT: $*"
  if [ $PING = 1 ] && [ $STUCK = 0 ]; then PING=0; job $JW/W19-ping-stop.cmd && grep -a '^W19 END' $LAST | tee -a $RES; fi
  rm -f $LAST; echo "S268R ABORT $*" | tee -a $RES; exit 1; }
# job <file> <tag>: queue, wait for kind 3 (max 30 min), decoded log -> $LAST (capped) and appended to $RES (capped)
job() { local f=$1 j i
  [ $STUCK = 0 ] || return 1
  python3 -I $ROOT/transport-s123/queue_job.py $L $f >/dev/null || { say "queue fail $(basename $f)"; return 1; }
  j=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  for i in $(seq 1 600); do grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl && break; sleep 3; done
  grep -q "\"job\": $j, \"kind\": 3" $L/events.jsonl || { STUCK=1; say "job $j $(basename $f) not finished after 30 min"; return 1; }
  iconv -c -f CP949 -t UTF-8 $L/job-$j.log | tr -d '\r' | head -c 60000 > $LAST
  { echo "----- job $j $(basename $f)"; head -c 20000 $LAST; echo; } >> $RES; }
[ -n "$OUT" ] && [ -n "$L" ] && [ -n "$LOG" ] || { echo "usage: $0 <launcher .out> <LOG.link> <LOG> [wifi_window_s]"; exit 1; }
case "$WIN" in ''|*[!0-9]*) echo "window must be seconds"; exit 1 ;; esac
[ "$WIN" -ge 720 ] && [ "$WIN" -le 2200 ] || { echo "window must be 720..2200 s (>= one connected scan; W19 pings for 2400 s)"; exit 1; }
say "START out=$OUT link=$L log=$LOG window=${WIN}s"
[ -f "$OUT" ] && [ -d "$L" ] && [ -f "$LOG" ] && [ -f "$L/events.jsonl" ] || abort "out/link/log path missing"
grep -a -q "LAUNCHER EXIT" "$OUT" && abort "launcher already exited (Windows not running under it)"
grep -q '"kind": 4' $L/events.jsonl || abort "worker never connected on this link"
grep -a -q '^\[S268\]' "$LOG" || abort "LOG is not an S268 launcher log"
if [ -f $L/job.json ]; then j0=$(python3 -I -c "import json,sys;print(json.load(open(sys.argv[1]))['id'])" $L/job.json)
  grep -q "\"job\": $j0, \"kind\": 3" $L/events.jsonl || abort "job $j0 still in flight on this link"; fi
grep -a -E '^\[S268\]|^\[S262\] NVMe QD engine|\[s261\] READY|\[s259\] READY' "$LOG" | head -6 | tee -a $RES
nrecs() { [ -f $L/nvmeqd.jsonl ] && awk 'NF{n++} END{print n+0}' $L/nvmeqd.jsonl || echo 0; }
QREC0=$(nrecs)

# ================= 1. Wi-Fi
say "== 1 Wi-Fi"
job $JR/R1-wifi-drvver.cmd || abort "R1"
grep -a -E '^R1 |NwoaFwVersion|NwoaCfgS258o' $LAST | cut -c1-300 | tee -a $RES
DRV=$(sed -n 's/^R1 DEVPKEY_Device_DriverVersion = \(.*\)$/\1/p' $LAST | tr -d ' ' | tail -1)
[ "$DRV" = 1.0.1.9 ] && say "Wi-Fi driver 1.0.1.9 (s258r) installed" || say "CHECK FAIL: Wi-Fi driver version '$DRV' (expected 1.0.1.9)"
KEYS='NwoaScanFlow |NwoaCntTxStall |NwoaStallLog |NwoaHostDisassocLog |NwoaFwVersion |NwoaCfgS258o '
job $JW/W2-status.cmd || abort "W2 start"; echo "W2SNAP start" >> $RES
grep -a -E "^ *($KEYS)" $LAST | sed 's/^/SNAP0 /' | cut -c1-600 | tee -a $RES
job $JR/R2-wlan-since-boot.cmd || abort "R2 start"; grep -a '^R2 ' $LAST | sed 's/^/R2S /' | tee -a $RES
job $JW/W10-net-check.cmd || abort "W10 start"; grep -a -E '^NIC|^HTTP|패킷|Packets|loss|손실' $LAST | head -8 | tee -a $RES
job $JW/W19-ping-start-s258r.cmd || abort "W19 start"; grep -a '^W19\|S258 W19 FAIL' $LAST | head -4 | tee -a $RES
grep -a -q 'W19 CREATE rc=0' $LAST || abort "W19 ping not started (s258r drop script missing?)"
PING=1; T0=$(date +%s); n=0
while [ $(( $(date +%s) - T0 )) -lt $WIN ]; do
  left=$(( WIN - ($(date +%s) - T0) )); [ $left -gt 240 ] && sleep 240 || sleep $left
  n=$((n+1)); job $JW/W2-status.cmd || abort "W2 #$n"; say "W2 #$n ($(( $(date +%s) - T0 )) s)"
  grep -a -E "^ *(NwoaScanFlow |NwoaCntTxStall |NwoaHostDisassocLog )" $LAST | cut -c1-300 | tee -a $RES
  grep -a -E '^ *(상태|State) +:' $LAST | head -1 | tee -a $RES
done
echo "W2SNAP end" >> $RES; grep -a -E "^ *($KEYS)" $LAST | sed 's/^/SNAP1 /' | cut -c1-600 >> $RES
job $JW/W19-ping-stop.cmd || abort "W19 stop"; PING=0
grep -a -E '^W19 (START|GW|CHG|LINES|CAP)' $LAST | head -150 | sed 's/^/PING /' >> $RES
grep -a -E '^W19 END' $LAST | tail -1 | sed 's/^/PING /' | tee -a $RES
job $JW/W15-wlan-events.cmd || abort "W15"
grep -a -E 'W15 (NOW|NICS|NIC |STAT|PROF|COUNT3H)' $LAST | cut -c1-200 | head -20 | tee -a $RES
job $JW/W10-net-check.cmd || abort "W10 end"; grep -a -E '^NIC|^HTTP|패킷|Packets|loss|손실' $LAST | head -8 | tee -a $RES
job $JR/R2-wlan-since-boot.cmd || abort "R2 end"; grep -a '^R2 ' $LAST | sed 's/^/R2E /' | tee -a $RES

# ================= 2. SSD (Bluetooth on)
say "== 2 SSD"
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
job $JQ/Q1S-io-integ-normal.cmd || abort "Q1S"
grep -a -E 'IOPRIO|H2|IOPS|MB/s|INTEG|integ_rc|io_rc|FAIL' $LAST | head -40 | tee -a $RES
Q1S=FAIL; grep -a -q 'IOPRIO child pid=.* process=2(normal) thread=2(normal)' $LAST && grep -a -q 'INTEG PASS' $LAST &&
  grep -a -q 'io_rc=0 integ_rc=0' $LAST && Q1S=PASS
grep -a -q 'IOPRIO child pid=.* process=2(normal) thread=2(normal)' $LAST || say "Q1S: I/O priority 2 not applied"
say "Q1S $Q1S"; S1=ok; safety || S1=BAD
job $JQ/Q2-verify.cmd || abort "Q2"; grep -a -E 'INTEG|verify_rc|FAIL' $LAST | head -10 | tee -a $RES
Q2=FAIL; grep -a -q 'verify_rc=0' $LAST && Q2=PASS; say "Q2 $Q2"; S2=ok; safety || S2=BAD
job $JR/R3-chkdsk-diskevt-boot.cmd || abort "R3"
grep -a -E 'chkdsk_rc|DISKEVT|BOOT153|R3 SINCE' $LAST | head -40 | tee -a $RES
CHK=$(sed -n 's/^S268R R3 chkdsk_rc=\([0-9]*\).*/\1/p' $LAST | tail -1); DE=$(sed -n 's/^DISKEVT_COUNT \([0-9]*\).*/\1/p' $LAST | tail -1)

# ================= 3. Bluetooth after the load
say "== 3 Bluetooth"
job $JB/B2-status.cmd || abort "B2"
grep -a -E '^DEV |^RADIO|^SVC ' $LAST | cut -c1-220 | head -20 | tee -a $RES
BT=FAIL; grep -a -q '^DEV NWBT\\BCM4378BTH.* status=OK' $LAST && grep -a -q '^DEV ACPI\\NWOA5F69\\0 status=OK' $LAST && BT=OK
grep -a -q 'RADIO kind=Bluetooth state=On' $LAST || BT="$BT(radio-not-On)"

# ================= summary (numbers from the lines above)
python3 -I - "$RES" "$DRV" "$Q1S" "$Q2" "$S1" "$S2" "${CHK:-?}" "${DE:-?}" "$BT" "$WIN" <<'EOF' | tee -a $RES
import re, sys
res, drv, q1s, q2, s1, s2, chk, de, bt, win = sys.argv[1:11]
t = open(res, encoding='utf-8', errors='replace').read()
def snap(tag, key):
    m = re.findall(r'^' + tag + r' +' + key + r'\s+REG_(?:SZ|DWORD)\s*(.*)$', t, re.M)
    return m[-1].strip() if m else ''
def sf(tag):
    return {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', snap(tag, 'NwoaScanFlow'))}
a, b = sf('SNAP0'), sf('SNAP1')
d = {k: b.get(k, 0) - a.get(k, 0) for k in ('conn', 'stuck', 'refresh', 'refreshfail', 'alive', 'dbfix')}
def disc(tag):
    h = snap(tag, 'NwoaHostDisassocLog')
    return len(re.findall(r'(?:^|[^0-9])[38]@', h))
hd = disc('SNAP1') - disc('SNAP0')
stall = snap('SNAP1', 'NwoaCntTxStall') or '?'
stall0 = snap('SNAP0', 'NwoaCntTxStall') or '?'
def r2(tag, i):
    m = re.findall(r'^' + tag + r' R2 COUNT ' + str(i) + r' (\d+)', t, re.M)
    return int(m[-1]) if m else None
w0, w1 = r2('R2S', 8003), r2('R2E', 8003)
wd = (w1 - w0) if w0 is not None and w1 is not None else '?'
end = re.findall(r'^PING W19 END t_ms=(\d+) ok=(\d+) bad=(\d+) maxrtt=(\d+)', t, re.M)
ok, bad, mx = (int(end[-1][1]), int(end[-1][2]), int(end[-1][3])) if end else (0, 0, -1)
loss = 100.0 * bad / (ok + bad) if ok + bad else 100.0
chg = len([1 for s in re.findall(r'^PING W19 CHG t_ms=\d+ (\S+)', t, re.M) if s != 'Success'])
prof = re.findall(r'^R2E R2 PROF (v4=\S+)', t, re.M)
v4 = prof[-1] if prof else 'v4=?'
fw = snap('SNAP1', 'NwoaFwVersion')
fwv = (re.findall(r'version (\S+)', fw) or ['?'])[0]
rows = [('wifi driver', drv, drv == '1.0.1.9'), ('wifi fw', fwv, fwv.startswith('18.20.383')),
        ('disconnects 8003 (window)', wd, wd == 0), ('host disassoc 3@/8@ (window)', hd, hd == 0),
        ('ping sent/lost', '%d/%d (%.2f%%) maxrtt=%dms nonSuccess_runs=%d' % (ok + bad, bad, loss, mx, chg), ok + bad > 0 and loss < 3.0),
        ('conn scans (window)', d['conn'], d['conn'] >= 1), ('stuck/recreate(refresh)/refreshfail', '%d/%d/%d' % (d['stuck'], d['refresh'], d['refreshfail']), d['refreshfail'] == 0),
        ('TX stall cnt start->end', '%s->%s' % (stall0, stall), stall == stall0), ('connectivity end', v4, v4 == 'v4=Internet'),
        ('SSD Q1S (QD1/8/32+seq, integ)', q1s, q1s == 'PASS'), ('SSD Q2 verify', q2, q2 == 'PASS'),
        ('NVMe QD safety', '%s/%s' % (s1, s2), s1 == s2 == 'ok'), ('chkdsk rc', chk, chk == '0'), ('disk events since boot', de, de == '0'),
        ('bluetooth after load', bt, bt == 'OK')]
for n, v, g in rows:
    print('S268R ROW %-36s %-44s %s' % (n, v, 'ok' if g else 'CHECK'))
print('S268R DONE | ' + ' | '.join('%s=%s%s' % (n.split(' (')[0].replace(' ', '_'), str(v).split(' maxrtt')[0].replace(' ', ''), '' if g else '(!)') for n, v, g in rows))
EOF
rm -f $LAST
