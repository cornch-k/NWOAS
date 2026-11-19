#!/bin/sh
# NWOAS 4-1 failure branch for asahi-installer #425 / #255 ("Local policy update is in progress" loops).
# Same work as asahi-installer tools/cleanbp.sh (MIT; read 2026-11-20 via gh api): remove the
# /System/Volumes/iSCPreboot/<UUID> folders of volume groups that no longer exist.  Offline (no curl).
#   sh /tmp/esp/m1n1/cleanbp-local.sh            -> LIST ONLY (nothing removed)
#   sh /tmp/esp/m1n1/cleanbp-local.sh --apply    -> removes the RM folders, after typing DELETE
# The --apply run is approved by the user on the spot.  Uses: cat diskutil cd grep rm echo read.
set -e
mode="${1:-list}"
cat > /tmp/nwoas-uuids.txt <<UUIDS
3D3287DE-280D-4619-AAAB-D97469CA9C71
C8858560-55AC-400F-BBB9-C9220A8DAC0D
UUIDS
diskutil apfs listVolumeGroups >> /tmp/nwoas-uuids.txt
cd /System/Volumes/iSCPreboot
n=0
for i in ????????-????-????-????-????????????; do
    [ -e "$i" ] || continue
    if grep -q "$i" /tmp/nwoas-uuids.txt; then
        echo "KEEP $i"
    else
        echo "RM   $i"
        n=$((n + 1))
    fi
done
echo "$n folder(s) would be removed."
[ "$mode" = "--apply" ] || { echo "LIST ONLY: nothing removed. To remove them run again with --apply."; exit 0; }
[ "$n" -gt 0 ] || { echo "Nothing to remove."; exit 0; }
echo "Type DELETE (capitals) to remove the RM folders above, anything else to stop:"
read answer
[ "$answer" = "DELETE" ] || { echo "Stopped: nothing removed."; exit 0; }
for i in ????????-????-????-????-????????????; do
    [ -e "$i" ] || continue
    grep -q "$i" /tmp/nwoas-uuids.txt || { echo "rm -rf $i"; rm -rf "$i"; }
done
echo "Done. Shut down (shutdown -h now), then start go1.sh again from the same 1TR."
