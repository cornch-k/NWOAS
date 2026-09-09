import ast
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock

class LegacyGateTests(unittest.TestCase):
 def build(self,enabled,ready=True):
  source=Path('/Volumes/X31/NWOAS/nwoas_scripts/nvme-s130/guest_module.py').read_text()
  node=next(n for n in ast.parse(source).body if isinstance(n,ast.FunctionDef) and n.name=='_fast_arm_if_ready')
  target=Mock(return_value=1)
  g=dict(FASTPATH_ENABLED=enabled,fast_armed=False,_fast_ready=Mock(return_value=ready),_fast_flags=lambda:3,p=SimpleNamespace(nwoas_nvme_fastpath=target),hv=SimpleNamespace(log=Mock()),c=SimpleNamespace(sq={1:SimpleNamespace(base=0x1000,size=64)},cq={1:SimpleNamespace(base=0x2000,size=64)}))
  exec(compile(ast.Module(body=[node],type_ignores=[]),'guest_module.py','exec'),g)
  return g,target
 def test_disabled_never_issues_unsupported_firmware_command(self):
  g,p=self.build(False);g['_fast_arm_if_ready']();p.assert_not_called();self.assertFalse(g['fast_armed'])
 def test_enabled_arms_only_ready_queues(self):
  g,p=self.build(True);g['_fast_arm_if_ready']();p.assert_called_once_with(1,0x1000,64,0x2000,64,3);self.assertTrue(g['fast_armed'])
 def test_not_ready_leaves_python_path(self):
  g,p=self.build(True,False);g['_fast_arm_if_ready']();p.assert_not_called()
 def test_armed_does_not_rearm(self):
  g,p=self.build(True);g['fast_armed']=True;g['_fast_arm_if_ready']();p.assert_not_called()

if __name__=='__main__':unittest.main()
