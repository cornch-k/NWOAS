#!/bin/sh
# NWOAS tethered-hypervisor runner (run from the MacBook).
#
# Boots the Mac mini into m1n1 *proxy* mode, then launches our m1n1+dtb+UEFI
# payload as an EL1 hypervisor GUEST. In this mode the outer m1n1 runs at EL2
# and emulates the vGIC, so the guest's GIC accesses trap and the
# instrumentation markers ([vgic-init]/handle_vgic_*/NWOAS-SERR) finally fire.
#
# Prereq (one time): the mini must have the BARE m1n1 installed (m1n1-bare.bin)
# via kmutil so it boots into "Running proxy...". See NWOAS_HV_TEST.md.
#
# Do NOT run picocom in this mode — the proxyclient owns /dev/cu.debug-console
# and prints the guest's serial output to this terminal / the log file.

set -e

ROOT=/Volumes/X31/NWOAS
M1N1="$ROOT/m1n1_windows"
VDM="$ROOT/macvdmtool/macvdmtool"
DEV=/dev/cu.debug-console
PY="$M1N1/.venv-hv/bin/python3"
GUEST="$M1N1/m1n1-payload.bin"     # m1n1 + apple-j274.dtb + UEFI (the hv guest)
FRESH="$M1N1/build/m1n1.bin"        # clean m1n1 to chainload as the hypervisor
LOG="$HOME/Desktop/hv-log.txt"

cd "$M1N1"

[ -f "$GUEST" ] || { echo "ERROR: $GUEST 없음"; exit 1; }
[ -x "$PY" ]    || { echo "ERROR: venv 없음 ($PY)"; exit 1; }

echo "=== [1/2] 미니를 m1n1 proxy로 리부트 (+시리얼 채널) ==="
sudo "$VDM" reboot serial
echo "    (Connection: Sink 여야 정상)"

echo "=== m1n1이 'Running proxy...'에 도달하도록 잠시 대기 ==="
sleep 6
[ -e "$DEV" ] || { echo "ERROR: $DEV 없음 (시리얼 케이블/DFU 포트 확인)"; exit 1; }

# proxyclient와 충돌하므로 시리얼 모니터(picocom/screen)를 먼저 정리
pkill -f "picocom.*debug-console" 2>/dev/null || true
pkill -f "screen.*debug-console" 2>/dev/null || true

# NWOAS: chainload는 시리얼 핸드오프에서 desync(UartCMDError)를 일으켜 제거한다(어젯밤
# 검증된 안정 구성 = chainload 없음). 하이퍼바이저는 설치된 m1n1-bare를 쓴다 — dedup을
# 테스트하려면 그 m1n1-bare가 dedup 빌드여야 하므로 kmutil로 build/m1n1.bin(=m1n1-bare.bin,
# DEDUP-v5)을 설치해 둘 것.
#
# 기본값은 M1N1_KEEP_BAUD=1(115200 고정, VDM 시리얼 안정성↑, 속도↓)이다. 더 빠른 baud를
# 쓰려면 먼저 proxyclient/tools/baud_ladder.py로 검증한 뒤 M1N1_BAUD=<검증값> ./run-hv.sh
# 처럼 환경변수로 지정할 것 (예: M1N1_BAUD=375000 ./run-hv.sh). M1N1_BAUD를 지정하지 않고
# 그냥 실행하면(기본 경로) 검증 전 기본값인 115200(M1N1_KEEP_BAUD=1)을 그대로 쓴다.
if [ -n "$M1N1_BAUD" ]; then
    BAUD_ENV="M1N1_BAUD=$M1N1_BAUD"
else
    BAUD_ENV="M1N1_KEEP_BAUD=1"
fi

echo "=== [2/2] UEFI를 hv 게스트로 실행 (vGIC). 로그 -> $LOG ===" | tee "$LOG"
echo "    baud 설정: $BAUD_ENV"
echo "    출력이 멈추면(또는 ^D) 로그 파일을 저에게 주세요."
# NWOAS: bringup_atc1.py is intentionally not used here. It has a stale symbol-VMA
# call target and can jump into the wrong function after m1n1 relinks. USB/xHCI
# handoff is provided by pcie_emul.py (FL1100 BAR emulation + low DMA alias).
M1N1DEVICE="$DEV" env "$BAUD_ENV" "$PY" proxyclient/tools/run_guest.py -r "$GUEST" -m "$M1N1/pcie_emul.py" 2>&1 | tee -a "$LOG"
