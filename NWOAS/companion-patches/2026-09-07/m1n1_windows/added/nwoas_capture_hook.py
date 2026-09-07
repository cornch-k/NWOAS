# NWOAS host-side stage-8 dispatcher-wait capture (READ-ONLY, no kmutil).
# Injected via run_guest.py -m. Uses hv.readmem (proxy guest-VA read) + verified ntoskrnl
# 22621.525 offsets to find the blocked boot thread's wait object at the wedge.
# Break-in: hv.interrupt() (writes "!" -> m1n1 hv_tick HV_USER_INTERRUPT -> handle_exception
# -> our patched run_shell runs the walk, then continues the guest).
import sys, threading, struct, time, traceback
import os
from m1n1.proxy import EXC_RET

# ---------- guest read helpers ----------
def _rd(hv, va, n):
    try:
        b = hv.readmem(va, n)
    except Exception:
        return None
    return b if (b is not None and len(b) == n) else None
def _r64(hv, va):
    b = _rd(hv, va, 8); return struct.unpack("<Q", b)[0] if b else None
def _r32(hv, va):
    b = _rd(hv, va, 4); return struct.unpack("<I", b)[0] if b else None
def _r8(hv, va):
    b = _rd(hv, va, 1); return b[0] if b else None
def _kva(va):
    return (va is not None) and ((va >> 48) == 0xffff)

# ---------- ntoskrnl KASLR base (PE scan; fast guess from idle-WFI rva 0x434b64) ----------
def _pe_ok(hv, base):
    mz = _rd(hv, base, 2)
    if not mz or mz[0] != 0x4D or mz[1] != 0x5A: return False
    peoff = _r32(hv, base + 0x3c)
    if peoff is None or peoff > 0x400: return False
    if _r32(hv, base + peoff) != 0x00004550: return False
    return _r32(hv, base + peoff + 0x28) == 0x9a3340   # AddressOfEntryPoint = KiSystemStartup
def _find_ntos(hv, kpc):
    # fast path: idle WFI is rva 0x434b64 -> base page = (kpc&~0xfff) - 0x434000
    g = (kpc & ~0xfff) - 0x434000
    if _pe_ok(hv, g): return g
    page = kpc & ~0xfff
    for i in range(0x1200):
        base = page - i * 0x1000
        if _pe_ok(hv, base): return base
    return None

OBJTYPE = {0:"Event-Notif",1:"Event-Sync",2:"Mutant",3:"Process",4:"Queue",5:"Semaphore",
           6:"Thread",7:"Gate",8:"Timer-Notif",9:"Timer-Sync"}
STATE = {0:"Init",1:"Ready",2:"Running",3:"Standby",4:"Terminated",5:"Waiting",6:"Transition",
         7:"DeferredReady",8:"GateWait",9:"WaitingUnsafe"}
REASON = ["Executive","FreePage","PageIn","PoolAllocation","DelayExecution","Suspended",
    "UserRequest","WrExecutive","WrFreePage","WrPageIn","WrPoolAllocation","WrDelayExecution",
    "WrSuspended","WrUserRequest","WrEventPair","WrQueue","WrLpcReceive","WrLpcReply",
    "WrVirtualMemory","WrPageOut","WrRendezvous","WrKeyedEvent","WrTerminated","WrProcessInSwap",
    "WrCpuRateControl","WrCalloutStack","WrKernel","WrResource","WrPushLock","WrMutex",
    "WrQuantumEnd","WrDispatchInt","WrPreempted","WrYieldExecution","WrFastMutex","WrGuardedMutex",
    "WrRundown","WrAlertByThreadId","WrDeferredPreempt","WrPhysicalFault","WrIoRing","WrMdlCache"]
def _rn(r): return REASON[r] if (r is not None and r < len(REASON)) else "?"

# ---------- offsets (Task-D CONFIRMED, ntoskrnl 22621.525) ----------
KT_TYPE=0x00; KT_STATE=0x17c; KT_WAITREAS=0x2a3; KT_WBLIST=0xc8; KT_WB0_OBJ=0x158; KT_PROCESS=0x240
WB_WAITTYPE=0x10; WB_BLOCKST=0x11; WB_THREAD=0x18; WB_OBJECT=0x20
DH_SIGNAL=0x04; PRCB_CURTH=0x988; PSISP_RVA=0xd1aa20

def _hex(hv, tag, va, n):
    for o in range(0, n, 8):
        w = _r64(hv, va + o)
        print(f"NW8 RAW {tag}+0x{o:x} VA={va+o:#x} = " + (f"{w:#018x}" if w is not None else "<unmapped>"))

def _report(hv, t, raw=False):
    state = _r8(hv, t+KT_STATE); reason = _r8(hv, t+KT_WAITREAS)
    wbl = _r64(hv, t+KT_WBLIST); objB = _r64(hv, t+KT_WB0_OBJ)
    objA=wtype=bstate=wbthr=None
    if _kva(wbl):
        objA=_r64(hv,wbl+WB_OBJECT); wtype=_r8(hv,wbl+WB_WAITTYPE); bstate=_r8(hv,wbl+WB_BLOCKST); wbthr=_r64(hv,wbl+WB_THREAD)
    obj = objA if _kva(objA) else objB
    otype=sig=None
    if _kva(obj):
        otype=_r8(hv,obj); sig=_r32(hv,obj+DH_SIGNAL)
    to = (otype & 0x7f) if otype is not None else None
    print(f"NW8   t={t:#x} state={state}({STATE.get(state,'?')}) reason={reason}({_rn(reason)})")
    print(f"NW8   wbl={wbl if wbl is None else hex(wbl)} objA={objA if objA is None else hex(objA)} objB={objB if objB is None else hex(objB)} wb.Thread={wbthr if wbthr is None else hex(wbthr)} eq_t={wbthr==t}")
    print(f"NW8   ==> OBJECT={obj if obj is None else hex(obj)} TYPE={to}({OBJTYPE.get(to,'?')}) Signal={sig} WaitType={wtype} BlockState={bstate}")
    if raw:
        _hex(hv,"KT",t,0x30); _hex(hv,"KTst",t+0x170,0x18); _hex(hv,"KTpr",t+0x240,0x10)
        _hex(hv,"KTwb0",t+0x138,0x30)
        if _kva(wbl): _hex(hv,"WBL",wbl,0x30)
        if _kva(obj): _hex(hv,"OBJ",obj,0x18)

# resolve KTHREAD base from a thread-list node (Type==6 & Process==sys_eproc), yields tle offset
def _kt_from_node(hv, node, sys_eproc):
    for d in range(0x40, 0xc08, 8):
        b = node - d
        ty = _r8(hv, b+KT_TYPE)
        if ty != 6: continue
        proc = _r64(hv, b+KT_PROCESS)
        if proc == sys_eproc:
            return b, d
    return None, None
# discover KPROCESS.ThreadListHead in EPROCESS by circular-list invariant + node->KTHREAD
def _find_threadlist(hv, eproc):
    for o in range(0x20, 0x188, 8):
        f = _r64(hv, eproc+o); bl = _r64(hv, eproc+o+8)
        if not _kva(f) or not _kva(bl) or f == eproc+o: continue
        if _r64(hv, bl) != eproc+o: continue          # Blink->Flink == head
        if _r64(hv, f+8) != eproc+o: continue          # Flink->Blink == head
        kt, tle = _kt_from_node(hv, f, eproc)
        if kt: return eproc+o, o, tle
    return None, None, None

