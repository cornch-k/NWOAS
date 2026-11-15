# GPU 하이퍼바이저 이식 1단계 (gpu-hv1)

## 한국어 요약

**상태: 빌드만 됨, 실기기 미검증.** 빌드가 됐다고 GPU가 동작하는 것은 아니며, 이 단계의 코드는 GPU를
켜지도, 메모리에 연결하지도 않습니다. Mac mini에서는 아직 한 번도 실행하지 않았습니다.

이번에 한 일:

1. 리뷰(`REVIEW-20261115.md`)가 지적한 결함 6개를 고쳤습니다. 원본 복사 트리 `m1n1-gpu-s240`은 그대로 두고,
   그것을 복사한 새 트리 `m1n1-gpu-hv1`에서 고쳤습니다.
   - J313 전용 GPU 영역 주소(0x9fffb8000, Mac mini 16GB에서는 일반 메모리)를 코드에서 지웠습니다. 주소는
     실행할 때 ADT에서 읽고, 없으면 거부합니다.
   - 인터럽트 번호 목록을 코드에서 지우고 ADT `/arm-io/sgx` `interrupts`에서 읽게 했습니다.
   - 없는 파일을 가리키던 include·설명을 고치고, 라이선스 원문과 고지 문서를 넣었습니다.
2. `make NWOAS_AGX=1`일 때만 GPU 관련 코드가 빌드에 들어가게 했습니다. 하이퍼바이저 시작 시 ADT를 읽어
   기록만 남기는 함수(`nwoas_agx_probe`) 하나를 만들었고, `NWOAS_AGX_PROBE=1`로 빌드할 때만 불립니다.
   기본값은 꺼짐입니다.
3. S233 안내서와 같은 명령으로 빌드했고, 같은 빌드를 두 번 해서 결과가 바이트 단위로 같은지 확인했습니다.
4. 저장된 Mac mini ADT 덤프(2026-09-06, Tahoe 펌웨어)로 이 함수가 읽을 값을 미리 확인했습니다.

| 바이너리 | 빌드 조건 | 크기(바이트) | sha256 | S233b와 차이 |
|---|---|---|---|---|
| `out/m1n1-g240O-agx-probeoff.bin` | `NWOAS_AGX=1`, 확인 함수 호출 꺼짐(기본) | 2162688 | `54c109da6dcc71c22255bfd4607011f5a493a47411d611d670b5c91050c5a78b` | 버전 문자열 8바이트만 다름 |
| `out/m1n1-g240P-agx-probe.bin` | `NWOAS_AGX=1 NWOAS_AGX_PROBE=1` | 2179072 | `337ed2ed5d1690acfc2ff59e6a44581168269e3e817092197f171f7383f50325` | 새 함수 6개 + `hv_init`에 호출 1줄 + 문자열, 이미지 16KiB 커짐 |
| (대조, 파일 미보관) `g240N` | `NWOAS_AGX` 없음 | 2162688 | `8393b26ec81540ecf7644f88b589a5001c9bcfcacc9d1a0c89608443df569913` | 버전 문자열 8바이트만 다름 |
| S233b 재빌드 | S233 안내서 (b) 명령 | 2162688 | `d2eb62b53310dbd6c5b481a37bdbbf39aea5826f2bfb8d4a9c740cba3c529316` | 배포본과 동일 |

꺼짐 빌드(O)는 링커가 쓰이지 않는 GPU 코드를 모두 버려서, S233b와 기계어·데이터가 완전히 같고 버전
문자열만 다릅니다.

2단계에 필요한 것(자세한 내용은 아래 "Step 2 needs"):

1. 실기기에서 P 바이너리로 확인 함수의 출력을 한 번 받기(새 런처 필요 — 기존 런처는 HV 크기 2162688과
   해시를 고정 검사하므로 P를 거부합니다).
2. 그 출력으로 GPU 영역 주소, 인터럽트 9개(sgx 5개 + gfx-asc 4개), 일반 메모리 영역 번호를 13.5 펌웨어
   조건에서 확정하기.
3. J313 전용으로 남은 부분(보드 0x26 검사, J313 성능 값, J313 실행 계약, 전원 코드)을 J274용으로 바꾸기.

