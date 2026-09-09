"""Run the actual opt-in module against a register-level fake proxy."""
import os
from pathlib import Path
import runpy
import types
import unittest
from unittest.mock import patch

CMD=0x211e20020
FIELDS=0xf01f
BASE=(1<<42)|(1<<20)|(7<<12)|7

class FakeProxy:
    def __init__(self, chip=0x8103, stuck=False, mismatch=False):
        self.chip=chip; self.value=BASE; self.writes=[]
        self.stuck=stuck; self.mismatch=mismatch
    def get_chipid(self): return self.chip
    def read64(self, address):
        if address==CMD+0x30: return 0xcc
        assert address==CMD
        if self.stuck and self.writes and self.writes[-1][2]&31==12:
            return self.value|(1<<31)
        return self.value
    def mask64(self,address,clear,value):
        assert address==CMD
        self.writes.append((address,clear,value))
        self.value=(self.value&~clear)|value
        if self.mismatch and value&31==12: self.value^=1

class PstateTests(unittest.TestCase):
    def run_module(self,proxy,enabled=True):
        tick=iter(i/10 for i in range(100))
        with patch.dict(os.environ,{'NWOAS_CPU_PSTATE':'12' if enabled else ''}), \
             patch('time.monotonic',side_effect=lambda:next(tick)):
            return runpy.run_path(str(Path(__file__).with_name('pstate12.py')),
                init_globals={'p':proxy,'hv':types.SimpleNamespace(log=lambda s:None)})
    def test_preserves_controls_and_uses_exact_field_mask(self):
        p=FakeProxy();self.run_module(p)
        self.assertEqual(p.writes,[(CMD,FIELDS,(1<<25)|12|(12<<12))])
        self.assertEqual(p.value&~(FIELDS|(1<<25)),BASE&~(FIELDS|(1<<25)))
    def test_opt_in_and_chip_gate_precede_writes(self):
        for p,enabled in [(FakeProxy(),False),(FakeProxy(chip=0x8112),True)]:
            with self.assertRaises(RuntimeError):self.run_module(p,enabled)
            self.assertEqual(p.writes,[])
    def test_bad_baseline_precedes_writes(self):
        p=FakeProxy();p.value=1|(1<<12)
        with self.assertRaises(RuntimeError):self.run_module(p)
        self.assertEqual(p.writes,[])
    def test_busy_timeout_never_writes_another_request_while_busy(self):
        p=FakeProxy(stuck=True)
        with self.assertRaises(TimeoutError):self.run_module(p)
        self.assertEqual([w[2]&31 for w in p.writes],[12])
    def test_readback_mismatch_restores_p7_and_aborts(self):
        p=FakeProxy(mismatch=True)
        with self.assertRaises(RuntimeError):self.run_module(p)
        self.assertEqual([w[2]&31 for w in p.writes],[12,7])

if __name__=='__main__':unittest.main()
