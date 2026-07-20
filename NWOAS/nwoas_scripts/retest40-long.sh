#!/bin/bash
# retest40 — 22H2 test: does Windows 11 22H2 (22621.525) get PAST the 0xc0000221 winload wall?
#  FAIL like 25H2 => 0xc0000221 / FBDUMP / ResetSystem2 Shutdown  => version not the cause
#  PROGRESS       => no 0xc0000221, winload/kernel handoff signals => 25H2 WAS the cause (past the wall)
# SAFE (SIGTERM only, NO SIGINT). Payload = current H1/H2 diagnostic build (FB dump still captures errors).
LOG=/Volumes/X31/NWOAS/nwoas_scripts/logs/retest40-22h2-long.log
: > "$LOG"
pkill -f run_guest 2>/dev/null; pkill -f "picocom.*debug-console" 2>/dev/null; sleep 1
echo "=== retest40: 22H2 22621.525 winload test ===" >> "$LOG"
sudo /Volumes/X31/NWOAS/macvdmtool/macvdmtool reboot serial >> "$LOG" 2>&1
sleep 10
[ -e /dev/cu.debug-console ] || { echo "=== retest40 done (no serial) ===" >> "$LOG"; exit 1; }
pkill -f "picocom.*debug-console" 2>/dev/null || true
cd /Volumes/X31/NWOAS/m1n1_windows
M1N1DEVICE=/dev/cu.debug-console M1N1_KEEP_BAUD=1 .venv-hv/bin/python3 proxyclient/tools/run_guest.py -r m1n1-payload.bin -m pcie_emul.py >> "$LOG" 2>&1 &
RGPID=$!
CONCLUDED=""; PREV=0; STUCK=0
for i in $(seq 1 600); do
  sleep 5
  if grep -qaE 'FBDUMP END|ResetSystem2: ResetType Shutdown' "$LOG"; then CONCLUDED="shutdown/error@iter$i"; sleep 12; break; fi
  if grep -qa 'END NULL-LOOP ONE-SHOT DUMP' "$LOG"; then CONCLUDED="nullloop-dump@iter$i"; sleep 12; break; fi
  CUR=$(wc -l < "$LOG"); if [ "$CUR" -eq "$PREV" ]; then STUCK=$((STUCK+1)); else STUCK=0; PREV=$CUR; fi
  if [ "$STUCK" -ge 400 ]; then CONCLUDED="STUCK@iter$i(line$CUR)"; break; fi
  kill -0 $RGPID 2>/dev/null || { CONCLUDED="rg-exit@iter$i"; break; }
done
kill $RGPID 2>/dev/null; sleep 2; kill -9 $RGPID 2>/dev/null; pkill -f run_guest.py 2>/dev/null
echo "=== retest40 done ($CONCLUDED, iter $i) ===" >> "$LOG"
echo "---- 22H2 판정 신호 ----" >> "$LOG"
echo "0xc0000221 (winload 체크섬 실패): $(grep -ac '0xc0000221\|c0000221' "$LOG")" >> "$LOG"
echo "FBDUMP(에러화면): $(grep -ac 'FBDUMP START' "$LOG") | Shutdown: $(grep -ac 'ResetSystem2: ResetType Shutdown' "$LOG")" >> "$LOG"
echo "bootmgfw boot.wim 읽기(최대LBA): $(grep -aoE 'LBA \(0x[0-9a-f]+\)' "$LOG" | grep -oE '0x[0-9a-f]+' | python3 -c 'import sys;v=[int(x,16) for x in sys.stdin];print("0x%x (%dMB)"%(max(v),max(v)*512//1048576) if v else "none")' 2>/dev/null)" >> "$LOG"
echo "winload/커널 신호: $(grep -acE 'winload|OSLOADER|kernel|ntoskrnl|SError|vgic-maint' "$LOG")" >> "$LOG"
echo "---- 신규 판정 신호 (2026-07-02 분석 반영) ----" >> "$LOG"
echo "널루프 원샷덤프: $(grep -ac 'NWOAS-NULLLOOP-DUMP' "$LOG") | 클린정지: $(grep -ac 'Hypervisor exited' "$LOG")" >> "$LOG"
echo "winload SMC 0xc3000001: $(grep -ac 'req=0xc3000001' "$LOG") | PSCI CPU_ON(0xc4000003, 스테이지8 신호): $(grep -ac 'req=0xc4000003' "$LOG")" >> "$LOG"
echo "커널 캐노니컬 VA(elr=0xffff, 스테이지8 확정신호): $(grep -acE 'elr=0xffff' "$LOG")" >> "$LOG"
echo "NULL+0xe 루프(구 블로커 재발): $(grep -ac 'Unmapped IPA 0xe' "$LOG")" >> "$LOG"
