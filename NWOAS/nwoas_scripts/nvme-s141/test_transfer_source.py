from pathlib import Path

ROOT = Path(__file__).parents[2]
GUEST = (ROOT / 'nwoas_scripts/nvme-s130/guest_module.py').read_text()
NVME = (ROOT / 'm1n1_windows/src/nvme.c').read_text()


def test_default_transfer_ceiling_remains_64k():
    assert "NWOAS_MAX_TRANSFER','65536'" in GUEST
    assert '#define NVME_MAX_BLOCKS 16' in NVME


def test_candidate_is_explicitly_bounded_to_1m():
    assert "MAX_TRANSFER not in (65536,1048576)" in GUEST
    assert "MAX_BLOCKS=MAX_TRANSFER//4096" in GUEST
    assert "_prp.LIMIT=MAX_TRANSFER" in GUEST
    assert "nsbuf=u.memalign(0x4000,MAX_TRANSFER)" in GUEST
