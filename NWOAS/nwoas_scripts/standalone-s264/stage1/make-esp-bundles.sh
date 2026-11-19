#!/bin/bash
# NWOAS 4-1 / 4-2: build the ESP file bundles and their Windows jobs (host only; nothing is sent or run).
#   esp-4-1/  -> \m1n1\ on the ESP (PLAN-4-1 7장 단계 3), used by the rehearsals and by go1.sh:
#             boot.bin (= D4, md5 7bec0225...), stage1.bin (2,097,152 B), go1.sh (size/MD5 of stage1.bin filled in),
#             cleanbp-local.sh, nwoas-s264g.bin (= s264G + "nwoas_autostart=3", read only by the 4-2 rehearsal's
#             stage1-42.bin), nwoas-payload.bin (= S261 payload, read by s264G auto-start)
#   esp-4-2/  boot.bin (= s264G + "nwoas_autostart=3") -> \m1n1\boot.bin, only after 4-1 is registered and passed
#             (PLAN-4-1 5장); the payload is already on the ESP from esp-4-1
#   rehearsal/ (MacBook only, never on the ESP): stage1-missing.bin, stage1-fault.bin, stage1-42.bin
#   out/      copy of the build-stage1.sh output (4 variants + stage1.sha256)
# Jobs: file-delivery-s243 deliver jobs (C:\NWOAS\drop\esp-4-x) + copy jobs J-esp-4-x-copy.cmd (mountvol S: /S,
# keep a copy of anything already in S:\m1n1, copy, SHA-256 read-back on the ESP, mountvol S: /D).
# usage: make-esp-bundles.sh <stage1 out dir of build-stage1.sh>
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
ST="${1:?usage: make-esp-bundles.sh <build-stage1.sh out dir>}"
FD="$ROOT/nwoas_scripts/file-delivery-s243"
D4="$ROOT/experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin"
G="$ROOT/nwoas_scripts/standalone-s264/out-s264G/m1n1-s264G-agx-hv2-standalone.bin"
PAY="$ROOT/nwoas_scripts/fw-bt-s261/out/s261/m1n1-payload-s261-bt.bin"
[ "$(md5 -q "$D4")" = 7bec0225edc101857be3b7f195dd7357 ] || { echo "STOP: D4 changed"; exit 2; }
[ "$(shasum -a 256 "$G" | awk '{print $1}')" = d1fea3d73ad7d0d7da562802dbdec88527490dfe5f395d28dbd464a324c43ce6 ] || { echo "STOP: s264G"; exit 2; }
[ "$(shasum -a 256 "$PAY" | awk '{print $1}')" = 26428b579ba117ae3b26c8d914461cc125b07140de1c1bf3393ff1a62fd0ab92 ] || { echo "STOP: S261 payload"; exit 2; }
S1="$ST/stage1.bin"
( cd "$ST" && diff stage1.sha256 "$HERE/stage1-variants.sha256" ) || { echo "STOP: $ST is not the recorded stage-1 build"; exit 2; }
rm -rf "$HERE/esp-4-1" "$HERE/esp-4-2" "$HERE/rehearsal" "$HERE/out"
mkdir -p "$HERE/esp-4-1" "$HERE/esp-4-2" "$HERE/rehearsal" "$HERE/out"
cp "$ST"/stage1.bin "$ST"/stage1-missing.bin "$ST"/stage1-fault.bin "$ST"/stage1-42.bin "$ST"/stage1.sha256 "$HERE/out/"
cp "$D4" "$HERE/esp-4-1/boot.bin"
cp "$S1" "$HERE/esp-4-1/stage1.bin"
cp "$ST/stage1-missing.bin" "$ST/stage1-fault.bin" "$ST/stage1-42.bin" "$HERE/rehearsal/"
sed -e "s/^ST1_SIZE=.*/ST1_SIZE=$(stat -f %z "$S1")/" -e "s/^ST1_MD5=.*/ST1_MD5=$(md5 -q "$S1")/" "$HERE/go1.sh" > "$HERE/esp-4-1/go1.sh"
cp "$HERE/cleanbp-local.sh" "$HERE/esp-4-1/"
python3 -I - "$G" "$HERE/esp-4-2/boot.bin" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
open(sys.argv[2], "wb").write(data + b"nwoas_autostart=3\n" + b"\0\0\0\0")
PY
cp "$HERE/esp-4-2/boot.bin" "$HERE/esp-4-1/nwoas-s264g.bin"
cp "$PAY" "$HERE/esp-4-1/nwoas-payload.bin"
for b in esp-4-1 esp-4-2; do
    rm -f "$FD/manifests/$b.json" "$FD/jobs/$b-deliver.cmd"      # this script owns these two files
    ( cd "$FD" && python3 -I s243_manifest.py "manifests/$b.json" "$HERE/$b" >/dev/null \
      && python3 -I make_job.py "manifests/$b.json" "C:\\NWOAS\\drop\\$b" "jobs/$b-deliver.cmd" >/dev/null )
    python3 -I - "$FD/manifests/$b.json" "$b" "$HERE/J-$b-copy.cmd" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