# ---------- xHCI (DWC3 @0x502280000) register dump: is there a pending event (IMAN.IP)? ----------
def xhci_dump(hv):
    try:
        X = 0x502280000
        rd32 = lambda pa: (hv.p.read32(pa) & 0xffffffff)
        rd64 = lambda pa: (hv.p.read64(pa) & 0xffffffffffffffff)
        cap0 = rd32(X); caplen = cap0 & 0xff; hciver = (cap0 >> 16) & 0xffff
        op = X + caplen
        hcsp1 = rd32(X + 0x04); nports = (hcsp1 >> 24) & 0xff
        rtsoff = rd32(X + 0x18) & ~0x1f
        rt = X + rtsoff
        usbcmd = rd32(op + 0x00); usbsts = rd32(op + 0x04)
        iman = rd32(rt + 0x20); imod = rd32(rt + 0x24); erstsz = rd32(rt + 0x28)
        erstba = rd64(rt + 0x30); erdp = rd64(rt + 0x38)
        print(f"XHCI caplen={caplen:#x} hciver={hciver:#x} rtsoff={rtsoff:#x} nports={nports}")
        print(f"XHCI USBCMD={usbcmd:#x}(RS={usbcmd&1} HCRST={(usbcmd>>1)&1} INTE={(usbcmd>>2)&1}) USBSTS={usbsts:#x}(HCH={usbsts&1} EINT={(usbsts>>3)&1})")
        print(f"XHCI ★IMAN={iman:#x} (IP={iman&1} IE={(iman>>1)&1}) IMOD={imod:#x} ERSTSZ={erstsz:#x}")
        print(f"XHCI ERSTBA={erstba:#x} ERDP={erdp:#x} (EHB={(erdp>>3)&1})")
        for n in range(max(1, min(nports, 6))):
            ps = rd32(op + 0x400 + n*0x10)
            print(f"XHCI PORTSC[{n}]={ps:#x} (CCS={ps&1} PED={(ps>>1)&1} PLS={(ps>>5)&0xf} PP={(ps>>9)&1} CSC={(ps>>17)&1} PRC={(ps>>21)&1})")
        # if ERSTBA valid, peek the event ring dequeue TRB (is there an unprocessed event?)
        if erstba and (erstba >> 40) == 0:
            seg0 = rd64(erstba); ringbase = seg0
            if ringbase:
                dq = erdp & ~0xf
                trb = [rd32(dq), rd32(dq+4), rd32(dq+8), rd32(dq+12)]
                print(f"XHCI EVT-ring base={ringbase:#x} DQ-TRB@{dq:#x}: {[hex(x) for x in trb]} (type={(trb[3]>>10)&0x3f} C={trb[3]&1})")
    except Exception:
        traceback.print_exc()

# ---------- FL1100 (USB-A 부트 컨트롤러) MSI-경로 덤프 (host EL2 물리, READ-ONLY) ----------
# 상수 검증출처: DTB apple-j274.dtb pcie@690000000, pcie.c, 런타임로그 retest40.
FL_ECAM   = 0x690000000                                 # reg-names "config", 16MB (DTB 확정)
FL_PORTS  = (0x681000000, 0x682000000, 0x683000000)     # reg-names port0/1/2, 각 0x4000 (DTB 확정)
FL_VENDOR = 0x1b73                                       # Fresco Logic
FL_BAR_CPU_FALLBACK = 0x6c0000000                        # pcie_emul BYPASS 대상(검증)
PCI2CPU_ADD = 0x600000000    # DTB ranges: PCI 0xC0000000 <-> CPU 0x6C0000000 (32bit mem win)
# Asahi t8103 포트-MSI 레지스터(=m1n1 T8103가 프로그램 안 함 → EN=0이면 F-A):
P_MSICFG=0x124; P_MSIBASE=0x128; P_MSIADDR=0x168
DOORBELL_HINT = 0xfffff000   # Asahi 기준 doorbell(참고용; Apple는 IOVA/DART 경유라 휴리스틱)

def _cfa(bus, dev, fn, off):        # Apple ECAM: base + (bus<<20)|(devfn<<12)|off
    return FL_ECAM + (bus << 20) + (dev << 15) + (fn << 12) + off

