#!/bin/bash
# NWOAS serial-baud probe: can run_guest bootstrap the m1n1 proxy over the USB CDC-ACM uartproxy
# gadget (/dev/cu.usbmodemC07HL05SQ6NY1, USB2 480Mbps) instead of the 115200-capped VDM debug-console?
# If the proxy handshakes over USB ("Fetching ADT" appears), future iterations can be far faster.
# Contained + timeboxed: boots deferred-v1 far enough to confirm the channel, then kills.
set -u
ROOT=/Volumes/X31/NWOAS
VDM="$ROOT/macvdmtool/macvdmtool"
LOG="$ROOT/nwoas_scripts/logs/serial-usb-probe.log"
: > "$LOG"
echo "=== serial-usb-probe start $(date '+%H:%M:%S') ===" | tee -a "$LOG"
pkill -f run_guest 2>/dev/null; sleep 1
set +x
PW="$(cat "$ROOT/.env")"
printf '%s\n' "$PW" | sudo -S -p '' "$VDM" reboot serial >> "$LOG" 2>&1
PW=""
sleep 12
echo "--- serial devices after reboot ---" | tee -a "$LOG"
ls -1 /dev/cu.usbmodem* /dev/cu.debug-console 2>/dev/null | tee -a "$LOG"
USBDEV=$(ls -1 /dev/cu.usbmodem*1 2>/dev/null | head -1)   # NY1 = main proxy console (per analysis)
if [ -z "$USBDEV" ]; then echo "NO usbmodem device -> USB gadget not enumerated; USB-proxy path unavailable" | tee -a "$LOG"; exit 2; fi
echo "trying M1N1DEVICE=$USBDEV run_guest bootstrap (70s window)" | tee -a "$LOG"
cd "$ROOT/m1n1_windows"
env M1N1DEVICE="$USBDEV" M1N1_KEEP_BAUD=1 \
  nohup .venv-hv/bin/python3 -u proxyclient/tools/run_guest.py \
  -r m1n1-payload-deferred-v1.bin -m pcie_emul-exp5.py >> "$LOG" 2>&1 &
RG=$!
OK=0
for i in $(seq 1 14); do
  sleep 5
  if grep -qa "Fetching ADT\|Jumping to payload\|proxy.*version\|Sending payload" "$LOG"; then OK=1; break; fi
  kill -0 $RG 2>/dev/null || break
done
kill $RG 2>/dev/null; pkill -f run_guest 2>/dev/null
if [ $OK -eq 1 ]; then
  echo "=== ★USB-PROXY WORKS over $USBDEV (handshake reached ADT/payload) @ $(date '+%H:%M:%S') ===" | tee -a "$LOG"
else
  echo "=== USB-proxy did NOT handshake over $USBDEV (proxy likely on VDM debug-console only) @ $(date '+%H:%M:%S') ===" | tee -a "$LOG"
fi
echo "--- last 20 log lines ---" | tee -a "$LOG"
tail -20 "$LOG"
