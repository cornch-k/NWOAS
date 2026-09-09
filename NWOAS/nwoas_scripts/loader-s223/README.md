# S223 — deterministic FD tail candidate

The S216 FD supplies0x1d88000 bytes while its Image header requests0x1e00000.
S215 inline loading copies the header length; host load_raw places subsequent
allocations immediately after the payload. The final0x78000 bytes of that
copy therefore came from outside the supplied FD. See the source-only Claude
review; no neighbouring firmware or machine data was read for this analysis.

`build_candidate.py` appends480KiB of0xff, following FDF ErasePolarity=1.
Every existing source-payload byte is unchanged, the copy span is fully
supplied, and the result is reproducible. The reason the original build emits
a shorter file has not been established; file length alone does not prove
the generator's trimming mechanism. This is not an attributed watchdog cause.

Hardware status is in `manifest.json` and any subsequent hardware result files.
Until qualified, the S216 baseline remains the measured configuration.
The15-minute mixed test has a distinct completion marker and16 samples,
checked with an explicit900-second minimum span; it is not a30-minute test.

Final bounded qualification: eight-core integration and 256 MiB full-word integrity, 10 GiB memory across three passes, 16 valid mixed samples over 903.888258 seconds, and 2247 active reads over 300074 ms all passed. The active-read median was 126125 us and maximum 974580 us; the outlier is unexplained. This is one S223 boot, not long-term stability proof. No Cinebench was measured on S223.