---

## 1. Base and provenance (review defect 6)

| Tree | What | Evidence |
|---|---|---|
| `m1n1-usb-s233` | S233 source; its (b) build is the production HV (`../../../nwoas_scripts/usb-s236/README.md`: validation ran on the unchanged S233b HV) | `build/m1n1.bin` sha256 `bbeb3dbb…c01207` = shipped (a) |
| `m1n1-gpu-s240` | `rsync -a --exclude build` of s233 + copied windows-on-m1 files (S240 task). **Not modified by this task.** | `BASE-MANIFEST-s240.sha256` (599 files, taken before any edit). `diff -rq --exclude build m1n1-usb-s233 m1n1-gpu-s240` today: only the added `src/hv_agx_*`, `src/nwoas_agx*`, `src/agx_shared/` (re-checked 2026-11-15). Same-second mtime overlap with an s233 rebuild noted by the review stays a HYPOTHESIS about who built it; the content equality is FACT. |
| `m1n1-gpu-hv1` | `rsync -a --exclude build m1n1-gpu-s240/` + this task's edits | `MANIFEST-hv1.sha256`, full diff `out/src-s240-vs-hv1.diff` |

Note: `data/bootlogo_{48,128,256}.png` are dangling symlinks in s233, s240 and hv1 alike; the build does not
need them (logo objects come from `data/*.bin`).

## 2. The six review defects

| # | Defect (review) | Fix (FACT: file:line in `m1n1-gpu-hv1`) |
|---|---|---|
| 1 | J313 gpu-region base 0x9fffb8000 hard-coded (`hv_agx_g2.generated.h:12`, `hv_agx_config_snapshot.c:22`) | Constants removed: `src/hv_agx_g2.generated.h:12-17` (comment only; old macro names now fail to compile), duplicates in `src/agx_shared/include/j313_agx_g2.generated.h:86-87,92-93`. `hv_agx_config_snapshot.c:60` now requires `gpu_region_base == nwoas_agx_gpu_base` and refuses while that is 0. `hv_agx_retained_platform.c` uses `nwoas_agx_gpu_base/_size` (runtime, from ADT) instead of the constant. The value is read by `nwoas_agx_probe()` from `/arm-io/sgx` `gpu-region-base/-size` (same property names as `src/kboot_gpu.c:376-387`). |
| 2 | IRQ list 563..579 hard-coded, 575-578 stated as fact (`nwoas_agx.h:40`) | List removed from `src/nwoas_agx.h`. The probe reads `/arm-io/sgx` and `/arm-io/gfx-asc` `interrupts` from the ADT and only compares them with the J313 contract set (`HV_AGX_G2_INTERRUPT_ROUTE_VALUES`) for the log. Offline result (section 5): on the saved J274 ADT, sgx = 563,564,565,566,579 and **575-578 belong to gfx-asc** (576,575,578,577). |
| 3 | Dangling references / uncompilable includes | `nwoas_agx.c` and this README now exist. `hv_agx_retained_backing.h:3` includes `nwoas_agx_contract.h`. `-Isrc/agx_shared/include` is added only under `NWOAS_AGX=1` (`Makefile:191`). The J313-launch-dependent `hv_agx_retained_platform.c` is not in the object list and has an `#error` guard (`:12-14`). `nwoas_agx.h` no longer declares the never-written ARM/POWER/STATUS/mmio functions. `nwoas_agx_contract.h` no longer cites a non-existent `P_HV_NWOAS_AGX_ARM`. |
| 4 | License text / notices missing | `m1n1-gpu-hv1/3rdparty_licenses/LICENSE.MIT.windows-on-m1` (byte copy of upstream `LICENSE`, "Copyright (c) 2026 paulsmir"), `m1n1-gpu-hv1/THIRD_PARTY_NOTICES.md` (revisions 882985fb / 6c656732 as recorded by S240, per-file header status, every NWOAS edit). |
| 5 | Wrong citations in the S240 report | Correct ones (in `m1n1-gpu-s240/src/nwoas_agx_mmio.c.orig_ref`, = upstream `hv_agx_power_mmio.c`): gpu-region stage-2 hook `:973` `hv_map_hook(HV_AGX_G2_GPU_BASE, hv_agx_retained_gpu_region, HV_AGX_G2_GPU_SIZE)`; power-broker hook `:1014` at `HV_AGX_G2_POWER_BROKER_BASE` (= 0x300000000, `m1n1-gpu-s240/src/hv_agx_g2.generated.h:16`). Power calls: `hv_agx_power_platform.c:19,25,31,37` (pmgr enable/disable of `/arm-io/gfx-asc`, `/arm-io/sgx`). |
| 6 | Provenance of the copy | Section 1. |

