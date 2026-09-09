"""Opt-in P12 after the installed Windows worker connects, once per boot.

The inner payload resets a pre-UEFI request to P7. Apply only after that stage.
No interactive pause or timer callback; one existing NS2 rendezvous is extended.
A failed bounded transition is recorded and never automatically retried.
"""
import builtins,hashlib,json,os,time
from pathlib import Path

if os.environ.get('NWOAS_LATE_P12')!='1' or os.environ.get('NWOAS_CPU_PSTATE')!='12':
    raise RuntimeError('S164 late P12 needs explicit opt-in')
_late_path=Path('/Volumes/X31/NWOAS/nwoas_scripts/cpufreq-s164/pstate12.py')
_late_source=_late_path.read_bytes()
_late_code=compile(_late_source,str(_late_path),'exec')
_late_base=hv.nwoas_nvme_link_handler
_late_done=False
_late_result=Path(os.environ['NWOAS_LINK_DIR'])/'late-p12.json'


def _late_link(addr):
    global _late_done
    result=_late_base(addr)
    if result and not _late_done and hv._nwoas_link.connected:
        _late_done=True
        start=time.time()
        record={'started':start,'phase':'after Windows worker connection','helper_sha256':hashlib.sha256(_late_source).hexdigest()}
        try:
            builtins.exec(_late_code,{'p':p,'hv':hv})
            record['p_cmd']=hex(p.read64(0x211e20020))
            record['p_status']=hex(p.read64(0x211e20050))
            record['e_cmd']=hex(p.read64(0x210e20020))
            record['e_status']=hex(p.read64(0x210e20050))
            record['pass']=(int(record['p_status'],16)&0xff)==0xcc
            if not record['pass']:record['error']='post-transition STATUS is not P12/P12'
        except Exception as error:
            # The helper tries bounded P7 restoration. Do not terminate a running
            # guest or keep writing repeated requests if the transition fails.
            record['pass']=False;record['error']=repr(error)
        record['finished']=time.time()
        _late_result.write_text(json.dumps(record,indent=2)+'\n')
        hv.log('[S164] late P12 '+json.dumps(record,sort_keys=True))
    return result

hv.nwoas_nvme_link_handler=_late_link
hv.log('[S164] one-shot P12 scheduled at first Windows-worker rendezvous (no periodic automation)')
