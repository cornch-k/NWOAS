# S223 review: FD tail over-read in load_kernel (source-only)

Scope: read-only review of `loader-s215/payload-s215.c` `load_kernel()`,
`firmware-s216/s131_builder.py` length checks, `firmware-s216/manifest.json`,
`MacMini2020.fdf`, and `hv/__init__.py` `load_raw()`. No build, no device,
no firmware-content or dump inspection. No source or payload changed.

## Observed behavior (from source, not inferred)

`load_kernel(p, size)` at `payload-s215.c:136` copies the image with:

```
memcpy(new_addr, kernel, size ? size : kernel->image_size);   // line 155
```

For an inline (uncompressed) payload the caller passes `size = 0`
(`load_one_payload` -> `load_kernel(p, size)` at line 247, `size` from the
outer payload walk). With `size == 0` the copy length is
`kernel->image_size`, read from the Image header at bytes [16:24].

Concrete sizes:

| quantity | value | bytes |
| --- | --- | --- |
| FD file length | 0x1d88000 | 30,965,760 |
| Image header `image_size` | 0x1e00000 | 31,457,280 |
| over-read / shortfall | 0x78000 | 491,520 (480 KiB) |

`s131_builder.py:132` asserts `len(fd) == 0x1d88000` and header
`fd[16:24] == 0x1e00000`, so the two values differ by design. The payload
is `prefix + dtb + fd` (`s131_builder.py:133`): prefix 0x20c000, dtb
0x10000, fd 0x1d88000. The FD begins at payload offset 0x21c000 and ends
at 0x1fa4000, which equals the total payload length (manifest `bytes`
33,177,600). The FD is the last component; nothing follows it in the
payload buffer.

Conclusion: with `size == 0` the `memcpy` source runs from the FD start
for `image_size` bytes and therefore reads 0x78000 bytes past the end of
the FD, which is also past the end of the whole payload buffer, into
whatever RAM was allocated after it. The destination is not the issue:
`new_addr` is `heapblock_alloc_aligned(image_size, ...)` (line 151), sized
to the full `image_size`. Only the source is short.

`load_raw()` in `hv/__init__.py:2186` shows the adjacent layout in the
hosted path: `image_size = align(len(image))`, then SEPFW, preoslog, and
BootArgs regions are placed immediately after the image
(`sepfw_off = image_size`, lines 2194-2201). So the 0x78000 over-read
lands in defined, mapped, live neighbouring allocations rather than
unmapped memory. It copies real neighbour bytes into the FD tail; it does
not fault. The copied tail is nondeterministic across boots because it
reflects whatever those neighbouring bytes currently hold.

## Root of the mismatch

`MacMini2020.fdf:31` sets `PcdFdSize = 0x1E00000` and lines 35-36
(`BlockSize 0x4000`, `NumBlocks 0x780`) multiply to exactly 0x1e00000. The
Image `image_size` correctly equals `PcdFdSize`. The 0x1d88000 `.fd`
emitted by the build is the same volume with its trailing free space
trimmed. The header describes the full defined FD; the file is short by
the trimmed free region.

## Watchdog: not attributed

Any watchdog reset or boot hang is out of scope for a source read and is
not established here. The over-read is a deterministic source fact; a
causal link from it to any watchdog event is unproven and should not be
claimed without a boot-time reproduction that isolates it.

## Recommendation: deterministic explicit tail padding

Warranted. Append 0x78000 (491,520) bytes of explicit padding to the FD
so the payload FD region reaches `image_size` (0x1e00000 == PcdFdSize).
This makes the `size == 0` copy read only defined bytes and produces a
byte-identical payload across builds.

Padding value: 0xFF. The trimmed region is FDF free space inside the
volume, which GenFv leaves as erased flash (0xFF). 0xFF reconstructs the
canonical untrimmed FD that the header already claims. 0x00 is equally
deterministic and safe if a zero fill is preferred; the region is free
space and is not interpreted, so either removes the nondeterminism. Prefer
0xFF to match the FD definition.

Do the padding at the payload-assembly stage (append to the FD bytes
before `payload = prefix + dtb + fd`), not by editing the header, since
the header is already correct.

## Bounded validation plan

1. Static: confirm the FD is the payload tail (offset 0x21c000, end
   0x1fa4000 == manifest bytes) and that 0x1e00000 - 0x1d88000 == 0x78000.
   Already confirmed above.
2. Add the 0x78000 0xFF tail pad; adjust the `s131_builder.py:132`
   assertion so the padded length is checked (padded FD length 0x1e00000),
   keeping the header check unchanged.
3. Determinism: build twice, compare `fd_sha256` and `payload_sha256` in
   the emitted `components-*.json`; require equality across both runs.
4. Single hosted boot under the proxyclient/HV path only (no device
   flashing): read guest RAM over [FD_start + 0x1d88000, FD_start +
   0x1e00000) immediately before the `memcpy` and confirm it is now the
   defined pad value, and that the printed
   `NWOAS-S215: handoff [...] image [dest, dest+image_size)` range is
   unchanged from the unpadded build.
5. Stop there. Do not assert any watchdog outcome unless a separate,
   isolated reproduction is run.
