"""Opt-in preboot CLKM vs PMU RTC evidence; never applies a guest seed."""
import os,sys,time,json
from pathlib import Path
sys.path.insert(0,'/Volumes/X31/NWOAS/nwoas_scripts/rtc-s183')
sys.path.insert(0,'/Volumes/X31/NWOAS/nwoas_scripts/rtc-s178')
from bounded_smc import BoundedSMC
from bounded_spmi import read_rtc_register
from rtc_math import rtc_to_epoch_ticks

def probe():
    if os.environ.get('NWOAS_SMC_RTC_PROBE')!='1':return
    result={'started':time.time(),'guest_adt_changed':False,'pmu_writes':False};smc=None
    try:
        adt=hv.u.adt
        if adt['/chosen'].getprop('chip-id')!=0x8103:raise ValueError('T8103 only')
        node=adt['/arm-io/smc'];base,size=node.get_reg(0)
        nub=adt['/arm-io/smc/iop-smc-nub'];region=nub.getprop('region-base');length=nub.getprop('region-size')
        if base!=0x23e400000 or size!=0x6c000 or region!=0x23fe00000 or length!=0x100000:raise ValueError('unvalidated SMC ADT layout')
        smc=BoundedSMC(hv.u,base);smc.set_budget(5)
        smc.start();smc.start_ep(0x20)
        t0=hv.u.mrs('CNTPCT_EL0');freq=hv.u.mrs('CNTFRQ_EL0')
        result.update(shared_buffer=smc.smcep.shmem,region_base=region,region_size=length)
        clkm=smc.smcep.read_clkm(region,length)
        ctr,h0=read_rtc_register(p.read32,p.write32,0xd002)
        off,h1=read_rtc_register(p.read32,p.write32,0xd100)
        t1=hv.u.mrs('CNTPCT_EL0')
        epoch,sub=rtc_to_epoch_ticks(int.from_bytes(clkm,'little'),int.from_bytes(off,'little'))
        result.update(clkm=clkm.hex(),direct_counter=ctr.hex(),offset=off.hex(),cntpct0=t0,cntpct1=t1,cntfrq=freq,epoch=epoch,subticks=sub,host_delta_s=epoch-time.time(),plausible=freq>0 and t1>=t0 and t1-t0<2*freq and abs(epoch-time.time())<300,shared_buffer=smc.smcep.shmem,region_base=region,region_size=length,response_headers=[h0,h1])
    except Exception as e:result['error']=type(e).__name__+': '+str(e)
    finally:
        if smc is not None:
            try:smc.set_budget(2);smc.stop(state=0x10);result['quiesce']='complete'
            except Exception as e:result['quiesce']='FAILED '+type(e).__name__+': '+str(e)
    result['finished']=time.time()
    (Path(os.environ['NWOAS_LINK_DIR'])/'rtc-smc-probe.json').write_text(json.dumps(result,indent=2)+'\n')
    hv.log('[rtc-s183] '+json.dumps(result,sort_keys=True))
    if result.get('quiesce','').startswith('FAILED'):raise RuntimeError('SMC quiesce unconfirmed; do not start guest')
probe()
