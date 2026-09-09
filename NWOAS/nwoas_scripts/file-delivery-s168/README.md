# file-delivery-s168 — isolated host artifact channel

Pure, self-contained delivery channel that hands a **single, explicitly
registered, immutable `bytes` artifact** to the guest over the existing NS2
host-RAM image, using an unused pair of blocks in the pre-FAT gap. Built from
the S168 review in `../claude-s156/opus-s168-delivery-result.json`.

This directory contains **only host-side pure logic and its tests**. It does
not load any file, touch hardware, open any device, or import/reload the live
`transport.py` / `readonly_namespace.py` modules. Wiring it into a running
controller is the main agent's job (see *Adapter integration* below).

## Files
- `channel.py` — `HostArtifactChannel`, a pure request/response state machine.
- `test_channel.py` — 37 unit tests (`python3 -m unittest test_channel -v`).
- `README.md` — this file.

## Block map (inside the transport-s123 NS2 pre-FAT gap)

| LBA | role | direction | size |
|---|---|---|---|
| 127 | request port | guest **write**, exactly 1 block | 4096 B |
| 128 | existing job/stdout mailbox | **unchanged, never referenced** | 4096 B |
| 129…144 | response window | guest **read**, ≤ 16 blocks | 64 KiB |

The window is 16 blocks because that is the Identify `mdts=4` (64 KiB) ceiling,
so a single guest read can fetch a whole chunk. `overlay()` only ever rewrites
bytes inside 129…144, so **LBA 128, the FAT partition, the NS capacity, and NS1
are all preserved** — nothing in this channel can reach the SSD or NS1 (the
whole thing is NS2 host RAM, and this class holds no backend reference at all).

## Wire format

**Request (4096 B, written to LBA 127):**

```
0    16  magic   b'NWOAS-S16x-REQ !'   (asserted 16 bytes at import)
16   16  token   boot session token (must match)
32    4  id      u32, >=1, strictly increasing
36    8  offset  u64  (0xFFFF_FFFF_FFFF_FFFF => manifest request)
44    4  length  u32  requested payload bytes
48    4  crc32   over bytes[0:48]
52 …4044 MUST be zero (validated)
```

**Response window (start of LBA 129); 128-byte header then payload:**

```
0    16  magic       b'NWOAS-S16x-FILE!'  (asserted 16 bytes at import)
16   16  token
32    4  id           echo of request id
36    4  status       0=data 1=final-short/EOF 2=no-request 3=manifest
40    8  file_size    total artifact size
48    8  offset       echo of request offset
56    4  length       valid payload bytes in this chunk
60    4  payload_crc32 over payload[0:length]
64   32  sha256       full-artifact digest (constant)
96   28  reserved     zero
124   4  header_crc32 over header[0:124]  -- protects ALL metadata above
128 …    payload
```

Both a **header CRC** and a **payload CRC** are present, so every metadata field
(id, status, size, offset, length, sha256, payload_crc) is integrity-protected
in addition to the payload bytes themselves. Max payload per chunk =
`65536 − 128 = 65408` bytes.

## Guarantees (and where they are tested)

- **Single immutable artifact.** `register()` may be called once; it copies the
  bytes, so later mutation of the caller's buffer cannot change what is served.
  No path ever comes from the guest — the request carries only `{id,offset,length}`.
  *(Registration, `test_source_mutation_does_not_leak`.)*
- **Pure, deterministic reads.** A response is a pure function of the latched
  request and the frozen artifact and is cached as immutable bytes. Repeated,
  partial, and arbitrary overlapping reads of 129…144 are byte-identical and
  equal to the matching slice of the full window. *(OverlayReads.)*
- **Strict validation, no silent clamping.** Bad magic/token/CRC/non-zero tail,
  wrong block length, `id==0`, `length==0`, `length>cap`, and `offset>file_size`
  are all rejected and **leave channel state unchanged**. A short final chunk
  near EOF is the defined `status=1` "final-short" outcome, not a rejection;
  `offset==file_size` yields an empty final chunk. *(StrictValidation,
  CapacityAndBoundaries.)*
- **Monotonic ids with idempotent retry.** A new request must have `id` strictly
  greater than the last accepted id, except that re-sending the exact same
  `(id,offset,length)` block is idempotent (accepted, response unchanged). A
  replayed stale id after advancing is rejected. *(MonotonicAndIdempotent.)*
- **Manifest request.** `offset==UINT64_MAX` (with `length==0`) returns
  `status=3` carrying `file_size` + `sha256` so the guest learns size and digest
  over the channel itself. *(ManifestRequest.)*

## Adapter integration (done separately by the main agent)

`HostArtifactChannel` is designed to drop into a `LinkNamespace` subclass with a
few lines. It exposes the geometry as class attributes (`REQ_LBA`, `REQ_BLOCKS`,
`RESP_LBA`, `WINDOW_BLOCKS`, `PAYLOAD_CAP`, `SENTINEL`) and two helpers,
`is_request_write(lba,n)` and `intersects_window(lba,n)`. Sketch:

```python
class DeliveryLink(LinkNamespace):
    def __init__(self, image, directory, artifact):
        super().__init__(image, directory)
        self.chan = HostArtifactChannel(self.token).register(artifact)

    def read(self, lba, n):
        out = super().read(lba, n)            # keeps the LBA-128 frame() overlay
        if HostArtifactChannel.intersects_window(lba, n):
            out = self.chan.overlay(lba, n, out)
        return out
    # in io()'s write branch, before the WRITE_TO_RO guard:
    #   if HostArtifactChannel.is_request_write(lba, n):
    #       return Result(SUCCESS if self.chan.handle_request(data) else INVALID_FIELD)
```

- `super().read()` is applied first, so the existing LBA-128 mailbox `frame()`
  overlay is untouched and the delivery window is layered on top only for
  129…144.
- Command serialization is provided by the existing single-threaded dispatch
  (`Controller` MMIO path and the S150 fastpath), so the two-command
  write-127 → read-129 rendezvous needs no extra locking here.
- File loading, size/SHA assertions at bring-up, and the guest EXE are added by
  the main agent; this module intentionally does none of that.

## Scope / safety notes

- **Do not integrate during the live S163 soak.** This channel adds no NVMe
  namespace and no new Windows-visible disk (it reuses the guarded NS2 RAM), but
  wiring it still means editing the live read path — leave it out until the soak
  completes.
- **Transfer size claims.** Do not assume NS2 supports a 1 MiB transfer just
  because the old read-only Identify advertises a particular MDTS; the runtime
  may override `MAX_TRANSFER`. The window here is fixed at 16 blocks (64 KiB),
  the documented `mdts=4` ceiling, and does not depend on any larger MDTS.
- Registering a large artifact costs host (Mac) RAM for the frozen copy; it is
  unrelated to the guest's memory budget.
```

## Main integration (2026-09-10, offline)

`adapter.py` wraps the already initialized LinkNamespace by composition. This
avoids the bound `read_block=self.read` pitfall: reads first use the existing
backend validation, then overlay response bytes. The only new write accepted
is exactly one block at LBA127; all other writes retain original guards.
`module.py` is explicit opt-in, preboot only, registers the known official ZIP
by pinned length/SHA, and replaces only NamespacePair.link. Current live modules
have not been reloaded. 43 channel+adapter tests pass. Client and hardware
round-trip validation are still pending.

The 64KiB response is our chosen transfer size, not a claim that current global
MDTS remains 64KiB: the deployed NS1 controller advertises a larger limit.
