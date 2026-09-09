# S169 fixed installation-source backup

Offline built, not yet deployed. Transfers ONLY C:\S117SRC\install.swm to the
fixed X31 `file-export-s168/verified-backup/install.swm` destination. Expected
3987720636 bytes and original known SHA are pinned in both host and client.
No arbitrary guest paths, no NS1 raw writes, no source deletion. Exclusive
new partial output; no-clobber final link after exact size and full SHA match.
Final durability fsync and independent hash run on host after guest completion,
outside synchronous NVMe rendezvous (`verify_backup.py`). Keep source until then.

64KiB writes only LBA160..175, read-only ACK176, existing PORT128 and FAT256+
unchanged. Header+payload CRC, session token, sequence, offset, expected length,
known full digest, idempotent exact retry, partial preservation on failure.

11 host receiver/adapter tests pass, including real PRP-list 64KiB command.
Actual C frame/ACK helpers compile on host and interoperate with Python receiver.
Native ARM64 no-CRT executable builds with zero warnings. Hardware unverified.
The main agent implemented client after Claude Opus4.8 declined that subtask;
no rewording or model fallback was used to retry the declined request.

`memory-s161-upload-guest-test.sh` adds the inactive upload channel and two RAM
FAT tools to S161 control. Test S161 layout and hardware before queuing backup.
No host artifact download channel is loaded by this launcher. It must not be
reused after a backup/partial exists without deliberately handling that output.
