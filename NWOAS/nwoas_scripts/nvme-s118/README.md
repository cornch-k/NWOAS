# S118: apply verified SSD-local Windows image

HARDWARE-UNVERIFIED. S117 copied both split files correctly; all four digests passed.
This stage changes only the existing Windows trial NTFS volume on the authorized Mac mini.
NWGUARD is read-only: checks NTFS label/serial/total bytes and partition style, offset, length,
number through IOCTL_DISK_GET_PARTITION_INFO_EX; ready also requires full logical image size
plus 256MiB available. Any query failure stops before cleanup/apply.
The two source hashes are checked again after reboot before cleanup.
An explicit list of failed installation directories plus old t1.swm/t2.swm is deleted.
Top-level reparse points and an existing S118-APPLY-STARTED marker stop automatic retries.
The correct S117SRC is preserved, and no formatting or partition edits occur.
DISM /Index:2 /CheckIntegrity /Verify applies from local SWMs to the target root.
PASS requires DISM exit0 plus ntoskrnl.exe, winload.efi and SYSTEM registry hive.
This does not configure ESP/BCD or prove first boot. Keep S103/S102 for this experiment.
Report is assembled in RAM, retained on the modified SSD and USB, then WinPE shuts down.

References:
- https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/dism-image-management-command-line-options-s14
- https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-partition_information_ex
- https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getdiskfreespaceexw
