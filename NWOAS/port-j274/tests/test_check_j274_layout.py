"""Offline tests for the J274 draft layout.

Run: cd port-j274 && python3 -I -B -m unittest discover -s tests -v

These tests only exercise arithmetic and file contents. They read, as plain data and read-only:
the reference J313 layout JSON (json.loads; no reference code is imported or run), our saved
S231 boot log, every file in nwoas_scripts/logs (scanned for "Physical memory:" lines), the
extracted ADT facts JSON and the ADT dump bytes (sha256 only). Passing them says nothing about
booting on hardware.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import mmap
import re
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path

PORT = Path(__file__).resolve().parent.parent
N = PORT.parent
sys.path.insert(0, str(PORT / "tools"))
import check_j274_layout as c  # noqa: E402
import extract_adt_facts as x  # noqa: E402  (imports nothing from the proxyclient at import time)

REF_J313_LAYOUT = N / "external/windows-on-m1/config/j313-guest-layout.json"
J274_LAYOUT = PORT / "config/j274-guest-layout.json"
ADT_FACTS = PORT / "config/j274-adt-facts.json"
LOGS = N / "nwoas_scripts/logs"
S231_LOG = LOGS / "layout-s231-20261115-191038.Vc0lr2"
LOW_WINDOW_GAP = 0x34000   # guest_module.py:44 / usb-s174/guest_map.py:14 (code constant)


def load(path):
    return c.decode(json.loads(path.read_text()))


def hexint(m, i=1):
    return int(m.group(i), 16)


def parse_s231_log():
    """Values of the S231 boot, parsed from the saved log (last boot in the file)."""
    text = S231_LOG.read_text(errors="replace")
    pm = list(re.finditer(r"^Physical memory: (0x[0-9a-f]+) \.\. (0x[0-9a-f]+)$", text, re.M))[-1]
    before = text[:pm.start()]
    out = {
        "phys_base": hexint(pm, 1), "ram_end": hexint(pm, 2),
        "devtree_size": hexint(list(re.finditer(r"devtree_size: (0x[0-9a-f]+)", before))[-1]),
        "ba_phys_base": hexint(list(re.finditer(r"phys_base:\s+(0x[0-9a-f]+)", before))[-1]),
        "ba_mem_size": hexint(list(re.finditer(r"mem_size:\s+(0x[0-9a-f]+)", before))[-1]),
    }
    after = text[pm.end():]
    for key, rx in (("guest_start", r"Guest region start: (0x[0-9a-f]+)"),
                    ("kernel_size", r"Loading kernel image \((0x[0-9a-f]+) bytes\)"),
                    ("sepfw_size", r"Copying SEPFW \((0x[0-9a-f]+) bytes\)"),
                    ("tc_size", r"Copying TrustCache \((0x[0-9a-f]+) bytes\)"),
                    ("preoslog_size", r"Copying preoslog \((0x[0-9a-f]+) bytes\)"),
                    ("bootargs", r"Setting up bootargs at (0x[0-9a-f]+)")):
        out[key] = hexint(re.search(rx, after))
    lw = re.search(r"low-window map (0x[0-9a-f]+)\+(0x[0-9a-f]+) -> (0x[0-9a-f]+)", after)
    out.update(low_ipa=hexint(lw, 1), low_size=hexint(lw, 2), low_backing=hexint(lw, 3))
    return out


_NATURAL = None


def natural_bases():
    """Every 'Physical memory: A .. B' line in nwoas_scripts/logs (read-only scan)."""
    global _NATURAL
    if _NATURAL is None:
        pat = re.compile(rb"Physical memory: (0x[0-9a-f]+) \.\. (0x[0-9a-f]+)")
        bases, ends, files = set(), set(), 0
        for f in LOGS.iterdir():
            if f.is_symlink() or not f.is_file():
                continue
            try:
                if f.stat().st_size == 0:
                    continue
                with open(f, "rb") as fh, mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ) as m:
                    if m.find(b"Physical memory: 0x") < 0:
                        continue
                    files += 1
                    for mo in pat.finditer(m):
                        bases.add(int(mo.group(1), 16))
                        ends.add(int(mo.group(2), 16))
            except (OSError, ValueError):
                continue
        _NATURAL = (bases, ends, files)
    return _NATURAL


def run_main(*argv):
    buf = io.StringIO()
    code = c.main(list(argv) + ["--json"], out=buf)
    return code, json.loads(buf.getvalue())


def run_main_err(*argv):
    """-> (exit code, stdout, stderr) for paths that may not print a JSON result."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stderr(err):
        try:
            code = c.main(list(argv) + ["--json"], out=out)
        except SystemExit as e:
            code = e.code
    return code, out.getvalue(), err.getvalue()


