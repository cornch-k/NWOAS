"""One read-only operating-point snapshot at first Windows-worker rendezvous."""
import json,os,time
from pathlib import Path
_base189=hv.nwoas_nvme_link_handler
_done189=False
def _link189(addr):
    global _done189
    result=_base189(addr)
    if result and not _done189 and hv._nwoas_link.connected:
        _done189=True
        record={'phase':'Windows worker connected; no host P-state writes','utc':time.time()}
        for key,address in [('p_cmd',0x211e20020),('p_status',0x211e20050),('e_cmd',0x210e20020),('e_status',0x210e20050)]:
            record[key]=hex(p.read64(address))
        record['p12_selected']=(int(record['p_status'],16)&255)==0xcc
        Path(os.environ['NWOAS_LINK_DIR'],'native-p12.json').write_text(json.dumps(record,indent=2)+'\n')
        hv.log('[S189] read-only native P12 '+json.dumps(record,sort_keys=True))
    return result
hv.nwoas_nvme_link_handler=_link189
