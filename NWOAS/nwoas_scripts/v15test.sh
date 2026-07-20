#!/bin/bash
# NWOAS v15 test harness.
#   v15-diag (default): v14 markers + NEW NWOAS-XHC hcreset marker on the
#            DESTRUCTIVE XhcReset (protocol EFI_USB2_HC_PROTOCOL.Reset) path.
#            PURE DIAGNOSTIC (no behavior change) -> tells us the \BCD re-init
#            loop source: DriverBindingStart (bindstart) vs XhcReset (hcreset).
#   v15-fix : v15-diag + XhcReset live-controller decline guard (candidate fix,
#            HARDWARE-UNVERIFIED). Test ONLY after diag points at XhcReset, or as
#            a one-shot to try the fix directly.
#
# Usage:
#   bash nwoas_scripts/v15test.sh            # diag (recommended first)
#   bash nwoas_scripts/v15test.sh fix        # candidate fix
#   SETTLE_ITERS=48 bash nwoas_scripts/v15test.sh fix   # longer settle for fix
#
# Rules honored: no reboot-hammer (bootstrap fail => retry run_guest only, not
# reboot); 115200 (M1N1_KEEP_BAUD=1, transport-capped); build success != boot success.
set -u
MODE="${1:-diag}"
case "$MODE" in
  diag) PAYLOAD=m1n1-payload-exp5c-v15-hcresetdiag.bin; DEF_SETTLE=18 ;;  # ~90s: loop source is captured fast+deterministically
  fix)  PAYLOAD=m1n1-payload-exp5c-v15-hcresetfix.bin;  DEF_SETTLE=48 ;;  # ~4min: give the fix time to progress
  *) echo "usage: $0 [diag|fix]"; exit 2 ;;
esac
SETTLE_ITERS="${SETTLE_ITERS:-$DEF_SETTLE}"
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/v15test-$MODE.log
VDM=/Volumes/X31/NWOAS/macvdmtool/macvdmtool
: > "$LOG"
echo "=== v15 $MODE payload=$PAYLOAD settle=${SETTLE_ITERS}x5s ===" | tee -a "$LOG"

for attempt in 1 2 3 4 5; do
  echo "=== attempt $attempt start $(date '+%H:%M:%S') ===" | tee -a "$LOG"
  pkill -f run_guest 2>/dev/null; sleep 1
  sudo -n "$VDM" reboot serial >> "$LOG" 2>&1; sleep 10
  [ -e /dev/cu.debug-console ] || { echo "no serial" | tee -a "$LOG"; exit 1; }
  cd /Volumes/X31/NWOAS/m1n1_windows
  M1N1DEVICE=/dev/cu.debug-console NWOAS_WIN_BASE=0 NWOAS_WIN_SIZE=0x100000000 M1N1_KEEP_BAUD=1 NWOAS_LOG="$LOG" \
    .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
    -r "$PAYLOAD" -m pcie_emul-exp5.py >> "$LOG" 2>&1 &
  RGPID=$!
  echo "run_guest pid=$RGPID (attempt $attempt)" | tee -a "$LOG"
  base=$(grep -ca "Enable Slot Successfully" "$LOG"); dud=1; loaded=0
  for i in $(seq 1 400); do
    sleep 5
    kill -0 $RGPID 2>/dev/null || { echo "run_guest exited @iter$i" | tee -a "$LOG"; break; }
    if [ "$loaded" = 0 ] && grep -qa "EntryPoint=0x00010023490" "$LOG"; then
      loaded=$i; echo "=== bootmgfw LOADED @iter$i $(date '+%H:%M:%S'); settling ${SETTLE_ITERS}x5s ===" | tee -a "$LOG"
    fi
    # real progress past the wedge = decisive success (both modes)
    if grep -qa "winload\|elr=0xfffff8\|Windows is loading" "$LOG"; then
      echo "=== ★PROGRESS PAST WEDGE @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 10; dud=0; break
    fi
    # diag: once the destructive-reset path has fired enough to reveal the source, stop early (cycle saver)
    if [ "$MODE" = diag ] && [ "$loaded" != 0 ]; then
      hc=$(grep -ca 'NWOAS-XHC hcreset' "$LOG"); bs=$(grep -ca 'NWOAS-XHC bindstart' "$LOG")
      if [ $((hc+bs)) -ge 40 ]; then
        echo "=== diag source captured (hcreset=$hc bindstart=$bs) @iter$i; stopping early ===" | tee -a "$LOG"; dud=0; break
      fi
    fi
    if [ "$loaded" != 0 ] && [ $((i-loaded)) -ge "$SETTLE_ITERS" ]; then
      echo "=== settled @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; dud=0; break
    fi
    if [ "$i" -ge 108 ] && [ "$(grep -ca 'Enable Slot Successfully' "$LOG")" = "$base" ]; then
      echo "=== DUD, retry @iter$i ===" | tee -a "$LOG"; break
    fi
  done
  kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
  [ "$dud" = 0 ] && { echo "=== observed (attempt $attempt) ===" | tee -a "$LOG"; break; }
