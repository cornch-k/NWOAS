#!/bin/bash
# NWOAS S264C hardware step (a) (copy of s264-dryrun-compare.sh for the s264C HV + s264C config):
# NWOAS S264 hardware step (a): MacBook chainloads the S264 HV, asks the C launcher for a DRY-RUN
# (ADT, boot args, stage-2 map, sysregs, ... nothing executed) and compares it with the Python
# launcher path in the same boot.  The guest is NEVER started.  Afterwards reboot the Mac mini.
# Prologue (gates, chainload, vUART capture) = agxdcp-s259-guest-test.sh.  HARDWARE-UNVERIFIED.
# TOUCHES THE MAC MINI: run only when the orchestrator schedules a hardware slot.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
M1N1="$ROOT/m1n1_windows"
HERE="$ROOT/nwoas_scripts/standalone-s264"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
VUART=/dev/cu.usbmodemC07HL05SQ6NY3
PY="$M1N1/.venv-hv/bin/python3"
HV="$ROOT/nwoas_scripts/standalone-s264/out-s264C/m1n1-s264C-agx-hv2-standalone.bin"
HV_SIZE=2424832 HV_SHA=6e6182c0d7a36ef09174d2c6af96142f5d2534476cf2ca292db18f8b3ce0ae0d
PAYLOAD="$ROOT/nwoas_scripts/fw-wifi-s259/out/s259/m1n1-payload-s259-wifi.bin"
PAYLOAD_SHA=fbc4406b0103cf028be5be2eb8ec60844af8952d9c845405338d48cb64e50497
S244="$ROOT/nwoas_scripts/display-s244"
OUT="$ROOT/nwoas_scripts/logs/s264c-compare-$(date '+%Y%m%d-%H%M%S')"
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 M1N1_SPLIT_CONSOLE=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
export NWOAS_LOG="$OUT.log" NWOAS_LINK_DIR="$OUT.link" NWOAS_HV_IMAGE="$HV"
[ -e "$DEV" ] || { echo 'STOP: direct CDC proxy missing'; exit 1; }
[ -e "$VUART" ] || { echo 'STOP: direct CDC diagnostic pipe missing'; exit 1; }
[ "$(stat -f %z "$HV")" -eq "$HV_SIZE" ]
[ "$(shasum -a 256 "$HV" | awk '{print $1}')" = "$HV_SHA" ]
[ "$(shasum -a 256 "$PAYLOAD" | awk '{print $1}')" = "$PAYLOAD_SHA" ]
( cd "$HERE" && shasum -a 256 -c - ) <<'SUMS'
bab895591f45582252bfe2cdef19cd4a79338a985a6c64b419398352534cddcb  s264_common.py
c40f1d0dbcfca1ea2dcbdcdf67caac58271b161cd76ada80fa5859f6c7d25431  s264_pyref.py
1b60292cf5b7e7e6e7ce9d497e2382d8fcb53d736796572e36e354b7f515934d  s264c.py
eb5119e172a7ebe5d26e71c272e7fcd37ce885fec29e19b41cabb1e264b6b4ad  s264c_compare.py
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
set +e
"$PY" -B -u "$HERE/s264c_compare.py" --payload "$PAYLOAD" --out "$OUT" 2>&1 | tee -a "$OUT.log"
RC=${PIPESTATUS[0]}
set -e
echo "[S264C] result table: $OUT/compare.md (exit $RC). The Mac mini must be rebooted now."
exit "$RC"
