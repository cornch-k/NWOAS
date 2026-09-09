# S215 — keep the Project Mu image beyond its fixed handoff copies

Dedicated NWOAS guest m1n1 build from `bddf7f06`, the S197 compatibility patch,
`placement.patch`, and `handoff_layout.h`. This is not a general upstream
m1n1 allocator change. It applies to the T8103 Project Mu payload configuration.

S204 and S211 both entered Project Mu at `0x840000000`, overlapping its fixed
BootArgs destination at `0x840000000` and ADT destination at `0x840004000`.
Both failed with PC `ffffffffffffffff` / ESR `8a000000` before Windows. S211
used the old 256.875MiB Image header, so the header reduction is not by itself the
cause. S203's one successful source-prefix boot cannot prove safe placement
across varying loader heap addresses.

The guarded build always copies the kernel beyond the aligned end of the
handoff region and beyond the current monotonically advancing heap. It rejects
an overlapping live m1n1 prefix, wrapped lengths and insufficient guest RAM.
All later monotonic allocations remain beyond the copies too. For the observed
ADT size `0x5c000`, the reserved copy range ends at `0x840060000`; a kernel
which would have landed at `0x840000000` moves to `0x840200000` (2MiB aligned).

`test_layout.c` compiles the exact policy header and passes ASan+UBSan on
196,608 placements plus observed collision, prefix overlap, integer overflow
and RAM exhaustion cases. `build.sh` uses pinned local LLVM/LLD20.1.8 and
Rust1.88.0, verifies vendored sources and restores tracked worktree changes.
The helper header remains in the dedicated worktree as an untracked build
input. No main checkout or installed boot object is replaced by this build.

Prefix SHA256: `d91700ccbe277461513ca9d8320263bf41fc33da8cb814ba095d0f857d02e334`.
`manifest.json` pins the original-S192-FD carrier for an isolated placement
trial. S216 additionally combines this prefix with a source-built 30MiB-header
FD and explicit UEFI handoff reservation. Hardware results are recorded
separately; build and range tests alone do not establish boot compatibility.