## 3. What is wired, and what the probe does

`Makefile:179-199`: `make NWOAS_AGX=1` adds `nwoas_agx.o` plus the pure-logic copies
(`hv_agx_config_snapshot`, `_firmware_prefix`, `_gpuva_v5`, `_gpuva_v5_mmio`, `_local_reserve`,
`_power_broker`, `_retained_backing`, `_retained_mmio`, `_retained_root`, `_retained_tables`), all compiled
with zero new warnings. **Not built:** `hv_agx_power_platform.c` (pmgr power), `hv_agx_retained_platform.c`
(J313 launch contract, writes the gpu-region RAM), `nwoas_agx_mmio.c.orig_ref` (reference only). The link
uses `--gc-sections`, so nothing unreferenced reaches the image.
`NWOAS_AGX_PROBE=1` (only together with `NWOAS_AGX=1`; `Makefile:196-198` and `src/hv.c:19-22` refuse
otherwise) adds one call at the end of `hv_init()` (`src/hv.c:150-152`). No proxy command was added.

`nwoas_agx_probe()` (`src/nwoas_agx.c:234-368`) reads only the ADT and prints lines starting with
`NWOAS-AGX:`:

- chip_id, board_id, `target-type`, OS/system firmware strings;
- `/arm-io/sgx`: compatible, every `reg` (translated by `adt_get_reg`), `interrupts`, `perf-state-count`,
  `perf-state-table-count`, `gpu-num-perf-states`, `gpu-perf-base-pstate`, `gpu-power-sample-period`,
  `perf-states` length, and `gpu-region`, `gfx-handoff`, `gfx-shared-region` base/size with a 16 KiB alignment
  and inside-DRAM check (`ram_base`, `mem_size_actual`);
- `/arm-io/gfx-asc`: compatible, `reg`, `interrupts`;
- `/chosen/carveout-memory-map`: the region-id entries that overlap the three GPU ranges, the largest entry
  (normal-DRAM owner candidate) and the entry count.

It never calls pmgr, never installs a hook or mapping, never reads SGX/ASC MMIO, never reads or writes the
gpu-region RAM. Gates (`:326-353`, first failure is logged as `REFUSED:`): T8103, board-id 0x22, sgx reg[0],
sgx interrupts (≤32), gfx-asc reg[0], the three ranges present + 16 KiB aligned + non-overlapping. Only on
success are `nwoas_agx_gpu_base/_size`, `_handoff_*`, `_shared_*`, `nwoas_agx_sgx_irqs[]` set; otherwise they
stay 0, which also keeps `hv_agx_config_snapshot_validate()` refusing.

Board id 0x22 is an identity check (`../../j274-board-profile.json` ID-2), not a hardware address.

## 4. Build and binary comparison

Command (same as `../../../nwoas_scripts/usb-s233/README.md` (b), each variant in a fresh copy; script
`tools/buildvar.sh`):

```
env RUSTUP_TOOLCHAIN=1.88.0 CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=2 \
    M1N1_VERSION_TAG=v1.0.2-1474-gbddf7f06-g240O \
    RUSTFLAGS="--remap-path-prefix=$HOME/.rustup/toolchains/1.88.0-aarch64-apple-darwin/lib/rustlib/src/rust=/rustc/6b00bc3880198600130e1cf62b8f8a93494488cc" \
    nice -n 19 make TOOLCHAIN=/opt/homebrew/opt/llvm@20/bin/ LLDDIR=/opt/homebrew/opt/lld@20/bin/ USE_CLANG=1 \
    EXTRA_CFLAGS='-DNWOAS_NVME_MAX_BLOCKS=256 -DNWOAS_NVME_REASSERT_US=50 -DNWOAS_FL_RING_FULL_CLEAN=1' \
    NWOAS_AGX=1 -j4
# P: tag ...-g240P and add NWOAS_AGX_PROBE=1.   N: tag ...-g240N, no NWOAS_AGX.
```

