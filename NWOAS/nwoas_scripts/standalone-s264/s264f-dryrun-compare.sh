#!/bin/bash
# NWOAS S264F hardware step (copy of s264e-dryrun-compare.sh): s264F HV = s264E + power_module.py in C.
# NWOAS S264E hardware step (copy of s264d-dryrun-compare.sh): s264E HV (S261 module set in C, bounded RTKit/SMC/AFK waits,
# sound protection moderate), S261 Bluetooth payload.
# (the dry-run compare is always the S261 set: Python bt_module.py vs the s264E default plan)
# NWOAS S264D hardware step (a) (copy of s264c-dryrun-compare.sh for the s264D HV + s264D config).
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
HV="$ROOT/nwoas_scripts/standalone-s264/out-s264F/m1n1-s264F-agx-hv2-standalone.bin"
HV_SIZE=2572288 HV_SHA=9db7de2ae0ef1a8afb781bc3246d8a33795465524015a166b9998717868fc8ea
PAYLOAD="$ROOT/nwoas_scripts/fw-bt-s261/out/s261/m1n1-payload-s261-bt.bin"
PAYLOAD_SHA=26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92
S244="$ROOT/nwoas_scripts/display-s244"
OUT="$ROOT/nwoas_scripts/logs/s264f-compare-$(date '+%Y%m%d-%H%M%S')"
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
d3fa010e7b3b1cdd3f5f1b2c2aeca8647d86033372ea383130fac79615864f7a  s264d.py
9d7cd0de5d88e7a3dfb11b73c0d5eeb1f48863f73e2ad0b4d77a59e6bc4108cf  s264e.py
b8537756e06a6d0c5961091df0ccc824fa50944cbd9583c39aa2fc18da990a7f  s264f.py
7b6d1948334d23bd16d758d2b68f18b407afd2a9c2d3f55bf2ceb2d8bdef0af2  s264f_compare.py
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
"$PY" -B -u "$HERE/s264f_compare.py" --payload "$PAYLOAD" --out "$OUT" 2>&1 | tee -a "$OUT.log"
RC=${PIPESTATUS[0]}
set -e
echo "[S264F] result table: $OUT/compare.md (exit $RC). The Mac mini must be rebooted now."
exit "$RC"
