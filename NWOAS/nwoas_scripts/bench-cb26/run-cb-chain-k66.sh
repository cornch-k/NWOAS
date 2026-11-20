#!/bin/bash
# K66 copy of bench-cb26/run-cb-chain.sh (2026-11-20): reproduce the Cinebench window rendering corruption seen
# in run-cb-chain.sh's "base" boots at 20:10 / 20:49 (S261 payload, g262U), with the GPU lane's K66 watcher.
#   bash bench-cb26/run-cb-chain-k66.sh <current launcher .out> <current LOG.link> [cfg ...]
#   bash bench-cb26/run-cb-chain-k66.sh idle - [cfg ...]                (Mac mini idle at the D4 proxy)
# cfg (default: s261 only):
#   s261   daily launcher with ONLY the payload lines swapped back to S261 (launch-s268-k66.sh; HV g262U, QD 32,
#          BT as daily) = the payload of the 20:10 / 20:49 boots
#   base   daily launcher unchanged (today: S280 = S261 + MADT)
# Per boot = run-cb-chain.sh, plus: after "CB settle 90 s" and right before C3n, gpucov/jobs/K66s-cinebench-watch-
# start.cmd (waited for, ~5 s); C3n and the C4 polling unchanged; after the scores gpucov/jobs/K66r-cinebench-watch-
# read.cmd then gpucov/jobs/K67-k66-pictures-export.cmd, their job-N.log paths and the "K66 SUMMARY" line are
# printed ("K66 ..." lines, copied into chain.txt).  Final plain daily boot as before.
# run-cb-chain.sh and the daily launcher are not modified.  Not run by the agent that wrote it.  HARDWARE-UNVERIFIED.
# About 40 min per cfg.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
OUT0=${1:?usage: run-cb-chain.sh <old .out>|idle <old .link>|- [cfg ...]}; LINK0=${2:?usage}; shift 2
CFGS=("$@"); [ ${#CFGS[@]} -gt 0 ] || CFGS=(s261)
D=$ROOT/bench-cb26/runs/chain-k66-$(date +%Y%m%d-%H%M%S); mkdir -p $D
RES=$D/chain.txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
for c in "${CFGS[@]}"; do case $c in s261|base) ;; *) echo "unknown cfg $c"; exit 1;; esac; done
[ "$(shasum -a 256 $ROOT/fw-bt-s261/out/s261/m1n1-payload-s261-bt.bin | awk '{print $1}')" = \
  26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92 ] || { echo "STOP: S261 payload changed"; exit 1; }
for k in K66s-cinebench-watch-start K66r-cinebench-watch-read K67-k66-pictures-export; do
  [ -f $ROOT/gpucov/jobs/$k.cmd ] || { echo "STOP: gpucov/jobs/$k.cmd missing"; exit 1; }; done
shasum -a 256 $ROOT/gpucov/jobs/K66s-cinebench-watch-start.cmd $ROOT/gpucov/jobs/K66r-cinebench-watch-read.cmd \
  $ROOT/gpucov/jobs/K67-k66-pictures-export.cmd > $D/k66-jobs.sha256

# derived wrapper (same for every cfg; the cfg comes in through the environment)
DER=$D/run-s268-cb.sh
python3 -I - $ROOT/bt-s268/run-s268.sh $DER $D <<'PY'
import sys
base, der, d = sys.argv[1:]
t = open(base).read()
lcall = 'bash $ROOT/agxdcp-s268-guest-test.sh'
resl = 'RES=$ROOT/bt-s268/runs/run-s268-$(date "+%Y%m%d-%H%M%S").txt'
last = 'runjob $JW/W1-wifi-pci-status.cmd'
for s in (lcall, resl, last):
    assert t.count(s) == 1, ("bt-s268/run-s268.sh line changed; update run-cb-chain.sh", s)
