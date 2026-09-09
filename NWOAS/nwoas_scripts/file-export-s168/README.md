# S168 binary stdout export preparation

HARDWARE-UNVERIFIED. No export or deletion performed yet.
Existing NWOS captures child stdout by byte count and sends kind2 frames with
CRC/sequence; LinkNamespace appends raw bytes to job-N.log. The first command
checks this on hardware with a known1MiB+37byte vector including NUL bytes.
Do not run source exports unless that raw job log exactly matches the manifest.

Source commands stream only C:\S117SRC\install.swm or install2.swm through
stdout, with bounded buffers. They emit no text on success; failures can put
error text in the file, so verify exit0, exact length, and expected SHA256 on
the host before treating any result as a backup. Check parent/final reparse
attributes first. No source is removed by these commands. Original hashes are
from the verified S124 installation in Sept8 status. Binary backups are private
installation assets; never commit them to GitHub.
