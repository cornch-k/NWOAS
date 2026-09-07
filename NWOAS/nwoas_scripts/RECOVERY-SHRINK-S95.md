# S95: Recovery-mode APFS shrink + Windows test partition (user-executed on Mac mini)

Goal: shrink Macintosh HD APFS container by ~25GB and create an ExFAT placeholder
partition WINTEST between the container and Apple Recovery. macOS, the custom m1n1
boot object, ISC and Apple Recovery are untouched. No macOS erase in this step.

Pre-state (S90/S91 read-only measurement): namespace 251000193024 B,
ISC disk0s1 524288000 B, APFS container disk0s2 245107195904 B (allocated ~211GB,
free ~34GB), Apple Recovery disk0s3 5368664064 B.

## In Recovery Terminal (Mac mini, USB-A keyboard)
1. diskutil list internal          -> confirm disk0 = 251.0 GB, disk0s2 = Apple_APFS Container
2. diskutil apfs resizeContainer disk0s2 limits   -> note "Minimum" value
3. If minimum > 220 GB: diskutil apfs listSnapshots disk0s2 ; delete local snapshots
   (diskutil apfs deleteSnapshot <synthesized volume> -uuid <uuid>), rerun step 2.
4. diskutil apfs resizeContainer disk0s2 220g ExFAT WINTEST 0
5. diskutil list internal          -> expect new disk0s4 (ExFAT WINTEST ~25GB) — report full output.
6. Apple menu -> Restart. Mini boots custom object (m1n1 proxy); host verifies new GPT read-only.

Do NOT use Disk Utility GUI "Erase", do NOT touch disk0s1/disk0s3, do NOT format from Windows.