b = sys.argv[2]
names = [a["name"] for a in m["artifacts"]]
hashes = [a["sha256"] for a in m["artifacts"]]
L = ["@echo off", f"echo NWOAS {b.upper()} COPY BEGIN: C:\\NWOAS\\drop\\{b} -> ESP \\m1n1",
     f'set "T=C:\\NWOAS\\drop\\{b}"',
     "mountvol S: /S", "if errorlevel 1 (echo NWOAS ESP MOUNT FAIL & exit /b 1)",
     "if not exist S:\\m1n1 mkdir S:\\m1n1",
     "if not exist \"%T%\\before\" mkdir \"%T%\\before\""]
for n in names:
    L.append(f'if exist "S:\\m1n1\\{n}" copy /Y "S:\\m1n1\\{n}" "%T%\\before\\{n}" >nul')
    L.append(f'copy /Y "%T%\\{n}" "S:\\m1n1\\{n}" >nul')
    L.append(f"if errorlevel 1 (echo NWOAS ESP COPY FAIL {n} & mountvol S: /D & exit /b 1)")
ps = ("$n=@(" + ",".join(f"'{n}'" for n in names) + "); $h=@(" + ",".join(f"'{h}'" for h in hashes) + "); $bad=0; "
      "for($i=0;$i -lt $n.Count;$i++){ $f='S:\\m1n1\\'+$n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); "
      "if($g -eq $h[$i]){ 'NWOAS ESP READBACK MATCH '+$n[$i]+' '+$g } else { 'NWOAS ESP READBACK MISMATCH '+$n[$i]+' '+$g; $bad=1 } }; exit $bad")
L.append(f'"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoProfile -NonInteractive -Command "{ps}" 2>&1')
L.append("set RB=%errorlevel%")
L.append("dir S:\\m1n1")
L.append("mountvol S: /D")
L.append(f"if not \"%RB%\"==\"0\" (echo NWOAS {b.upper()} COPY FAIL readback & exit /b 1)")
L.append(f"echo NWOAS {b.upper()} COPY COMPLETE")
L.append("exit /b 0")
open(sys.argv[3], "w", newline="\r\n").write("\n".join(L) + "\n")
PY
done
( cd "$HERE" && shasum -a 256 esp-4-1/* esp-4-2/* rehearsal/* out/* J-esp-4-1-copy.cmd J-esp-4-2-copy.cmd "$FD/jobs/esp-4-1-deliver.cmd" "$FD/jobs/esp-4-2-deliver.cmd" \
  go1.sh cleanbp-local.sh build-stage1.sh hv2-s264G-to-stage1.patch stage1-variants.sha256 stage1_layout_test.py \
  run-stage1-rehearsal.sh run-stage1-42-rehearsal.sh run-s268-after-stage1.sh run-stage1-rehearsal-slot.sh \
  chainload_nonop.py probe_running_image.py watch-after-go1.sh make-esp-bundles.sh \
  "$FD/manifests/esp-4-1.json" "$FD/manifests/esp-4-2.json" ) | sed "s|  $FD/|  ../../file-delivery-s243/|" > "$HERE/SHA256SUMS"
cat "$HERE/SHA256SUMS"
