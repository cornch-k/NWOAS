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
    # Follow MFT's own runlist; first 4MiB cannot cover the existing partial install.
    r0=fixup(rd(PART+mft_lcn*spc,1)[:rec]);off=struct.unpack_from('<H',r0,0x14)[0];mftruns=None
    while off+8<=len(r0):
        typ,length=struct.unpack_from('<II',r0,off)
        if typ==0xffffffff or not length:break
        if typ==0x80 and r0[off+8] and not r0[off+9]:
            ro=struct.unpack_from('<H',r0,off+32)[0];mftruns=runlist(r0[off+ro:off+length]);mftsize=struct.unpack_from('<Q',r0,off+48)[0]
        off+=length
    assert mftruns and mftsize<=256*1024*1024
    print('MFT bytes',mftsize,'runs',mftruns,flush=True)
    chunks=[];remaining=mftsize
    for lcn,count in mftruns:
        assert lcn is not None
        lba=PART+lcn*spc;n=count*spc
        while n and remaining:
            k=min(n,1024,(remaining+BS-1)//BS);chunk=rd(lba,k)[:remaining]
            chunks.append(chunk);remaining-=len(chunk);lba+=k;n-=k
    assert not remaining
    mft_bytes=b''.join(chunks)
    rows=[]
    for i in range(len(mft_bytes)//rec):
        raw=mft_bytes[i*rec:(i+1)*rec]
        if raw[:4]!=b'FILE':continue
        try:r_=fixup(raw)
        except AssertionError:continue
        if not struct.unpack_from('<H',r_,0x16)[0]&1:continue
        off=struct.unpack_from('<H',r_,0x14)[0];names=[];attrs=[]
        while off+8<=len(r_):
            typ,length=struct.unpack_from('<II',r_,off)
            if typ==0xffffffff or not length:break
            assert length>=24 and off+length<=len(r_)
            nr=r_[off+8];nl=r_[off+9]
            if typ==0x30 and not nr and not nl:
                vo=struct.unpack_from('<H',r_,off+20)[0];fn=r_[off+vo:off+length]
                name=fn[66:66+2*fn[64]].decode('utf-16le');parent=struct.unpack_from('<Q',fn)[0]&((1<<48)-1)
                names.append(dict(name=name,parent=parent))
            if typ==0x80 and not nl:
                if nr:attrs.append(dict(size=struct.unpack_from('<Q',r_,off+48)[0]))
                else:
                    size,vo=struct.unpack_from('<IH',r_,off+16)
                    attrs.append(dict(size=size,text=r_[off+vo:off+vo+min(size,256)].decode('ascii',errors='replace')))
            off+=length
        if any(x['name'].lower() in ('s117src','verified-s117.txt','install.swm','install2.swm','s118-apply-started.txt') for x in names):
            rows.append(dict(record=i,names=names,data=attrs))
    report=dict(storage_writes=False,mft_bytes_scanned=len(mft_bytes),matches=rows)
    print(json.dumps(report,indent=2),flush=True)
    Path('nwoas_scripts/logs/s119-mft-inspect.json').write_text(json.dumps(report,indent=2)+'\n')
finally:
    try:
        signal.alarm(30);p.nvme_shutdown()
    finally:
        signal.alarm(0);iface.dev.close()