def write_json(d, name, obj):
    p = Path(d) / name
    p.write_text(obj if isinstance(obj, str) else json.dumps(obj))
    return p


class KeySet(unittest.TestCase):
    def test_same_keys_as_reference_file(self):
        ref_keys = set(json.loads(REF_J313_LAYOUT.read_text()).keys())
        ours = set(json.loads(J274_LAYOUT.read_text()).keys())
        self.assertEqual(ref_keys, set(c.FIELDS))
        self.assertEqual(ours, set(c.FIELDS))


class RulesMatchReference(unittest.TestCase):
    def test_reference_j313_file_passes_restated_rules(self):
        # Sanity check of the re-statement: the reference's own file must pass.
        self.assertEqual(c.reference_rule_violations(load(REF_J313_LAYOUT)), [])


class ArithmeticMatchesOurLogs(unittest.TestCase):
    """REVIEW defect 4: values are parsed from the S231 log, not typed in."""

    @classmethod
    def setUpClass(cls):
        cls.log = parse_s231_log()

    def test_s231_guest_base_and_bootargs(self):
        g = self.log
        lay = c.assisted_layout(g["phys_base"], g["devtree_size"], g["tc_size"], g["kernel_size"],
                                g["sepfw_size"], g["preoslog_size"])
        self.assertEqual(lay["firmware_base"], g["guest_start"])   # "Guest region start"
        self.assertEqual(lay["boot_args_base"], g["bootargs"])      # "Setting up bootargs at"

    def test_s231_ram_end_and_sizes(self):
        g = self.log
        self.assertEqual(g["ram_end"], g["ba_phys_base"] + g["ba_mem_size"])
        self.assertEqual(g["ram_end"], c.J274_PLATFORM_RAM_END)
        self.assertEqual((g["devtree_size"], g["tc_size"], g["sepfw_size"], g["preoslog_size"]),
                         (c.J274_SIZES["adt"], c.J274_SIZES["trust_cache"], c.J274_SIZES["sepfw"],
                          c.J274_SIZES["preoslog"]))

    def test_s231_low_window_backing(self):
        g = self.log
        self.assertEqual(g["ram_end"] - g["low_size"] - LOW_WINDOW_GAP, g["low_backing"])
        lay = load(J274_LAYOUT)
        self.assertEqual((lay["low_mem_ipa"], lay["low_mem_size"], lay["low_mem_pa"]),
                         (g["low_ipa"], g["low_size"], g["low_backing"]))


