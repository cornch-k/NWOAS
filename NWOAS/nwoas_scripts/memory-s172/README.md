# S172 bounded 8 GiB high-memory candidate

Prepared after S167 booted and Windows reported 8,639,746,048 bytes, eight cores.
Same S161 normalization and S167 descriptor-splitting implementation, changing
only high keep extent from 4 GiB to 8 GiB. Original low 4 GiB window retained.
This is a built candidate only until hardware results are recorded.
It does not include S170 firmware P12; use the already validated late callback.
The embedded S167 log prefix names the splitter; high_keep extent identifies the actual candidate.

Existing split-header/wrapper tests cover arbitrary extents; no new descriptor
algorithm is introduced. Real RAM integrity, USB-A DMA and storage checks must
pass before treating it as usable memory. This does not allocate or resize disks.