Tags keep the S233 length (`g240N/O/P` vs `s233B`; no other binary in `nwoas_scripts` uses `g240`).
Every build: rc=0, the same 43 compiler warnings as S233b plus the 2 Makefile warnings (shifted line
numbers). Reproducibility: S233b rebuilt in a fresh copy = shipped `d2eb62b5…`; P built twice, identical.

Comparison with S233b (`nwoas_scripts/usb-s233/tools/bindiff.py` + `tools/maskanon.py` + `tools/tailcheck.py`,
run with `m1n1_windows/.venv-hv/bin/python3 -I`):

| Item | O (probe off) | P (probe on) |
|---|---|---|
| Differing bytes in .bin | 8 (two copies of the version tag) | layout shift, see below |
| Functions only in this build | none | `nwoas_agx_probe` (0xd00), `agx_print_regs` (0x2a8), `agx_read_region` (0x208), and 3 existing Rust ADT exports now linked: `adt_first_property_offset` (0xd0), `adt_get_property_count` (0x8c), `adt_getprop_by_offset` (0x4c) |
| Existing functions with real code change | 0 | 1: `hv_init` +1 insn `bl nwoas_agx_probe` (0x190 -> 0x194) |
| Existing functions differing only by moved anonymous data addresses | 0 | 224 |
| .data / bss-region file bytes | identical | identical (moved by 0x4000) |
| New relocations | 0 | 12: 8 GOT entries for the `nwoas_agx_*` globals, 1 for the `refusal` string, 3 for a Rust panic location string |
| Image size | 2162688 | 2179072 (+0x4000: `_text_size`, `_data_start`, `_bss_start`, image end all +0x4000) |

Outputs: `out/bindiff-s233b-vs-g240{N,O,P}.txt`, `out/masked-s233b-vs-g240P.txt`,
`out/tail-s233b-vs-g240P.txt`, `out/build-*.log`, `out/src-s240-vs-hv1.diff`.

## 5. Offline check on a saved J274 ADT (no hardware)

`tools/adt_probe_offline.py` / `tools/adt_carveouts.py` on `experiments/tahoe-afk-20260906/current.adt`
(sha256 `466ef47d…51e57`; `d6-adt.bin` gives the same values). Output: `out/adt-offline-current.txt`,
`out/adt-carveouts-current.txt`. FACT for that dump (J274, board-id 34 = 0x22, chip 0x8103, system firmware
mBoot-18000.121.3):

| Field | Value |
|---|---|
| sgx reg[0] / reg[1] | 0x204000000 + 0x4000000 / 0x204d00000 + 0x20000 (reg[0] equals the J313 contract aperture) |
| sgx interrupts | 563, 564, 565, 566, 579 |
| gfx-asc reg[0] / reg[1] | 0x206400000 + 0x6c000 / 0x206050000 + 0x8 |
| gfx-asc interrupts | 576, 575, 578, 577 |
| gpu-region | 0xbfffb8000 + 0x4000 (= carveout region-id-8) |
| gfx-handoff | 0xbfff70000 + 0x4000 (= region-id-56) |
| gfx-shared-region | 0xbfff78000 + 0x40000 (= region-id-52) |
| Largest carveout | region-id-24 = 0x801504000 + 0x3dfafc000 (same id J313 upstream treats as normal DRAM) |
| perf-state-count / table-count / gpu-num-perf-states / gpu-power-sample-period | 7 / 1 / 6 / 8 (equal to the J313 constants) |
| gpu-perf-base-pstate | **missing** |
| DRAM | 0x800000000 + 0x400000000 |

HYPOTHESIS: the Windows-side boot (OS firmware 13.5 stub) gives the same values. Only gpu-region 0xbfffb8000 and
handoff 0xbfff70000 are confirmed for that boot (J274 13.5 log, `../../j274-board-profile.json` GPU-2). The
probe run in step 2 settles it. Consequence already visible: upstream `hv_agx_config_snapshot_from_adt()`
requires `gpu-perf-base-pstate`, so on this ADT it would refuse (fail closed).

