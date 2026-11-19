#!/bin/bash
# NWOAS S264G hardware step (copy of s264g-launch-watch.sh): s264G HV = s264F + 4-2 auto-start code. Chainloaded by the
# MacBook (no "nwoas_autostart=" variable) it behaves exactly as s264F; the same s264f_*.py files are used.
# NWOAS S264F hardware step (copy of s264g-launch-watch.sh): s264F HV = s264E + power_module.py in C.
# s264F switch: S264F_NO_PWR=1 (power step off).
# NWOAS S264E hardware step (copy of s264g-launch-watch.sh): s264E HV (S261 module set in C, bounded RTKit/SMC/AFK waits,
# sound protection moderate), S261 Bluetooth payload.  s264E switches: S264E_NO_BT=1 (NWOAS_BT=0),
# S264E_S259=1 (wifi_module.py set + S259 payload), S264E_NO_FENCE_WR=1 (NWOAS_BT_FENCE_WIFI_RESET=0).
# NWOAS S264D hardware step (b) (copy of s264c-launch-watch.sh): s264D HV = s264C + sound B/S + NWOAS_WIFI=1 in C.
# Environment (s264D): S264D_NO_WIFI=1 (control page DISABLED, no SMC), S264D_NO_AUDIO_B=1 (sound B off),
#   S264D_AUDIO_PROT=sense|moderate|full (default moderate); S264C_* switches as before.
# NWOAS S264C hardware step (b) (copy of s264-launch-watch.sh): s264C HV, exception policy LOCAL, S262 QD=32;
# Environment: S264C_NO_QD=1 (S262 QD off), S264C_NO_LOCAL=1 (policy off = s264B), S264C_FATAL=1 (fatal -> log + reboot).
# NWOAS S264 hardware step (b): MacBook chainloads the S264 HV, uploads the payload and sends ONE
# request (C LAUNCH).  The C code builds and enters the guest (s264D: sound stage B/S and NWOAS_WIFI=1
# in C unless switched off above; NS1 only = no NS2 job channel, GPU mediation as S259, S259 payload).  The MacBook only serves and
# COUNTS anything still forwarded to it.  Prologue = agxdcp-s259-guest-test.sh.  HARDWARE-UNVERIFIED.
# TOUCHES THE MAC MINI AND BOOTS WINDOWS: run only after step (a) passed and when scheduled.
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
M1N1="$ROOT/m1n1_windows"
HERE="$ROOT/nwoas_scripts/standalone-s264"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
VUART=/dev/cu.usbmodemC07HL05SQ6NY3
PY="$M1N1/.venv-hv/bin/python3"
HV="$ROOT/nwoas_scripts/standalone-s264/out-s264G/m1n1-s264G-agx-hv2-standalone.bin"
HV_SIZE=2605056 HV_SHA=d1fea3d73ad7d0d7da562802dbdec88527490dfe5f395d28dbd464a324c43ce6
PAYLOAD="$ROOT/nwoas_scripts/fw-bt-s261/out/s261/m1n1-payload-s261-bt.bin"
PAYLOAD_SHA=26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92
if [ -n "${S264E_S259:-}" ]; then   # wifi_module.py set: its SSDT reads only the Wi-Fi page
    PAYLOAD="$ROOT/nwoas_scripts/fw-wifi-s259/out/s259/m1n1-payload-s259-wifi.bin"
    PAYLOAD_SHA=fbc4406b0103cf028be5be2eb8ec60844af8952d9c845405338d48cb64e50497
fi
S244="$ROOT/nwoas_scripts/display-s244"
OUT="$ROOT/nwoas_scripts/logs/s264g-launch-$(date '+%Y%m%d-%H%M%S')"
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
1b60292cf5b7e7e6e7ce9d497e2382d8fcb53d736796572e36e354b7f515934d  s264c.py
d3fa010e7b3b1cdd3f5f1b2c2aeca8647d86033372ea383130fac79615864f7a  s264d.py
9d7cd0de5d88e7a3dfb11b73c0d5eeb1f48863f73e2ad0b4d77a59e6bc4108cf  s264e.py
b8537756e06a6d0c5961091df0ccc824fa50944cbd9583c39aa2fc18da990a7f  s264f.py
88a76e09e5d7cde0c924678168e76d14b1793a156b810c8dc8173e1aa31de144  s264f_launch.py
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
"$PY" -B -u "$HERE/s264f_launch.py" --payload "$PAYLOAD" --out "$OUT" ${S264C_FATAL:+--fatal} ${S264C_NO_QD:+--no-qd} ${S264C_NO_LOCAL:+--no-local} ${S264D_NO_WIFI:+--no-wifi} ${S264D_NO_AUDIO_B:+--no-audio-b} --audio-prot "${S264D_AUDIO_PROT:-moderate}" ${S264E_NO_BT:+--no-bt} ${S264E_S259:+--s259} ${S264E_NO_FENCE_WR:+--no-fence-on-wifi-reset} ${S264F_NO_PWR:+--no-pwr} >> "$OUT.log" 2>&1