class J274Draft(unittest.TestCase):
    def setUp(self):
        self.lay = load(J274_LAYOUT)

    def test_pinned_addresses_derive_exactly(self):
        notes = c.j274_extra_findings(self.lay, c.J274_FW_SIZE_PADDED)
        self.assertFalse([n for n in notes if n.startswith("MISMATCH")], notes)

    def test_unpadded_fd_would_move_boot_args(self):
        notes = c.j274_extra_findings(self.lay, c.J274_FW_SIZE_UNPADDED)
        self.assertTrue(any(n.startswith("MISMATCH boot_args_base") for n in notes), notes)

    def test_only_known_reference_rule_deviation(self):
        bad = c.reference_rule_violations(self.lay)
        self.assertEqual(len(bad), 1, bad)
        self.assertTrue(bad[0].startswith("low_mem_ipa < 0x1000"))

    def test_low_window_size_does_not_fit_uint32_pcd(self):
        notes = c.j274_extra_findings(self.lay, c.J274_FW_SIZE_PADDED)
        self.assertTrue(any("UINT32" in n for n in notes), notes)

    def test_ecam_avoids_real_apcie(self):
        notes = c.j274_extra_findings(self.lay, c.J274_FW_SIZE_PADDED)
        self.assertFalse(any(n.startswith("CONFLICT") for n in notes), notes)

    def test_reference_ecam_would_conflict_on_j274(self):
        lay = dict(self.lay, pci_ecam=0x690000000)
        notes = c.j274_extra_findings(lay, c.J274_FW_SIZE_PADDED)
        self.assertTrue(any(n.startswith("CONFLICT L-6") for n in notes), notes)

    def test_phys_base_above_every_observed_natural_base(self):
        # REVIEW defect 4: the maximum comes from a read-only scan of nwoas_scripts/logs.
        bases, ends, files = natural_bases()
        self.assertGreater(files, 0)
        self.assertGreater(len(bases), 1)
        self.assertGreater(self.lay["phys_base"], max(bases), hex(max(bases)))
        self.assertEqual(ends, {c.J274_PLATFORM_RAM_END})
        self.assertEqual(self.lay["ram_end"], c.J274_PLATFORM_RAM_END)


