# Companion changes through S93

These are source changes against the exact upstream commits recorded in
`manifest.json`, not replacement upstream repositories. They include accumulated
Tahoe/DCP, vGIC, USB input, ANS2 and S93 NVMe changes needed by the local session.
Original source licenses remain applicable; upstream license files are retained.

For each component, use a clean checkout at `base_commit`, run
`git apply --check /absolute/path/to/changes.patch`, then apply that patch and copy
its `added/` contents into the component root. Initialize Project Mu submodules at
the recorded commits and apply their separate patch entries. Do not apply these
again to the already modified live workspace.

All four patches passed `git apply --check --cached` against temporary indexes
loaded from their recorded base commits. This checks patch applicability; it does
not imply that a clean machine has all build tools or local firmware/media assets.
The S93 runtime still uses machine-specific paths and pre-existing local payloads;
see the handoff for exact binary hashes and build/reconstruction commands.

No upstream remotes were pushed. No secrets, Windows media, raw RAM/disk captures
or large runtime logs are included. `manifest.json` identifies every added source.
