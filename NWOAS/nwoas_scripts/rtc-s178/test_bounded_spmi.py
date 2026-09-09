from bounded_spmi import read_rtc_register,BASE,RX_EMPTY
import unittest
class TestRead(unittest.TestCase):
 def fixture(self,initial=RX_EMPTY,words=None):
  self.writes=[];self.words=list(words or [0,0x04030201,0x0605]);self.started=False;self.t=0
  def rd(a):
   if a==BASE:return initial if not self.started else (0 if self.words else RX_EMPTY)
   assert a==BASE+8;return self.words.pop(0)
  def wr(a,v):self.writes.append((a,v));self.started=True
  def clk():self.t+=.001;return self.t
  return rd,wr,clk
 def test_exact_bus_read_and_bytes(self):
  for reg in (0xd002,0xd100):
   rd,wr,clk=self.fixture();b,status=read_rtc_register(rd,wr,reg,clock=clk)
   self.assertEqual(b,bytes(range(1,7)));self.assertEqual(status,0)
   self.assertEqual(self.writes,[(BASE+4,(reg<<16)|0x8000|0xf00|0x3d)])
 def test_busy_fifo_no_write(self):
  for initial in (0,RX_EMPTY|1):
   rd,wr,clk=self.fixture(initial)
   with self.assertRaises(RuntimeError):read_rtc_register(rd,wr,0xd002,clock=clk)
   self.assertEqual(self.writes,[])
 def test_timeout_no_retry_or_pmu_write(self):
  rd,wr,clk=self.fixture()
  with self.assertRaises(TimeoutError):read_rtc_register(lambda a:RX_EMPTY,wr,0xd002,clock=clk)
  self.assertEqual(len(self.writes),1)
 def test_extra_data_preserved(self):
  rd,wr,clk=self.fixture(words=[0,1,2,3])
  with self.assertRaises(RuntimeError):read_rtc_register(rd,wr,0xd002,clock=clk)
  self.assertEqual(self.words,[3])
 def test_register_whitelist(self):
  rd,wr,clk=self.fixture()
  with self.assertRaises(ValueError):read_rtc_register(rd,wr,0xd000,clock=clk)
  self.assertEqual(self.writes,[])
if __name__=='__main__':unittest.main()