class ExitCodeFailClosed(unittest.TestCase):
    """REVIEW defect 1: rule violations and PCD findings make the checker fail unless accepted."""

    def test_default_draft_fails(self):
        code, res = run_main()
        self.assertEqual(code, 1)
        ids = {b.split(":")[0] for b in res["blocking"]}
        self.assertEqual(ids, {"L-1", "L-2", "L-8"})

    def test_each_blocker_must_be_accepted(self):
        for missing in ("L-1", "L-2", "L-8"):
            with self.subTest(not_allowed=missing):
                allow = [x for x in ("L-1", "L-2", "L-8") if x != missing]
                code, res = run_main(*sum((["--allow", a] for a in allow), []))
                self.assertEqual(code, 1)
                self.assertEqual({b.split(":")[0] for b in res["blocking"]}, {missing})

    def test_all_accepted_passes(self):
        code, res = run_main("--allow", "L-1", "--allow", "L-2", "--allow", "L-8")
        self.assertEqual(code, 0, res["blocking"])
        self.assertTrue(res["accepted"])

    def test_mismatch_cannot_be_accepted(self):
        code, res = run_main("--fw-size", "0x1d88000", *sum((["--allow", a] for a in sorted(c.ALLOWABLE)), []))
        self.assertEqual(code, 1)
        self.assertTrue(any(b.startswith("MISMATCH") for b in res["blocking"]))

    def test_other_reference_rule_cannot_be_accepted(self):
        with tempfile.TemporaryDirectory() as d:
            raw = json.loads(J274_LAYOUT.read_text())
            raw["cpu_count"] = 4
            p = Path(d) / "bad.json"
            p.write_text(json.dumps(raw))
            code, res = run_main(str(p), *sum((["--allow", a] for a in sorted(c.ALLOWABLE)), []))
        self.assertEqual(code, 1)
        self.assertTrue(any(b.startswith("REF: cpu_count") for b in res["blocking"]))

    def test_reference_ecam_blocks_as_l6(self):
        with tempfile.TemporaryDirectory() as d:
            raw = json.loads(J274_LAYOUT.read_text())
            raw["pci_ecam"] = "0x690000000"
            p = Path(d) / "ecam.json"
            p.write_text(json.dumps(raw))
            code, res = run_main(str(p), "--allow", "L-1", "--allow", "L-2", "--allow", "L-8")
        self.assertEqual(code, 1)
        self.assertEqual({b.split(":")[0] for b in res["blocking"]}, {"L-6"})

    def test_unknown_allow_id_is_usage_error(self):
        with self.assertRaises(SystemExit) as cm:
            c.main(["--allow", "L-99"], out=io.StringIO())
        self.assertEqual(cm.exception.code, 2)

    def test_l6_can_never_be_allowed(self):
        # Round-2 design note: a guest ECAM inside the real apcie ECAM can never pass.
        self.assertNotIn("L-6", c.ALLOWABLE)
        code, _, err = run_main_err("--allow", "L-6")
        self.assertEqual(code, 2, err)
        with tempfile.TemporaryDirectory() as d:
            raw = json.loads(J274_LAYOUT.read_text())
            raw["pci_ecam"] = "0x690000000"
            p = write_json(d, "ecam.json", raw)
            code, res = run_main(str(p), *sum((["--allow", a] for a in sorted(c.ALLOWABLE)), []))
        self.assertEqual(code, 1)
        self.assertEqual({b.split(":")[0] for b in res["blocking"]}, {"L-6"})

    def test_explicit_missing_carveouts_is_usage_error(self):
        # Round-2 defect 4: a typo in --carveouts must not silently become "not checked".
        code, out, err = run_main_err("--carveouts", "/nonexistent/j274-adt-facts.json",
                                      "--allow", "L-1", "--allow", "L-2", "--allow", "L-8")
        self.assertEqual(code, 2)
        self.assertEqual(out, "")
        self.assertIn("no such file", err)

    def test_explicit_existing_carveouts_is_used(self):
        code, res = run_main("--carveouts", str(ADT_FACTS))
        self.assertEqual(res["carveouts"], str(ADT_FACTS))
        self.assertTrue(res["carveout_findings"])

    def test_bad_inputs_are_usage_errors(self):
        # Round-2 defect 7: malformed input is exit 2 with a message, not a traceback.
        raw = json.loads(J274_LAYOUT.read_text())
        with tempfile.TemporaryDirectory() as d:
            layouts = {
                "not json": write_json(d, "a.json", "{not json"),
                "not an object": write_json(d, "b.json", [1, 2]),
                "bad hex": write_json(d, "c.json", dict(raw, phys_base="0xzz")),
                "list value": write_json(d, "d.json", dict(raw, cpu_count=[8])),
                "bool value": write_json(d, "e.json", dict(raw, cpu_count=True)),
                "missing file": Path(d) / "nope.json",
            }
            for name, path in layouts.items():
                with self.subTest(layout=name):
                    code, out, err = run_main_err(str(path))
                    self.assertEqual(code, 2, (name, err))
                    self.assertIn("USAGE ERROR", err)
                    self.assertNotIn("Traceback", err)
            facts = {
                "not json": write_json(d, "f1.json", "]"),
                "no carveouts key": write_json(d, "f2.json", {"x": 1}),
                "bad base": write_json(d, "f3.json", {"carveouts": [{"id": 1, "base": "zz", "size": "0x1"}]}),
                "missing size": write_json(d, "f4.json", {"carveouts": [{"id": 1, "base": "0x1"}]}),
            }
            for name, path in facts.items():
                with self.subTest(facts=name):
                    code, out, err = run_main_err("--carveouts", str(path))
                    self.assertEqual(code, 2, (name, err))
                    self.assertIn("USAGE ERROR", err)
        code, _, err = run_main_err("--fw-size", "zz")
        self.assertEqual(code, 2, err)


