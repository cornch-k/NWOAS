# S157 USB-C no persisted transfer segment A/B

Baseline is committed S154 (bddf7f06), not S156. Reverts unproven FL1100 S155/S156 changes and removes only USB-C D83 saved_ctx/saved_dq/live_segment persistence relative to S154. Each doorbell starts from freshly read output-context DQ. This tests stale persisted segment lifetime. It does not prove ownership safety, and can regress the old D82 one-movement symptom if output DQ lags a retired segment. Do not call it a fix without sustained input validation.

Image SHA256: d9eba5645b34ced6d43b8a72cfeba57c2c3f37ec9c2637683d11a5d1e28d14de
S156 source snapshot preserved in hv_vm-before-s157.c. Source remains uncommitted.
