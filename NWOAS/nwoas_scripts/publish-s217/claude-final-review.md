# Final evidence-only review (S216 / S218 / S221 / S223 / S217 companion)

**Time-of-review snapshot: 2026-09-09 23:40:51 UTC (2026-09-10 08:40 +09:00).**
At this instant the S223 15-minute mixed soak was reported as still running. Nothing
below covers its outcome; main will record that result separately. This review is
read-only: no hardware was touched, no builds were run, no raw logs or dumps were
opened, and no existing file was edited. Only this file was written.

Files read: `bench-s218/result.json`, `bench-s218/cinebench-evidence.txt`,
`firmware-s216/hardware-result.json`, `firmware-s216/repeat-integration.json`,
`firmware-s216/soak-result.json`, `firmware-s216/source-dtb-rebuild-result.json`,
`dtb-s221/NOTICE.md`, `loader-s223/manifest.json`, `loader-s223/hardware-result.json`,
`publish-s217/companion/manifest.json`.

## Required checks

| Check | Result | Evidence |
|---|---|---|
| 1888.454 score is assigned to S216, not S223 | PASS | `bench-s218/result.json` payload string is "S216 guarded source-built firmware"; `payload_sha256` is `0ae1cb75…` which equals the S216 payload hash in `firmware-s216/*` and the companion `uefi_payload_sha256`. It differs from the S223 payload `2343f7fc…`. Limitation line: "Measured S216, not the subsequent S223 tail-padding candidate." The `log` field points to the S216 handoff session `074136.hXAU0A`, the same session as `repeat-integration.json` and `soak-result.json`. |
| -0.883% vs S196 is not called an improvement | PASS | `score_change_percent` is -0.8831768…; arithmetic (1888.454 − 1905.281) / 1905.281 checks out. The only wording is the limitation "One sample per configuration; the small score difference is not a causally isolated performance change." No "improvement", "regression", or "faster/slower" claim appears in any reviewed file. |
| Same-Mini macOS parity is unmeasured | PASS | `bench-s218/result.json` limitation: "No matching same-Mini macOS baseline measured." No reviewed file asserts parity. |
| Native host-independent drivers not claimed | PASS | `result.json` limitation: "Host EL2 mediation and runtime service transport remain." Companion `hardware_status`: "no standalone/native-driver claim." The three hardware results describe `native_p12` as P-state selection "no host P-state writes", which is a narrower claim and is not a driver-independence claim. |
| S223 short integration and 10 GiB x3 passed | CONFIRMED | `loader-s223/hardware-result.json`: status PASS, 8 CPUs, 33554432 words verified, `memory_test` 10737418240 bytes x 3 passes with acknowledged exit 0, S223 payload `2343f7fc…`, HV gap50 `f801c5be…`. Qualification text is "Short integration only; see separate active/soak results." No S223 active/soak result file exists yet, which matches "soak currently active". |

## Concrete contradictions and missing qualifiers

1. **Stale status in `loader-s223/manifest.json`.** The manifest still reads
   `"status": "SOURCE-ASSEMBLY-ONLY; HARDWARE-UNVERIFIED"` and its note says "no
   hardware qualification yet", while `loader-s223/hardware-result.json` records a
   PASS for the same payload hash `2343f7fc…`. The two files contradict each other as
   of this snapshot. When main records the soak, the manifest status should be updated
   to state exactly what passed (short integration + 10 GiB x3, plus the soak result)
   and what remains unmeasured (no Cinebench on S223).

2. **Companion manifest describes S216 only.** `publish-s217/companion/manifest.json`
   exports `uefi_payload_sha256` `0ae1cb75…` (S216) and `hardware_status` mentions only
   S215/S216/S218. It does not mention S223 or its tail padding. This is consistent, not
   contradictory, but if the release intends to ship or reference S223 the manifest
   needs an explicit line saying S223 is a separate padding candidate with its own
   (partial) qualification, or an explicit line that the companion is S216 only.

3. **Soak timing caveat is present but only in two places.** `soak-result.json` and
   `source-dtb-rebuild-result.json` both disclose that a two-job host source build ran
   during part of the 30-minute mixed soak, so soak timings are not an isolated
   performance comparison. The companion `hardware_status` cites the "30-minute mixed
   soak" without that caveat. Missing qualifier, not a contradiction.

4. **Cinebench metadata.** `cinebench-evidence.txt` reports "CPU Speed (MHz) 30.000"
   and "Processor: Not Specified". `result.json` already carries the limitation that
   CPU MHz 30 is not a measured clock. No further action; noted so nobody quotes the
   30 MHz figure as hardware data.

## Internal consistency spot checks (all consistent)

- Cinebench run window 23:19:09Z to 23:30:12Z on 2026-09-09; the S216 soak's last
  guest sample is 08:18:05 +09:00 (23:18:05Z). The benchmark started one minute after
  the soak ended in the same session, so the two did not overlap.
- Wrapper elapsed 663.5 s vs. render 635.4 s plus 6.3 s preparation: plausible.
- `stdout_sha256` `76073064…` in `result.json` matches the `cb-s218.out` SHA-256 in
  `cinebench-evidence.txt`; `cb-s218.err` is empty (SHA-256 of empty input).
- HV hash `f801c5be…` is identical across bench, S216 hardware/soak, S223 hardware, and
  the companion `gap50` entry.
- `source-dtb-rebuild-result.json` reproduces S216 payload `0ae1cb75…` from source with
  `matches_previous_two_source_builds: true`; S223 manifest lists that same hash as its
  `source_payload_sha256` and `original_bytes_unchanged: true`, so the padding chain is
  traceable.
- S216 `hardware-result.json` (session 073841) and `repeat-integration.json` (session
  074136) are two separate PASS boots with the same payload and HV hashes, matching
  the companion claim of "two S216 Windows boots".
- `dtb-s221/NOTICE.md` and the companion `dtb.provenance` agree: the recovered DTS
  reproduces the existing static FDT, upstream revision unknown, Asahi attribution is
  source-family evidence only. No claim of a match to a specific upstream revision.

## Not reviewed

`firmware-s216/active-result.json` (5-minute active reads cited in the companion
`hardware_status`) was not in the requested list and was not opened. The S223 soak
result does not exist yet at this snapshot.

Reviewed by: Claude Code CLI, evidence-only.
