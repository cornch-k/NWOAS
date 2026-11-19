#!/bin/bash
# NWOAS 4-2 end-to-end rehearsal from D4 (PLAN-4-1-disk-boot.md 7장 단계 4) - PREPARED, NOT RUN BY THE AGENT.
# The MacBook chainloads stage1-42.bin (same first-stage code as the registered stage1.bin, only the file name
# differs: chainload=<ESP>;m1n1/nwoas-s264g.bin) from the running D4 proxy with chainload_nonop.py, then sends
# NOTHING more and only listens on the diagnostic pipe NY3 (capture_vuart.py reads, never writes):
#   D4 -> stage 1 (NVMe up, ESP m1n1/nwoas-s264g.bin = s264G + nwoas_autostart=3, NVMe down) -> s264G
#   -> no proxy bytes for 3 s -> auto-start from the ESP (nowin / cfg / nwoas-payload.bin) -> C launcher -> Windows.
# PASS (rc 0) needs "S264 entering guest" on NY1 or NY3 within 240 s and no "Proxy: host seen" (the first two
# lines below are reported when captured):
#   "S264G no MacBook proxy client within 3 s", "S264G auto-start: ... -> ", "S264 entering guest";
#   then the user confirms the Windows desktop on the Mac mini screen (the script cannot see it).
# Afterwards Windows runs with no MacBook link at all (no job channel): to come back, restart Windows from its
# Start menu (or hold the power button); the stub still boots D4 (nothing registered or written here).
# Needs: the Mac mini in the D4 proxy (W4 first), esp-4-1 jobs done (m1n1/nwoas-s264g.bin + nwoas-payload.bin
# on the ESP, m1n1/nowin absent).  rc: 0 PASS | 1 STOP before anything | 2 not all lines seen | 3 host seen
set -eu
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
M1N1="$ROOT/m1n1_windows"
HERE="$ROOT/nwoas_scripts/standalone-s264/stage1"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
VUART=/dev/cu.usbmodemC07HL05SQ6NY3
PY="$M1N1/.venv-hv/bin/python3"
S244="$ROOT/nwoas_scripts/display-s244"
ST1="$HERE/rehearsal/stage1-42.bin"
OUT="$ROOT/nwoas_scripts/logs/stage1-42-rehearsal-$(date '+%Y%m%d-%H%M%S')"
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 M1N1_SPLIT_CONSOLE=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$ROOT/nwoas_scripts"
[ -e "$DEV" ] || { echo 'STOP: direct CDC proxy missing'; exit 1; }
( cd "$HERE" && shasum -a 256 -c --quiet SHA256SUMS ) || { echo 'STOP: a stage-1 file is not the recorded one'; exit 1; }
[ "$(md5 -q "$ROOT/experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin")" = 7bec0225edc101857be3b7f195dd7357 ]
if /usr/sbin/lsof -t "$DEV" "$VUART" >/dev/null 2>&1; then echo 'STOP: CDC pipe busy'; exit 1; fi
VUART_PID=
NY1_PID=
cleanup() { for p in $VUART_PID $NY1_PID; do kill "$p" >/dev/null 2>&1 || true; wait "$p" >/dev/null 2>&1 || true; done; }
trap cleanup EXIT HUP TERM
echo "Log: $OUT.log"
"$PY" -u "$ROOT/nwoas_scripts/recovery-s126/proxy_ready.py" >> "$OUT.log" 2>&1
RC=0
"$PY" -B -u "$S244/probe_fw135_s244.py" > "$OUT.probe" 2>&1 || RC=$?
cat "$OUT.probe" >> "$OUT.log"
[ "$RC" -eq 0 ] || { echo "STOP: the running m1n1 is not the D4 proxy (probe exit $RC); nothing chainloaded"; exit 1; }
: > "$OUT.vuart"
# listen first (read-only, survives the re-enumeration), so nothing s264G prints in its first seconds is missed
"$PY" -u "$ROOT/nwoas_scripts/baud-s144/capture_vuart.py" --port "$VUART" --output "$OUT.vuart" &
VUART_PID=$!
sleep 1
echo "[4-2 rehearsal] D4 proxy confirmed; chainloading stage1-42.bin, then the MacBook only listens on NY3" | tee -a "$OUT.log"
"$PY" -u "$HERE/chainload_nonop.py" -r "$ST1" >> "$OUT.log" 2>&1 || true
grep -qa 'Reloading into stub\|Jumping to stub' "$OUT.log" || { echo "[4-2 rehearsal] STOP: the jump was not sent" | tee -a "$OUT.log"; exit 1; }
# s264G prints its first lines (auto-start decision) on NY1: the second pipe NY3 is console only after the C plan's
# SPLIT step (nwoas_s264_plan.c:1626-1629).  Listen on NY1 too, read-only (never writes = no "host seen").
: > "$OUT.ny1"
"$PY" -u "$ROOT/nwoas_scripts/baud-s144/capture_vuart.py" --port "$DEV" --output "$OUT.ny1" &
NY1_PID=$!
A='S264G no MacBook proxy client within 3 s'; B='S264G auto-start: '; C='S264 entering guest'; H='Proxy: host seen'
both() { cat "$OUT.ny1" "$OUT.vuart" 2>/dev/null; }
for i in $(seq 1 240); do
    both | grep -aq "$H" && break
    both | grep -aq "$C" && break
    both | grep -aq 'staying in the proxy' && break
    sleep 1
done
sleep 5
both | grep -a -E 'S264G|S264 entering|S264 C launcher|Proxy: host seen|staying in the proxy|NWOAS-STAGE1' | head -40 | tee -a "$OUT.log"
if both | grep -aq "$H"; then
    echo "[4-2 rehearsal] FAIL: s264G saw host bytes (auto-start off) - something on the MacBook wrote to the proxy port" | tee -a "$OUT.log"; exit 3
fi
if both | grep -aq "$C"; then
    both | grep -aq "$A" && both | grep -aq "$B" || echo "  (the first two lines were not captured; the guest entry line was)" | tee -a "$OUT.log"
    echo "[4-2 rehearsal] PASS (pipe): stage 1 -> ESP s264G -> auto-start -> C launcher entered the guest with no MacBook bytes." | tee -a "$OUT.log"
    echo "  Now the user looks at the Mac mini screen: Windows login/desktop = 4-2 rehearsal PASS. (Not proof of the registered boot.)" | tee -a "$OUT.log"
    exit 0
fi
echo "[4-2 rehearsal] NO PIPE EVIDENCE: \"S264 entering guest\" not seen on NY1/NY3 within 240 s ($OUT.ny1, $OUT.vuart)." | tee -a "$OUT.log"
echo "  LOOK AT THE SCREEN FIRST: Windows up = only the pipe evidence is missing -> restart from the Start menu." | tee -a "$OUT.log"
echo "  Hold the power button only if the screen stays dead (nothing was written)." | tee -a "$OUT.log"
exit 2