def fl1100_dump(hv):
    try:
        rd32 = lambda pa: (hv.p.read32(pa) & 0xffffffff)
        rd64 = lambda pa: (hv.p.read64(pa) & 0xffffffffffffffff)
        cfg  = lambda b,d,f,o: rd32(_cfa(b,d,f,o))
        print("NWFL ===== FL1100 MSI-path dump (host EL2, READ-ONLY) =====")

        # (A) Apple RC 포트 MSI 변환기 상태 — m1n1 T8103는 미프로그램. EN=0 이면 F-A(변환기 off).
        #     주의: 리셋값일 수 있음. 이 값이 F-A '확정'은 아니고 '강한 정황'.
        for i, pb in enumerate(FL_PORTS):
            cfgv=rd32(pb+P_MSICFG); addrv=rd32(pb+P_MSIADDR); basev=rd32(pb+P_MSIBASE)
            en=cfgv&1; l2=(cfgv>>4)&0xf
            print(f"NWFL PORT[{i}]@{pb:#x} MSICFG={cfgv:#x}(EN={en} L2NUM={l2} nvec={1<<l2 if en else 0}) "
                  f"MSIADDR={addrv:#x} MSIBASE={basev:#x}  [m1n1 T8103 미프로그램; EN=0 예상]")

        # (B) ECAM 스캔: 모든 버스/디바이스 열거 + 각 CMD(MEM/BusMaster) 덤프. FL1100뿐 아니라
        #     Ethernet(0x14e4)/WiFi/RC브리지도 CMD확인 → CMD=0이 FL1100전용인지 apcie전체인지 판정.
        found=[None]
        def find(bus, depth=0):
            if depth>4: return
            for dev in range(32):
                vd=cfg(bus,dev,0,0x00)
                if vd in (0x0,0xffffffff): continue
                vid=vd&0xffff; did=(vd>>16)&0xffff; hdr=(cfg(bus,dev,0,0x0c)>>16)&0x7f
                cmdv=cfg(bus,dev,0,0x04)&0xffff
                print(f"NWFL scan bus{bus} dev{dev}: VID={vid:#06x} DID={did:#06x} hdr={hdr:#x} "
                      f"CMD={cmdv:#06x}(MEM={(cmdv>>1)&1} BM={(cmdv>>2)&1} INTxDis={(cmdv>>10)&1})")
                if vid==FL_VENDOR and found[0] is None: found[0]=(bus,dev,0)
                if hdr==1:
                    # NWOAS Stage 2 diag: did Windows open a memory window behind this bridge?
                    # A CLOSED window (base>limit) => leaf BARs cannot be placed => leaf won't start.
                    bctl=cfg(bus,dev,0,0x18); pri=bctl&0xff; sec=(bctl>>8)&0xff; sub=(bctl>>16)&0xff
                    memv=cfg(bus,dev,0,0x20); mbase=(memv&0xfff0)<<16; mlim=(((memv>>16)&0xfff0)<<16)|0xfffff
                    pmv=cfg(bus,dev,0,0x24); pbase=(pmv&0xfff0)<<16; plim=(((pmv>>16)&0xfff0)<<16)|0xfffff
                    bcmd=cfg(bus,dev,0,0x04)&0xffff
                    print(f"NWFL   BRIDGE bus{bus}dev{dev}: pri={pri} sec={sec} sub={sub} "
                          f"CMD={bcmd:#06x}(MEM={(bcmd>>1)&1} BM={(bcmd>>2)&1}) "
                          f"memwin={mbase:#x}..{mlim:#x}({'OPEN' if mbase<=mlim else 'CLOSED'}) "
                          f"pfwin={pbase:#x}..{plim:#x}({'OPEN' if pbase<=plim else 'CLOSED'})")
                    if sec and sec!=bus: find(sec,depth+1)
        find(0)
        fl=found[0]
        if not fl:
            print("NWFL !! FL1100 NOT FOUND via ECAM (link down / 미열거 / ECAM base 오류). "
                  "config-space 판정 전부 무효 — 아래는 BAR 폴백만 신뢰."); 
            _fl_xhci(hv, rd32, rd64, FL_BAR_CPU_FALLBACK, "BAR-fallback"); 
            print("NWFL ===== dump end (no-cfg) ====="); return
        b,d,f=fl; print(f"NWFL FL1100 @ bus{b} dev{d} fn{f} cfg={_cfa(b,d,f,0):#x}  (VID 자기검증 OK)")
        # NWOAS Stage 2 diag: class code decides whether USBXHCI.sys binds. xHCI = 0x0C0330
        # (class 0x0C SerialBus / sub 0x03 USB / progIF 0x30 xHCI). Wrong => no in-box driver => never starts.
        ccr=cfg(b,d,f,0x08); rev=ccr&0xff; progif=(ccr>>8)&0xff; subcl=(ccr>>16)&0xff; cls=(ccr>>24)&0xff
        capp=cfg(b,d,f,0x34)&0xff if (cfg(b,d,f,0x04)>>16)&0x10 else 0
        capoff={}; cp=capp; _g=0
        while cp and cp!=0xff and _g<16:
            cv=cfg(b,d,f,cp); cid=cv&0xff; capoff.setdefault(cid,cp); cp=(cv>>8)&0xff; _g+=1
        capnames={0x01:"PM",0x05:"MSI",0x10:"PCIe",0x11:"MSI-X"}
        capstr=",".join(capnames.get(c,hex(c)) for c in capoff) or "none"
        print(f"NWFL CLASS={cls:#04x}/{subcl:#04x}/{progif:#04x} rev={rev:#x} "
              f"({'xHCI-OK' if (cls,subcl,progif)==(0x0c,0x03,0x30) else 'NOT-xHCI!'}) caps=[{capstr}]")
        # NWOAS redo-diag: WHY not started? power state (PMCSR D-state), PCIe error/pending status.
        # D3 => Windows couldn't power it to D0. UR/error bits => a failed transaction. TxPend => stuck.
        if 0x01 in capoff:
            pm=cfg(b,d,f,capoff[0x01]+4)&0xffff; ds=pm&3
            print(f"NWFL PM: PMCSR={pm:#06x} power=D{ds} PMEstat={(pm>>15)&1} {'<<NOT D0 (unpowered)' if ds else '(D0 ok)'}")
        if 0x10 in capoff:
            pc=capoff[0x10]; dc=cfg(b,d,f,pc+8); dsts=(dc>>16)&0xffff
            lk=(cfg(b,d,f,pc+0x10)>>16)&0xffff
            print(f"NWFL PCIe: DevCtl={dc&0xffff:#06x} DevSts={dsts:#06x}"
                  f"(CorrErr={dsts&1} NonFatal={(dsts>>1)&1} Fatal={(dsts>>2)&1} UnsuppReq={(dsts>>3)&1} TxPend={(dsts>>5)&1}) "
                  f"LnkSts={lk:#06x}(gen{lk&0xf} x{(lk>>4)&0x3f})")

        # (C) Command/Status + BAR0. BusMaster=0 이면 MSI DMA 자체 불가.
        cs=cfg(b,d,f,0x04); cmd=cs&0xffff
        print(f"NWFL CMD={cmd:#06x}(IO={cmd&1} MEM={(cmd>>1)&1} BusMaster={(cmd>>2)&1} INTxDis={(cmd>>10)&1}) STS={(cs>>16):#06x}")
        bar0=cfg(b,d,f,0x10); bar1=cfg(b,d,f,0x14); is64=((bar0>>1)&3)==2
        pci_base=(bar0&~0xf)|((bar1<<32) if is64 else 0)
        cpu_base=(pci_base+PCI2CPU_ADD) if pci_base else FL_BAR_CPU_FALLBACK
        print(f"NWFL BAR0={bar0:#x} BAR1={bar1:#x} PCI_base={pci_base:#x} -> CPU_base={cpu_base:#x} (DTB ranges +{PCI2CPU_ADD:#x})")
        # NWOAS Stage 2 diag: interrupt routing + BAR-assigned verdict. If BAR0==0 => Windows never
        # placed the BAR (resource-assignment stopped before StartDevice). pin=1 => INTA => _PRT->698.
        intr=cfg(b,d,f,0x3c); iline=intr&0xff; ipin=(intr>>8)&0xff
        print(f"NWFL INT: line={iline} pin={ipin}({'INTA' if ipin==1 else 'none' if ipin==0 else 'INT'+chr(64+ipin)}) "
              f"| BAR0_assigned={'NO(=0, unplaced)' if not (bar0&~0xf) else 'YES'} "
              f"| verdict: {'Windows never assigned BAR -> StartDevice not reached' if not (bar0&~0xf) else 'BAR placed; check driver/interrupt'}")

        # (D) capability list: MSI(0x05)/MSI-X(0x11) — Windows가 무엇으로/어디로 무장했나.
        cp=cfg(b,d,f,0x34)&0xff; msi=msix=None; g=0
        while cp and g<48:
            g+=1; h=cfg(b,d,f,cp); cid=h&0xff
            if cid==0x05: msi=cp
            elif cid==0x11: msix=cp
            cp=(h>>8)&0xff
        if msi is not None:
            mc=(cfg(b,d,f,msi)>>16)&0xffff; en=mc&1; c64=(mc>>7)&1; mmen=(mc>>4)&7
            alo=cfg(b,d,f,msi+0x04); ahi=cfg(b,d,f,msi+0x08) if c64 else 0
            data=cfg(b,d,f,msi+(0x0c if c64 else 0x08))&0xffff
            addr=alo|(ahi<<32); door=(addr&~0xfff)==DOORBELL_HINT
            print(f"NWFL MSI@{msi:#x} EN={en} 64b={c64} multi_en={mmen} ADDR={addr:#x}(PCI/IOVA) DATA={data:#x} "
                  f"{'==Asahi-doorbell' if door else '!=doorbell (GIC ITS/LPI 또는 미프로그램)'}")
            if en: print(f"NWFL MSI  => 변환성공 시 AIC SPI = 0x2c0+(vec) ∈ [704,735] (DTB msi-ranges)")
        else: print("NWFL MSI cap: none")
        if msix is not None:
            xc=(cfg(b,d,f,msix)>>16)&0xffff; xen=(xc>>15)&1; xm=(xc>>14)&1; ts=(xc&0x7ff)+1
            tbl=cfg(b,d,f,msix+0x04); bir=tbl&7; toff=tbl&~7
            print(f"NWFL MSIX@{msix:#x} EN={xen} FuncMask={xm} TableSize={ts} BIR={bir} Off={toff:#x}")
            if xen and bir==0 and cpu_base:
                te=cpu_base+toff; a=rd32(te)|(rd32(te+4)<<32); dt=rd32(te+8); vc=rd32(te+12)
                print(f"NWFL MSIX[0] ADDR={a:#x} DATA={dt:#x} VCTRL={vc:#x}(mask={vc&1})")
        else: print("NWFL MSIX cap: none")

        # (E) FL1100 xHCI interrupter — HW가 이벤트를 만들었나 (IMAN.IP) + 디바이스 연결(CCS).
        # ★가드: MEM=0(BAR 디코드 꺼짐)이면 BAR MMIO 읽기가 m1n1 data abort(L2C_ERR) 유발 →
        # 읽지 않음. Command.MEM(bit1)이 1일 때만 xHCI MMIO 접근.
        if (cmd >> 1) & 1:
            _fl_xhci(hv, rd32, rd64, cpu_base, "cfg-derived")
        else:
            print("NWFL XHCI skip: CMD.MEM=0 (BAR 디코드 꺼짐) — MMIO 읽으면 data abort. 컨트롤러 미시작.")
        print("NWFL ===== 판정축: HWevent=IMAN.IP / MSIarmed=MSI(X).EN+ADDR / PortConv=PORT_MSICFG.EN / dev=CCS =====")
    except Exception:
        traceback.print_exc()

def _fl_xhci(hv, rd32, rd64, X, tag):
    try:
        cap0=rd32(X); caplen=cap0&0xff
        if cap0==0xffffffff or caplen in (0x00,0xff):
            print(f"NWFL XHCI[{tag}] base={X:#x} caplen={caplen:#x} cap0={cap0:#x} => MMIO DEAD "
                  f"(all-{'1' if cap0==0xffffffff else '0'}s; PCI MEM decode off 또는 미매핑) — 컨트롤러 미시작"); return
        rtsoff=rd32(X+0x18)&~0x1f; op=X+caplen; rt=X+rtsoff
        nports=(rd32(X+0x04)>>24)&0xff
        usbcmd=rd32(op+0x00); usbsts=rd32(op+0x04); iman=rd32(rt+0x20); imod=rd32(rt+0x24)
        erstba=rd64(rt+0x30); erdp=rd64(rt+0x38)
        print(f"NWFL XHCI[{tag}] base={X:#x} caplen={caplen:#x} rtsoff={rtsoff:#x} nports={nports}")
        print(f"NWFL XHCI USBCMD={usbcmd:#x}(RS={usbcmd&1} INTE={(usbcmd>>2)&1}) USBSTS={usbsts:#x}(HCH={usbsts&1} CNR={(usbsts>>11)&1} EINT={(usbsts>>3)&1})")
        print(f"NWFL XHCI ★IMAN={iman:#x}(IP={iman&1} IE={(iman>>1)&1}) IMOD={imod:#x} ERSTBA={erstba:#x} ERDP={erdp:#x}(EHB={(erdp>>3)&1})")
        # NWOAS Stage3: init-progress registers. Windows drives these in order during StartDevice:
        # CONFIG(MaxSlots) -> DCBAAP -> CRCR -> ERSTSZ/ERSTBA -> USBCMD.RS=1. Nonzero DCBAAP/CRCR
        # => Windows set up DMA structures (the _DMA window works). CONFIG!=0 => reached config.
        config=rd32(op+0x38); dcbaap=rd64(op+0x30); crcr=rd64(op+0x18); erstsz=rd32(rt+0x28)
        print(f"NWFL XHCI CONFIG={config:#x}(MaxSlotsEn={config&0xff}) DCBAAP={dcbaap:#x} CRCR={crcr:#x} ERSTSZ={erstsz:#x} "
              f"=> init: {'RUN' if usbcmd&1 else ('ring-DMA-set' if (dcbaap or crcr) else ('touched(INTE)' if usbcmd else 'untouched'))}")
        for n in range(max(1,min(nports,6))):
            ps=rd32(op+0x400+n*0x10)
            print(f"NWFL XHCI PORTSC[{n}]={ps:#x}(CCS={ps&1} PED={(ps>>1)&1} PLS={(ps>>5)&0xf} PP={(ps>>9)&1} CSC={(ps>>17)&1})")
    except Exception:
        traceback.print_exc()