class CarveoutOverlap(unittest.TestCase):
    """REVIEW defect 2: carveouts inside guest RAM are reported as an L-8 blocker."""

    def setUp(self):
        self.lay = load(J274_LAYOUT)

    def test_recorded_carveouts_conflict(self):
        found = c.carveout_findings(self.lay, c.load_carveouts(ADT_FACTS))
        conflicts = [f for f in found if f.startswith("CONFLICT L-8")]
        # region-id-48 covers ADT/firmware/boot args/vfb; 19 ids lie in 0x864000000..0x86fe38000
        self.assertTrue(any("region-id-48 " in f and "firmware" in f and "ADT" in f for f in conflicts))
        inside = [f for f in conflicts if "[0x86" in f]
        self.assertEqual(len(inside), 19, inside)
        self.assertEqual(len(conflicts), 20)
        self.assertTrue(any(f.startswith("INFO carveout region-id-24 ") for f in found))

    def test_synthetic(self):
        lay = self.lay
        cvs = [{"id": 1, "base": lay["phys_base"] - 0x4000, "size": 0x4000},         # just below: none
               {"id": 2, "base": lay["firmware_base"] + 0x1000, "size": 0x1000},     # in firmware
               {"id": 3, "base": lay["ram_end"] - 0x4000, "size": 0x8000},          # straddles end
               {"id": 4, "base": lay["phys_base"], "size": lay["ram_end"] - lay["phys_base"]}]
        found = c.carveout_findings(lay, cvs)
        self.assertFalse(any("region-id-1 " in f for f in found))
        self.assertTrue(any(f.startswith("CONFLICT L-8 carveout region-id-2 ") and "firmware" in f for f in found))
        self.assertTrue(any(f.startswith("CONFLICT L-8 carveout region-id-3 ") for f in found))
        # Round-2 defect 5: a region that contains all of guest RAM is NOT INFO unless it is the
        # recorded RAM region (id 24, base 0x801504000, size 0x3dfafc000).
        self.assertTrue(any(f.startswith("CONFLICT L-8 carveout region-id-4 ") and "contains all" in f
                            for f in found), found)
        self.assertFalse(any(f.startswith("INFO") for f in found), found)

    def test_only_recorded_ram_region_is_info(self):
        lay = self.lay
        ram = dict(c.J274_RAM_REGION)
        self.assertEqual(ram["base"] + ram["size"], c.J274_PLATFORM_RAM_END)
        cases = {"exact": (ram, "INFO"),
                 "other id": (dict(ram, id=25), "CONFLICT"),
                 "other base": (dict(ram, base=ram["base"] - 0x4000, size=ram["size"] + 0x4000), "CONFLICT"),
                 "bigger": (dict(ram, size=ram["size"] + 0x4000), "CONFLICT")}
        for name, (cv, want) in cases.items():
            with self.subTest(case=name):
                found = c.carveout_findings(lay, [cv])
                self.assertEqual(len(found), 1, found)
                self.assertTrue(found[0].startswith(want), found)
        rec = [cv for cv in c.load_carveouts(ADT_FACTS) if cv["id"] == 24]
        self.assertEqual(rec, [ram], "constant equals the recorded table entry")

    def test_no_carveouts_is_unchecked_blocker(self):
        # Round-2 defect 4: "never checked" has its own id that --allow cannot accept.
        code, res = run_main("--no-carveouts", "--allow", "L-1", "--allow", "L-2", "--allow", "L-8")
        self.assertEqual(code, 1)
        self.assertEqual({b.split(":")[0] for b in res["blocking"]}, {"UNCHECKED"})
        self.assertIsNone(res["carveouts"])

    def test_unchecked_cannot_be_allowed(self):
        code, _, err = run_main_err("--no-carveouts", "--allow", "UNCHECKED")
        self.assertEqual(code, 2, err)


class AdtFactsReproducible(unittest.TestCase):
    """REVIEW defect 5: ADT values stored as JSON with the dump sha256 and the exact command."""

    def test_facts_record(self):
        facts = json.loads(ADT_FACTS.read_text())
        dump = N / facts["source_dump"]
        self.assertEqual(hashlib.sha256(dump.read_bytes()).hexdigest(), facts["source_sha256"])
        self.assertIn("tools/extract_adt_facts.py", facts["command"])
        self.assertEqual(facts["apcie_reg0"], {"base": hex(c.J274_REAL_APCIE_ECAM[0]),
                                               "size": hex(c.J274_REAL_APCIE_ECAM[1])})
        self.assertEqual(facts["disp0_external"], 1)
        self.assertIn("J274AP", facts["compatible"])


