#!/usr/bin/env python3
"""Offline check of choose_timing() in the S241 hook vs the S190 original.

Pure Python: the function is pulled out of each hook with ast and executed
alone, so no m1n1, proxy, DCP or hardware is touched.  Mode list is the one
the attached sink advertised in
nwoas_scripts/logs/usb-s233b-20261115-200823.QXp7BD:277 (same list as
layout-s231-20261115-181021.Uk2agx:189).  VRAM size 22020096 is from the same
log, line 279 ('vram_size').

Run:  python3 -I -B nwoas_scripts/display-s241/test_mode_rank.py -v
"""
import ast
import contextlib
import io
import struct
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
S241 = HERE / "tahoe_dcp_guest_hook.py"
S190 = HERE.parent / "display-s190" / "tahoe_dcp_guest_hook.py"
VRAM = 22020096  # /vram reg[0].size, 0x1500000

# (valid, width, height, refresh<<16, a, b) exactly as logged.
SINK_20261115 = [
    (1, 720, 480, 3928232, 64, 0), (1, 720, 480, 3932160, 64, 0),
    (1, 1280, 720, 3928232, 64, 0), (1, 1280, 720, 3932160, 64, 0),
    (1, 720, 576, 3276800, 64, 0), (1, 1280, 720, 3276800, 64, 0),
    (1, 1920, 1080, 3928232, 64, 0), (1, 1920, 1080, 3932160, 64, 0),
    (1, 1920, 1080, 3276800, 64, 0), (1, 3840, 2160, 1571293, 64, 0),
    (1, 3840, 2160, 1572864, 64, 0), (1, 3840, 2160, 1638400, 64, 0),
    (1, 3840, 2160, 1964116, 64, 0), (1, 3840, 2160, 1966080, 64, 0),
    (1, 3840, 2160, 3276800, 64, 0), (1, 3840, 2160, 3928232, 64, 0),
    (1, 3840, 2160, 3932160, 64, 0), (1, 800, 600, 3932160, 0, 0),
    (1, 640, 480, 3932160, 0, 0), (1, 1024, 768, 3932160, 0, 0),
    (1, 1280, 960, 3932160, 0, 0), (1, 1280, 1024, 3932160, 0, 0),
    (1, 1280, 720, 3932160, 0, 0), (1, 1600, 900, 3932160, 0, 0),
    (1, 1920, 1080, 3932160, 0, 0), (1, 640, 480, 3928232, 0, 0),
    (1, 720, 480, 3928232, 0, 0), (1, 720, 480, 3932160, 0, 0),
    (1, 1280, 720, 3928232, 0, 0), (1, 1920, 1080, 3928232, 0, 0),
    (1, 1920, 1080, 3276800, 0, 0), (1, 1280, 720, 3276800, 0, 0),
    (1, 720, 576, 3276800, 14456872, 0),
]


def load_choose_timing(path):
    tree = ast.parse(path.read_text())
    fn = next(n for n in tree.body
              if isinstance(n, ast.FunctionDef) and n.name == "choose_timing")
    ns = {"struct": struct}
    exec(compile(ast.Module(body=[fn], type_ignores=[]), str(path), "exec"), ns)
    return ns["choose_timing"]


def blob(modes):
    return struct.pack("<I", len(modes)) + b"".join(struct.pack("<6I", *m) for m in modes)


def pick(fn, modes, vram=VRAM, preferred=None):
    with contextlib.redirect_stdout(io.StringIO()):
        raw, chosen = fn(blob(modes), len(modes), vram, preferred)
    assert raw == struct.pack("<6I", *chosen)
    return chosen


class ModeRank(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.new = staticmethod(load_choose_timing(S241))
        cls.old = staticmethod(load_choose_timing(S190))

    def test_original_still_picks_720p(self):
        # Matches the logged '[FACT] selected timing (1, 1280, 720, 3932160, 0, 0)'.
        self.assertEqual(pick(self.old, SINK_20261115), (1, 1280, 720, 3932160, 0, 0))

    def test_s241_picks_1080p60_today(self):
        self.assertEqual(pick(self.new, SINK_20261115), (1, 1920, 1080, 3932160, 0, 0))

    def test_s241_second_call_with_preferred_is_consistent(self):
        # configure_tahoe_display() re-runs choose_timing with (FB_WIDTH, FB_HEIGHT).
        first = pick(self.new, SINK_20261115)
        second = pick(self.new, SINK_20261115, preferred=first[1:3])
        self.assertEqual(first, second)

    def test_s241_falls_back_to_720p_without_1080p(self):
        modes = [m for m in SINK_20261115 if m[1:3] != (1920, 1080)]
        self.assertEqual(pick(self.new, modes), (1, 1280, 720, 3932160, 0, 0))

    def test_4k_filtered_by_vram(self):
        self.assertGreater(3840 * 2160 * 4, VRAM)
        modes = [m for m in SINK_20261115 if m[1:3] == (3840, 2160)]
        with self.assertRaises(AssertionError):
            pick(self.new, modes)

    def test_1080p_filtered_if_vram_too_small(self):
        # Defensive: same VRAM rule as before; 1080p needs 8294400 bytes.
        self.assertEqual(pick(self.new, SINK_20261115, vram=8294400 - 1),
                         (1, 1280, 720, 3932160, 0, 0))

    def test_vram_math(self):
        need_720, need_1080 = 5120 * 720, 7680 * 1080
        self.assertEqual((need_720, need_1080), (3686400, 8294400))
        self.assertLessEqual(need_1080, VRAM)
        # Mu AdtParser.c rounds stride*height up to 16 KiB.
        self.assertEqual(-(-need_1080 // 16384) * 16384, 8306688)
        # Color bars: FB_WIDTH // 8 pixels per bar, one row == stride.
        self.assertEqual((1920 // 8) * 8 * 4, 7680)


if __name__ == "__main__":
    unittest.main()