# ---------- ntoskrnl backtrace + waiting-thread 분류 (pefile 불요; host 파일 struct 파싱) ----------
import bisect
NT_PATH = os.environ.get("NWOAS_NTOS",
    # NWOAS 2026-07-08: 이전 기본값은 /tmp scratchpad(세션 소멸시 사라짐)였음.
    # X31의 세 사본(iw1, bw1tree, bw2tree)은 바이트 동일(sha1 89527cb0) = BT_ANCHORS 추출원과 동일 22621.525 커널.
    "/Volumes/X31/NWOAS/bw2tree/System32/ntoskrnl.exe")
BT_KERNELSTACK=0x58; BT_LR=0x58; BT_FP=0x50   # 디스어셈으로 확정된 스위치프레임 오프셋
# 앵커(비수출 함수 포함) + 수출 I/O 앵커(전부 rva 실측 확인):
BT_ANCHORS={0x2884f8:'KiCommitThreadWait',0x20c5b0:'KiSwapContext',0x2a3948:'SwapContext',
 0x255840:'KeWaitForSingleObject',0x254c50:'KeWaitForMultipleObjects',0x2541c0:'KeDelayExecutionThread',
 0x26caa0:'KeRemoveQueueEx',0x26c9c0:'KeRemoveQueue',0x434b50:'HalProcessorIdle',0x9a3340:'KiSystemStartup',
 0x247690:'IofCallDriver',0x2446c0:'IoCallDriver',0x787d10:'IoBuildSynchronousFsdRequest',
 0x244590:'IoBuildDeviceIoControlRequest',0x7913a0:'NtDeviceIoControlFile',0x247070:'IoStartPacket'}
# 34개 harvested StartRoutine(워커/배경) — 여기에 매칭되면 부트스레드가 아님(감산분류).
BT_SR={0x46fe50:'worker',0x490580:'worker',0x53d530:'worker',0x230de0:'bg',0x311a80:'bg',0x322210:'bg',
 0x3efbd0:'bg',0x47b9f0:'bg',0x22f0c0:'bg',0x4622c0:'bg',0x8a4130:'bg',0x535340:'bg',0x3f00c0:'bg',
 0x5c8f90:'bg',0x5cf480:'bg',0x4aaa10:'bg',0x62ac60:'bg',0x4bf1d0:'bg',0x9aa790:'bg',0x6bf6f0:'bg',
 0x6c9220:'bg',0x6d9e10:'bg',0x553860:'bg',0x3969a0:'bg',0x3d42c0:'bg',0x72ec20:'bg',0x6da5f0:'bg',
 0x9ab620:'bg',0x9d47b0:'bg',0x9dc6a0:'bg',0x634f70:'bg',0x5b0420:'bg',0x687b40:'bg'}
BT_IO_SIG=('IofCallDriver','IoCallDriver','IoBuildSynchronousFsdRequest',
           'IoBuildDeviceIoControlRequest','NtDeviceIoControlFile','IoStartPacket','FsRtl')
