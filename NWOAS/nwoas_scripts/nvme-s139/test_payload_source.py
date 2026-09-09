from pathlib import Path


source = (Path(__file__).parent / "build_payload.py").read_text()
assert "m1n1-payload-s131-8cpu.bin" in source
assert 'Device (XHC1)' in source
assert "Return (Zero)" in source
assert "S131.write_bytes(stable_s131)" in source
assert "S131_MANIFEST.write_bytes(stable_s131_manifest)" in source
print("S139 payload source checks: PASS")
