#!/bin/bash
# NWOAS v14 = v13 decline(Xhci) + bindstart/Stop 계측 + BmBoot 재연결 게이트(BdsDxe).
# 목적: (a)decline 非발화 원인(wasInited/liveRunning) (b)Stop 호출여부 (c)BM게이트가 재연결루프를
#       끊고 BCD읽기 완료→0xc0000272 넘어 진행하는지. 로드까지 도달 후 4분 정착 관찰.
set -u
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/v14test.log
VDM=/Volumes/X31/NWOAS/macvdmtool/macvdmtool
PAYLOAD=m1n1-payload-exp5c-v14-bmgate.bin
: > "$LOG"
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
      loaded=$i; echo "=== bootmgfw LOADED @iter$i $(date '+%H:%M:%S'); settling 4min to observe ===" | tee -a "$LOG"
    fi
    if grep -qa "winload\|elr=0xfffff8\|Windows is loading" "$LOG"; then
      echo "=== ★PROGRESS PAST WEDGE @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; sleep 10; dud=0; break
    fi
    if [ "$loaded" != 0 ] && [ $((i-loaded)) -ge 48 ]; then
      echo "=== settled 4min post-load @iter$i $(date '+%H:%M:%S') ===" | tee -a "$LOG"; dud=0; break
    fi
    if [ "$i" -ge 108 ] && [ "$(grep -ca 'Enable Slot Successfully' "$LOG")" = "$base" ]; then
      echo "=== DUD, retry @iter$i ===" | tee -a "$LOG"; break
    fi
  done
  kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
  [ "$dud" = 0 ] && { echo "=== observed (attempt $attempt) ===" | tee -a "$LOG"; break; }
done
echo "=== ended $(date '+%H:%M:%S') ===" | tee -a "$LOG"

# ---- 요약 추출 (로드 라인 이후) ----
LOADLINE=$(grep -na "EntryPoint=0x00010023490" "$LOG" | head -1 | cut -d: -f1); LOADLINE=${LOADLINE:-1}
post() { tail -n +"$LOADLINE" "$LOG"; }
echo ""
echo "=== ★v14 결과 요약 (로드 라인 $LOADLINE 以後) ==="
echo "★BM게이트 발화(재연결 skip): $(post | grep -ca 'NWOAS-BM skip redundant')"
echo "★decline 발화: $(post | grep -ca 'NWOAS-XHC decline')"
echo "--- bindstart 계측 (wasInited/liveRunning) ---"
post | grep -a 'NWOAS-XHC bindstart' | tail -6
echo "--- Stop 호출 ---"
echo "NWOAS-XHC Stop 횟수: $(post | grep -ca 'NWOAS-XHC Stop')"
echo "---"
echo "XhcInitSched(재init루프): $(post | grep -ca 'XhcInitSched') [前=68]"
echo "NWOAS-PORT(포트스톰): $(post | grep -ca 'NWOAS-PORT') | Media changed: $(post | grep -ca 'Media changed') [前=22]"
echo "★★UrbFin=1(읽기완료): $(post | grep -ca 'UrbFin=1')"
echo "★★★진행: winload=$(post | grep -ca 'winload') | 커널fffff8=$(post | grep -ca 'elr=0xfffff8') | BDS재시도Boot0=$(post | grep -ca 'Boot0000') [前=69]"
echo "최대 LBA:"; post | grep -oaE 'LBA \(0x[0-9a-fA-F]+\)' | sort -u | tail -3
