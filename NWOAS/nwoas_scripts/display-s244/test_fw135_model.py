"""Offline tests for fw135_model.py.  No device.  Uses the two saved Tahoe J274 ADTs.

  cd /Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows
  PYTHONPATH=proxyclient .venv-hv/bin/python3 -B ../nwoas_scripts/display-s244/test_fw135_model.py
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fw135_model as m  # noqa: E402
from m1n1.adt import load_adt  # noqa: E402

ROOT = Path("/Users/angigyeom/Desktop/DEV/NWOAS")
ADTS = [ROOT / "experiments/tahoe-afk-20260906/current.adt",
        ROOT / "experiments/tahoe-afk-20260906/d6-adt.bin"]
# S233b boot_args on Tahoe (logs/display-s243-20261115-233800.LQA3Ng lines 135-145)
PHYS, MEM = 0x801504000, 0x3DFAFC000


class Over:
    """ADT wrapper: override whole nodes by path, pass everything else through."""
    def __init__(self, adt, nodes):
        self._adt, self._nodes = adt, nodes
        self.compatible = adt.compatible

    def __getitem__(self, path):
        if path in self._nodes:
            if self._nodes[path] is None:
                raise KeyError(path)
            return self._nodes[path]
        return self._adt[path]


def chosen(os_fw, sys_fw="mBoot-18000.121.3"):
    return SimpleNamespace(firmware_version=os_fw.encode() + b"\0" * 8, system_firmware_version=sys_fw)


def ba(w=1920, h=1080, base=0xBE3F60000, depth=32, phys=PHYS, mem=MEM):
    return SimpleNamespace(phys_base=phys, mem_size=mem,
                           video=SimpleNamespace(base=base, display=1, stride=w * 4, width=w,
                                                 height=h, depth=depth))


class Model(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.adts = [load_adt(p.read_bytes()) for p in ADTS]

    def fw135(self, adt, **nodes):
        return Over(adt, {"/chosen": chosen(m.OS_FW_135), **nodes})

    def test_sio_model_reproduces_observed_tahoe_skew(self):
        # The pinned 0x34000 was measured on hardware (guest_module.py:50-51). The model must give it.
        for adt in self.adts:
            sio, notes = m.sio_alloc(adt)
            self.assertEqual(sio, 0x2C000, notes)
            r = m.evaluate(adt, ba())
            self.assertEqual(int(r["predictions"]["guest_skew"], 16), 0x34000)
            self.assertFalse(any("unknown key" in n for n in r["notes"]), r["notes"])

    def test_tahoe_adt_fails_only_g1(self):
        r = m.evaluate(self.adts[0], ba())
        bad = [k for k, c in r["checks"].items() if not c["ok"]]
        self.assertEqual(bad, ["G1_os_fw_13_5"])
        self.assertFalse(m.gate_g1(r))

    def test_stub_probe_bug_system_fw_is_str(self):
        ch = self.adts[0]["/chosen"]
        self.assertIsInstance(ch.system_firmware_version, str)
        with self.assertRaises(TypeError):
            bytes(ch.system_firmware_version)   # what stub-135/tool/probe_fw135.py does
        self.assertEqual(m.cstr(ch.system_firmware_version), "mBoot-18000.121.3")

    def test_13_5_same_layout_passes(self):
        r = m.evaluate(self.fw135(self.adts[0]), ba())
        self.assertTrue(r["ok"], m.failed(r))
        self.assertTrue(r["predictions"]["is_1080p"])
        self.assertTrue(m.gate_g1(r))

    def test_dummy_iboot_fb_is_reported_not_gated(self):
        r = m.evaluate(self.fw135(self.adts[0]), ba(640, 1136))
        self.assertTrue(r["ok"], m.failed(r))
        self.assertFalse(r["predictions"]["m1n1_mode_set"])

    def test_vram_moved_breaks_fbprobe(self):
        vram = SimpleNamespace(reg=[SimpleNamespace(addr=0xBE5000000, size=0x1500000)])
        r = m.evaluate(self.fw135(self.adts[0], **{"/vram": vram}), ba(base=0xBE5000000))
        self.assertFalse(r["checks"]["G2_fbprobe_inside_vram"]["ok"])
        self.assertFalse(r["ok"])

    def test_vram_inside_guest_ram_fails(self):
        r = m.evaluate(self.fw135(self.adts[0]), ba(mem=0x3E3000000))
        self.assertFalse(r["checks"]["G2_vram_outside_guest_ram"]["ok"])

    def test_small_vram_means_guest_relocation_and_skew_change(self):
        vram = SimpleNamespace(reg=[SimpleNamespace(addr=0xBE3F60000, size=0x800000)])
        r = m.evaluate(self.fw135(self.adts[0], **{"/vram": vram}), ba())
        self.assertTrue(r["predictions"]["guest_fb_relocates"])
        self.assertFalse(r["checks"]["G2_guest_skew_matches_pin"]["ok"])

    def test_longer_sio_tunables_change_skew(self):
        real = self.adts[0]["/arm-io/sio"]
        fake = SimpleNamespace(**{k: getattr(real, k) for k in
                                  ("segment-ranges", "map-range", "dmashim", "device-type")})
        setattr(fake, "asio-ascwrap-tunables", b"\0" * 0x3000)
        r = m.evaluate(self.fw135(self.adts[0], **{"/arm-io/sio": fake}), ba())
        self.assertEqual(int(r["predictions"]["guest_skew"], 16), 0x38000)
        self.assertFalse(r["ok"])

    def test_isp_present_refuses(self):
        r = m.evaluate(self.fw135(self.adts[0], **{"/arm-io/isp": SimpleNamespace()}), ba())
        self.assertFalse(r["checks"]["G2_guest_skew_matches_pin"]["ok"])

    def test_non_32bpp_needs_headless_override(self):
        r = m.evaluate(self.fw135(self.adts[0]), ba(depth=30))
        self.assertFalse(r["checks"]["G2_video_geometry"]["ok"])
        r = m.evaluate(self.fw135(self.adts[0]), ba(depth=30), headless_ok=True)
        self.assertTrue(r["checks"]["G2_video_geometry"]["ok"])

    def test_guest_fb_need_1080p(self):
        self.assertEqual(m.guest_fb_need(1920, 1080), 0x84C000)
        self.assertLessEqual(m.guest_fb_need(1920, 1200), 0x1500000)

    def test_probe_offline_cli(self):
        with tempfile.TemporaryDirectory() as td:
            j = Path(td, "ba.json")
            j.write_text(json.dumps({"phys_base": hex(PHYS), "mem_size": hex(MEM), "video": {
                "base": "0xbe3f60000", "display": "1", "stride": str(1920 * 4), "width": "1920",
                "height": "1080", "depth": "32"}}))
            env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
            out = subprocess.run([sys.executable, "-B", str(HERE / "probe_fw135_s244.py"),
                                  "--adt-file", str(ADTS[0]), "--ba-json", str(j)],
                                 capture_output=True, text=True, env=env)
            self.assertEqual(out.returncode, 1, out.stdout + out.stderr)   # Tahoe ADT: G-1 not met
            self.assertIn("G-1 NOT MET", out.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
