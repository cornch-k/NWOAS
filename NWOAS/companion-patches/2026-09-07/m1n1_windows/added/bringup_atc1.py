# bringup_atc1.py  (NWOAS)
# Bring up the ATC-PHY1 USB2 host PHY for DWC3_1 (the non-serial USB-C port) by calling
# m1n1's OWN usb_phy_bringup(1) (src/usb.c:120-154) on the running outer-m1n1 via the
# proxy, BEFORE hv.start().  That function does the pmgr power-enable (ATC/DART/DRD) AND
# the PHY pokes -- the part iBoot performs on a normal boot but SKIPS when booting m1n1.
#
# A previous raw-poke attempt showed the pipehandler write landed (mux=0x22) but the ATC
# core write did NOT (core=0x0) -> the atc-phy1 power domain was off.  Calling the real
# function fixes that because it powers the domain first.
#
# We locate usb_phy_bringup's runtime address SAFELY: read its machine code from
# build/m1n1.elf and match it against memory at candidate bases, then p.call only on a
# verified match (no blind call / no crash).  Runs with p/u/hv in scope (run_guest.py -m).
import struct, time

ELF      = "/Volumes/X31/NWOAS/m1n1_windows/build/m1n1.elf"
SYM_VMA  = 0x348d4     # usb_phy_bringup  (llvm-nm build/m1n1.elf)
SEG_VMA  = 0x4000      # RWE code LOAD segment VMA      (readelf -l)
SEG_FOFF = 0x14000     # ... its file offset
FOFF     = SEG_FOFF + (SYM_VMA - SEG_VMA)

with open(ELF, "rb") as f:
    f.seek(FOFF)
    want = struct.unpack("<4I", f.read(16))
print("[ATC1] usb_phy_bringup code @elf 0x%x = %s" % (FOFF, " ".join("%08x" % w for w in want)))

# The RAW m1n1.bin starts at the RWE code segment (VMA 0x4000): the boot entry-point
# offset 0x800 lines up with _start (VMA 0x4800).  So the runtime offset of a symbol
# from the load base is (VMA - 0x4000), not VMA.  Try a few base/offset combos and let
# the code match pick the right one.
IMG_VMA = 0x4000
bases = [(nm, val) for nm, val in (
            ("u.base",    getattr(u, "base", None)),
            ("virt_base", getattr(getattr(u, "ba", None), "virt_base", None)),
            ("phys_base", getattr(getattr(u, "ba", None), "phys_base", None)))
         if val is not None]
cands = []
for nm, base in bases:
    cands.append(("%s+(vma-0x4000)" % nm, base + (SYM_VMA - IMG_VMA)))
    cands.append(("%s+vma" % nm,          base + SYM_VMA))

# The INSTALLED outer-m1n1 may differ slightly from build/m1n1.elf (unrelated code
# shifted usb_phy_bringup by a few instructions), so the exact predicted address can be
# off. SCAN a window around each candidate base for the function's byte signature and
# call the real match -- calling the real function is what powers the ATC PHY core
# (raw pokes alone leave core=0x0, which previously confounded the CD321x test).
addr = None
SCAN = list(range(0, 1028, 4)) + list(range(-1024, 0, 4))
for nm, base_a in cands:
    try:
        head = tuple(p.read32(base_a + 4 * i) for i in range(4))
    except Exception as e:
        print("[ATC1] cand %s @0x%x: unreadable (%s)" % (nm, base_a, e)); continue
    print("[ATC1] cand %s @0x%x = %s (scan +/-1KB for signature)"
          % (nm, base_a, " ".join("%08x" % w for w in head)))
    for off in SCAN:
        a = base_a + off
        try:
            got = tuple(p.read32(a + 4 * i) for i in range(4))
        except Exception:
            continue
        if got == want:
            addr = a
            print("[ATC1] MATCH: %s + %d -> usb_phy_bringup @0x%x" % (nm, off, a))
            break
    if addr is not None:
        break

if addr is not None:
    print("[ATC1] calling usb_phy_bringup(1)...")
    try:
        r = p.call(addr, 1)
        print("[ATC1] usb_phy_bringup(1) returned %d (0=ok)" % r)
    except Exception as e:
        print("[ATC1] p.call FAILED: %s" % e)
else:
    print("[ATC1] NO code match -- skipping p.call. Doing raw PHY pokes (no power-enable).")
    ATC = 0x503000000; PIPE = 0x502a84000
    for off, val in ((0x08,0x01c1000f),(0x04,3),(0x04,0),(0x1c,0x008c0813),(0x00,2)):
        p.write32(ATC + off, val)
    p.write32(PIPE + 0x0c, 0x22); p.write32(PIPE + 0x1c, 0x01); p.write32(PIPE + 0x20, 0x9332)

# diagnostic readback
time.sleep(0.5)
ATC = 0x503000000; PIPE = 0x502a84000; DWC3 = 0x502280000
op = DWC3 + (p.read32(DWC3) & 0xff)
psc0 = p.read32(op + 0x400); psc1 = p.read32(op + 0x410)
print("[ATC1] core=0x%x(exp 0x2)  mux=0x%x(exp 0x22)  PORTSC0=0x%x PORTSC1=0x%x"
      % (p.read32(ATC), p.read32(PIPE + 0x0c), psc0, psc1))