## 6. Hardware check for the main session (not done)

Only P calls the probe. Existing launchers pin the HV size (2162688) and hash (e.g.
`nwoas_scripts/usb-s233b-guest-test.sh:30-31`), so they refuse P as is. A new launcher copy is needed that pins
P's size/hash; nothing else changes. HYPOTHESIS to check first: the 16 KiB larger image does not collide with
the payload/heap placement used by the S231 layout (m1n1 places them after `_end` at run time).

Expected log (pass = probe ran and refused or passed with real values; nothing else changes):

```
NWOAS-AGX: probe start (step 1, read-only: ADT reads only; no power, no map, no MMIO)
NWOAS-AGX:   sgx interrupts (5): 563 564 565 566 579
NWOAS-AGX:   sgx gpu-region = 0x... size 0x... (16K-aligned, inside DRAM ...)
NWOAS-AGX:   carveout region-id-8 = 0x... [gpu-region]
NWOAS-AGX: probe OK: gpu-region ... Step 1 is inert: nothing powered, mapped or hooked.
```

Then the guest must boot exactly like S233b (same USB/NVMe/display behaviour). A difference there would mean
the size change matters, not the probe (which only reads). A Rust panic from `adt_first_property_offset`
(`unwrap`) is possible only on a corrupt carveout node; it would stop m1n1 before the guest starts.

## 7. Step 2 needs

1. **Probe output from the real boot path** (P binary, 13.5 stub): record all `NWOAS-AGX:` lines. They fix
   gpu-region/handoff/shared, sgx + gfx-asc interrupts, the normal-DRAM region id and the perf properties.
2. **Interrupts**: the J313 contract's 9 IRQs = sgx 5 + gfx-asc 4 on the saved J274 ADT. Our HV delivers AIC
   IRQs identity-mapped (vINTID == hwirq, `src/hv_exc.c`), so the J274 AGX0 ACPI device should list the ADT
   numbers. The driver side mentions IRQ 889 and HVC 0x4d31/0x4d32 (`../driver/README.md:125`); those are
   not in the copied HV files and must be ported from upstream separately.
3. **Replace J313-only gates**: `hv_agx_retained_platform.c:469` (board 0x26), the J313 perf constants in
   `hv_agx_config_snapshot.c:19-23` (and `gpu-perf-base-pstate` being absent on J274), the J313 launch contract
   (`hv_launch_j313.h`, `hv_autonomous_layout.generated.h`) used by `hv_agx_retained_platform.c`; then remove
   its `#error`.
4. **Guest RAM description**: `nwoas_agx_contract.h` needs a source on J274 (planned: a new proxy command
   from the launcher with the S231 guest RAM layout).
5. **Power**: port `hv_agx_power_platform.c` behind the probe result (pmgr `/arm-io/gfx-asc`, `/arm-io/sgx`),
   only after 1-4.
6. **Hooks**: gpu-region hook (orig_ref:973) at `nwoas_agx_gpu_base`, and a J274 IPA for the power broker
   (0x300000000 is outside J274 `/arm-io` ranges, `../../j274-board-profile.json` GPU-3; check it is free in
   the S231 guest layout).
7. **Big state at run time**: the gpuva_v5 broker state (~9-10 MB in upstream) must be allocated after the
   gates pass, not static, or the HV image and payload placement move.
8. **Shared driver files**: point `src/agx_shared` at the patched J274 copy from `../driver/` and build with
   the same `APPLE_AGX_BOARD_J274=1` (`../driver/REVIEW-20261115.md`, HV coupling item).
9. **VMID**: our VTTBR has VMID 0 (`src/hv_vm.c:276`); whether that matters for the GPU TLB is still upstream's
   HYPOTHESIS.

## 8. Open questions

- Does the 13.5 stub ADT match the saved Tahoe-firmware ADT (section 5)? Only the probe run can answer.
- Is a 16 KiB larger HV image safe for the current launcher/payload layout? Not checked on hardware.
- Upstream revisions 882985fb / 6c656732 were not re-verified with git (untrusted clone, read-only).

## Installs

None.
