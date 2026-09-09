#!/usr/bin/env python3
"""S106: read-only. Parse the NTFS volume in the Windows partition (LBA 53920000, 4096-byte
sectors) from the host through m1n1 nvme_read_n, find a file by name in the MFT, walk its
$DATA runlist and SHA-256 the on-disk bytes. Never writes."""
import sys,struct,hashlib,signal,subprocess,time,json,zlib,uuid
from pathlib import Path
NAME=sys.argv[1] if len(sys.argv)>1 else 't1.swm'
PART=53920000;LAST=59968511;BS=4096
EXPECTED='8118bfe1173b8f72161d1eece7eb76b09caa7b30b333f78ae039d390bb04bc8c' if NAME=='t1.swm' else None
PORT='/dev/cu.usbmodemC07HL05SQ6NY1'
r=subprocess.run(['lsof','-t',PORT],capture_output=True);assert r.returncode==1 and not r.stdout and not r.stderr
def dl(*_):raise TimeoutError('deadline')
signal.signal(signal.SIGALRM,dl);signal.alarm(120)
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
iface=UartInterface();p=M1N1Proxy(iface,debug=False);bootstrap_port(iface,p);u=ProxyUtils(p)
assert 'j274' in str(u.adt.compatible).lower();assert p.nvme_init()
try:
    buf=u.memalign(0x4000,0x10000)
    def rd(lba,n):
        assert isinstance(lba,int) and isinstance(n,int) and PART<=lba<=LAST and 1<=n<=1024 and lba+n-1<=LAST,'read outside Windows partition'
        out=b''
        while n:
            k=min(n,16);signal.alarm(30);assert p.nvme_read_n(1,lba,buf,k),f'read {lba}+{k}';out+=iface.readmem(buf,k*BS);lba+=k;n-=k
        return out
    boot=rd(PART,1)
    assert boot[3:11]==b'NTFS    ',boot[:16]
    bps,spc=struct.unpack_from('<HB',boot,0x0b);mft_lcn=struct.unpack_from('<Q',boot,0x30)[0];cpr=struct.unpack_from('<b',boot,0x40)[0]
    assert bps==BS and spc in (1,2,4,8,16,32,64,128),(bps,spc)
    csz=bps*spc;rec=(1<<(-cpr)) if cpr<0 else cpr*csz
    assert rec in (1024,2048,4096),'unexpected MFT record size'
    print(f'NTFS bps={bps} spc={spc} cluster={csz} mft_lcn={mft_lcn} record={rec}')
    def fixup(b):
        b=bytearray(b);uo,uc=struct.unpack_from('<HH',b,4);usa=b[uo:uo+2*uc];stride=len(b)//(uc-1)
        for i in range(1,uc):
            assert b[i*stride-2:i*stride]==usa[0:2],'fixup mismatch'
            b[i*stride-2:i*stride]=usa[2*i:2*i+2]
        return bytes(b)
    def runlist(rl):
        runs=[];lcn=0;i=0
        while i<len(rl) and rl[i]:
            h=rl[i];ls=h&15;os_=h>>4;i+=1
            assert 1<=ls<=8 and os_<=8 and i+ls+os_<=len(rl),'invalid runlist' 
            ln=int.from_bytes(rl[i:i+ls],'little');i+=ls
            assert ln>0,'empty run'
            if os_:
                off=int.from_bytes(rl[i:i+os_],'little',signed=True);i+=os_;lcn+=off;runs.append((lcn,ln))
            else:runs.append((None,ln))
        return runs
    # read the first 4 MiB of the MFT (enough records for a nearly-empty volume)
    mft_bytes=rd(PART+mft_lcn*spc,4*1024*1024//BS)
    found=None
    for i in range(len(mft_bytes)//rec):
        r_=mft_bytes[i*rec:(i+1)*rec]
        if r_[:4]!=b'FILE':continue
        try:r_=fixup(r_)
        except AssertionError:continue
        if not struct.unpack_from('<H',r_,0x16)[0]&1:continue # not in use
        off=struct.unpack_from('<H',r_,0x14)[0];name=None;data=None
        while off+8<=len(r_):
            t,l=struct.unpack_from('<II',r_,off)
            if t==0xffffffff or l==0:break
            nonres=r_[off+8];nl=r_[off+9]
            if t==0x30 and not nonres and nl==0:
                vo=struct.unpack_from('<H',r_,off+0x14)[0];fn=r_[off+vo:off+vo+struct.unpack_from('<I',r_,off+0x10)[0]]
                n=fn[0x40];name=fn[0x42:0x42+2*n].decode('utf-16le')
            if t==0x80 and nl==0 and nonres:
                rlo=struct.unpack_from('<H',r_,off+0x20)[0];size=struct.unpack_from('<Q',r_,off+0x30)[0]
                data=(runlist(r_[off+rlo:off+l]),size)
            off+=l
        if name and name.lower()==NAME.lower() and data:found=(i,name,data);break
    assert found,f'{NAME} not found in first MFT records'
    idx,name,(runs,size)=found
    print(f'record {idx} {name} size={size} runs={len(runs)} first={runs[:3]}')
    assert 0<size<=(LAST-PART+1)*BS,'invalid file size'
    h=hashlib.sha256();left=size;t0=time.monotonic();done=0
    for lcn,ln in runs:
        assert lcn is not None,'sparse run'
        lba=PART+lcn*spc;n=ln*spc
        while n and left:
            k=min(n,1024);b=rd(lba,k);take=min(len(b),left);h.update(b[:take]);left-=take;lba+=k;n-=k;done+=take
            if done%(256*1024*1024)<k*BS:print(f'  {done/1e9:.2f} GB {time.monotonic()-t0:.0f}s',flush=True)
    assert left==0 and done==size,'incomplete runlist; refusing partial hash'
    report={'experiment':'S106','file':name,'bytes':size,'sha256':h.hexdigest(),'expected_sha256':EXPECTED,'matches_original':h.hexdigest()==EXPECTED if EXPECTED else None,'elapsed_seconds':round(time.monotonic()-t0,2),'storage_writes':False}
    print(json.dumps(report,indent=2),flush=True)
    Path('/tmp/nwoas-s106-'+NAME+'-result.json').write_text(json.dumps(report,indent=2)+'\n')
finally:
    try:
        signal.alarm(30);p.nvme_shutdown()
    finally:
        signal.alarm(0);iface.dev.close()
