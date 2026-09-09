# S98: manual partition + image apply from WinPE (user types; USB-A keyboard only)

Goal: Windows itself rewrites the GPT inside the WINTEST zone (delete WINTEST, create
EFI/MSR/Windows) through the validated relay, then DISM applies install.swm. Apple
partitions must stay byte-identical (relay refuses otherwise; host re-verifies GPT after).

At the Setup screen press Shift+F10 for a command prompt.

    diskpart
    list disk                         -> Disk 0 = 233 GB internal (relay). Note the USB disk number too.
    select disk 0
    list partition                    -> 1 500MB, 2 220GB, 3 25GB (WINTEST), 4 5GB. STOP if different.
    select partition 3
    delete partition override
    create partition efi size=100
    format fs=fat32 quick label=SYSTEM
    assign letter=S
    create partition msr size=16
    create partition primary
    format fs=ntfs quick label=Windows
    assign letter=W
    list partition                    -> report the output
    exit

    dir D:\sources\install.swm        (if not found: try E:, F: ... ; the media letter may have moved)
    dism /Get-WimInfo /WimFile:D:\sources\install.swm
    dism /Apply-Image /ImageFile:D:\sources\install.swm /SWMFile:D:\sources\install*.swm /Index:<N> /ApplyDir:W:\
    bcdboot W:\Windows /s S: /f UEFI

Expected relay log: [S98] GPT COMMIT lines for LBA 1..5 and 61279339..61279343, zero REFUSED,
then bulk writes inside 53839104..59968511 during Apply-Image (~5 GB, ~15–20 min at ~5 MB/s).
If diskpart reports an error on delete/create, stop and report: the relay refused a table it judged unsafe.
Do NOT run `clean`, `convert`, or select any other disk.
