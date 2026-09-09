"""Exercise the actual run_guest script/command tail with a fake HV."""
import ast
from pathlib import Path
import types
import unittest

SOURCE=Path('/Volumes/X31/NWOAS/m1n1_windows/proxyclient/tools/run_guest.py')

class StrictInitTests(unittest.TestCase):
    def execute(self,strict,failed=None):
        calls=[]
        def run(kind,value):
            calls.append((kind,value))
            if kind==failed:raise RuntimeError('test init failure')
        hv=types.SimpleNamespace(run_script=lambda v:run('script',v),
            run_code=lambda v:run('command',v),start=lambda:calls.append(('start',None)),shell_locals={})
        args=types.SimpleNamespace(script=['init'],command=['setup'],strict_init=strict,shell=False)
        nodes=ast.parse(SOURCE.read_text()).body
        start=next(i for i,n in enumerate(nodes) if isinstance(n,ast.For) and
                   isinstance(n.iter,ast.Attribute) and n.iter.attr=='script')
        end=next(i for i,n in enumerate(nodes[start:],start) if isinstance(n,ast.Expr) and
                 isinstance(n.value,ast.Call) and isinstance(n.value.func,ast.Attribute) and
                 n.value.func.attr=='start')
        env={'args':args,'hv':hv,'traceback':types.SimpleNamespace(print_exc=lambda:None),
             'run_shell':lambda *a:calls.append(('shell',None))}
        error=None
        try:exec(compile(ast.Module(body=nodes[start:end+1],type_ignores=[]),'<actual launcher tail>','exec'),env)
        except RuntimeError as e:error=e
        return calls,error
    def test_strict_module_failure_never_enters_guest_or_later_commands(self):
        calls,error=self.execute(True,'script')
        self.assertIsNotNone(error);self.assertEqual(calls,[('script','init')])
    def test_strict_command_failure_never_enters_guest(self):
        calls,error=self.execute(True,'command')
        self.assertIsNotNone(error);self.assertEqual(calls,[('script','init'),('command','setup')])
    def test_success_starts_guest(self):
        calls,error=self.execute(True)
        self.assertIsNone(error);self.assertEqual(calls[-1],('start',None))
    def test_legacy_shell_behavior_preserved_without_flag(self):
        calls,error=self.execute(False,'script')
        self.assertIsNone(error);self.assertEqual(calls[-2:],[('shell',None),('start',None)])

if __name__=='__main__':unittest.main()
