#!/bin/sh
# NWOAS 4-1 (PLAN-4-1-disk-boot.md 8장): register the first-stage m1n1 (stage1.bin, next to this file on the
# ESP) for the stub (VGID C15A9C5D-...).  Run ONLY in the stub's paired 1TR (macOS 13.5 recoveryOS) by the user:
#   sh "<ESP mount point>/m1n1/go1.sh"          register stage1.bin
#   sh "<ESP mount point>/m1n1/go1.sh" --d4     second rollback: register boot.bin (= D4) again with kmutil
#                                               only (the way D4 was re-registered on Macintosh HD on 09-07
#                                               and 11-15); the first rollback is the stub's own step2.sh
# Derived from stub-135/tool/step2/step2.sh (itself from asahi-installer v0.9.2 src/step2/step2.sh, MIT).
# Uses only commands in research-20261116/C4-recovery-13.5-commands.txt (no shasum).  Nothing is changed
# before the "Press enter" after the checks; any failed check stops with no change.  Writes only to /tmp (the
# ESP is mounted read-only, so the registered boot reads exactly the ESP bytes the rehearsals read).
# Does NOT run "bputil -nc" (done on 2026-11-16 for this stub) and never touches Macintosh HD.

ST1_SIZE=2097152
ST1_MD5=@ST1_MD5@
D4_SIZE=2097152
D4_MD5=7bec0225edc101857be3b7f195dd7357
WANT_VGID=C15A9C5D-F2F8-486D-95CA-F47E3C47A7CD

stop() { echo; echo "STOP: $*"; echo "Nothing was changed. Power off: shutdown -h now"; exit 1; }

case "${1:-}" in
    "")   what=stage1.bin; want_size=$ST1_SIZE; want_md5=$ST1_MD5;;
    --d4) what=boot.bin;   want_size=$D4_SIZE;  want_md5=$D4_MD5;;
    *)    stop "unknown option $1 (only --d4)";;
esac
echo "NWOAS 4-1 registration (go1.sh): $what"
echo

# 0. every command this script uses must exist in this recoveryOS
for c in stat md5 cp grep sed bputil kmutil diskutil mount reboot shutdown; do
    command -v "$c" >/dev/null 2>&1 || stop "command '$c' is missing in this recoveryOS"
done
echo "CHECK commands present"

here="${0%/*}"
[ "$here" = "$0" ] && here=.
img="$here/$what"
[ -f "$img" ] || stop "$img not found (ESP not mounted at the expected place?)"
[ "$(stat -f %z "$img")" = "$want_size" ] || stop "$what size is not $want_size"
[ "$(md5 -q "$img")" = "$want_md5" ] || stop "$what MD5 is not $want_md5"
cp "$img" /tmp/nwoas-go1.bin || stop "copy to /tmp failed"
[ "$(md5 -q /tmp/nwoas-go1.bin)" = "$want_md5" ] || stop "copy in /tmp has a different MD5"
echo "CHECK $what $want_size B, MD5 $want_md5 (copied to /tmp)"

# 1. the stub volume = the one whose installed step2.sh carries the stub VGID (filled on 2026-11-16)
sys=""
for f in /Volumes/*/"Finish Installation.app"/Contents/Resources/step2.sh; do
    [ -f "$f" ] || continue
    vg="$(sed -n 's/^VGID="\(.*\)"$/\1/p' "$f")"
    if [ "$vg" = "$WANT_VGID" ]; then
        sys="${f%/Finish Installation.app/*}"
        preboot="$(sed -n 's/^PREBOOT="\(.*\)"$/\1/p' "$f")"
        break
    fi
done
[ -n "$sys" ] || stop "no mounted volume with the stub's step2.sh (VGID $WANT_VGID)"
echo "CHECK stub system volume: $sys"

# 2. paired one true recoveryOS of that volume group
bputil -d -v "$WANT_VGID" >/tmp/nwoas-bp.txt 2>&1 || stop "bputil -d failed (see /tmp/nwoas-bp.txt)"
grep -q ': Paired' /tmp/nwoas-bp.txt || stop "NOT-PAIRED: this is not the stub's own recoveryOS (PLAN-4-1 8장)"
grep -q 'one true recoveryOS' /tmp/nwoas-bp.txt || stop "NOT-1TR: hold the power button in ONE long press (PLAN-4-1 8장)"
echo "CHECK paired 1TR for $WANT_VGID"
echo
echo "ROLLBACK LINE (take a photo; type it in this stub's 1TR if the MacBook says ROLLBACK or kmutil fails below):"
echo "  sh \"$sys/Finish Installation.app/Contents/Resources/step2.sh\""
echo
echo "Next, for THIS volume group only:"
echo "  kmutil configure-boot -c /tmp/nwoas-go1.bin --raw --entry-point 2048 --lowest-virtual-address 0 -v \"$sys\""
echo "You will be asked for the macOS admin password (the account used on 2026-11-16)."
echo
echo "Press enter to continue (Ctrl-C to stop; nothing has been changed yet)."
read _

# 3. (step2.sh) kmutil reads AdminUserRecoveryInfo.plist from iSCPreboot on some setups
if [ -n "$preboot" ] && [ -e "/System/Volumes/iSCPreboot/$WANT_VGID/boot" ]; then
    diskutil mount "$preboot" >/dev/null 2>&1
    pb="$(diskutil info "$preboot" | grep "Mount Point" | sed 's, *Mount Point: *,,')"
    [ -n "$pb" ] && cp -R "$pb/$WANT_VGID/var" "/System/Volumes/iSCPreboot/$WANT_VGID/" 2>/dev/null
fi

while ! kmutil configure-boot -c /tmp/nwoas-go1.bin --raw --entry-point 2048 --lowest-virtual-address 0 -v "$sys"; do
    echo
    echo "kmutil failed. Wrong password? Press enter to try again (Ctrl-C to stop)."
    read _
done

bputil -d -v "$WANT_VGID" >/tmp/nwoas-go1-after.txt 2>&1 || true
grep -i -E 'Security Mode|Custom|Kernel|Paired' /tmp/nwoas-go1-after.txt
echo
echo "GO1-DONE: $what registered (policy lines above; nothing is written to the ESP)"
echo "This is registration success only, NOT proof that anything boots."
echo "Tell the MacBook side the moment you press enter (it starts watch-after-go1.sh then)."
echo "The Mac mini screen stays black either way; only the MacBook's last line (KEEP / ROLLBACK) counts."
echo "Press enter to reboot."
read _
reboot
