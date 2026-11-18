#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# NWOAS S256: write the quiet test WAV for jobs/A3-play.cmd.
# 48 kHz, 16-bit, stereo (the only format the NWAU0001 endpoint offers), 440 Hz at -30 dBFS,
# 1.5 s with 20 ms fades, 0.2 s silence before and after.  With the driver's volume cap
# (amp 0x05 >= 0x30 = -24 dB) the speaker path gets at most about -54 dB; S255 heard -60/-50/-44 dB.
# Usage: python3 -I make_tone_wav.py OUT.wav
import math, struct, sys

RATE, FREQ, DBFS, TONE_S, PAD_S, FADE_S = 48000, 440.0, -30.0, 1.5, 0.2, 0.02


def main(out):
    amp = 32767 * 10 ** (DBFS / 20.0)
    n, fade, pad = int(TONE_S * RATE), int(FADE_S * RATE), int(PAD_S * RATE)
    frames = bytearray(4 * pad)
    for i in range(n):
        g = min(1.0, i / fade, (n - 1 - i) / fade)
        s = int(round(amp * g * math.sin(2 * math.pi * FREQ * i / RATE)))
        frames += struct.pack("<hh", s, s)
    frames += bytes(4 * pad)
    hdr = b"RIFF" + struct.pack("<I", 36 + len(frames)) + b"WAVE"
    hdr += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, RATE, RATE * 4, 4, 16)
    hdr += b"data" + struct.pack("<I", len(frames))
    with open(out, "wb") as f:
        f.write(hdr + frames)
    peak = max(abs(struct.unpack_from("<h", frames, k)[0]) for k in range(0, len(frames), 4))
    print(f"wrote {out}: {len(hdr) + len(frames)} bytes, peak {peak} ({20 * math.log10(peak / 32767):.1f} dBFS)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: make_tone_wav.py OUT.wav")
    main(sys.argv[1])
