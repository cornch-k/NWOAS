# dtb-s221 review: static J274 FDT roundtrip

Scope: static device-tree roundtrip only. No hardware, no live ADT, no raw dumps,
no payload build or edit. Only files under `nwoas_scripts/dtb-s221`,
`nwoas_scripts/rtc-s178/linux-ref` plus its describing `PLAN.md`, and the
non-log, non-secret text and DTB files in `m1n1_windows` were read.

## 1. Byte reproducibility: confirmed

| artifact | sha256 | size |
|---|---|---|
| `m1n1_windows/apple-j274-padded.dtb` (original) | `ecc93b24…190f76` | 65536 |
| `dtb-s221/rebuilt-j274.dtb` | `ecc93b24…190f76` | 65536 |
| `dtb-s221/recovered-j274.dts` | `0cd8f7c7…be61a8` | 3320 lines |

- Hashes match `roundtrip.json`. `cmp` reports the two DTBs identical.
- Local `dtc --version` is `DTC 1.8.1`, matching the recorded toolchain.
- FDT header of the original: version 17, last-compatible 16, `totalsize` = 65536,
  empty memory-reservation block, structure block ends at 60256 and strings at 62963.
  The remainder to 65536 is zero fill, which `dtc -S 65536` reproduces.
- The unpadded sibling `m1n1_windows/apple-j274.dtb` (62963 bytes) differs from
  the padded file only at header bytes 5–7, i.e. the `totalsize` field.
  `apple-j274-pad.dtb` is byte-identical to `apple-j274-padded.dtb`.
  So the padded payload FDT is the unpadded DTB with `totalsize` rewritten and zero fill; no
  node or property content was changed by padding.
- `build-warnings.log` contains only `dtc` checker warnings (mostly phandle-reference
  checks such as `power_domains_property`, `clocks_property`, `iommus_property` on a
  decompiled tree without labels, plus `unit_address_vs_reg`, `simple_bus_reg`,
  `alias_paths`). No errors. These are expected for a `-I dtb -O dts` roundtrip and do
  not affect the binary result.

Conclusion: the DTS is a faithful, byte-reproducible decompilation of the payload FDT.

## 2. Upstream provenance: not established by local records

What the roundtrip proves: nothing about origin. A DTB decompiled and recompiled
reproduces itself regardless of where it came from. `roundtrip.json` already
says so (`upstream_source_revision: unknown`), and that statement is correct.

What the local reference records do and do not say:

- `rtc-s178/PLAN.md` §3 records that `linux-ref/t8103.dtsi`, `t8103-jxxx.dtsi`,
  `t8103-j274.dts` were fetched with curl from "torvalds/linux master". No commit
  hash, tag, date, or URL is recorded. There is no manifest file in `linux-ref/`.
  File mtimes are 2026-09-10 03:57 local. The exact revision is therefore
  **not recoverable** from local records and is not guessed here.
- The three local reference files carry `SPDX-License-Identifier: GPL-2.0+ OR MIT`
  and `Copyright The Asahi Linux Contributors`. That licensing statement applies to
  those reference source files. It cannot be transferred to the DTB by inference.
- The local reference set is incomplete for compilation: it lacks
  `t8103-pmgr.dtsi`, `hwmon-common.dtsi`, `hwmon-mini.dtsi`, `spi1-nvram.dtsi`
  and the `dt-bindings` headers that the three files `#include`. It cannot be
  compiled to test whether it yields the payload DTB.
- Content comparison shows the payload DTB was **not** generated from the local
  mainline reference files. The recovered DTS contains compatibles absent from all
  three local reference files, including `apple,t8103-dcp`, `apple,t8103-dcpext`,
  `apple,display-subsystem`, `apple,t8103-isp`, `apple,sep`, `apple,t8103-sio`,
  `apple,t8103-aop*`, `apple,j274-macaudio` / `apple,macaudio`, `apple,agx-t8103`,
  and nodes such as `dcp@231c00000`, `display-subsystem`, `isp@22a000000`,
  `aop@24ac00000`, `sound`. The local mainline `t8103.dtsi` has the `gpu@206400000`
  node and the `uat-*` reserved regions but none of the DCP/ISP/SEP/SIO/AOP/audio
  nodes. These features are characteristic of the downstream Asahi Linux tree,
  but the local records do not identify that tree or its revision.
- The `m1n1_windows` fork (git HEAD `bddf7f06…`, remote `cornch-k/m1n1_windows`)
  does not track any of the three `apple-j274*.dtb` files, so its history gives no
  origin either. Its README shows the generic upstream m1n1 payload recipe
  (`build/dtb/apple-j274.dtb`) but does not say where this specific DTB was built.
- `m1n1_windows/3rdparty_licenses` covers libfdt, dwc3, ARM, minlzma, tinf, fonts,
  and GPL-2 for m1n1 itself. None of these entries refers to the device tree.

Accurate statements that can be made:

1. The payload FDT is a static, public-style Apple J274 (Mac mini M1, 2020) board
   device tree, `compatible = "apple,j274", "apple,t8103", "apple,arm-platform"`,
   whose node structure and property naming follow the Apple DTS family in
   `arch/arm64/boot/dts/apple/` as maintained by the Asahi Linux project.
2. It is consistent with a downstream Asahi Linux build rather than the local
   mainline reference snapshot. The specific tree, revision, and build date are
   **not recorded anywhere locally**.
3. Attribution to "The Asahi Linux Contributors" under `GPL-2.0+ OR MIT` is the
   documented licensing of the upstream *source* files of this family. Applying it
   to this binary requires the source revision, which is missing. Until that is
   recorded, provenance should be described as "unknown revision, Asahi-family
   Apple DTS", not as a specific upstream commit.

## 3. Machine-specific identity in the recovered source: none beyond template placeholders

Property names present that could in principle carry per-unit identity:

- `local-mac-address` (Wi-Fi node under `pci@0,0`; Ethernet node under `pci@2,0`)
- `local-bd-address` (Bluetooth node under `pci@0,0`)
- `apple,antenna-sku` (Wi-Fi node)
- `chosen/framebuffer@0/reg`
- `memory@800000000/reg`
- `chosen/stdout-path`
- `nvram` alias pointing at a SPI flash partition

Assessment (names only, values withheld): all of these hold the same
loader-placeholder pattern as the upstream `t8103-jxxx.dtsi` / `t8103-j274.dts`
templates, where the comments read "To be filled by the loader". Nothing in the
recovered DTS looks like a unit-specific MAC, Bluetooth address, serial, ECID,
chip or board ID, seed, or boot-arguments string. There are no `serial-number`,
`bootargs`, `linux,initrd-*`, `kaslr-seed`, or `rng-seed` properties. The
`model` and `sound/model` strings are the generic product names from upstream.

The recovered DTS contains no comments, labels, SPDX line, or copyright notice.
That is inherent to `dtc -I dtb -O dts` output and is not evidence of removal.

## 4. Recommendations

- Keep `roundtrip.json` as is. Its "unknown" provenance field is the honest value.
- If provenance matters for redistribution, add a source manifest (tree URL,
  commit, and the build command) next to the DTB in `m1n1_windows` and re-run this
  roundtrip against a DTB compiled from that recorded revision. Only a build from
  a recorded source that yields the same `ecc93b24…` hash would establish upstream
  provenance.
- Record the commit hash for `rtc-s178/linux-ref` in `PLAN.md` on the next fetch so
  the reference set is citable.
