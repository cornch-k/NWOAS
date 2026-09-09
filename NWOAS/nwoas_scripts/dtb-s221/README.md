# S221 — exact rebuild of the static J274 FDT

`recovered-j274.dts` was decoded from the pre-existing static
`m1n1_windows/apple-j274-padded.dtb`. It is not a machine-generated Apple
Device Tree (ADT), NVRAM dump, or a new board configuration. DTC 1.8.1 with
`-I dts -O dtb -S 65536` reproduces every byte of that existing 64KiB artifact.
`python3 build.py` validates the tool version and complete expected SHA256.

Combining the rebuilt DTB with the source-built S215 prefix and S216 FD
reproduces the exact hardware-tested S216 payload (`payload-assembly.json`).
No installed firmware or live memory was modified by this exercise.

The exact original upstream source revision is not recovered. Reconstructed
source byte reproducibility is distinct from an identified upstream revision.
The accompanying Claude source/attribution review records those limits.
