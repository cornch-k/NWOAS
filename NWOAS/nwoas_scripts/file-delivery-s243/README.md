# file-delivery-s243: deliver any host files to the installed Windows on the Mac mini

Generalized copy of `../file-delivery-s168` (s168 is left unchanged). The host picks the artifacts
**at launch** (manifest with path + expected SHA-256). The guest client `NWGET243.EXE` writes one
artifact to a target path given in the job, checks SHA-256 and **refuses to overwrite**.
Same safety rule as s168: all channel traffic uses NS2 host-RAM blocks only (request LBA 127,
response LBA 129-144). Nothing reaches NS1 (the SSD namespace).

Launcher: `../display-s243-guest-test.sh`. This is `display-s242-guest-test.sh` plus `-m file-delivery-s243/module.py`
(loaded right after `memory-s180/link_module.py`), six extra sha256 pins, the log prefix
`display-s243` and the banner. With no delivery env var set, the module does nothing (it only logs).

## Use

```sh
cd nwoas_scripts/file-delivery-s243
# 1. manifest (hashes are computed now; module re-checks size+sha at launch and refuses on any change)
python3 -I s243_manifest.py manifests/NAME.json /path/to/package_dir [/more/files ...]
# 2. job (<= 4032 ASCII bytes; about 25 files per job at most)
python3 -I make_job.py manifests/NAME.json 'C:\NWOAS\drop\NAME' jobs/NAME.cmd [--index=0,2]
# 3. restart Windows into S243 with the manifest (restart procedure as display-s242/run.sh:
#    queue layout-s231/jobs/W4-restart.cmd, wait for "LAUNCHER EXIT", wait for both cu.usbmodem…NY1/NY3, then)
NWOAS_DELIVER_MANIFEST=$PWD/manifests/NAME.json caffeinate -dims bash ../display-s243-guest-test.sh > ../logs/launcher-s243-….out 2>&1
#    or a single file: NWOAS_DELIVER_FILE=/abs/file NWOAS_DELIVER_SHA256=<hex>
# 4. once the worker is connected (events.jsonl kind 4):
python3 -I ../transport-s123/queue_job.py <LOG>.link jobs/NAME.cmd
```

The job does four things: (a) it refuses a target path when an existing part of it is a file or a reparse point, and
otherwise creates the directory; (b) it runs `%~d0\NWGET243.EXE <index> <sha256> "<dir>\<name>"` for each file;
(c) it re-hashes every file with PowerShell `Get-FileHash` and prints `S243 READBACK MATCH|MISMATCH`;
(d) it exits 1 on any failure. `--delete-after` is only for throughput self-tests.

The client exits 0 on success. On failure it exits with one of these codes: 2 usage, 3 target or target.part
already exists, 4 not enough free space, 6 NS2 not found, 7 channel/manifest/SHA mismatch, 8 cannot create .part,
9 a chunk failed checks, 10 write error, 11 short transfer, 13 streamed hash mismatch, 14 disk-readback hash
mismatch, 15 rename failed. After any failure past file creation it deletes its own `.part`.

## Design

| Part | What it does |
|---|---|
| `s243_channel.py` | Pure state machine with a frozen list of up to 64 named artifacts. Request = {id, index, offset, length} plus CRC. The guest never sends a host path. Ids only go up, and an identical retry is accepted again. The response header carries index, count, size, sha256, a header CRC and a payload CRC. A manifest request returns the artifact name as its payload. A window read before any request gives the last accepted id, so every client run can continue the id sequence. |
| `s243_adapter.py` | Wraps the live `LinkNamespace` (copy of the s168 adapter). The only new write it accepts is exactly 1 block at LBA 127. Reads that overlap LBA 129-144 get the response window laid over them. Every other command goes to the original link with its original guards. |
| `s243_fat.py` | Adds `NWGET243.EXE` to the NS2 RAM FAT (D:) before the guest boots. It continues `transport.fat_image`'s sequential layout. Unit test: `fat_append(fat_image(F), n, d) == fat_image(F + {n: d})` byte for byte, including the real 22-file `memory-s180/link_module.py` set. It refuses non-pristine images. MBR and LBA 0-255 stay unchanged. |
| `s243_manifest.py` | Create and verify manifests. Accepts absolute regular non-symlink files only, with exact size and SHA-256. Total limit is 1 GiB of host RAM. |
| `module.py` | Opt-in through the env vars. Pins the client sha256. Refuses to run unless the link is in preboot state (worker not connected, no job). Writes `<LOG>.link/artifact.json`. |
| `client/` | `nwget243.c` + `wire.h` + `sha256.h`. No CRT, ARM64, built like s168 (`sh build.sh`; lld-link from `PATH`). Raw disk: it opens only the disk identified as NS2 (exact size, MBR signature, LBA128 magic and token), writes only LBA 127 and reads LBA 128/129. Files: `<target>.part` is opened CREATE_NEW. The streamed SHA-256 must equal the job digest, which must equal the host manifest digest. Then it re-reads the file from C: and hashes again, and finally `MoveFileExW` without REPLACE_EXISTING. Targets must be `C:\...`, printable ASCII, with no `..` / `:` / wildcard. |

