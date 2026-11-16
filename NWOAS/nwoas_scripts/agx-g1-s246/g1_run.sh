#!/bin/bash
# G1: run cycles FIRST..LAST of the AGX firmware gate, rebooting the Mac mini (m1n1 proxy reboot,
# PMU panic counter cleared first) before every cycle except when --no-first-reboot is not given...
# Usage: g1_run.sh EVIDENCE_DIR FIRST LAST
# Stops at the first non-PASS cycle, missing proxy, or a boot whose m1n1 base + random seed did not change.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
D=$ROOT/nwoas_scripts/agx-g1-s246
C=$D/evidence/g0-contract-20261116
E=${1:?evidence dir}; FIRST=${2:?first}; LAST=${3:?last}
PY=$ROOT/m1n1_windows/.venv-hv/bin/python3
export M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=$ROOT/m1n1_windows/proxyclient
mkdir -p "$E"
ident() { "$PY" -c "import json;d=json.load(open('$1'));i=d['identity'];print(i['m1n1_base'],i['random_seed_sha256'])"; }
for n in $(seq "$FIRST" "$LAST"); do
  N=$(printf %02d "$n"); P=$(printf %02d $((n-1)))
  [ -e "$E/cycle$N.json" ] && { echo "STOP: $E/cycle$N.json exists"; exit 2; }
  if [ -e "$E/cycle$P.json" ]; then
    echo "[run] reboot before cycle $N"
    timeout 60 "$PY" -u "$ROOT/m1n1_windows/proxyclient/tools/reboot.py" > "$E/reboot$N.log" 2>&1
    gone=0; for i in $(seq 1 60); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] || { gone=1; break; }; sleep 1; done
    [ $gone = 1 ] || { echo "STOP: proxy port never disappeared after reboot"; exit 3; }
    up=0; for i in $(seq 1 120); do [ -e /dev/cu.usbmodemC07HL05SQ6NY1 ] && [ -e /dev/cu.usbmodemC07HL05SQ6NY3 ] && { up=1; break; }; sleep 2; done
    [ $up = 1 ] || { echo "STOP: proxy did not come back within 240 s"; exit 3; }
    sleep 3
  fi
  timeout 60 "$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" 2>&1 | grep -v RuntimeWarning | tail -1 | grep -q PASS || { echo "STOP: proxy not ready"; exit 3; }
  timeout 300 "$PY" -u "$D/g1_cycle.py" --contract "$C/j274-agx.json" --review "$C/j274-agx-review.json" --out "$E/cycle$N.json" > "$E/cycle$N.log" 2>&1
  rc=$?
  R=$("$PY" -c "import json;d=json.load(open('$E/cycle$N.json'));print(d['result'],d.get('pong_latency_s'))" 2>/dev/null)
  echo "[run] cycle $N rc=$rc $R"
  [ $rc = 0 ] || { echo "STOP: cycle $N failed"; exit 1; }
  if [ -e "$E/cycle$P.json" ]; then
    a=$(ident "$E/cycle$P.json"); b=$(ident "$E/cycle$N.json")
    [ "${a%% *}" != "${b%% *}" ] && [ "${a##* }" != "${b##* }" ] || { echo "STOP: no fresh-boot proof ($a / $b)"; exit 4; }
  fi
done
echo "[run] cycles $FIRST..$LAST PASS"
