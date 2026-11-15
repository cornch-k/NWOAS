#!/bin/sh
# NWOAS stub-135 second step. Derived from asahi-installer v0.9.2 src/step2/step2.sh (MIT).
# Runs in the stub's OWN paired 1TR (macOS 13.5 recoveryOS) via "Finish Installation.app", or by hand:
#   sh "/Volumes/<stub System>/Finish Installation.app/Contents/Resources/step2.sh"
# Changes vs Asahi: verifies boot.bin is the NWOAS D4 m1n1 before anything; prints what it will do;
# security changes apply ONLY to this stub's volume group (bputil -v / kmutil -v), never Macintosh HD.
# ##DISTRO## / ##VGID## / ##PREBOOT## are filled in by stub.py (StubInstaller.install_files).

set -e

DISTRO="##DISTRO##"
VGID="##VGID##"
PREBOOT="##PREBOOT##"
BOOT_MD5=7bec0225edc101857be3b7f195dd7357
BOOT_SHA256=80a13ca257fcf1d04676e08355b78ffec57cf82fbbbf3b3395f3524daa187e48
BOOT_SIZE=2097152

self="$0"
cd "${self%%step2.sh}"

system_dir="$(cd ../../../; pwd)"
os_name="${system_dir##*/}"

printf '\033[2J\033[H'
echo "$DISTRO - second step (stub-135)"
echo
echo "VGID: $VGID"
echo "System volume: $system_dir"
echo

[ "$(stat -f %z boot.bin)" = "$BOOT_SIZE" ] || { echo "STOP: boot.bin size is not $BOOT_SIZE"; exit 1; }
[ "$(md5 -q boot.bin)" = "$BOOT_MD5" ] || { echo "STOP: boot.bin MD5 is not the D4 m1n1"; exit 1; }
[ "$(shasum -a 256 boot.bin | cut -d' ' -f1)" = "$BOOT_SHA256" ] || { echo "STOP: boot.bin SHA-256 mismatch"; exit 1; }
echo "CHECK boot.bin = NWOAS D4 m1n1 (MD5 $BOOT_MD5)"
echo

bputil -d -v "$VGID" >/tmp/bp.txt

if ! grep -q ': Paired' /tmp/bp.txt; then
    echo "NOT-PAIRED: this is not the stub's own recoveryOS."
    echo "The stub will be set as the default boot volume, then the Mac shuts down."
    echo "After it is off: wait 10 s, HOLD the power button until 'Loading startup options',"
    echo "then select '$os_name' again."
    echo
    echo "Press enter to continue."
    read _
    while ! bless --setBoot --mount "$system_dir"; do
        echo
        echo "bless failed. Wrong password? Press enter to try again."
        read _
    done
    echo
    echo "Press enter to shut down."
    read _
    shutdown -h now
    exit 1
fi

if ! grep -q 'one true recoveryOS' /tmp/bp.txt; then
    echo "NOT-1TR: the power button was not held in one continuous press."
    echo "Shut down, wait 10 s, HOLD the power button until 'Loading startup options',"
    echo "then select '$os_name' again."
    echo
    echo "Press enter to shut down."
    read _
    shutdown -h now
    exit 1
fi

echo "CHECK paired 1TR for $VGID"
echo
echo "Next, for THIS volume group only:"
echo "  1. bputil -nc -v $VGID      (Permissive Security + kernel CTRR off)"
echo "  2. kmutil configure-boot -c boot.bin --raw --entry-point 2048 --lowest-virtual-address 0 -v \"$system_dir\""
echo "Macintosh HD's security policy is not changed."
echo "You will be asked for macOS admin credentials (the same account used for step 1)."
echo
echo "Press enter to continue (Ctrl-C to stop; nothing has been changed yet)."
read _

while ! bputil -nc -v "$VGID"; do
    echo
    echo "bputil failed. Wrong password? Press enter to try again."
    read _
done

echo
echo

if [ -e "/System/Volumes/iSCPreboot/$VGID/boot" ]; then
    # Asahi workaround for kmutil reading AdminUserRecoveryInfo.plist from the wrong place.
    diskutil mount "$PREBOOT"
    preboot="$(diskutil info "$PREBOOT" | grep "Mount Point" | sed 's, *Mount Point: *,,')"
    cp -R "$preboot/$VGID/var" "/System/Volumes/iSCPreboot/$VGID/"
fi

while ! kmutil configure-boot -c boot.bin --raw --entry-point 2048 --lowest-virtual-address 0 -v "$system_dir"; do
    echo
    echo "kmutil failed. Wrong password? Press enter to try again."
    read _
done

echo
echo "Wrapping up..."
echo

mount -u -w "$system_dir"

if [ -e "$system_dir/.IAPhysicalMedia" ]; then
    mv "$system_dir/.IAPhysicalMedia" "$system_dir/IAPhysicalMedia-disabled.plist"
fi

if [ -e "$system_dir/System/Library/CoreServices/SystemVersion-disabled.plist" ]; then
    mv -f "$system_dir/System/Library/CoreServices/SystemVersion"{-disabled,}".plist"
fi

bputil -d -v "$VGID" > "$system_dir/stub135-bputil-after.txt" 2>&1 || true
echo "STEP2-DONE: policy recorded in $system_dir/stub135-bputil-after.txt"
echo "This is registration success only, NOT proof that m1n1 or Windows boots."
echo
echo "Press enter to reboot. The Mac will start the stub (m1n1 waits for the MacBook over USB)."
read _

reboot