done
echo "=== ended $(date '+%H:%M:%S') ===" | tee -a "$LOG"

# ---- summary (after the load line) ----
LOADLINE=$(grep -na "EntryPoint=0x00010023490" "$LOG" | head -1 | cut -d: -f1); LOADLINE=${LOADLINE:-1}
post() { tail -n +"$LOADLINE" "$LOG"; }
HC=$(post | grep -ca 'NWOAS-XHC hcreset'); BS=$(post | grep -ca 'NWOAS-XHC bindstart')
IS=$(post | grep -ca 'XhcInitSched'); DEC=$(post | grep -ca 'NWOAS-XHC decline')
echo ""
echo "=== ★v15 $MODE 결과 요약 (로드 라인 $LOADLINE 以後) ==="
echo "XhcInitSched(재init루프): $IS   [前=68]"
echo "NWOAS-XHC hcreset (XhcReset 파괴경로): $HC"
post | grep -a 'NWOAS-XHC hcreset' | tail -4
echo "NWOAS-XHC bindstart (DriverBindingStart 경로): $BS"
post | grep -a 'NWOAS-XHC bindstart' | tail -4
echo "NWOAS-XHC decline (v13 재바인딩 거절): $DEC | NWOAS-XHC 'decline destructive'(v15-fix): $(post | grep -ca 'decline destructive')"
echo "NWOAS-XHC Stop: $(post | grep -ca 'NWOAS-XHC Stop') | NWOAS-BM skip: $(post | grep -ca 'NWOAS-BM skip')"
echo "★★UrbFin=1(읽기완료): $(post | grep -ca 'UrbFin=1')"
echo "★★★진행: winload=$(post | grep -ca 'winload') | 커널fffff8=$(post | grep -ca 'elr=0xfffff8') [화면 0xc0000272 돌파신호]"
echo ""
echo "=== ★★ 재init 출처 판정 (SOURCE VERDICT) ==="
if [ "$HC" -ge 20 ] && [ "$BS" -lt 10 ]; then
  echo "  => XhcReset(프로토콜 HC reset)이 재init 출처. bootmgfw가 외부에서 호출. -> v15-fix(decline) 테스트."
elif [ "$BS" -ge 20 ] && [ "$HC" -lt 10 ]; then
  echo "  => DriverBindingStart가 재init 출처. -> bindstart의 wasInited/liveRunning 값으로 가드 발화조건 교정."
elif [ "$HC" -lt 10 ] && [ "$BS" -lt 10 ] && [ "$IS" -ge 20 ]; then
  echo "  => 제3 경로(XhcInitSched는 도는데 두 마커 다 저조). 재조사 필요(스트리밍 누락 or 다른 호출지점)."
else
  echo "  => 미결정 (hcreset=$HC bindstart=$BS initsched=$IS). 로그 전체 확인 필요(PSCI 미도달로 vuart 덤프 누락 가능)."
fi
