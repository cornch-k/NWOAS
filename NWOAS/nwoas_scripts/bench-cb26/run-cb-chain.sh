#!/bin/bash
# Cinebench 2026 A/B chain on the DAILY stack (S268 + g262U + NVMe QD 32 + Bluetooth), one fresh boot per config.
#   bash bench-cb26/run-cb-chain.sh <current launcher .out> <current LOG.link> [cfg ...]
#   bash bench-cb26/run-cb-chain.sh idle - [cfg ...]                (Mac mini idle at the D4 proxy)
# cfg (default: all, in this order):
#   base         daily, nothing changed
#   wfi          NWOAS_PWR_WFI=1         (C-1 idle guest WFI really sleeps in EL2)
#   sectick      NWOAS_PWR_SECTICK=100   (C-2 secondary HV tick 100 Hz while the guest timer routes are open)
#   wfi_sectick  both
#   madt         S280 payload (S261 BT + MADT EfficiencyClass 1 on CPU4-7)
#   madt_all     S280 payload + WFI + SECTICK=100   (= the 11-19 09:07 combination, now on the daily stack)
# Per boot (bt-s268/run-s268.sh read at run time; only the launcher call, the RES= line and the end are changed):
#   S268 boot + D28/B0/W1 as daily -> 90 s settle -> C3n (Cinebench multi then single in the logged-on user's
#   session, task priority 4, minimum test duration 60 s, as bench-cb26/run-cb-user.sh on 11-19) -> C4 polls until
#   both scores -> W2 Wi-Fi status -> Q2 SSD read-verify of C:\NWOAS\s262\integ.bin -> nvmeqd safety counters
#   -> count of "== Exception taken from" lines in the HV log (guest exceptions the HV reported to the MacBook).
# After the last cfg one more plain daily boot (bt-s268/run-s268.sh, no switches) leaves the Mac mini in the
# daily state.  Result: bench-cb26/runs/chain-<time>/chain.txt, last line "CBCHAIN DONE | ..." plus a table.
# Not run by the agent that wrote it.  HARDWARE-UNVERIFIED.  About 35-40 min per cfg (Cinebench single ~25 min
# on 11-19), all six about 3.5-4 h.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS/nwoas_scripts
OUT0=${1:?usage: run-cb-chain.sh <old .out>|idle <old .link>|- [cfg ...]}; LINK0=${2:?usage}; shift 2
CFGS=("$@"); [ ${#CFGS[@]} -gt 0 ] || CFGS=(base wfi sectick wfi_sectick madt madt_all)
D=$ROOT/bench-cb26/runs/chain-$(date +%Y%m%d-%H%M%S); mkdir -p $D
RES=$D/chain.txt
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
for c in "${CFGS[@]}"; do case $c in base|wfi|sectick|wfi_sectick|madt|madt_all) ;; *) echo "unknown cfg $c"; exit 1;; esac; done
[ "$(shasum -a 256 $ROOT/fw-btmadt-s280/out/s280/m1n1-payload-s280-btmadt.bin | awk '{print $1}')" = \
  8b0730fd6787306f4ada28bf858c5c60c846fe47b4777ccc05d318b5a84da158 ] || { echo "STOP: S280 payload changed"; exit 1; }
python3 -B $ROOT/fw-btmadt-s280/verify_vs_s261.py > $D/s280-verify.txt 2>&1 || { echo "STOP: S280 host check failed"; exit 1; }

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
t = t.replace(lcall, 'bash $ROOT/bench-cb26/launch-s268-cb.sh')
t = t.replace(resl, 'RES=%s/boot-$CB_CFG-$(date "+%%H%%M%%S").txt' % d)
end = r'''
# ---- bench-cb26/run-cb-chain.sh additions
CBJ=$ROOT/bench-cb26/jobs
grep -a -E '^\[S260\]|^\[S262\] NVMe QD engine' $LOG | head -3 | tee -a $RES
say "CB settle 90 s"; sleep 90
EXC0=$(grep -a -c '== Exception taken from' $LOG)
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
  E=(CB_CFG=$c NWOAS_PWR_WFI=0 NWOAS_PWR_SECTICK= CB_MADT=0)
  case $c in
    wfi)         E+=(NWOAS_PWR_WFI=1);;
    sectick)     E+=(NWOAS_PWR_SECTICK=100);;
    wfi_sectick) E+=(NWOAS_PWR_WFI=1 NWOAS_PWR_SECTICK=100);;
    madt)        E+=(CB_MADT=1);;
    madt_all)    E+=(CB_MADT=1 NWOAS_PWR_WFI=1 NWOAS_PWR_SECTICK=100);;
  esac
  T=$D/wrap-$c.txt
  say "cfg $c: ${E[*]} (from $O)"
  (env "${E[@]}" nohup bash $DER $O $K > $T 2>&1 &)
  for i in $(seq 1 900); do grep -a -q -E "S268 DONE|ABORT" $T && break; sleep 5; done
  grep -a "CBCHAIN ROW" $T | tail -1 >> $RES || true
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
