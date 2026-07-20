#!/bin/bash
# NWOAS helper: stage the current diagnostic device m1n1 onto the staging USB.
# Run AFTER re-inserting the "USB" staging stick into the MacBook.
set -e
SRC=/Volumes/X31/NWOAS/m1n1_windows/m1n1-bare-wfidiag14-phyresync.bin
SRC_MD5=19c8a0ed75d239a1fb47c42466bd0edb
# Recovery binaries copied fresh from X31 each run so the on-USB known-good is never a
# stale/broken diagnostic. GOOD = original bare m1n1 (b64269a0). GOOD2 = wfidiag4
# (dc68992d), which is a diagnostic build confirmed to boot (run14).
GOOD=/Volumes/X31/NWOAS/m1n1_windows/m1n1-bare.bin
GOOD2=/Volumes/X31/NWOAS/m1n1_windows/m1n1-bare-wfidiag4.bin

[ -d /Volumes/USB ] || { echo "ERROR: /Volumes/USB not mounted. Re-insert the staging USB (vol 'USB')."; exit 1; }
# safety: confirm /Volumes/USB is the removable USB, NOT the X31 work SSD
REMOVABLE=$(diskutil info /Volumes/USB | awk -F': *' '/Removable Media/{print $2}')
echo "USB removable=$REMOVABLE"
[ "$REMOVABLE" = "Removable" ] || { echo "ERROR: /Volumes/USB is not removable — aborting for safety."; exit 1; }

# 1) refresh the known-good recovery binaries on the USB (always overwrite from X31)
cp "$GOOD"  /Volumes/USB/m1n1-bare-GOOD.bin
cp "$GOOD2" /Volumes/USB/m1n1-bare-wfidiag4.bin
# 2) stage the new diagnostic as the boot object
cp "$SRC" /Volumes/USB/m1n1-bare.bin
sync
echo "staged m1n1-bare.bin: $(md5 -q /Volumes/USB/m1n1-bare.bin)  (expected $SRC_MD5)"
echo "recovery known-good on USB: m1n1-bare-GOOD.bin ($(md5 -q /Volumes/USB/m1n1-bare-GOOD.bin))"
echo ""
echo "Next: eject USB, plug into mini, 1TR terminal, then:"
echo "  kmutil configure-boot -c /Volumes/USB/m1n1-bare.bin --raw --entry-point 2048 --lowest-virtual-address 0 -v \"/Volumes/Macintosh HD\""
echo "If it fails to boot: same command with m1n1-bare-GOOD.bin (or m1n1-bare-prev.bin) to recover."
