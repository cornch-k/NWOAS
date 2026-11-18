#!/bin/bash
# NWOAS S264 hardware step (b): MacBook chainloads the S264 HV, uploads the payload and sends ONE
# request (C LAUNCH).  The C code builds and enters the guest (no sound, NS1 only = no NS2 job
# channel, Wi-Fi control page DISABLED, GPU mediation as S259).  The MacBook only serves and
# COUNTS anything still forwarded to it.  Prologue = agxdcp-s259-guest-test.sh.  HARDWARE-UNVERIFIED.
# TOUCHES THE MAC MINI AND BOOTS WINDOWS: run only after step (a) passed and when scheduled.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
M1N1="$ROOT/m1n1_windows"
HERE="$ROOT/nwoas_scripts/standalone-s264"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
VUART=/dev/cu.usbmodemC07HL05SQ6NY3
PY="$M1N1/.venv-hv/bin/python3"
HV="$ROOT/port-j274/gpu/hv/out-hv2/m1n1-s264A-agx-hv2-standalone.bin"
HV_SIZE=2408448 HV_SHA=178a004f963d424f17a9fbac1662c1ffe1d1dd24045a464ffffccf266de46c11
PAYLOAD="$ROOT/nwoas_scripts/fw-wifi-s259/out/s259/m1n1-payload-s259-wifi.bin"
PAYLOAD_SHA=fbc4406b0103cf028be5be2eb8ec60844af8952d9c845405338d48cb64e50497
S244="$ROOT/nwoas_scripts/display-s244"
OUT="$ROOT/nwoas_scripts/logs/s264-launch-$(date '+%Y%m%d-%H%M%S')"
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 M1N1_SPLIT_CONSOLE=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
[ -e "$DEV" ] || { echo 'STOP: direct CDC proxy missing'; exit 1; }
[ -e "$VUART" ] || { echo 'STOP: direct CDC diagnostic pipe missing'; exit 1; }
[ "$(stat -f %z "$HV")" -eq "$HV_SIZE" ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "$HV_SHA" ]
[ "$(shasum -a 256 "$PAYLOAD" | awk '{print $1}')" = "$PAYLOAD_SHA" ]
( cd "$HERE" && shasum -a 256 -c - ) <<'SUMS'
bab895591f45582252bfe2cdef19cd4a79338a985a6c64b419398352534cddcb  s264_common.py
c40f1d0dbcfca1ea2dcbdcdf67caac58271b161cd76ada80fa5859f6c7d25431  s264_pyref.py
af59fd89e0d5a3ac88dfaaa642062f6dc418664a86b196b8a725fd66e14eb5d0  s264_launch.py
SUMS
[ "$(md5 -q "$ROOT/experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin")" = 7bec0225edc101857be3b7f195dd7357 ]
if /usr/sbin/lsof -t "$DEV" "$VUART" >/dev/null 2>&1; then echo 'STOP: CDC pipe busy'; exit 1; fi
VUART_PID=
cleanup() { [ -z "$VUART_PID" ] || { kill "$VUART_PID" >/dev/null 2>&1 || true; wait "$VUART_PID" >/dev/null 2>&1 || true; }; }
trap cleanup EXIT HUP TERM
trap ':' INT
echo "Log: $OUT.log"
"$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$OUT.log" 2>&1
PROBE_RC=0
"$PY" -B -u "$S244/probe_fw135_s244.py" > "$OUT.probe" 2>&1 || PROBE_RC=$?
cat "$OUT.probe" >> "$OUT.log"
[ "$PROBE_RC" -eq 0 ] || { echo "STOP: S244 probe exit $PROBE_RC; nothing chainloaded"; exit 1; }
"$PY" -u "$M1N1/proxyclient/tools/chainload.py" -r "$HV" >> "$OUT.log" 2>&1
grep -qa 'Proxy is alive again\|WARNING: chainload confirm' "$OUT.log" || exit 2
"$PY" -u "$ROOT/nwoas_scripts/baud-s144/capture_vuart.py" --port "$VUART" --output "$OUT.vuart" --ignore-sigint &
VUART_PID=$!
"$PY" -B -u "$HERE/s264_launch.py" --payload "$PAYLOAD" --out "$OUT" >> "$OUT.log" 2>&1
