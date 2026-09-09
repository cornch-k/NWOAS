from pathlib import Path


SOURCE = Path(__file__).with_name("init.py").read_text()


def test_init_is_t8103_bounded_and_checks_both_clusters():
    assert "CHIP_T8103 = 0x8103" in SOURCE
    assert "E_CLUSTER = 0x210E00000" in SOURCE
    assert "P_CLUSTER = 0x211E00000" in SOURCE
    assert "p.cpufreq_init()" in SOURCE
    assert "after_e[1] != 5 or after_p[1] != 7" in SOURCE


def test_init_fails_closed_on_transition_error():
    assert "if result != 0" in SOURCE
    assert "if after_e[2] or after_p[2]" in SOURCE
