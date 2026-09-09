# S210: source-built guest prefix and UEFI, without the opaque S129 prefix

> Later qualification: the compat payload is byte-identical to the failed
> S204 payload and lacks the S215 placement guard. Do not select it as the
> validated configuration. S216 supersedes it; the head variant remains untested.

This recipe builds the S204 UEFI source (S192 native RTC/P12, eight cores, normalized RAM, plus the corrected 30MiB Image header) and directly assembles it with the source-rebuilt S197 guest m1n1 and pinned J274 DTB. The generated inner builder contains no `m1n1-payload-s129` read. All temporary Mu edits are restored; both variants have recorded restore proofs.

Build the S197 prefix first using `loader-s197/build.sh compat` or `head`, then:

```
NWOAS_GUEST_VARIANT=compat python3 nwoas_scripts/firmware-s210/build_candidate.py
NWOAS_GUEST_VARIANT=head python3 nwoas_scripts/firmware-s210/build_candidate.py
```

The build uses the pinned companion base trees and existing Mu toolchain. Python package versions are in `build-tools.json`; guest loader toolchain/source pins remain in S197. The original S131 script is a code template only: its old prefix assembly is replaced and checked before it can run. Both outputs are 33,177,600 bytes. Components are verified before concatenation.

| Variant | Payload SHA256 | Status |
|---|---|---|
| compat | `548fd6b035e58024fc61969ae2ad14ce5efa5b84a18a503c35a20ba7691bd783` | Identical to S204 candidate; observed placement failure |
| head | `38b0f279a74c19ab51839d68877101dadb00d6449c96d9bcbddb01a9064d598b` | Hardware pending |

The source-built FD hash is `e3407c3687b2b860bc168f050acdfd686ac0fbc73fe5345fc52ebaf86640cb57` in both builds. The compat variant preserves old guest-visible display/USB initialization behavior using the documented S197 patch; head retains tracked HEAD behavior. These are distinct hardware candidates, not interchangeable labels.

This removes an unknown binary input from the build recipe. It does not remove the MacBook runtime service, EL2 mediation, or native-driver work. Successful source construction is not a hardware boot claim. Local setup paths are explicit and must be recreated on another build host.