_BT={'exp':None,'rvas':None,'fn':None,'ok':False}
def _bt_load():
    if _BT['exp'] is not None: return _BT['ok']
    _BT['exp']=[]; _BT['rvas']=[]; _BT['fn']=[]
    try: d=open(NT_PATH,'rb').read()
    except Exception as e:
        print(f"NWBT symbols: cannot open {NT_PATH}: {e!r} (degrade: 앵커/nearest-baked만)"); return False
    try:
        e=struct.unpack_from('<I',d,0x3c)[0]
        if d[e:e+4]!=b'PE\x00\x00': raise ValueError('no PE')
        nsec=struct.unpack_from('<H',d,e+6)[0]; szopt=struct.unpack_from('<H',d,e+20)[0]; opt=e+24
        ddir=opt+112; exp_va,_=struct.unpack_from('<II',d,ddir+0*8); exc_va,exc_sz=struct.unpack_from('<II',d,ddir+3*8)
        sects=[(struct.unpack_from('<I',d,opt+szopt+i*40+12)[0],
                max(struct.unpack_from('<I',d,opt+szopt+i*40+8)[0],struct.unpack_from('<I',d,opt+szopt+i*40+16)[0]),
                struct.unpack_from('<I',d,opt+szopt+i*40+20)[0]) for i in range(nsec)]
        def r2o(r):
            for vva,vsz,rp in sects:
                if vva<=r<vva+vsz: return rp+(r-vva)
            return None
        eo=r2o(exp_va); nnam=struct.unpack_from('<I',d,eo+0x18)[0]
        ofun=r2o(struct.unpack_from('<I',d,eo+0x1c)[0]); onam=r2o(struct.unpack_from('<I',d,eo+0x20)[0]); oord=r2o(struct.unpack_from('<I',d,eo+0x24)[0])
        ex=[]
        for i in range(nnam):
            no=r2o(struct.unpack_from('<I',d,onam+i*4)[0]); nm=d[no:d.index(b'\x00',no)].decode('latin1')
            ex.append((struct.unpack_from('<I',d,ofun+struct.unpack_from('<H',d,oord+i*2)[0]*4)[0],nm))
        ex.sort(); _BT['exp']=ex; _BT['rvas']=[a for a,_ in ex]
        po=r2o(exc_va); fs=[struct.unpack_from('<I',d,po+i*8)[0] for i in range(exc_sz//8)]
        _BT['fn']=sorted(x for x in fs if x); _BT['ok']=True
        print(f"NWBT symbols: {len(ex)} exports, {len(_BT['fn'])} pdata funcs loaded (AoE check via KiSystemStartup)")
    except Exception as ex:
        print(f"NWBT symbols: parse failed {ex!r} (degrade mode)")
    return _BT['ok']
def _bt_fstart(rva):
    fn=_BT['fn']
    if not fn: return None
    i=bisect.bisect_right(fn,rva)-1; return fn[i] if i>=0 else None
def _bt_sym(rva):
    fs=_bt_fstart(rva); key=fs if fs is not None else rva; off=rva-key; suf='' if off==0 else '+0x%x'%off
    if key in BT_ANCHORS: return BT_ANCHORS[key]+suf
    if key in BT_SR: return 'SR<%s>@0x%x'%(BT_SR[key],key)+suf
    rv=_BT['rvas']
    if rv:
        i=bisect.bisect_right(rv,key)-1
        if i>=0:
            b,n=_BT['exp'][i]; return (n if b==key else '%s+0x%x'%(n,key-b))+suf
    return 'func_0x%x'%key+suf
def _bt_strip(v): return (v&0x0000FFFFFFFFFFFF)|0xFFFF000000000000
def _bt_unwind(hv, kt, ntbase, maxf=40):
    ks=_r64(hv, kt+BT_KERNELSTACK)
    if not _kva(ks): return ks,[]
    lr0=_r64(hv, ks+BT_LR)
    if lr0 is None: return ks,[]
    frames=[( _bt_strip(lr0), _bt_strip(lr0)-ntbase )]
    fp=_r64(hv, ks+BT_FP); last=0
    for _ in range(maxf):
        if not _kva(fp) or fp<=last or (fp&0xf): break
        ret=_r64(hv, fp+8); nfp=_r64(hv, fp)
        if ret is None or nfp is None: break
        r=_bt_strip(ret); frames.append((r, r-ntbase)); last=fp; fp=nfp
    return ks,frames
_MODS=[None]
def _load_modules(hv, ntbase):
    # PsLoadedModuleList 순회 → [(base,size,name)] : driver?_ 프레임을 드라이버명(.sys)으로 해석.
    if _MODS[0] is not None: return _MODS[0]
    mods=[]
    try:
        _bt_load()
        plm=None
        for rva,nm in _BT['exp']:
            if nm=='PsLoadedModuleList': plm=ntbase+rva; break
        if plm:
            head=plm; node=_r64(hv, head); n=0
            while _kva(node) and node!=head and n<512:
                n+=1
                base=_r64(hv, node+0x30); size=_r32(hv, node+0x40)
                nl=_r32(hv, node+0x58); buf=_r64(hv, node+0x60)   # BaseDllName UNICODE_STRING
                name='?'; length=(nl&0xffff) if nl else 0
                if _kva(buf) and 0<length<=520:
                    raw=_rd(hv, buf, length)
                    if raw:
                        try: name=raw.decode('utf-16-le','replace')
                        except Exception: name='?'
                if base and size and _kva(base): mods.append((base,size,name))
                node=_r64(hv, node)
            print(f"NWBT modules: {len(mods)} drivers mapped (PsLoadedModuleList@{plm:#x})")
        else:
            print("NWBT modules: PsLoadedModuleList export 못찾음 (driver frames는 raw주소로)")
    except Exception:
        traceback.print_exc()
    _MODS[0]=sorted(mods); return _MODS[0]
def _mod_name(addr, mods):
    if not mods: return None
    i=bisect.bisect_right([m[0] for m in mods], addr)-1
    if 0<=i<len(mods):
        base,size,name=mods[i]
        if base<=addr<base+size: return f"{name}+0x{addr-base:x}"
    return None
def _bt_classify(hv, kt, ntbase, mods=None, imgsize=0x2000000):
    ks,frames=_bt_unwind(hv, kt, ntbase)
    named=[]
    for pc,rva in frames:
        if 0<=rva<imgsize: nm=_bt_sym(rva)
        else: nm=_mod_name(pc, mods) or ('driver?_%x'%pc)
        named.append((pc,rva,nm))
    frame0_ok = any(n[2].startswith('KiCommitThreadWait') for n in named[:3])
    fstarts=[_bt_fstart(rva) for _,rva,_ in named if 0<=rva<imgsize]
    known_sr=[f for f in fstarts if f in BT_SR]
    role = BT_SR.get(known_sr[0]) if known_sr else 'UNKNOWN-SR'
    drv=[nm for _,_,nm in named if ('.sys' in nm.lower() or nm.startswith('driver?'))]
    io = any(any(s in nm for s in BT_IO_SIG) for _,_,nm in named) or bool(drv)
    return dict(kt=kt, ks=ks, frame0_ok=frame0_ok, role=role, io=io, drv=drv, frames=named)
def bt_report(hv, kts, ntbase):
    _bt_load()
    mods=_load_modules(hv, ntbase)
    print(f"NWBT ===== backtrace/분류: {len(kts)} waiting threads (ntbase={ntbase:#x}) =====")
    res=[_bt_classify(hv, kt, ntbase, mods) for kt in kts]
    # 부트/Phase1 후보 랭킹: io-wait 우선, 그다음 UNKNOWN-SR, 마지막 worker/bg
    rank=lambda r:(0 if r['io'] else (1 if r['role']=='UNKNOWN-SR' else 2))
    for r in sorted(res, key=rank):
        tag = 'BOOT/PHASE1 후보(IO-WAIT)' if r['io'] else ('후보(UNKNOWN-SR)' if r['role']=='UNKNOWN-SR' else '['+str(r['role'])+']')
        warn = '' if r['frame0_ok'] else '  !!frame0!=KiCommitThreadWait(대기스레드 아님/스킵권장)'
        drvs=r.get('drv',[])
        print(f"NWBT kt={r['kt']:#x} KernelStack={r['ks'] if r['ks'] is None else hex(r['ks'])} role={r['role']} io={r['io']} drv={drvs[:3]} {tag}{warn}")
        for pc,rva,nm in r['frames'][:16]:
            print(f"NWBT     {pc:#018x}  rva {rva:#010x}  {nm}")
    # 드라이버 집계: io-wait 스레드들이 어느 드라이버(.sys)에서 대기하나 — 진짜 블로커 지목
    from collections import Counter
    alld=Counter(nm.split('+')[0] for r in res for nm in r.get('drv',[]) if '.sys' in nm.lower())
    print(f"NWBT ★드라이버 집계(대기중 .sys, 많은순): {alld.most_common(12)}")
    print("NWBT ===== 끝: IO-WAIT+대기객체(Event/Sem Signal=0)+드라이버명 교차 = 진짜 부트스레드/블로커 =====")

# ---------- NWLW: low-window (IPA 0x80000000) pool-inclusion probe (EXP-2) ----------
# Decisive EXP-2 test: did Windows fold the sub-4GB alias window [IPA 0x80000000, 0xA0000000)
# into its FREE physical-page pool? If yes, kernel pool blocks (a 16B POOL_HEADER whose 4-char
# ASCII PoolTag sits at +4) are scattered through the backing physical RAM. If Windows never took
# the window as usable RAM, the backing holds only UEFI leftovers (mostly zero / firmware structs,
# ~no pool tags). Answers the frontier: "Windows won't pick the low window for DCBAAP because it
# isn't RAM to Windows." READ-ONLY, one-shot at break-in, only a few tens of KB (serial-drop budget).
LW_IPA_BASE   = 0x80000000                 # guest-physical base of the low alias window
LW_IPA_SIZE   = 0x20000000                 # 512MB window size (== UEFI SYSTEM_MEMORY HOB)
LW_DART_BACK  = 0xBC0FCC000                 # UEFI DART backing PA (task-stated; 0x34000=208KB below hv alias)
_PTE_TARGET_MASK = 0x0003FFFFFFFFC000      # GENMASK(49,14): m1n1 16KB-granule stage-2 PTE PA field
# A subset of common non-paged / boot pool tags. Stored little-endian => bytes read left-to-right as
# the string (e.g. 'MmSt' == b'MmSt'). A hit here is HIGH-confidence "this physical RAM is Windows pool".
_KNOWN_POOL_TAGS = {
    'MmSt','Mm  ','MmCa','MmDb','MmRe','File','Ntf ','Ntfx','NtfF','FMsl','FMfl','Even','EtwB','EtwR',
    'Toke','Thre','Proc','PsWs','PspT','Vad ','VadS','Vadl','Se  ','SeSd','SeGa','ObNm','Obtb','ObDi',
    'Dire','CM  ','CM25','CM31','CM44','CcSc','CcVa','MDL ','Irp ','IrpM','Devi','Driv','Gpnp','PnpD',
    'PnpF','Wait','KeyE','Cont','Ttfd','Usbx','Ucx ','usbp','UsbP','Uhub','Ubhb','Ubhc','PciB','Pool',
    'ViMm','PfMd','PfSN','Ppdm','LSwi','TmTm','TmTr','VmMp','KSem','Ffvp','Wfsp','8042','DxgK','Wdf ',
    'FSfm','InPA','Iogn','LStr','AlMs','PsJb','RfSt','usbc','Gh05','Gh08','Fatx','FatV','Ntff','Sysm',
}
def _lw_is_tag(b4):
    # b4: 4 bytes with the protected-pool high bit already masked off the 4th char. Pattern
    # = [A-Za-z][A-Za-z0-9 ]{3} (task-specified). First char must be a letter (rejects most noise).
    c0 = b4[0]
    if not (65 <= c0 <= 90 or 97 <= c0 <= 122):
        return False
    for c in (b4[1], b4[2], b4[3]):
        if not (65 <= c <= 90 or 97 <= c <= 122 or 48 <= c <= 57 or c == 32):
            return False
    return True
def _lw_pool_scan(buf):
    # Scan 16-byte-granular POOL_HEADER candidates: the PoolTag ULONG lives at +4 of a header, pool
    # blocks are 16B-granular and the buffer is page-aligned, so tag positions land at buf[4,20,36,...].
    # bit31 of PoolTag = "protected pool" flag -> mask 0x80 off the 4th byte before the ASCII test.
    hits = {}; total = 0; n = len(buf); off = 4
    while off + 4 <= n:
        cand = bytes((buf[off], buf[off+1], buf[off+2], buf[off+3] & 0x7f))
        if _lw_is_tag(cand):
            t = cand.decode('latin1'); hits[t] = hits.get(t, 0) + 1; total += 1
        off += 16
    known = {t: c for t, c in hits.items() if t in _KNOWN_POOL_TAGS}
    return total, hits, known
def _lw_stats(buf):
    nz = sum(1 for c in buf if c != 0)
    first_nz = next((i for i, c in enumerate(buf) if c != 0), None)
    return nz, first_nz, len(set(buf))

def lowwindow_probe(hv, ntos=None):
    print("NWLW ===== low-window pool-inclusion probe (EXP-2, host EL2, READ-ONLY, one-shot) =====")
    try:
        alias_pa = hv.u.ba.phys_base + hv.u.ba.mem_size - LW_IPA_SIZE   # == hv map_hw target == UEFI SysMemTop
    except Exception as _e:
        alias_pa = 0xbc1000000
        print(f"NWLW  (ba unavailable {_e!r}; using task-stated alias 0xbc1000000)")
    print(f"NWLW  alias_PA(hv map_hw target)={alias_pa:#x} DART_backing={LW_DART_BACK:#x} skew={alias_pa-LW_DART_BACK:#x}")
    SCAN = max(0x1000, int(os.environ.get("NWLW_SCAN_KB", "16")) * 1024)   # primary pool-scan window / region
    def rd_phys(pa, n):
        try:
            b = hv.iface.readmem(pa, n)                # physical read via proxy (same path as NWBACK)
            return bytes(b) if (b is not None and len(b) == n) else None
        except Exception as _e:
            print(f"NWLW  readmem({pa:#x},{n}) err: {_e!r}"); return None
    scan_summ = {}
    for name, base in (("alias", alias_pa), ("dart", LW_DART_BACK)):
        buf = rd_phys(base, SCAN)
        if buf is None:
            print(f"NWLW  [{name}] base={base:#x} READ FAILED (링크드롭/미매핑) — 이 리전 판정 무효"); continue
        nz, fnz, dist = _lw_stats(buf)
        total, hits, known = _lw_pool_scan(buf)
        top = sorted(hits.items(), key=lambda kv: -kv[1])[:16]
        sample = ", ".join(f"'{t}'x{c}" for t, c in top) or "(none)"
        ktop = dict(sorted(known.items(), key=lambda kv: -kv[1])[:12])
        print(f"NWLW  [{name}] base={base:#x} scan={len(buf)}B nonzero={nz}/{len(buf)} first_nz={fnz} distinct_bytes={dist}")
        print(f"NWLW  [{name}] pooltag_hits={total} unique={len(hits)} known_tags={len(known)} known={ktop}")
        print(f"NWLW  [{name}] samples: {sample}")
        scan_summ[name] = (nz, total, len(hits), len(known))
    # 512MB window zero-survey (light 64B probes across the whole alias window)
    print("NWLW  --- 512MB alias-window zero-survey (64B probes) ---")
    surveyed = nonzero_windows = 0
    for frac in (0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 0.99):
        off = int(LW_IPA_SIZE * frac) & ~0x3f
        b = rd_phys(alias_pa + off, 64)
        if b is None: continue
        surveyed += 1; nz = sum(1 for c in b if c != 0); nonzero_windows += 1 if nz else 0
        print(f"NWLW  survey +{off:#010x} nonzero={nz}/64 head={b[:16].hex()}")
    print(f"NWLW  survey: {nonzero_windows}/{surveyed} probed windows nonzero")
    # IPA stage-2 cross-check: resolve IPA 0x80000000 -> PA via m1n1 stage-2 PT walk (hv_pt_walk),
    # confirm it equals the alias PA and that content read via that PA matches the alias read.
    try:
        pte = hv.p.hv_pt_walk(LW_IPA_BASE)
        pa_res = pte & _PTE_TARGET_MASK; is_hw = bool(pte & 1)
        print(f"NWLW  stage2 pt_walk(IPA {LW_IPA_BASE:#x}) pte={pte:#x} -> PA={pa_res:#x} IS_HW={is_hw} eq_alias={pa_res==alias_pa}")
        a = rd_phys(alias_pa, 64); r = rd_phys(pa_res, 64) if pa_res else None
        if a is not None and r is not None:
            print(f"NWLW  cross-check alias[:16]={a[:16].hex()} ipa_pa[:16]={r[:16].hex()} MATCH={a==r}")
    except Exception as _e:
        print(f"NWLW  stage2 cross-check skipped: {_e!r}")
    # KeLoaderBlock pointer (exported; boot-only). Informational only — descriptor-list 파싱은 버전별
    # 구조체 추정이 필요해 회피(무리한 심볼추정 금지). NULL=boot완료 정상, non-NULL=stale.
    if ntos:
        try:
            _bt_load()
            klb_rva = next((rva for rva, nm in _BT['exp'] if nm == 'KeLoaderBlock'), None)
            if klb_rva:
                klb = _r64(hv, ntos + klb_rva)
                print(f"NWLW  KeLoaderBlock@{ntos+klb_rva:#x} -> {klb if klb is None else hex(klb)} "
                      f"({'NULL=boot완료(정상; 저메모리판정엔 무의미)' if not klb else 'non-NULL(stale/boot-only; descriptor list 파싱안함)'})")
            else:
                print("NWLW  KeLoaderBlock export 없음 — skip (심볼추정 회피)")
        except Exception as _e:
            print(f"NWLW  KeLoaderBlock read err: {_e!r}")
    a = scan_summ.get("alias", (0, 0, 0, 0)); d = scan_summ.get("dart", (0, 0, 0, 0))
    print(f"NWLW ===== 판정: alias(nz={a[0]} tags={a[1]} known={a[3]}) dart(nz={d[0]} tags={d[1]} known={d[3]}) =====")
    print("NWLW 판정표: known>=5 & pooltag_hits多수 => 저창이 Windows커널풀로 편입됨(가설 반증 -> EXP-4/5 어댑터/_DMA 경로) ; "
          "backing 전부 0(nz≈0)/tags≈0 => 저창 미편입(가설 지지 -> EXP-5 RPi4식 저RAM강제가 정답) ; "
          "nonzero지만 tags≈0 => UEFI잔재만(미편입에 가까움) ; MATCH=False => alias/stage2 backing 불일치(별도 조사)")

def walk(hv):
    try:
        xhci_dump(hv)
        fl1100_dump(hv)                      # <<< 추가 1: FL1100 MSI-경로 덤프
        ctx = hv.ctx
        x18 = ctx.regs[18]; elr = ctx.elr
        print("NW8 ================= stage8 dispatcher-wait dump (host-side, READ-ONLY) =================")
        print(f"NW8 elr={elr:#x} x18={x18:#x} kpcr(x18&~fff)={x18&~0xfff:#x}")
        # ★robust ntos-find: guest VBAR_EL1 (exception vector table) is ALWAYS inside ntoskrnl
        # (rva < SizeOfImage ~17MB), so scanning back from it finds ntos even when the break-in
        # caught the guest busy in a driver (elr/x18 can be >18MB from ntos -> scan misses).
        vbar = None
        try:
            from m1n1.sysreg import VBAR_EL12
            vbar = hv.u.mrs(VBAR_EL12)               # noqa: F821  (EL1 VBAR via VHE alias)
        except Exception:
            vbar = getattr(hv, "vbar_el1", None)
        ntos = (_find_ntos(hv, vbar) if _kva(vbar) else None) or _find_ntos(hv, elr) or _find_ntos(hv, x18)
        print(f"NW8 ntos_base={ntos if ntos is None else hex(ntos)} (vbar={vbar if vbar is None else hex(vbar)})")
        # NWOAS Stage 2 diag: is the xHCI driver even loaded? Distinguishes "no driver bound
        # (class-code/DB mismatch)" from "driver loaded but StartDevice failed (interrupt/BAR)".
        if ntos:
            try:
                _mods = _load_modules(hv, ntos)
                _usb = sorted({nm for _,_,nm in _mods if any(k in nm.lower() for k in
                       ('xhci','usbhub','usbport','usbccgp','usbstor','ucx01000','usbd','frescolog'))})
                _pci = sorted({nm for _,_,nm in _mods if 'pci' in nm.lower()})
                print(f"NW8 ★USB/xHCI drivers: {_usb or 'NONE-LOADED'} | pci-side: {_pci[:5]}")
            except Exception as _e:
                print(f"NW8 USB-driver check err: {_e}")
        cur = _r64(hv, (x18 & ~0xfff) + PRCB_CURTH)
        if _kva(cur):
            print(f"NW8 idle CurrentThread={cur:#x} state={STATE.get(_r8(hv,cur+KT_STATE),'?')}")
        if not ntos:
            print("NW8 FAIL: ntos base not found; raw KPCR page:"); _hex(hv,"KPCR",x18&~0xfff,0x40); return
        sysproc = _r64(hv, ntos+PSISP_RVA)
        print(f"NW8 PsInitialSystemProcess={sysproc if sysproc is None else hex(sysproc)}")
        if not _kva(sysproc):
            print("NW8 FAIL: PsInitialSystemProcess deref bad"); return
        tlh, tlh_off, tle_off = _find_threadlist(hv, sysproc)
        print(f"NW8 SystemEPROC={sysproc:#x} ThreadListHead={tlh if tlh is None else hex(tlh)} KPROC.off={tlh_off if tlh_off is None else hex(tlh_off)} KT.tle_off={tle_off if tle_off is None else hex(tle_off)}")
        _hex(hv,"EPROC",sysproc,0x80)
        if not tlh:
            print("NW8 FAIL: ThreadListHead not found (EPROC raw above)"); return
        node = _r64(hv, tlh); n=0; waiting=0; rawbudget=8
        waiting_kts = []                     # <<< 추가 2: 대기스레드 수집
        while _kva(node) and node != tlh and n < 400:
            kt = node - tle_off
            ty = _r8(hv, kt+KT_TYPE)
            if ty == 6:
                st = _r8(hv, kt+KT_STATE)
                if st == 5:
                    waiting += 1; waiting_kts.append(kt)
                    raw = rawbudget > 0; rawbudget -= 1 if raw else 0
                    print(f"NW8 [{n}] node={node:#x} kt={kt:#x} WAITING")
                    _report(hv, kt, raw)
            node2 = _r64(hv, node); node = node2; n += 1
        print(f"NW8 ===== done: {n} threads walked, {waiting} waiting =====")
        bt_report(hv, waiting_kts, ntos)     # <<< 추가 3: backtrace/분류 (ntbase=ntos)
        proc_walk(hv, ntos, sysproc, tlh_off, tle_off)   # <<< 추가 4: 전체 프로세스 열거
        # NWOAS: dump the low-window BACKING physical to decide whether Windows treats the low
        # alias window as usable RAM. backing = phys_base + mem_size - 512MB (== the alias target,
        # == UEFI SystemMemoryTop). If Windows reclaimed the window as available RAM it will have
        # written pool/zeroed-page data (nonzero); if not available to Windows, only UEFI leftovers.
        # This is the decisive check for the "Windows won't pick the low window for DCBAAP" frontier.
        try:
            _bk = hv.u.ba.phys_base + hv.u.ba.mem_size - 0x20000000
            for _off in (0x0, 0x1000000, 0x8000000, 0x10000000, 0x1f000000):
                _s = hv.iface.readmem(_bk + _off, 64)                      # noqa: F821  (physical read via proxy)
                _nz = sum(1 for _b in _s if _b != 0)
                print(f"NWBACK backing_PA={_bk+_off:#x} (+{_off:#x}) nonzero={_nz}/64 head={bytes(_s[:24]).hex()}")
        except Exception as _e:
            print(f"NWBACK error: {_e}")
        # NWOAS EXP-2: richer low-window pool-inclusion probe (superset of NWBACK; decides whether
        # Windows folded the sub-4GB alias window into its free page pool). Own try so it can never
        # break the existing NWFB/marker_scan flow below.
        try:
            lowwindow_probe(hv, ntos)
        except Exception as _e:
            print(f"NWLW error: {_e}")
        # NWOAS DISPLAY frontier: read the framebuffer (BootArgs video.base = the DCP scanout FB that
        # iBoot set up, and that SimpleFbDxe exposes via GOP). If Windows' BasicDisplay is drawing,
        # these pages are nonzero and change; if Windows never grabbed the GOP FB they stay as the
        # UEFI/boot logo (or zero). Decides display case (a) Windows not drawing vs (b/c) drawing-but-no-output.
        try:
            _fb = hv.u.ba.video.base
            for _fo in (0x0, 0x80000, 0x100000, 0x200000):
                _fs = hv.iface.readmem(_fb + _fo, 64)                     # noqa: F821  (physical read via proxy)
                _fnz = sum(1 for _b in _fs if _b != 0)
                print(f"NWFB video.base={_fb+_fo:#x} (+{_fo:#x}) nonzero={_fnz}/64 head={bytes(_fs[:24]).hex()}")
        except Exception as _e:
            print(f"NWFB error: {_e}")
        if os.environ.get("NWOAS_MARKERSCAN"):           # <<< 추가 5: autounattend 마커/디스크프로브 스캔(게이트)
            marker_scan(hv)
    except Exception:
        traceback.print_exc()

def marker_scan(hv):
    # autounattend 진단본이 X:\(WinPE RAM디스크)에 쓴 NWOAS_ 마커/디스크프로브를 게스트 물리 RAM에서
    # 스캔(best-effort). 느려서 NWOAS_MARKERSCAN=1일 때만 walk()에서 호출. 시간제한으로 캡처 안 막음.
    try:
        base = getattr(hv, 'guest_base', None) or getattr(hv, 'ram_base', None)
        if not base:
            print("NWMARK skip: guest_base 없음"); return
        # ★주의: 시리얼 프록시로 GB단위 읽기는 매우 느림 → 실험적/best-effort. 시간제한이 주 가드.
        CHUNK = 16 << 20                       # 16MB 청크
        LIMIT = int(os.environ.get("NWOAS_MARKERSCAN_GB", "3")) << 30
        TSEC  = int(os.environ.get("NWOAS_MARKERSCAN_SEC", "300"))
        print(f"NWMARK ===== guest RAM 마커스캔 base={base:#x} limit={LIMIT>>30}GB tlimit={TSEC}s =====")
        pa = base; scanned = 0; found = []; t0 = time.time()
        while scanned < LIMIT and (time.time() - t0) < TSEC:
            try:
                blk = hv.iface.readmem(pa, CHUNK)
            except Exception:
                pa += CHUNK; scanned += CHUNK; continue
            idx = 0
            while True:
                i = blk.find(b'NWOAS_', idx)
                if i < 0: break
                seg = blk[i:i+1400].split(b'\x00')[0]
                found.append((pa + i, seg)); idx = i + 6
                if len(found) > 40: break
            pa += CHUNK; scanned += CHUNK
            if len(found) > 40: break
        print(f"NWMARK scanned={scanned>>20}MB elapsed={int(time.time()-t0)}s found={len(found)}")
        seen = set()
        for addr, seg in found:
            try: s = seg.decode('latin1', 'replace')
            except Exception: s = repr(seg)
            key = s[:40]
            if key in seen: continue
            seen.add(key)
            print(f"NWMARK @{addr:#x}: {s[:600]}")
        print("NWMARK ===== 끝 (READ_OK=autounattend읽힘 / DISKPROBE=디스크열거 = Phase2 안전타겟팅) =====")
    except Exception:
        traceback.print_exc()

def proc_walk(hv, ntos, sysproc, tlh_off, tle_off):
    # 전체 프로세스 열거(PsActiveProcessHead) → 프로세스별 State5 대기스레드 수. 부트-크리티컬
    # 프로세스(smss/csrss/wininit/winpeshl/setup)가 System 밖에서 막혔는지 핀포인트. 오프셋 자동발견.
    try:
        if tlh_off is None or tle_off is None:
            print("NWPROC skip: tlh/tle off 없음"); return
        blob = _rd(hv, sysproc, 0x800)
        img_off = blob.find(b'System\x00') if blob else -1
        if img_off < 0:
            print("NWPROC ImageFileName(System\\0) 자동발견 실패"); return
        # PsActiveProcessHead는 export 아님 → System EPROCESS의 ActiveProcessLinks에서 순회.
        # apl_off 자동발견: LIST_ENTRY 불변(Flink의 Blink==self) + (Flink-X)가 ASCII name 가진 유효 EPROCESS.
        apl_off = None
        for X in range(0x3f0, 0x5a0, 8):
            fl = _r64(hv, sysproc + X)
            if not _kva(fl): continue
            if _r64(hv, fl + 8) != sysproc + X: continue
            nb = _rd(hv, (fl - X) + img_off, 15)
            if nb and nb[0] != 0 and all((32 <= c < 127) or c == 0 for c in nb):
                apl_off = X; break
        print(f"NWPROC img_off={img_off:#x} apl_off 자동발견={hex(apl_off) if apl_off else None}")
        if apl_off is None:
            print("NWPROC apl_off 자동발견 실패"); return
        start = sysproc + apl_off
        TARGET = ('setup.exe','winpeshl.exe','winlogon.exe','WallpaperHost.','csrss.exe')
        target_kts = []
        print("NWPROC ===== 전체 프로세스 (name | threads | State5대기 | eproc) =====")
        node = start; n=0; interesting=[]; first=True
        while _kva(node) and n < 300 and (first or node != start):
            first=False
            n += 1; eproc = node - apl_off
            nameb = _rd(hv, eproc+img_off, 15)
            name = nameb.split(b'\x00')[0].decode('latin1','replace') if nameb else '?'
            is_target = name in TARGET
            tlh = eproc + tlh_off; tn = _r64(hv, tlh); tc=0; wc=0; tk=0
            while _kva(tn) and tn != tlh and tk < 400:
                tk += 1; kt = tn - tle_off
                if _r8(hv, kt+KT_TYPE)==6:
                    tc += 1
                    if _r8(hv, kt+KT_STATE)==5:
                        wc += 1
                        if is_target and len(target_kts) < 48: target_kts.append((name, kt))
                tn = _r64(hv, tn)
            print(f"NWPROC {name:<18} threads={tc:<4} waiting={wc:<4} eproc={eproc:#x}")
            if name.lower() not in ('system','', '?') and tc>0:
                interesting.append(name)
            node = _r64(hv, node)
        print(f"NWPROC ===== {n} procs; 비-System 프로세스: {interesting} =====")
        # ★타겟(setup/UI) 스레드가 정확히 뭘 기다리나 — wait object + kernel backtrace
        if target_kts:
            print(f"NWPROC-T ===== 타겟 프로세스 {len(target_kts)}개 대기스레드: wait object =====")
            for nm, kt in target_kts:
                print(f"NWPROC-T [{nm}] kt={kt:#x}")
                _report(hv, kt, False)
            print("NWPROC-T ===== 타겟 스레드 kernel backtrace (win32k=입력UI, storNvme/usb=디바이스) =====")
            bt_report(hv, [kt for _, kt in target_kts], ntos)
        print("NWPROC-T ===== proc_walk done =====")
    except Exception:
        traceback.print_exc()

# ---------- wedge detector via stdout tee ----------
_state = {"kernel": False, "maint": 0}
_orig_write = sys.stdout.write
def _tee(s):
    try:
        if isinstance(s, str):
            if ("elr=0xfffff8" in s): _state["kernel"] = True
            if ("[vgic-maint]" in s) or ("NWOAS-WFX" in s) or ("NWOAS-VGATE" in s) or ("NWOAS-TINJ" in s):
                _state["maint"] += 1
    except Exception:
        pass
    return _orig_write(s)
sys.stdout.write = _tee

# ---------- patch run_shell: on break-in, run the walk once, then continue ----------
_done = {"v": False}
# ---------- KUSER_SHARED_DATA 클럭 스냅샷 (stuck-vs-slow eval 신호) ----------
# KUSD 커널VA 고정 0xFFFFF78000000000. InterruptTime(0x08 KSYSTEM_TIME, 100ns단위 uptime),
# TickCountQuad(0x320). 2회 break-in 델타로 게스트 클럭이 실시간과 비례하는지 판정.
KUSD = 0xFFFFF78000000000
_snap = []
def clock_snapshot(hv, label):
    try:
        it_lo=_r32(hv,KUSD+0x08); it_hi=_r32(hv,KUSD+0x0c)
        tc_lo=_r32(hv,KUSD+0x320); tc_hi=_r32(hv,KUSD+0x324)
        it=((it_hi<<32)|it_lo) if (it_lo is not None and it_hi is not None) else None
        tc=((tc_hi<<32)|tc_lo) if (tc_lo is not None and tc_hi is not None) else None
        its=(it/1e7) if it else None
        print(f"NWCLK[{label}] InterruptTime={it} uptime={its}s TickCountQuad={tc}")
        _snap.append((label, it, tc, time.time()))
    except Exception:
        traceback.print_exc()

# ---------- patch run_shell: 2회 break-in (stuck-vs-slow) ----------
_orig_run_shell = hv.run_shell   # noqa: F821  (hv injected by run_guest.py -m scope)
_bkn = {"n": 0}
def _patched_run_shell(entry_msg="", exit_msg="", **kw):
    _bkn["n"] += 1; n = _bkn["n"]
    print(f"NWOAS-CAPTURE: === BREAK-IN #{n}: clock + scheduler walk ===")
    clock_snapshot(hv, f"T{n}")
    walk(hv)                 # noqa: F821
    if n >= 2 and len(_snap) >= 2:
        (_,it1,tc1,rt1)=_snap[0]; (_,it2,tc2,rt2)=_snap[-1]
        dre=rt2-rt1
        dit=((it2-it1)/1e7) if (it1 and it2) else None
        dtc=(tc2-tc1) if (tc1 and tc2) else None
        ratio=(dit/dre) if (dit is not None and dre>0) else None
        print(f"NWCLK-DIFF real_elapsed={dre:.0f}s guest_uptime_delta={dit}s tick_delta={dtc} clock_ratio={ratio}")
        print("NWCLK-DIFF 판정: ratio~1=클럭건강(느림은 throughput문제) / ratio<<1=클럭굶주림(rate-limit) / ~0=stuck(클럭죽음=특정이벤트대기)")
    print(f"NWOAS-CAPTURE: === BREAK-IN #{n} done; continuing guest ===")
    return EXC_RET.HANDLED
hv.run_shell = _patched_run_shell   # noqa: F821

# ---------- break-in thread: wait for wedge, then interrupt ----------
# Primary signal: "[spi-en] irq=857" in the log = kernel armed the USB IRQ = wedge onset.
# The stdout tee is unreliable (console output bypasses sys.stdout), so tail the logfile.
import os
def _log_has(patt):
    lp = os.environ.get("NWOAS_LOG")
    if not lp:
        return _state["kernel"] and _state["maint"] > 300  # fallback to tee counters
    try:
        with open(lp, "r", errors="ignore") as f:
            data = f.read()
        return patt in data
    except Exception:
        return False
def _breaker():
    t0 = time.time()
    reason = "fallback"
    kernel_t = None
    while True:
        time.sleep(5)
        elapsed = time.time() - t0
        # kernel reached? (VBAR walk is robust even if guest is busy, so timing is less critical)
        if kernel_t is None and _log_has("elr=0xfffff8"):
            kernel_t = elapsed
        # trigger on FL1100 interrupt-enable (698) if seen, else Dwc3 (857). Then settle LONG:
        # spi-en fires when Windows CONNECTS the interrupt = xHCI StartDevice is just BEGINNING.
        # Over the slow tethered hv the rest of StartDevice (map BAR, enable MEM, reset xHCI) is
        # slow, so break in 240s later to see whether FL1100 CMD transitions 0->1 (slow) or is
        # genuinely stuck (failed StartDevice).
        if _log_has("[spi-en] irq=698") or _log_has("[spi-en] irq=857"):
            # Settle after spi-en so xHCI StartDevice can finish, BUT respect an overall ~1900s
            # budget: if spi-en fired late (slow boot), a fixed 600s overran the capture cap and
            # the break-in never ran (DCBAAP unread). Cap the settle to what the budget allows.
            settle = max(60, min(300, 1900 - int(elapsed)))
            reason = f"spi-en+{settle}s"; time.sleep(settle)
            break
        # secondary: kernel up + ~10min settle (setup session should be up by now; kernel+300s
        # was too early = only System/Registry). Still avoids the old blind 1400s.
        if kernel_t is not None and (elapsed - kernel_t) > 600:
            reason = f"kernel+{int(elapsed-kernel_t)}s"; break
        if elapsed > 1300:   # hard cap — VBAR walk works whenever we break in
            reason = "cap-1300s"; break
    _orig_write(f"\nNWOAS-CAPTURE: wedge signal ({reason}) t={int(time.time()-t0)}s; breaking in (proc-walk)...\n")
    try:
        hv.interrupt()   # noqa: F821
    except Exception:
        traceback.print_exc()
threading.Thread(target=_breaker, daemon=True, name="nwoas-breaker").start()
print("NWOAS-CAPTURE: hook installed (run_shell patched, breaker thread started)")
