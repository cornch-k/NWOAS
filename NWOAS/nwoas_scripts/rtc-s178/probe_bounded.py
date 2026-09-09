"""Opt-in preboot SPMI RTC evidence only. No ADT seed, no runtime/PMU writes.
Poll budget100ms per read, underlying proxy timeout remains independently bounded.
Failure records evidence and leaves the existing boot path unchanged.
"""
import os,sys,json,time,struct
from pathlib import Path
sys.path.insert(0,'/Volumes/X31/NWOAS/nwoas_scripts/rtc-s178')
from bounded_spmi import read_rtc_register,BASE
from rtc_math import rtc_to_epoch_ticks

def one_u32(value):
    if isinstance(value,(list,tuple)) and len(value)==1 and isinstance(value[0],int):return value[0]
    raise ValueError('expected one-cell ADT property')
def probe():
    if os.environ.get('NWOAS_RTC_SNAPSHOT')!='1':return
    rec={'started':time.time(),'source':'PMU info-rtc plus rtc_scrpad; CLKM equivalence unproven','guest_adt_changed':False}
    try:
        adt=hv.u.adt;chip=adt['/chosen'].getprop('chip-id')
        ctrl=adt['/arm-io/nub-spmi'];pmu=adt['/arm-io/nub-spmi/spmi-pmu']
        base,size=ctrl.get_reg(0);raw=pmu.reg
        if chip!=0x8103 or base!=BASE or size!=0x100 or not isinstance(raw,bytes) or len(raw)<4 or struct.unpack_from('<I',raw)[0]!=15:
            raise ValueError('not validated T8103 SPMI topology')
        if one_u32(pmu.getprop('info-rtc'))!=0xd002 or one_u32(pmu.getprop('info-rtc_scrpad'))!=0xd100:
            raise ValueError('RTC ADT addresses differ')
        tick0=hv.u.mrs('CNTPCT_EL0');hz=hv.u.mrs('CNTFRQ_EL0')
        c0,h0=read_rtc_register(p.read32,p.write32,0xd002)
        off,h1=read_rtc_register(p.read32,p.write32,0xd100)
        c1,h2=read_rtc_register(p.read32,p.write32,0xd002)
        tick1=hv.u.mrs('CNTPCT_EL0')
        a,b,o=map(lambda x:int.from_bytes(x,'little'),(c0,c1,off))
        epoch,sub=rtc_to_epoch_ticks(b,o)
        delta=(b-a)&((1<<48)-1)
        valid=hz>0 and tick1>=tick0 and tick1-tick0<hz*2 and delta<65536 and abs(epoch-time.time())<300
        rec.update(counter0=c0.hex(),counter1=c1.hex(),offset=off.hex(),response_headers=[h0,h1,h2],cntpct0=tick0,cntpct1=tick1,cntfrq=hz,epoch=epoch,subticks=sub,counter_delta=delta,host_delta_s=epoch-time.time(),plausible=valid)
    except Exception as e:rec['error']=type(e).__name__+': '+str(e)
    rec['finished']=time.time()
    path=Path(os.environ['NWOAS_LINK_DIR'])/'rtc-bounded-probe.json'
    path.write_text(json.dumps(rec,indent=2)+'\n')
    hv.log('[rtc-s178] bounded preboot evidence '+json.dumps(rec,sort_keys=True))
probe()
