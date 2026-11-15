# S240 Wi-Fi probe on the Mac mini - 2026-11-15 23:03

Run: `hw/run-hw.sh` (restart S242 Windows -> idle m1n1 proxy -> `run.sh --run` -> m1n1 reboot -> relaunch S242).
wifi_probe.py sha256 dab5251d35f68f93c117f35057770af5e63076f91a2808fcd4118c444f6a9de1, default sequence (no --controller-reset).
Result JSON: `run-20261115-230301-136u9xlf/result.json`, console: `hw/probe-console.txt`, timeline: `hw/results.txt`.

Verdict **PASS_IDS** (config-space observation only; no firmware, no driver, nothing handed to UEFI/Windows).

| Item | Value |
|---|---|
| SMC gP0d key info | 4 bytes, ui32; written 0x800001 (on), then 0x800000 (off) |
| Port 0 link | up, linksts 0x99000001, Gen2 (speed code 2) x1 |
| Bus 1 fn 0 | 14e4:4425, class 0x028000 (network, other), rev 3, subsystem 106b:4378 = BCM4378 Wi-Fi |
| Bus 1 fn 1 | 14e4:5f69, class 0x028000, rev 3, subsystem 106b:4378 = BCM4378 Bluetooth function |
| fn 0 BARs | BAR0 mem64 64 KiB, BAR2 mem64 8 MiB |
| fn 1 BARs | BAR0 mem64 32 KiB, BAR2 mem64 4 MiB |
| Power down | confirmed, no errors, port 1 (FL1100 USB-A) registers unchanged |

## Consequence for USB (important, HYPOTHESIS from EDK2 allocation rules, not measured)

If UEFI enumerates port 0 with Wi-Fi powered, the predicted 32-bit windows are
Wi-Fi bus 0xC0000000-0xC0CFFFFF and **FL1100 moves to 0xC0D00000-0xC0DFFFFF**.
S237 reserves only 0xC0000000-0xC00FFFFF, so the USB-A read corruption would come back at the new FL1100 window,
and the Wi-Fi window would overlap guest RAM too. Before powering Wi-Fi in UEFI, the firmware must reserve the
whole PCIe 32-bit aperture it can hand out (or at least 0xC0000000-0xC0DFFFFF) from guest RAM.

## Next (Wi-Fi stage 2)
1. UEFI: power gP0d + bring up port 0 before PCI enumeration, reserve the full MMIO32 aperture from RAM.
2. Windows driver for BCM4378 (PCIe FullMAC, msgbuf protocol, firmware/NVRAM/CLM from macOS
   /usr/share/firmware/wifi). No Windows driver for BCM4378 was found (GitHub repo + code search, several queries,
   2026-11-15); closest reference: erspicu/AprWindowsARMDriverPI5 (CYW43455 SDIO Wi-Fi plan for Windows on RPi5)
   and Linux brcmfmac PCIe (Asahi).
