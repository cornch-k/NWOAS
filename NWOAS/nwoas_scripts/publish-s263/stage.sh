#!/bin/bash
# copy source/scripts/docs (no binaries, run logs, Apple firmware) from NWOAS to NWOAS-publish/NWOAS
set -eu
S=/Users/angigyeom/Desktop/DEV/NWOAS; D=/Users/angigyeom/Desktop/DEV/NWOAS-publish/NWOAS
EX=(--exclude='out/' --exclude='out-*/' --exclude='runs/' --exclude='runs-*' --exclude='logs/' --exclude='bin/' --exclude='__pycache__/'
 --exclude='.DS_Store' --exclude='*.exe' --exclude='*.EXE' --exclude='*.sys' --exclude='*.dll' --exclude='*.cat' --exclude='*.bin'
 --exclude='*.zip' --exclude='*.dmp' --exclude='*.pdb' --exclude='*.lib' --exclude='*.obj' --exclude='*.o' --exclude='*.trx'
 --exclude='*.clmb' --exclude='*.txcb' --exclude='P-*.txt' --exclude='*.cer' --exclude='*.wim' --exclude='*.iso' --exclude='*.log'
 --exclude='*.jsonl' --exclude='.venv*/' --exclude='build/' --exclude='x64/' --exclude='ARM64/' --exclude='*.tlog' --exclude='*.ilk'
 --exclude='*.pch' --exclude='*.idb' --exclude='*.ipch' --exclude='Debug/' --exclude='Release/')
r() { mkdir -p "$D/$1"; rsync -a "${EX[@]}" "$S/$1/" "$D/$1/"; }
for d in nwoas_scripts/gpupool-s269d nwoas_scripts/fw-btmadt-s280 nwoas_scripts/hv-latch-g262U nwoas_scripts/gpupool-s280 nwoas_scripts/gpupool-s279 nwoas_scripts/gpupool-s278 nwoas_scripts/gpupool-s277 nwoas_scripts/gpupool-s276 nwoas_scripts/gpupool-s275 nwoas_scripts/gpupool-s273 nwoas_scripts/gpupool-s265 nwoas_scripts/gpupool-s266 nwoas_scripts/gpupool-s267 nwoas_scripts/gpupool-s269 nwoas_scripts/gpupool-s272 nwoas_scripts/wifi-s258 nwoas_scripts/standalone-s264 nwoas_scripts/diag-s264 nwoas_scripts/gpucov nwoas_scripts/bench-cb26 nwoas_scripts/bt-s261 nwoas_scripts/bt-s268 nwoas_scripts/power-s260 nwoas_scripts/nvmeqd-s262 \
  nwoas_scripts/net-diag nwoas_scripts/agx-s248 nwoas_scripts/file-delivery-s243/jobs nwoas_scripts/file-delivery-s243/manifests \
  nwoas_scripts/wifi-s258/jobs nwoas_scripts/wifi-s259 nwoas_scripts/sound-s256 nwoas_scripts/fw-power-s260 nwoas_scripts/fw-bt-s261 nwoas_scripts/fw-smbios-s262 nwoas_scripts/smbios-s262 nwoas_scripts/publish-s263 \
  port-j274/gpu/hv port-j274/wifi/bcm4378-wificx port-j274/bt/bcm4378-bthx port-j274/sound/nwoas-audio/Source research-20261118; do r $d; done
# GPU driver fork: our patch series and notes only (upstream source is public windows-on-m1)
mkdir -p $D/port-j274/gpu/driver-p15
rsync -a "${EX[@]}" --exclude='drivers/' --exclude='upstream-acpi-reference/' "$S/port-j274/gpu/driver-p15/" "$D/port-j274/gpu/driver-p15/"
for f in nwoas_scripts/agxdcp-s265-guest-test.sh nwoas_scripts/agxdcp-s259-guest-test.sh nwoas_scripts/agxdcp-s261-guest-test.sh nwoas_scripts/agxdcp-s268-guest-test.sh nwoas_scripts/agxdcp-s269-guest-test.sh ROADMAP-2026-11-17.md HANDOFF-2026-11-18.md; do cp -p "$S/$f" "$D/$f"; done
[ -f $S/HANDOFF-2026-11-19.md ] && cp -p $S/HANDOFF-2026-11-19.md $D/ || true
[ -f $S/HANDOFF-2026-11-19-night.md ] && cp -p $S/HANDOFF-2026-11-19-night.md $D/ || true
[ -f $S/HANDOFF-2026-11-19-final.md ] && cp -p $S/HANDOFF-2026-11-19-final.md $D/ || true
# redact Wi-Fi network names in the public copy
cd $D && grep -rl -I -i -E 'home-ap|neighbour-ap' --exclude-dir=.git . 2>/dev/null | grep -v 'rtc-s184/README.md' | while read f; do sed -i '' -e 's/home-ap/home-ap/g; s/home-ap/home-ap/g; s/neighbour-ap/neighbour-ap/g; s/neighbour-ap/neighbour-ap/g; s/neighbour-ap/neighbour-ap/g' "$f"; done
# S264C/S264D standalone launcher patches (out-* folders are excluded above)
mkdir -p $D/nwoas_scripts/standalone-s264/patches
for p in $S/nwoas_scripts/standalone-s264/out-s264*/hv2-*.patch; do [ -f "$p" ] && cp -p "$p" $D/nwoas_scripts/standalone-s264/patches/; done
[ -f $S/HANDOFF-2026-11-20.md ] && cp -p $S/HANDOFF-2026-11-20.md $D/ || true
