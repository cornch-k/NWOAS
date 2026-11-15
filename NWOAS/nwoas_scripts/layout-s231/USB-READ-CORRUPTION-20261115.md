# S231 WinPE: USB-A mass-storage reads corrupt under copy/DISM, not under sequential unbuffered reads

Session: logs/layout-s231-20261115-181130.tcRgQH (S231 HV, S223 payload), WinPE 26100 from WINARM25H2 USB (USB-A, E:).
Host truth: USB files re-read on the MacBook after re-mount: install.swm 9b827189..., install2.swm c19b4892...;
`wimlib-imagex verify` of the USB set passed (all resources OK).

| Guest operation | Result |
|---|---|
| NWREAD direct (1 MiB, one outstanding) E:\sources\install2.swm, 2 runs | c19b4892 (correct) |
| NWREAD buffered E:\sources\install2.swm | c19b4892 (correct) |
| NWREAD direct E:\sources\install.swm | 9b827189 (correct) |
| DISM /Apply-Image directly from E: | Error 1392, 0x80070570 "file corrupted in block" (ChsIFEComp.dll, same offset 5 retries) |
| `copy /b` E: -> C: (both files) | wrong hashes (install.swm 5ef00890, install2.swm 848281b4), stable on re-read |
| `copy /b` C: -> C: of the bad file | identical to its source (SSD write/read path consistent) |
| `robocopy /J` (unbuffered) E: -> C: install2.swm | c19b4892 (correct), 133 MB/s |

Hypothesis (unverified): USB-A (FL1100 xHCI behind the HV) returns wrong data when Windows issues large and/or
multiple concurrent bulk reads with arbitrary (cache-manager / mapped-section) buffers; sequential single-outstanding
unbuffered reads are fine. Candidates: DMA address translation for some guest buffer ranges (low-4GiB alias window vs
high RAM, cf. D76), scatter-gather TRB chains, or concurrent ring handling in the HV FLC/XHC mediation.
Workaround used for installation: robocopy /J to the SSD, SHA256 verify (direct + buffered), DISM from the SSD copy.
This must be root-caused before relying on USB storage in installed Windows.
