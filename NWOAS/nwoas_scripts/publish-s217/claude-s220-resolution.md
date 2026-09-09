# Main verification of S220 publication audit

- Added component toolchain versions, pinned FD/DTB, precise gap25/gap50 flags
  and hashes, public fork URLs and recipe paths to the companion manifest.
- Verified both guest and Mu base commit objects through GitHub API. Local
  remote names called `fork` do not imply that repositories are private.
- The build scripts are shipped elsewhere in the same public source tree;
  companion README now links their repository paths and workspace assumptions.
- Explicitly retained the unproven DTB source provenance and required-artifact
  limitation. Do not claim an independently self-contained payload build.
- S161 NwoasGuestRam.h is already included in the Mu patch; documented that.
- Updated S214 completion, configuration hashes and unexplained active-read
  outlier. De-duplicated and labelled plain/sanitized S209 test evidence.
- S215 completed short integration and five-minute active reads after the
  review snapshot. S216 and S218 hardware results remain separate evolving files.
- Benchmark score is a single S196 result under host EL2 mediation; no same-Mini
  macOS baseline or driver completion claim is made.

Subsequent S221 result: recovered DTS now rebuilds the exact static FDT. The
full S216 pipeline was rerun with that source input and reproduced the same
payload. Thus the local required-prebuilt-DTB limitation has been removed;
the historical upstream revision remains unknown and is documented separately.
