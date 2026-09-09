import unittest,tempfile,os,json
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace
SOURCE=Path(__file__).with_name('late_pstate12.py').read_text()
class Proxy:
 def __init__(self,fail=False):self.cmd=0x40000107107;self.status=0x77;self.writes=0;self.fail=fail
 def get_chipid(self):return 0x8103
 def read64(self,a):return {0x211e20020:self.cmd,0x211e20050:self.status,0x210e20020:0x40000105105,0x210e20050:0x55}[a]
 def mask64(self,a,mask,val):
  self.writes+=1
  if self.fail:raise RuntimeError('injected failure')
  self.cmd=(self.cmd&~mask)|val;self.status=((val&15)<<4)|(val&15)
class Tests(unittest.TestCase):
 def run_case(self,fail=False):
  td=tempfile.TemporaryDirectory();p=Proxy(fail);link=SimpleNamespace(connected=False)
  hv=SimpleNamespace(nwoas_nvme_link_handler=lambda addr:addr==1,_nwoas_link=link,log=lambda s:None)
  scope={'p':p,'hv':hv}
  env=patch.dict(os.environ,{'NWOAS_LATE_P12':'1','NWOAS_CPU_PSTATE':'12','NWOAS_LINK_DIR':td.name});env.start()
  self.addCleanup(env.stop);self.addCleanup(td.cleanup)
  exec(compile(SOURCE,'late_pstate12.py','exec'),scope)
  return p,hv,link,Path(td.name)
 def test_waits_for_completed_worker_request(self):
  p,hv,link,d=self.run_case();hv.nwoas_nvme_link_handler(1);self.assertEqual(p.writes,0)
  link.connected=True;hv.nwoas_nvme_link_handler(0);self.assertEqual(p.writes,0)
  hv.nwoas_nvme_link_handler(1);self.assertEqual(p.writes,1)
  r=json.loads((d/'late-p12.json').read_text());self.assertTrue(r['pass']);self.assertEqual(r['p_status'],'0xcc')
  hv.nwoas_nvme_link_handler(1);self.assertEqual(p.writes,1)
 def test_failure_not_retried_and_guest_result_preserved(self):
  p,hv,link,d=self.run_case(True);link.connected=True
  self.assertTrue(hv.nwoas_nvme_link_handler(1));n=p.writes;self.assertEqual(n,2)
  self.assertFalse(json.loads((d/'late-p12.json').read_text())['pass'])
  self.assertTrue(hv.nwoas_nvme_link_handler(1));self.assertEqual(p.writes,n)
if __name__=='__main__':unittest.main()