Tests: `python3 -I test_s243.py` (25 tests: channel, adapter over the real `LinkNamespace`, FAT, manifest,
`module.py` under a fake `hv`, job generator) and `sh client/test_host.sh` (C wire, SHA-256 and argument/path
checks against vectors from `s243_channel.py`, ASan/UBSan). All pass. These are host-only checks.

## Hardware result 2026-11-15 23:37-23:41 (`hw-results-20261115.txt`)

Restart via W4 → `display-s243-guest-test.sh` with 4 artifacts (index 0-2 = `manifests/gpu-j274-kmdonly.json`,
index 3 = random 128 MiB self-test blob). The worker connected 40 s after launch.

| Artifact → target | Bytes | SHA-256 (host manifest = client = Get-FileHash) | Result |
|---|---|---|---|
| AppleAgxRenderAdmission.kmdonly.inf → `C:\NWOAS\drop\gpu-j274\` | 2077 | 24e5a1a047e94f7cb435949c04468288648b08da1db5aa07f0461c177938ef44 | MATCH |
| AppleAgxRenderAdmission.sys → same | 638080 | 1965872e848383311793a6383ca289748c42e50b3ff27a2c0798d9a4e70ee580 | MATCH |
| appleagxrenderadmission.cat → same | 2741 | 7e8f980d7e4436b5355f155610524af23025f16755e5d1334b6a528edcc1ce5a | MATCH |
| s243-selftest-128m.bin → `C:\NWOAS\drop\s243-selftest\` (removed after the check) | 134217728 | 5237b423066680210d049c299d2e5d22bae4af74b3941fc897bcfe68cbac537e | MATCH (twice) |

| Timing (host clock) | Value |
|---|---|
| G1 job, 3 package files incl. 3 PowerShell steps, queue → complete | 6 s |
| 128 MiB, manifest line → 100 % progress (T1 re-run) | 31.9 s = 4.2 MB/s (4.0 MiB/s) |
| 128 MiB whole job incl. client readback hash (T1) | 34.3 s |
| 128 MiB G2 job incl. PowerShell dir check, Get-FileHash, delete | 42 s |

The client's own `transfer_ms`/`readback_ms` (2860/1218 ms; T1: 2234/797 ms) are **not** usable: in T1, Windows
`%TIME%` advanced 3.06 s while the host clock advanced about 33 s. During this transfer, guest timers ran about
10x slow. Only host timings are reported above. The reason is not investigated (open).

Refusal checks on hardware (`jobs/R1-refuse-overwrite.cmd`): delivering again onto the existing `.sys` →
`target already exists; refusing to overwrite`, exit 3 (file untouched). A wrong job SHA-256 → `host manifest SHA-256
differs from job SHA-256`, exit 7, stopped before any file was created.

Not done (out of scope): no driver install, no bcdedit/testsigning change, no certificate added.
The three files only sit in `C:\NWOAS\drop\gpu-j274\`.

## PowerShell "no output" in worker jobs: cause

PowerShell output **does** come back through the job channel. Evidence (experiments 2026-11-15, s242 session):
`exit 7` → ERRORLEVEL 7, `Set-Content` wrote a 5-byte file, and `[Console]::Out.WriteLine` / `Write-Output` / `$Host.Name`
(ConsoleHost) / LanguageMode (FullLanguage) all came back. It runs as `NT AUTHORITY\SYSTEM`, session 0, PS 5.1.26100.7920.
The empty D1-D3 results came from the **query**, not the channel:
`Get-CimInstance Win32_VideoController` (and `Get-WmiObject`) return **0 instances** with no error. This machine has
no Display-class device. The framebuffer driver is `ROOT\BASICDISPLAY\0000` "Microsoft Basic Display Driver" in
class **System**. Win32_VideoController lists Display-class adapters only, so `Format-List` printed nothing and
`Out-File` created the 0-byte `C:\Windows\Temp\s242d3.txt` (still present, size 0). So the worker and the job
wrapper do not need a fix. `jobs/P1-powershell-display.cmd` reads the resolution through PowerShell from the
display configuration registry instead. Result on hardware: `active 1920x1080 primsurf 1920x1080` (CCD set
MSNILNOEDID_1414_008D) and `Win32_VideoController count = 0`.

Side note: `Get-PnpDevice -PresentOnly` without a class filter took about 3 min in one run. P1 uses a filtered
`Get-CimInstance Win32_PnPEntity` instead (7 s job). The worker still has no child timeout, so a hung job blocks the channel.

## Verified vs hypothesis

- Verified on hardware: delivery of 4 artifacts with SHA-256 matched by the client stream, client disk readback and
  PowerShell Get-FileHash. No overwrite and wrong-digest refusal. PowerShell output returns. About 4.2 MB/s for 128 MiB.
- Verified on host only: refusal of malformed requests, FAT append equality, manifest/env checks.
- Hypothesis or untested: sizes of several hundred MB (only 128 MiB tested; the 1 GiB host limit is untested); the cause of
  the slow guest timer; whether the readback hash reads the disk or the Windows file cache.
