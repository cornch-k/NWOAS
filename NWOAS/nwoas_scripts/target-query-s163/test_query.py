#!/usr/bin/env python3
import ast,json,tempfile,types,unittest
from pathlib import Path

class QueryTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root=Path(self.temp.name)
        self.calls=[];self.clock=100.
        source=ast.parse(Path(__file__).with_name('module.py').read_text())
        funcs=[n for n in source.body if isinstance(n,ast.FunctionDef)]
        def query(a):
            self.calls.append(a)
            return (0x53313630<<32)|1 if a==8 else a*100
        self.env=dict(json=json,time=types.SimpleNamespace(monotonic=lambda:self.clock,time=lambda:self.clock),
            p=types.SimpleNamespace(nwoas_nvme_fastpath=query),_query_request=root/'request.json',
            _query_result=root/'result.json',_query_last_poll=0.,_query_last_id=None,
            _query_base=lambda addr:True,hv=types.SimpleNamespace(log=lambda value:None))
        exec(compile(ast.Module(body=funcs,type_ignores=[]),'<actual query functions>','exec'),self.env)
    def send(self,data):
        self.env['_query_request'].write_text(json.dumps(data))
        self.env['_query_link_handle'](0)
    def result(self):
        return json.loads(self.env['_query_result'].read_text())
    def test_query_once_does_not_change_namespace(self):
        self.send({'id':'first','actions':[3,4,10]})
        self.assertEqual(self.calls,[8,3,4,10])
        self.assertEqual(self.result()['values'],{'3':300,'4':400,'10':1000})
        self.clock+=2;self.env['_query_poll']()
        self.assertEqual(self.calls,[8,3,4,10])
    def test_control_actions_rejected_before_any_proxy_call(self):
        for actions in ([0],[1],[2],[11],[True],['3'],[3]*9):
            self.clock+=2;self.send({'id':'bad','actions':actions})
            self.assertIn('error',self.result());self.assertEqual(self.calls,[])
    def test_malformed_and_oversize(self):
        self.send({'id':'bad','actions':[3],'code':'do something'})
        self.assertIn('error',self.result());self.assertEqual(self.calls,[])
        self.clock+=2;self.env['_query_request'].write_text('x'*4097);self.env['_query_poll']()
        self.assertIn('error',self.result());self.assertEqual(self.calls,[])
    def test_no_file_or_base_failure_no_query(self):
        self.env['_query_link_handle'](0);self.assertEqual(self.calls,[])
        self.env['_query_base']=lambda addr:False
        self.send({'id':'valid','actions':[3]});self.assertEqual(self.calls,[])
    def test_capability_mismatch_blocks_actions(self):
        self.env['p'].nwoas_nvme_fastpath=lambda a:0
        self.send({'id':'wrongtarget','actions':[10]})
        self.assertIn('capability mismatch',self.result()['error'])

if __name__=='__main__':unittest.main()
