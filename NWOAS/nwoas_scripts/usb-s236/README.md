# S236: USB-A read corruption = DMA into the PCIe root-port bridge window (guest RAM 0xC0000000..0xC00FFFFF)

See `../usb-s232/HANDOFF-USB-A-CORRUPTION.md` sections 6-9 for the full evidence chain.

| File | Purpose |
|---|---|
| `B1-set-badmemory.cmd` | Windows job: `bcdedit /set {badmemory} badmemorylist 0xC0000 ... 0xC00FF` (256 x 4 KiB PFNs = guest-physical 0xC0000000..0xC00FFFFF). Takes effect at the next Windows boot. |
| `B2-revert-badmemory.cmd` | Windows job: `bcdedit /deletevalue {badmemory} badmemorylist` (full revert). |
| `B0-state.cmd` | Windows job: print `{current}` / `{badmemory}` BCD objects and total physical memory. |
| `validate.sh` | Host orchestrator: pre-jobs -> restart -> 3 x U3 cycles -> truncatememory 4 GiB -> 3 x U3 cycles -> revert truncatememory -> keep the last session alive. Results in `results-<tag>.txt`. |
| `firmware-reserve-bridge-window.patch` | PROPOSAL (not built, not booted): reserve the same 1 MiB in the UEFI memory map so that WinPE / a fresh install are covered too. |

The fix that was validated on hardware is the BCD `badmemorylist` setting (Windows-side, persistent in the
installed system's BCD store, reversible with B2). No HV or payload change is part of the fix; validation ran on
the unchanged S233b hypervisor and S223 payload.