print("[ATC1] CCS p0=%d p1=%d | PP p0=%d p1=%d  (CCS=1 => USB device electrically present)"
      % (psc0 & 1, psc1 & 1, (psc0 >> 9) & 1, (psc1 >> 9) & 1))

# --- PD chip (CD321x @ i2c0): compare IDLE target 0x3f vs LIVE serial 0x38 to find what
#     keeps 0x3f from acting as a host/source, then run m1n1's own SSPS powerup on 0x3f
#     ONLY (lowest-risk write; the serial chip 0x38 is never written).
try:
    from m1n1.hw.i2c import I2C
    _bus = I2C(u, "/arm-io/i2c0")
    def _tps_rd(a, r, n):
        raw = _bus.read_reg(a, r, n + 1)          # CD321x block frame: [count][data...]
        return raw[1:1 + min(n, raw[0])]
    def _tps_wr(a, r, data):
        data = bytes(data)
        _bus.write_reg(a, r, bytes([len(data)]) + data)   # [reg][count][data...]
    def _cmd4(a, cc, din=b""):                    # CD321x 4CC command via CMD1/DATA1
        if din: _tps_wr(a, 0x09, din)
        _tps_wr(a, 0x08, cc.encode()[:4])
        for _ in range(200):
            v = int.from_bytes(_tps_rd(a, 0x08, 4), "little")
            if v == 0x21434d44: return "!CMD"
            if v == 0: return "ok"
            p.udelay(100)
        return "timeout"
    REGS = (("Mode",0x03,4),("SysPwr",0x20,1),("Status",0x1a,8),("SysCfg27",0x27,4),
            ("SysCfg28",0x28,4),("PortCtl",0x29,4),("PwrStatus",0x3f,2),("PDStatus",0x40,4))
    for tag, a in (("BR-3f TARGET", 0x3f), ("BL-38 SERIAL(do-not-write)", 0x38)):
        print("[tps] --- %s ---" % tag)
        for nm, r, n in REGS:
            try: print("[tps]   0x%02x %-9s = %s" % (r, nm, _tps_rd(a, r, n).hex()))
            except Exception as e: print("[tps]   0x%02x %-9s = ERR %s" % (r, nm, e))
    # === CD321x activation test on 0x3f ONLY (overnight design + adversarial review) ===
    # The idle 0x3f reads fine (chip awake) but its PD policy engine looks un-started
    # (SSPS timed out). ACTION 1: Gaid warm-resets the engine so it re-runs Type-C
    # detection; if the port is source-capable it then sources VBUS to an attached sink.
    # If that alone doesn't, ACTION 2 probes whether PortInfo(0x28) role is host-WRITABLE.
    # >>> WATCH THE DRIVE LED while this runs <<<   Gaid is NEVER sent to 0x38 (serial).
    A = 0x3f
    _tps_wr(A, 0x70, bytes([0x00, 0xff]))                 # keep-awake
    print("[cd] BEFORE: PortInfo(0x28)=%s PwrStatus(0x3f)=%s ConnState(0x1a)=%s"
          % (_tps_rd(A, 0x28, 4).hex(), _tps_rd(A, 0x3f, 2).hex(), _tps_rd(A, 0x1a, 8).hex()))
    # --- ACTION 1: Gaid (warm reset; fire-and-wait, do not poll the resetting chip) ---
    _tps_wr(A, 0x08, b"Gaid")
    time.sleep(0.6)
    pw = b""
    for _t in range(20):                                  # i2c may NAK while it reboots
        try:
            pw = _tps_rd(A, 0x3f, 2)
            break
        except Exception:
            time.sleep(0.1)
    print("[cd] after Gaid (action1): PwrStatus(0x3f)=%s ConnState(0x1a)=%s  >>> WATCH DRIVE LED <<<"
          % (pw.hex(), _tps_rd(A, 0x1a, 8).hex()))
    # --- ACTION 2 (only if action1 didn't make it sense a connection): force role=Source ---
    if not (pw and (pw[0] & 1)):
        pib = _tps_rd(A, 0x28, 4)
        nb0 = (pib[0] & 0xf8) | 0x05                      # PortInfo bits[2:0] = 0b101 DRP-default-Source
        _tps_wr(A, 0x28, bytes([nb0]) + pib[1:4])
        pia = _tps_rd(A, 0x28, 4)
        print("[cd] action2: 0x28 PortInfo b0 %02x -> wrote %02x -> read %02x : %s"
              % (pib[0], nb0, pia[0],
                 "WRITABLE!" if pia[0] == nb0 else "locked/RO (role gated like 0x27 = Apple flash needed)"))
        print("[cd] action2: PwrStatus(0x3f)=%s  (run C tps6598x_port_enable(0x3f,2) for the full DISC)"
              % _tps_rd(A, 0x3f, 2).hex())
    _op = DWC3 + (p.read32(DWC3) & 0xff)
    _ps = p.read32(_op + 0x400)
    print("[cd] PORTSC1=0x%x CCS=%d  (drive LED on / PwrStatus bit0=1 / CCS=1 = port ACTIVE)"
          % (_ps, _ps & 1))
except Exception as e:
    print("[tps] i2c diagnostic failed: %s" % e)

print("[ATC1] done -- starting guest.")