class ExtractorRecordsRealInterpreter(unittest.TestCase):
    """Round-2 defect 3: the extractor records the interpreter it really runs under and refuses
    without -I -B. (The old test only checked a typed-in string.)"""

    def test_refuses_without_isolated_or_no_bytecode(self):
        before = set(sys.modules)
        for iso, nb in ((0, 1), (1, 0), (0, 0)):
            with self.subTest(isolated=iso, dont_write_bytecode=nb), tempfile.TemporaryDirectory() as d:
                out = Path(d) / "facts.json"
                err = io.StringIO()
                with contextlib.redirect_stderr(err):
                    code = x.main([str(N / "experiments/tahoe-afk-20260906/d6-adt.bin"), str(out)],
                                  flags=types.SimpleNamespace(isolated=iso, dont_write_bytecode=nb))
                self.assertEqual(code, 2)
                self.assertFalse(out.exists())
                self.assertIn("REFUSED", err.getvalue())
        self.assertNotIn("m1n1.adt", set(sys.modules) - before, "refusal happens before the import")

    def test_record_reads_sys(self):
        rec = x.interpreter_record(types.SimpleNamespace(isolated=1, dont_write_bytecode=0))
        self.assertEqual(rec["executable"], sys.executable)
        self.assertEqual((rec["isolated"], rec["dont_write_bytecode"]), (True, False))
        live = x.interpreter_record()
        self.assertEqual((live["isolated"], live["dont_write_bytecode"]),
                         (bool(sys.flags.isolated), bool(sys.flags.dont_write_bytecode)))

    def test_subprocess_run_records_its_flags(self):
        if importlib.util.find_spec("construct") is None:
            self.skipTest("needs construct: run with m1n1_windows/.venv-hv/bin/python3 -I -B")
        dump = N / "experiments/tahoe-afk-20260906/d6-adt.bin"
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "facts.json"
            r = subprocess.run([sys.executable, "-I", "-B", str(PORT / "tools/extract_adt_facts.py"),
                                str(dump), str(out)], capture_output=True, text=True, timeout=120)
            self.assertEqual(r.returncode, 0, r.stderr)
            rec = json.loads(out.read_text())
        self.assertEqual(rec["interpreter"]["executable"], sys.executable)
        self.assertIs(rec["interpreter"]["isolated"], True)
        self.assertIs(rec["interpreter"]["dont_write_bytecode"], True)
        committed = json.loads(ADT_FACTS.read_text())
        for k in ("source_sha256", "apcie_reg0", "disp0_external", "carveouts", "compatible"):
            self.assertEqual(rec[k], committed[k], k)

    def test_committed_record_has_real_flags(self):
        facts = json.loads(ADT_FACTS.read_text())
        if not isinstance(facts["interpreter"], dict):
            self.skipTest("config/j274-adt-facts.json predates round 2 (interpreter is a typed-in "
                          "string); regenerate it with the extractor")
        self.assertIs(facts["interpreter"]["isolated"], True)
        self.assertIs(facts["interpreter"]["dont_write_bytecode"], True)


class ReadmeCommands(unittest.TestCase):
    """REVIEW defect 3: documented commands use -I -B (PYTHONDONTWRITEBYTECODE is ignored under -I)."""

    def test_readme(self):
        text = (PORT / "README.md").read_text()
        for line in text.splitlines():
            self.assertFalse(line.lstrip().startswith("PYTHONDONTWRITEBYTECODE="), line)
            if re.match(r"\s*(\S*/)?python3 ", line):
                self.assertIn(" -I -B ", line, line)


if __name__ == "__main__":
    unittest.main()