t = t.replace(lcall, 'bash $ROOT/bench-cb26/launch-s268-k66.sh')
t = t.replace(resl, 'RES=%s/boot-$CB_CFG-$(date "+%%H%%M%%S").txt' % d)
end = r'''
# ---- bench-cb26/run-cb-chain-k66.sh additions (= run-cb-chain.sh + K66 jobs)
CBJ=$ROOT/bench-cb26/jobs
grep -a -E '^\[S260\]|^\[S262\] NVMe QD engine' $LOG | head -3 | tee -a $RES
say "CB settle 90 s"; sleep 90
EXC0=$(grep -a -c '== Exception taken from' $LOG)
runjob $ROOT/gpucov/jobs/K66s-cinebench-watch-start.cmd > /dev/null; K66S_LOG=$JOBLOG
say "K66 START job log=$K66S_LOG"
T0=$(date +%s)
runjob $CBJ/C3n-run-interactive-normal.cmd > /dev/null
C4=""
for i in $(seq 1 40); do
  C4=$(runjob $CBJ/C4-read-results.cmd)
  [ "$(printf '%s\n' "$C4" | grep -a -c '^CB ')" -ge 2 ] && break
  sleep 30
done
CBSEC=$(( $(date +%s) - T0 ))
CBM=$(printf '%s\n' "$C4" | awk '/^=== multi/{s="m"} /^=== single/{s="s"} /^CB /{if(s=="m"){print $2; exit}}')
CBS=$(printf '%s\n' "$C4" | awk '/^=== multi/{s="m"} /^=== single/{s="s"} /^CB /{if(s=="s"){print $2; exit}}')
EXC1=$(grep -a -c '== Exception taken from' $LOG)
runjob $ROOT/gpucov/jobs/K66r-cinebench-watch-read.cmd > $RES.k66r.txt; K66R_LOG=$JOBLOG
runjob $ROOT/gpucov/jobs/K67-k66-pictures-export.cmd > $RES.k67.txt; K67_LOG=$JOBLOG
say "K66 READ job log=$K66R_LOG"
say "K67 EXPORT job log=$K67_LOG"
say "K66 $(grep -a 'K66 SUMMARY' $RES.k66r.txt | tail -1 | sed 's/^K66 //')"
grep -a '^K66 PNG' $RES.k66r.txt | head -20 | while read -r x; do say "$x"; done
W2=$(runjob $ROOT/wifi-s258/jobs/W2-status.cmd)
WST=$(printf '%s\n' "$W2" | grep -a -E '^ *(상태|State) +:' | head -1 | sed 's/.*: *//' | tr -d ' ')
Q2=$(runjob $ROOT/nvmeqd-s262/jobs/Q2-verify.cmd)
Q2R=FAIL; printf '%s\n' "$Q2" | grep -a -q 'verify_rc=0' && Q2R=PASS
SAF=n/a
if [ -s $L/nvmeqd.jsonl ]; then SAF=$(python3 -I - $L/nvmeqd.jsonl <<'PY2'
import json,sys
r=[json.loads(l) for l in open(sys.argv[1]) if l.strip()][-1]['snapshot']
k=('faults','timeouts','dev_errors','drain_fail','eng_bad_tag','eng_inval_fail','storm','eng_faulted')
b={x:r.get(x,0) for x in k if r.get(x,0)}
print('ok' if not b else 'BAD:'+','.join('%s=%s'%kv for kv in b.items()))
PY2
); fi
QD=OFF; grep -a -q '^\[S262\] NVMe QD engine ON' $LOG && QD=ON
SERR=$(cat $LOG $LOG.vuart 2>/dev/null | grep -a -c 'NWOAS-SERR')
say "CBCHAIN ROW cfg=$CB_CFG multi=${CBM:-?} single=${CBS:-?} cb_sec=$CBSEC wifi=${WST:-?} q2=$Q2R qd=$QD nvme_safety=$SAF exc_boot=$EXC0 exc_cb=$((EXC1-EXC0)) serr=$SERR link=$L"
'''
t = t.replace(last, last + end)
open(der, "w").write(t)
PY
bash -n $DER || { echo "STOP: derived wrapper syntax"; exit 1; }
say "START cfgs=${CFGS[*]} derived=$DER base_sha=$(shasum -a 256 $ROOT/bt-s268/run-s268.sh | awk '{print $1}') launcher_sha=$(shasum -a 256 $ROOT/agxdcp-s268-guest-test.sh | awk '{print $1}')"

O=$OUT0; K=$LINK0
for c in "${CFGS[@]}"; do
  E=(CB_CFG=$c NWOAS_PWR_WFI=0 NWOAS_PWR_SECTICK= CB_PAYLOAD=)
  case $c in
    s261)        E+=(CB_PAYLOAD=s261);;
  esac
  T=$D/wrap-$c.txt
  say "cfg $c: ${E[*]} (from $O)"
  (env "${E[@]}" nohup bash $DER $O $K > $T 2>&1 &)
  for i in $(seq 1 900); do grep -a -q -E "S268 DONE|ABORT" $T && break; sleep 5; done
  grep -a -E "CBCHAIN ROW|K6[67] " $T >> $RES || true
  r=$(grep -a "S268 DONE" $T | tail -1)
  if [ -z "$r" ]; then say "cfg $c: no S268 DONE ($(grep -a ABORT $T | tail -1)); chain stops (power-cycle only if the proxy is gone)"; break; fi
  O=$(echo "$r" | sed -n 's/.* out=\([^ ]*\).*/\1/p'); K=$(echo "$r" | sed -n 's/.*link=\([^ ]*\).*/\1/p')
  say "cfg $c done: $(grep -a 'CBCHAIN ROW' $T | tail -1 | cut -d' ' -f3-)"
done

if [ -n "${r:-}" ]; then
  T=$D/wrap-daily.txt
  say "back to the daily boot (no switches)"
  (nohup bash $ROOT/bt-s268/run-s268.sh $O $K > $T 2>&1 &)
  for i in $(seq 1 240); do grep -a -q -E "S268 DONE|ABORT" $T && break; sleep 5; done
  say "daily: $(grep -a -E 'S268 DONE|ABORT' $T | tail -1 | cut -c1-160)"
fi

python3 -I - $RES <<'PY' | tee -a $RES
import re, sys
rows = [dict(re.findall(r'(\w+)=(\S+)', l)) for l in open(sys.argv[1], encoding='utf-8', errors='replace') if 'CBCHAIN ROW' in l]
print('| cfg | multi (pts) | single (pts) | Cinebench time (s) | Wi-Fi state | SSD Q2 | QD engine | NVMe safety | HV-reported guest exceptions during CB | SError lines |')
print('|---|---|---|---|---|---|---|---|---|---|')
for r in rows:
    print('| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |' % tuple(r.get(k, '?') for k in
          ('cfg', 'multi', 'single', 'cb_sec', 'wifi', 'q2', 'qd', 'nvme_safety', 'exc_cb', 'serr')))
print('CBCHAIN DONE | ' + ' | '.join('%s m=%s s=%s' % (r.get('cfg'), r.get('multi'), r.get('single')) for r in rows))
PY
