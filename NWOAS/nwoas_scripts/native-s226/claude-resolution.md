# S226 review disposition

The independent Claude Code review is preserved in claude-review.md and covers
the earlier source snapshot. These subsequent fixes were validated locally;
no independent re-review of the changed snapshot is claimed.

- F1: Added directed ASan/UBSan queue lifecycles, range/overflow cases and cache
  success/failure transitions. Assertions require 200 range calls and 300 cache
  calls; the separate 100,000 malformed cases remain.
- F2: Documented/tested NULL contains as disabling queue creation. An incomplete
  cache callback pair is an API error.
- F3: Added changed bit 2 on successful cache Set, even if the value is unchanged.
  The differential oracle compares this signal and callback counts.
- F4/F6: Added explicit generation-width, ring-cursor, cancellation, IRQ,
  deferred-AER, cache fatal-error and ready-state owner obligations below.
- F5: CC disable now calls the real Controller.write path; 32 generic feature
  commands exercise absent cache callbacks; fuzz toggles flush failure and
  compares set-cache counts; directed PC=0 and CQID=2 cases were added.
- F7: Inherited protocol leniencies remain deliberate compatibility behavior.

Latest local run: 11,714 differential commands, 241 held AERs, 32 generic feature
commands, directed plus 100,000 malformed sanitized cases. The combined
freestanding ARM64 object has no undefined imports. This remains uninstalled;
no live host dependency was removed by S226.
