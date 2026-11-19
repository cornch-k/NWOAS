#!/bin/bash
# NWOAS 4-1: the MacBook's verdict on the first boot after go1.sh registered stage1.bin (PLAN-4-1 8장).
# Start it the moment the user presses Enter on "GO1-DONE ... Press enter to reboot" (or on step2.sh's
# "Press enter to reboot" after a rollback): the 180 s clock starts when this script starts (in a terminal it
# waits for an Enter here first).  The user stays seated in front of the Mac mini until the last line.
# The Mac mini screen is NOT a signal (black is normal for success AND failure: D4 defers the display, stage 1
# never touches it).
#   bash standalone-s264/stage1/watch-after-go1.sh           PREPARED, NOT RUN BY THE AGENT THAT WROTE IT
#   bash standalone-s264/stage1/watch-after-go1.sh --rollback   after a rollback (step2.sh or go1.sh --d4):
#        only steps 1 and 3; last line "GO1-WATCH D4-BACK" (= the day's normal MacBook way works again)
# Last line: "GO1-WATCH KEEP | ..."  = keep the registration
#            "GO1-WATCH ROLLBACK <reason>" = a stage-1 signal failed (no proxy / fallback proxy / not D4 / G-2):
#                 the user rolls back NOW, same sitting (PLAN-4-1 8장 되돌리기)
#            "GO1-WATCH HOLD <reason>" = stage 1 handed over to D4 and G-2 passed, but the S268/SSD run did not
#                 pass: no 1TR action; keep the registration, rerun the failing check / the daily boot first
# Steps: 1 proxy devices within 180 s -> 2 not stage 1's fallback proxy -> 3 D4 identity + layout gate (G-2)
#        -> 4 run-s268-after-stage1.sh on that same boot (S268 DONE, QD engine ON, Q1S/Q2/chkdsk) VERDICT=PASS.
set -u
ROOT=/Users/angigyeom/Desktop/DEV/NWOAS
NS=$ROOT/nwoas_scripts
M1N1="$ROOT/m1n1_windows"
HERE="$NS/standalone-s264/stage1"
DEV=/dev/cu.usbmodemC07HL05SQ6NY1
VUART=/dev/cu.usbmodemC07HL05SQ6NY3
PY="$M1N1/.venv-hv/bin/python3"
mkdir -p $NS/standalone-s264/runs
RES=$NS/standalone-s264/runs/go1-watch-$(date +%Y%m%d-%H%M%S).txt
export M1N1DEVICE="$DEV" M1N1_KEEP_BAUD=1 M1N1_SPLIT_CONSOLE=1 PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$M1N1/proxyclient:$NS"
say() { echo "$(date '+%H:%M:%S') $*" | tee -a $RES; }
MODE=${1:-}
rollback() { [ "$MODE" = --rollback ] && { say "GO1-WATCH ROLLBACK-DID-NOT-BRING-D4-BACK $*"; exit 1; }
  say "GO1-WATCH ROLLBACK $*"; exit 1; }
( cd "$HERE" && shasum -a 256 -c --quiet SHA256SUMS ) || { say "STOP: a stage-1 file is not the recorded one"; exit 2; }
[ -t 0 ] && { echo "Press enter here when the user presses enter on the Mac mini"; read _; }
T0=$(date +%s)
say "clock started (user pressed enter on the Mac mini)"
for i in $(seq 1 180); do [ -e $DEV ] && [ -e $VUART ] && break; sleep 1; done
[ -e $DEV ] && [ -e $VUART ] || rollback "no m1n1 proxy within 180 s of the reboot (stage 1 hung, iBoot refused it, or a reboot loop)"
say "proxy devices up $(( $(date +%s) - T0 )) s after the reboot"
sleep 4
"$PY" -u $NS/recovery-s126/proxy_ready.py >> $RES 2>&1 || true
[ "$MODE" = --rollback ] || {
for t in 1 2 3; do
  RC=0; "$PY" -B -u "$HERE/probe_running_image.py" "$HERE/esp-4-1/stage1.bin" "NWOAS-STAGE1: USB proxy fallback" >> $RES 2>&1 || RC=$?
  [ $RC = 2 ] || break; sleep 5; "$PY" -u $NS/recovery-s126/proxy_ready.py >> $RES 2>&1 || true
done
[ $RC = 0 ] && rollback "stage 1 runs its fallback proxy: it could not read ESP m1n1/boot.bin or faulted (the MacBook could chainload D4 for today, but every boot would stop here)"
[ $RC = 2 ] && rollback "the proxy does not answer (3 probe errors)"
}
for t in 1 2 3; do
  RC=0; "$PY" -B -u $NS/display-s244/probe_fw135_s244.py > $RES.probe 2>&1 || RC=$?
  cat $RES.probe >> $RES
  [ $RC = 2 ] || break; sleep 5; "$PY" -u $NS/recovery-s126/proxy_ready.py >> $RES 2>&1 || true
done
rm -f $RES.probe
case $RC in
    0) say "FIRST BOOT: D4 identity and layout gate G-2 PASS through stage 1";;
    4) rollback "D4 runs but the layout gate G-2 differs after the stage-1 hop (launchers would refuse)";;
    3) rollback "the m1n1 answering is neither stage 1's fallback nor D4";;
    *) rollback "D4 probe exit $RC";;
esac
[ "$MODE" = --rollback ] && { say "GO1-WATCH D4-BACK | D4 identity and G-2 PASS after the rollback"; exit 0; }
T=$NS/standalone-s264/runs/go1-s268-$(date +%H%M%S).txt
(nohup bash $HERE/run-s268-after-stage1.sh > $T 2>&1 &)
for i in $(seq 1 600); do grep -a -q -E "ST1-S268 DONE|ST1-S268 ABORT" $T && break; sleep 5; done
cat $T >> $RES
L=$(grep -a -E "ST1-S268 DONE|ST1-S268 ABORT" $T | tail -1)
say "$L"
case "$L" in
    *VERDICT=PASS*) say "GO1-WATCH KEEP | first boot through the registered stage 1: D4 + G-2 PASS, S268 DONE, QD ON, SSD checks PASS";;
    *) say "GO1-WATCH HOLD | stage 1 -> D4 -> G-2 passed; S268/SSD on this boot: ${L:-no result in 50 min} (no 1TR action)"; exit 3;;
esac
