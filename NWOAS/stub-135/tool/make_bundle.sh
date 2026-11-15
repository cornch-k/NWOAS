#!/bin/bash
# Assemble stub-135/bundle/ (the folder copied to the Mac mini). Copies files only; touches no disk device.
# Inputs: dl/asahi-installer-pkg/installer-v0.9.2.tar.gz (official asahi-installer release, sha256 pinned),
#         dl/ipsw-13.5/<partial>.ipsw (fetch_ipsw_parts.py), the D4 m1n1, this tool.
set -euo pipefail
S=/Users/angigyeom/Desktop/DEV/NWOAS/stub-135
D4=/Users/angigyeom/Desktop/DEV/NWOAS/recovery-tahoe-proxy-20260906/m1n1-bare.bin
PKG=$S/dl/asahi-installer-pkg/installer-v0.9.2.tar.gz
IPSW=$S/dl/ipsw-13.5/UniversalMac_13.5_22G74_j274_stub-parts.ipsw
B=$S/bundle

[ "$(shasum -a 256 "$PKG" | cut -d' ' -f1)" = 484d999e53b4aef27d17c4478e49567e96842f8a6cb35c073a0df5bf0bbe8276 ]
[ "$(shasum -a 256 "$IPSW" | cut -d' ' -f1)" = 9c315566b75ec744e3135d92f3925e4b01f65211641af765e4b84a76201ed419 ]
[ "$(md5 -q "$D4")" = 7bec0225edc101857be3b7f195dd7357 ]
[ ! -e "$B" ] || { echo "STOP: $B exists; move it away first"; exit 1; }

mkdir -p "$B"/{asahi,tool,stage/step2,ipsw,boot}
tar -xzf "$PKG" -C "$B/asahi"
[ "$(cat "$B/asahi/version.tag")" = v0.9.2 ]
# Asahi's own m1n1, step2 and launcher are not used: our boot.bin is the D4 m1n1, our step2.sh is in stage/.
rm -rf "$B/asahi/boot" "$B/asahi/step2" "$B/asahi/install.sh"
cp "$B/asahi/logo.icns" "$B/stage/logo.icns"
tar -xzf "$PKG" -C "$B/stage" "./step2/Finish Installation.app" "./step2/IAPhysicalMedia.plist"
cp "$S/tool/step2/step2.sh" "$B/stage/step2/step2.sh"
cp "$S/tool/nwstub.py" "$S/tool/gptlib.py" "$B/tool/"
ln "$IPSW" "$B/ipsw/" 2>/dev/null || cp "$IPSW" "$B/ipsw/"   # hard link: same bytes, no second 685 MB copy
cp "$S/dl/ipsw-13.5/parts.json" "$B/ipsw/"
cp "$D4" "$B/boot/boot.bin"

cat > "$B/nwstub.sh" <<'EOF'
#!/bin/sh
# Run on the Mac mini in macOS Terminal:  sudo ./nwstub.sh   (dry run)   |   sudo ./nwstub.sh --apply
set -eu
cd "$(dirname "$0")"
B=$PWD
[ "$(uname -m)" = arm64 ] || { echo "STOP: arm64 Mac only"; exit 1; }
export LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8 PATH=/usr/bin:/bin:/usr/sbin:/sbin
export DYLD_LIBRARY_PATH="$B/asahi/Frameworks/Python.framework/Versions/Current/lib"
export DYLD_FRAMEWORK_PATH="$B/asahi/Frameworks"
unset IPSW_BASE PYTHONPATH PYTHONHOME
PY="${NWSTUB_PYTHON:-$B/asahi/Frameworks/Python.framework/Versions/3.13/bin/python3.13}"
exec "$PY" -I -B "$B/tool/nwstub.py" --bundle "$B" "$@"
EOF
chmod 755 "$B/nwstub.sh"
xattr -cr "$B" 2>/dev/null || true
(cd "$B" && find . -type f ! -name SHA256SUMS ! -path './out/*' | LC_ALL=C sort | sed 's,^\./,,' \
    | while IFS= read -r f; do printf '%s  %s\n' "$(shasum -a 256 "$f" | cut -d' ' -f1)" "$f"; done > SHA256SUMS)
echo "bundle ready: $B ($(du -sh "$B" | cut -f1), $(wc -l < "$B/SHA256SUMS") files)"
