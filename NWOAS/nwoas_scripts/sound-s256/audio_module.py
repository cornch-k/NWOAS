# SPDX-License-Identifier: MIT
# NWOAS S256: run_guest.py -m module (runs after hv.load_raw, before hv.start).
# Contract: research-20261116/sound/PLAN-S256.md.  Prepares the Mac mini (J274) internal speaker
# path for the Windows NWAU0001 driver WITHOUT playing anything:
#   power + AUDIO_P reset + NCO 0 (48000*256) + MCA cluster 0 (SERDES TXA EN=0) + mca-switch1,
#   ADMAC TX channel 0 reset/width (channel DISABLED), DART-sio stream 2 -> fixed buffer,
#   the same buffer mapped into the guest at WIN_IPA, amp GPIO released, amp volume 0x05=0x50,
#   gain 0x03=0x00, config writes, amp LEFT IN SOFTWARE SHUTDOWN (0x02=0x0e), header at WIN_IPA+0.
# Register sequence = nwoas_scripts/sound-s255/beep_tone.py (heard on hardware 2026-11-16 20:09),
# itself based on m1n1 proxyclient/experiments/speaker_amp.py (MIT).  Differences to beep_tone:
#   - no tone, no descriptor submitted, ADMAC channel never enabled, SERDES never enabled,
#     amp never leaves shutdown (0x02 is only ever written 0x0d/0x0e here)
#   - DART-sio: only stream 2 is (re)initialised (beep_tone called DART.initialize(), which resets
#     all 16 streams: SIO stream 0, AES stream 1, bypass stream 15).  See README.md.
#   - ADMAC object is built without a DART (no 4 MiB scratch mapping); the buffer is mapped at a
#     fixed IOVA instead.
# S259 (2026-11-18): header version 2 adds the V/I sense path for the driver's speaker thermal
# protection (PLAN-S256.md "V/I sense"): MCA cluster 0 RXB serdes (EN=0, 16 x 16-bit slots, port 0),
# DMA adapter B, ADMAC RX channel 3 (reset, 16-bit words, 2-word frames, DISABLED), amp TX slots
# (0x0e..0x10: zero fill, V sense slot 2, I sense slot 0; amp still in shutdown), sense ring at
# window + 0x21000 (0x10000 bytes, same DART-sio mapping).  Header status bit1 = sense prepared.
# The protection stage comes from the environment (the module hash stays pinned):
#   NWOAS_AUDIO_PROT=sense (default) -> prot_gain 0: the driver measures V/I and runs the model,
#                                       amp gain stays 0x00 (static -6 dB cap)
#   NWOAS_AUDIO_PROT=moderate        -> prot_gain 0x14, prot_vol_floor 0x18 (-12 dB at 21 dB: +9 dBV)
#   NWOAS_AUDIO_PROT=full            -> prot_gain 0x14, prot_vol_floor 0x00 (+21 dBV, Asahi level)
# Sense preparation failures never stop playback: bit1 stays 0 and the driver stays static.
# Failure policy:
#   stage A (layout checks, buffer, guest window, zeroed header with status 0): any error RAISES,
#     so run_guest --strict-init stops before guest entry (the SSDT would otherwise point Windows
#     at an unmapped IPA).
#   stage B (hardware): any error -> amp mute+shutdown, amp GPIO low, SERDES/ADMAC off,
#     header status bit0 stays 0, Windows still boots (driver refuses to start).
import os, struct, time

import m1n1.hw.mca as _s256_mca
from m1n1.hw.dart import DART as _S256_DART
from m1n1.hw.dart8020 import R_TCR as _S256_R_TCR, R_TTBR as _S256_R_TTBR
from m1n1.hw.i2c import I2C as _S256_I2C
from m1n1.hw.pmgr import PMGR as _S256_PMGR
from m1n1.hw.nco import NCO as _S256_NCO
from m1n1.hw.admac import ADMAC as _S256_ADMAC, E_BUSWIDTH as _S256_E_BUSWIDTH, E_FRAME as _S256_E_FRAME
from m1n1.utils import irange as _s256_irange

# ---- contract constants (PLAN-S256.md).  WIN_IPA is also the 5th _CRS resource of the SSDT
# (nwoas_scripts/fw-audio-s256/acpi/NwoasAudioJ274.asl.inc); fw-audio-s256/build.py refuses to
# build when the two differ.
S256_WIN_IPA = 0x340000000
S256_WIN_SIZE = 0x40000
S256_HDR_SIZE = 0x1000
S256_RING_OFF = 0x1000
S256_RING_BYTES = 0x20000
S256_IOVA_BASE = 0x80000000          # DART-sio stream 2 IOVA of the window's first byte
S256_MAGIC = 0x5541574e              # 'NWAU'
S256_VERSION = 2
S256_RATE = 48000
S256_FORMAT = 1                      # mono, s24 msb-aligned in 32 bits (value << 8)
S256_ADMAC_CH = 0                    # TX channel of cluster 0 TXA
S256_CLUSTER = 0
S256_AMP = 0x31
S256_VOL = 0x50                      # -40 dB, written and left by this module
S256_VOL_LIMIT = 0x0c                # smallest value the driver may write to 0x05 (-6 dB; gain 0x00 -> +5 dB full
                                     # scale; speakersafetyd j274 thermal model: continuous full-scale sine needs -4.6 dB)
S256_GAIN = 0x00
S256_AMP_SHUTDOWN = 0x0e
S256_GPIO181 = 0x23c1002d4           # ADT i2c1/audio-tas5770L-speaker function-reset = GPIO 181
# ---- header v2: V/I sense (PLAN-S256.md)
S256_SENSE_OFF = 0x21000             # window offset of the sense ring (after header + TX ring)
S256_SENSE_BYTES = 0x10000           # 16 x 4 KiB chunks of 1024 frames
S256_SENSE_CHAN = 4 * 0 + 3          # ADMAC RX channel = cluster 0 RXB (Asahi mca.c USE_RXB_FOR_CAPTURE, "rx0b")
S256_SENSE_FORMAT = 2                # per frame two s16: I (slot 0) then V (slot 1) -> 32-bit word I | V << 16
S256_STATUS_SENSE = 0x2
S256_AMP_TDM = [0x21, 0x42, 0x40]    # amp 0x0e: keeper, zero fill, offset 0, edge bit [hypothesis];
                                     # 0x0f: V sense on slot 2; 0x10: I sense on slot 0 (8-bit slots)
S256_SWITCH1_B = 0x205050            # DMA adapter B: 2 channels, RX/TX msb/lsb pad 16, (Asahi formula, S16 2ch)
S256_PROT_STAGES = {"sense": (0x00, 0xc9), "moderate": (0x14, 0x18), "full": (0x14, 0x00)}
S256_PROT_STAGE = os.environ.get("NWOAS_AUDIO_PROT", "sense").strip().lower()
if S256_PROT_STAGE not in S256_PROT_STAGES:
    print(f"[S256-audio] WARN NWOAS_AUDIO_PROT={S256_PROT_STAGE!r} unknown -> sense", flush=True)
    S256_PROT_STAGE = "sense"
S256_PROT_GAIN, S256_PROT_VOL_FLOOR = S256_PROT_STAGES[S256_PROT_STAGE]
# Contract resources 0..3 (SSDT order) = (ADT node, reg index, base, size).
S256_RES = [("/arm-io/i2c1", 0, 0x235014000, 0x4000),
            ("/arm-io/admac-sio", 0, 0x238200000, 0x34000),
            ("/arm-io/mca-switch", 0, 0x238400000, 0x18000),
            ("/arm-io/mca-switch", 1, 0x238300000, 0x30000)]
# C hypervisor (m1n1-gpu-hv2) guest-IPA hooks that are not Python tracers.
S256_C_HV_IPA = [("AGX broker page", 0x300000000, 0x1000),          # hv_agx_g2.generated.h:18-19
                 ("vGIC dist (T8103)", 0xF00000000, 0x10000),       # hv_vgic.c:74, 2001
                 ("vGIC redist x8 (T8103)", 0xF10000000, 0x20000 * 8),
                 ("vGIC ITS (T8103)", 0xF20000000, 0x10000)]


def _s256_log(msg):
    print(f"[S256-audio] {msg}", flush=True)


def _s256_require(cond, msg):
    # explicit check (not assert): must also hold under python -O
    if not cond:
        raise RuntimeError("[S256-audio] STOP " + msg)


def _s256_overlap(a, n, b, m):
    return a < b + m and b < a + n


S256_HDR_FMT = "<IIQIIIIIIIIQIIIII"
S256_HDR_LEN = 0x4c


def _s256_header(status):
    return struct.pack(S256_HDR_FMT, S256_MAGIC, S256_VERSION, S256_IOVA_BASE + S256_RING_OFF,
                       S256_RING_BYTES, S256_RATE, S256_FORMAT, S256_ADMAC_CH, S256_CLUSTER,
                       S256_AMP, S256_VOL_LIMIT, status,
                       S256_IOVA_BASE + S256_SENSE_OFF, S256_SENSE_BYTES, S256_SENSE_CHAN,
                       S256_SENSE_FORMAT, S256_PROT_GAIN, S256_PROT_VOL_FLOOR)


_s256_require(struct.calcsize(S256_HDR_FMT) == S256_HDR_LEN, "header struct size")
_s256_require(struct.calcsize("<IIQIIIIIIII") == 0x30, "v1 part of the header")


# ------------------------------------------------------------------ stage A: layout + window
def _s256_stage_a():
    win, size = S256_WIN_IPA, S256_WIN_SIZE
    _s256_require(win % 0x4000 == 0 and size % 0x4000 == 0, "window not 16 KiB aligned")
    _s256_require(S256_RING_OFF >= S256_HDR_LEN and S256_RING_OFF + S256_RING_BYTES <= size, "ring outside window")
    _s256_require(S256_SENSE_OFF >= S256_RING_OFF + S256_RING_BYTES and S256_SENSE_OFF % 0x1000 == 0 and
                  S256_SENSE_OFF + S256_SENSE_BYTES <= size and S256_SENSE_BYTES % 0x1000 == 0,
                  "sense ring outside the window or over the TX ring")
    _s256_require(S256_SENSE_CHAN % 2 == 1 and S256_SENSE_CHAN // 4 == S256_ADMAC_CH // 4, "sense channel")
    _s256_require(S256_PROT_GAIN in (0, 0x14) and 0 <= S256_PROT_VOL_FLOOR <= 0xc9, "protection stage values")
    _s256_require(win + size <= 1 << 36, "window above the 36-bit guest IPA space")
    bad = []
    # (1) guest RAM: low window IPA 0..4 GiB (usb-s174/guest_map.py) and DRAM identity range
    if _s256_overlap(win, size, 0, 0x100000000):
        bad.append("low 4 GiB guest RAM window")
    ram_lo = u.ba.phys_base & ~0xffffffff
    ram_hi = ram_lo + u.ba.mem_size_actual
    if _s256_overlap(win, size, ram_lo, ram_hi - ram_lo):
        bad.append(f"DRAM {ram_lo:#x}..{ram_hi:#x}")
    # (2) every Python tracer zone (HW /arm-io ranges, RAM-LOW/HIGH, VUART, NVMe, PCIe, USB-C ...)
    for zone, maps in hv.mmio_maps.overlaps(_s256_irange(win, size)):
        if maps:
            bad.append(f"tracer {zone.start:#x}..{zone.stop:#x} {sorted(k for k in maps)}")
    # (3) ADT /arm-io ranges (also covered by the HW tracers; explicit second check)
    for r in u.adt["/arm-io"].ranges:
        if _s256_overlap(win, size, r.parent_addr, r.size):
            bad.append(f"/arm-io range {r.parent_addr:#x}+{r.size:#x}")
    # (4) C hypervisor hooks and the AGX windows chosen by the launcher environment
    hooks = list(S256_C_HV_IPA)
    local = int(os.environ.get("NWOAS_AGX_LOCAL_IPA", "0"), 0)
    pool = int(os.environ.get("NWOAS_AGX_POOL_IPA", "0"), 0)
    if local:
        hooks.append(("AGX local reserve / scanout pool", local, 0x40000000))
    if pool:
        hooks.append(("AGX pool window", pool, 0x100000000))
    for name, b, n in hooks:
        if _s256_overlap(win, size, b, n):
            bad.append(f"{name} {b:#x}+{n:#x}")
    # (5) MCC carveouts (PA space; listed for completeness)
    cur = p.mcc_get_carveouts()
    carve = []
    for _ in range(64):
        b = p.read64(cur); n = p.read64(cur + 8)
        if not b:
            break
        carve.append((b, n)); cur += 16
    for b, n in carve:
        if _s256_overlap(win, size, b, n):
            bad.append(f"carveout {b:#x}+{n:#x}")
    if bad:
        raise RuntimeError("[S256-audio] STOP window IPA overlaps: " + "; ".join(bad))
    _s256_log(f"A1 window IPA {win:#x}+{size:#x}: no overlap with low window, DRAM "
              f"{ram_lo:#x}..{ram_hi:#x}, {len(list(hv.mmio_maps.items()))} tracer zones, /arm-io ranges, "
              f"{len(hooks)} C-HV/AGX windows, {len(carve)} carveouts")

    # buffer: Python proxy heap, which lies below guest RAM (load_raw: phys_base = heap_top)
    buf = u.memalign(0x4000, size)
    _s256_require(buf % 0x4000 == 0, "buffer not 16 KiB aligned")
    _s256_require(u.heap_base <= buf and buf + size <= u.heap_top, "buffer outside the proxy heap")
    _s256_require(buf + size <= hv.phys_base, "buffer inside guest RAM")
    for zone, maps in hv.mmio_maps.overlaps(_s256_irange(buf, size)):
        if maps:
            raise RuntimeError(f"[S256-audio] STOP buffer PA {buf:#x} is guest-visible via {sorted(maps)}")
    for b, n in carve:
        _s256_require(not _s256_overlap(buf, size, b, n), "buffer intersects a carveout")
    _s256_log(f"A2 buffer PA {buf:#x}+{size:#x} (proxy heap {u.heap_base:#x}..{u.heap_top:#x}, "
              f"guest RAM starts {hv.phys_base:#x})")

    p.memset8(buf, 0, size)
    iface.writemem(buf, _s256_header(0))
    p.dc_civac(buf, size)
    hv.map_hw(win, buf, size)
    back = iface.readmem(buf, S256_HDR_LEN)
    _s256_require(back == _s256_header(0), "header read-back mismatch")
    _s256_log(f"A3 guest IPA {win:#x} -> PA {buf:#x} mapped (map_hw), zeroed, header status=0 written")
    return buf


# ------------------------------------------------------------------ stage B: hardware
class _S256Hw:
    i2c = tx = serdes = regs = None
    rx = rxb = admac = cl = None
    switch1 = 0
    b0_ok = False          # ADT/contract check passed -> GPIO181 address is known good


def _s256_amp_read(hw, reg):
    return hw.i2c.read_reg(S256_AMP, reg, 1)[0]


def _s256_stage_b(buf, hw):
    # B0 contract resources must equal the live ADT and be passthrough (HW tracer)
    for path, idx, base, size in S256_RES:
        b, n = u.adt[path].get_reg(idx)
        if (b, n) != (base, size):
            raise RuntimeError(f"ADT {path} reg[{idx}] = {b:#x}+{n:#x}, contract {base:#x}+{size:#x}")
        for addr in (base, base + size - 1):
            maps = hv.mmio_maps.lookup(addr) or {}
            if set(maps) != {"HW"}:
                raise RuntimeError(f"{path} reg[{idx}] {addr:#x} is not plain passthrough: {sorted(maps)}")
    gpio_base = u.adt["/arm-io/gpio"].get_reg(0)[0]
    if gpio_base + 4 * 181 != S256_GPIO181:
        raise RuntimeError(f"/arm-io/gpio base {gpio_base:#x} does not give GPIO181 {S256_GPIO181:#x}")
    hw.b0_ok = True
    _s256_log("B0 ADT reg check: i2c1 / admac-sio / mca-switch reg0 / reg1 match the SSDT, all HW passthrough; GPIO181 address ok")

    # B1 clocks/power (same nodes as beep_tone; mca0/mca1 have no clock-gates -> harmless error)
    for node in ("/arm-io/i2c1", "/arm-io/admac-sio", "/arm-io/dart-sio", "/arm-io/mca-switch",
                 "/arm-io/mca0", "/arm-io/mca1"):
        try:
            r = p.pmgr_adt_clocks_enable(node)
            _s256_log(f"B1 clocks {node} -> {r}")
        except Exception as e:
            _s256_log(f"B1 clocks {node}: {e} (ignored, as in S255)")

    # B2 MCA clock muxes (m1n1 src/clk.c: bits 27:24 = 5 + min(4, i))
    clk_base, clk_size = u.adt["/arm-io/mca-switch"].get_reg(2)
    for i in range(clk_size // 4):
        a = clk_base + 4 * i
        v = p.read32(a)
        nv = (v & ~(0xf << 24)) | ((5 + min(4, i)) << 24)
        if nv != v:
            p.write32(a, nv)
    _s256_log(f"B2 MCA clock mux @ {clk_base:#x}: " +
              " ".join(f"{p.read32(clk_base + 4 * i):#x}" for i in range(clk_size // 4)))

    # B3 AUDIO_P reset (shared by MCA and ADMAC): PMGR PS4[5] = 0x23b700228
    ps = _S256_PMGR(u).regs[0].PS4[5]
    _s256_log(f"B3 AUDIO_P before reset {p.read32(0x23b700228):#x}")
    ps.set(DEV_DISABLE=1)
    ps.set(RESET=1)
    ps.set(RESET=0)
    ps.set(DEV_DISABLE=0)
    states = {n: p.read32(a) for n, a in (("SIO_CPU", 0x23b7001d0), ("I2C1", 0x23b7001f8),
                                          ("AUDIO_P", 0x23b700228), ("SIO_ADMA", 0x23b700230),
                                          ("MCA0", 0x23b7002b8))}
    _s256_log("B3 AUDIO_P reset done; PMGR " + " ".join(f"{k}={v:#x}" for k, v in states.items()))

    hw.i2c = _S256_I2C(u, "/arm-io/i2c1")

    # B4 DART-sio: stream 2 only
    dart = _S256_DART.from_adt(u, "/arm-io/dart-sio")
    d = dart.dart
    rg = d.regs
    _s256_log(f"B4 DART-sio before: CONFIG={rg.CONFIG.val:#x} ENABLED_STREAMS={rg.ENABLED_STREAMS.val:#x} "
              f"ERROR={rg.ERROR.val:#x} UNK1={rg.UNK1.val:#x} TCR0..3=" +
              ",".join(f"{rg.TCR[i].val:#x}" for i in range(4)) + " TTBR2=" +
              ",".join(f"{rg.TTBR[2, j].val:#x}" for j in range(4)) + " TTBR0[0]=" + f"{rg.TTBR[0, 0].val:#x}")
    keep = [(rg.TCR[i].val, rg.TTBR[i, 0].val) for i in (0, 1)]
    rg.TCR[2].reg = _S256_R_TCR(TRANSLATE_ENABLE=1)
    for j in range(4):
        rg.TTBR[2, j].reg = _S256_R_TTBR(VALID=0)
    if rg.ERROR.reg.FLAG:
        rg.ERROR.val = 0xffffffff
    d.enabled_streams = rg.ENABLED_STREAMS.val
    d.pt_cache = {}
    d.invalidate_streams(1 << 2)
    if rg.TCR[2].val != _S256_R_TCR(TRANSLATE_ENABLE=1).value:
        raise RuntimeError(f"DART-sio TCR2 write did not stick ({rg.TCR[2].val:#x}); locked?")
    dart.iomap_at(2, S256_IOVA_BASE, buf, S256_WIN_SIZE)
    for pt in d.pt_cache:               # page tables live in the proxy heap; push them to PoC
        p.dc_civac(pt, d.PAGE_SIZE)
    d.invalidate_streams(1 << 2)
    d.invalidate_cache()
    tr = dart.iotranslate(2, S256_IOVA_BASE, S256_WIN_SIZE)
    if tr != [(buf, S256_WIN_SIZE)]:
        raise RuntimeError(f"DART-sio stream 2 translate {tr} != [({buf:#x}, {S256_WIN_SIZE:#x})]")
    if [(rg.TCR[i].val, rg.TTBR[i, 0].val) for i in (0, 1)] != keep:
        raise RuntimeError("DART-sio streams 0/1 changed unexpectedly")
    _s256_log(f"B4 DART-sio stream 2: IOVA {S256_IOVA_BASE:#x}+{S256_WIN_SIZE:#x} -> PA {buf:#x} "
              f"(page tables re-read: ok); TCR2={rg.TCR[2].val:#x} TTBR2[0]={rg.TTBR[2, 0].val:#x} "
              f"ENABLED_STREAMS={rg.ENABLED_STREAMS.val:#x}; streams 0/1 unchanged")

    # B5 ADMAC TX channel 0: disable, reset, 32-bit words, 1-word frames (no DART object, no buffer)
    admac = _S256_ADMAC(u, "/arm-io/admac-sio", None, debug=False)
    hw.admac = admac
    hw.tx = admac.chans[4 * S256_CLUSTER]
    if not (hw.tx.ch == S256_ADMAC_CH and hw.tx.tx):
        raise RuntimeError("ADMAC channel object is not TX channel 0")
    hw.tx.disable()
    hw.tx.reset()
    hw.tx.read_reports()             # drains a stale report ring (normally empty after the reset)
    hw.tx.buswidth = _S256_E_BUSWIDTH.W_32BIT
    hw.tx.framesize = _S256_E_FRAME.F_1_WORD
    ar = admac.regs
    _s256_log(f"B5 ADMAC ch0 reset: BUSWIDTH={ar.CHAN_BUSWIDTH[0].val:#x} BURST={ar.CHAN_BURSTSIZE[0].val:#x} "
              f"SRAM={ar.CHAN_SRAM_CARVEOUT[0].val:#x} DESC_RING={ar.CHAN_DESC_RING[0].val:#x} "
              f"REPORT_RING={ar.CHAN_REPORT_RING[0].val:#x} TX_EN(read)={ar.TX_EN.val:#x}; channel left disabled")

    # B6 NCO 0 = 48000*256
    nco = _S256_NCO(u, "/arm-io/nco")
    nco[S256_CLUSTER].set_rate(S256_RATE * 256)
    nco[S256_CLUSTER].enable()
    _s256_log(f"B6 NCO{S256_CLUSTER} rate {S256_RATE * 256} enabled={nco[S256_CLUSTER].enabled()}")

    # B7 MCA cluster 0 (same values as beep_tone), SERDES TXA left disabled
    sw = u.adt["/arm-io/mca-switch"]
    switch1 = sw.get_reg(1)[0]
    cl = _s256_mca.MCACluster(u, sw.get_reg(0)[0] + 0x4000 * S256_CLUSTER)
    hw.cl, hw.switch1 = cl, switch1
    hw.regs, hw.serdes = cl.regs, cl.txa
    regs, serdes = cl.regs, cl.txa
    regs.SYNCGEN_STATUS.set(RST=1, EN=0)
    regs.SYNCGEN_STATUS.set(RST=0)
    regs.SYNCGEN_MCLK_SEL.val = 1 + S256_CLUSTER
    regs.SYNCGEN_HI_PERIOD.val = 0
    regs.SYNCGEN_LO_PERIOD.val = 0xfe
    serdes.STATUS.set(EN=0)
    serdes.CONF.set(NSLOTS=0, SLOT_WIDTH=_s256_mca.E_SLOT_WIDTH.W_32BIT, BCLK_POL=1, UNK1=1, UNK2=1,
                    IDLE_UNDRIVEN=1, SYNC_SEL=1 + S256_CLUSTER)
    serdes.BITDELAY.val = 0
    serdes.CHANMASK[0].val = 0xffff_fffe
    serdes.CHANMASK[1].val = 0xffff_fffe
    regs.PORT_ENABLES.set(CLOCK1=1, CLOCK2=1, DATA=1)
    regs.PORT_CLK_SEL.set(SEL=S256_CLUSTER + 1)
    regs.PORT_DATA_SEL.val = S256_CLUSTER + 1
    regs.MCLK_STATUS.set(EN=1)
    regs.SYNCGEN_STATUS.set(EN=1)
    # 2026-11-18 (S258): DMA adapter TX_LSB_PAD (bits 4:0, Asahi mca.c REG_DMA_ADAPTER_A) must be 0:
    # the ring holds msb-aligned 32-bit samples.  0x102048 (pad 8) shifted them left by 8 more,
    # so every sample above ~-48 dBFS wrapped (torn sound at every volume).
    p.write32(switch1 + 0x8000 * S256_CLUSTER, 0x102040)
    if serdes.STATUS.reg.EN:
        raise RuntimeError("SERDES TXA enabled after configuration")
    _s256_log(f"B7 MCA cluster {S256_CLUSTER}: MCLK={regs.MCLK_STATUS.val:#x} SYNCGEN={regs.SYNCGEN_STATUS.val:#x} "
              f"TXA STATUS={serdes.STATUS.val:#x} CONF={serdes.CONF.val:#x} PORT_EN={regs.PORT_ENABLES.val:#x} "
              f"switch1[{S256_CLUSTER}]={p.read32(switch1 + 0x8000 * S256_CLUSTER):#x} (SERDES EN=0, clocks running)")

    # B8 amp reset GPIO 181 released (same values as beep_tone)
    old = p.read32(S256_GPIO181)
    p.write32(S256_GPIO181, 0x76a02)
    time.sleep(0.01)
    p.write32(S256_GPIO181, 0x76a03)
    time.sleep(0.01)
    _s256_log(f"B8 GPIO181 {old:#x} -> {p.read32(S256_GPIO181):#x} (amp out of hardware reset)")

    # B9 amp: volume first (amp still in its reset state 0x02=0x0e), verify, config, verify, stay off
    st0 = _s256_amp_read(hw, 0x02)
    hw.i2c.write_reg(S256_AMP, 0x05, [S256_VOL])
    vol = _s256_amp_read(hw, 0x05)
    _s256_log(f"B9 amp 0x02={st0:#x} volume 0x05={vol:#x} (want {S256_VOL:#x})")
    if vol != S256_VOL:
        raise RuntimeError("amp volume read-back mismatch")
    hw.i2c.write_reg(S256_AMP, 0x02, [S256_AMP_SHUTDOWN])
    hw.i2c.write_reg(S256_AMP, 0x08, [0x40])
    hw.i2c.write_reg(S256_AMP, 0x0a, [0x06, 0x00, 0x1a])
    hw.i2c.write_reg(S256_AMP, 0x1b, [0x01, 0x82, 0x06])
    hw.i2c.write_reg(S256_AMP, 0x16, [0x50, 0x04])
    hw.i2c.write_reg(S256_AMP, 0x0d, [0x00])
    hw.i2c.write_reg(S256_AMP, 0x03, [S256_GAIN])
    hw.i2c.write_reg(S256_AMP, 0x05, [S256_VOL])
    hw.i2c.write_reg(S256_AMP, 0x02, [S256_AMP_SHUTDOWN])
    got = {r: _s256_amp_read(hw, r) for r in (0x02, 0x03, 0x05, 0x0a, 0x0b, 0x0c, 0x0d)}
    _s256_log("B9 amp after config: " + " ".join(f"{r:#04x}={v:#04x}" for r, v in got.items()))
    if got[0x02] != S256_AMP_SHUTDOWN or got[0x03] != S256_GAIN or got[0x05] != S256_VOL:
        raise RuntimeError("amp state/gain/volume read-back mismatch")

    # B10 final: nothing playing
    if serdes.STATUS.reg.EN:
        raise RuntimeError("SERDES TXA enabled at the end")
    err = rg.ERROR.val
    _s256_log(f"B10 final: amp 0x02={got[0x02]:#x} (shutdown), SERDES TXA EN=0, ADMAC ch0 disabled, "
              f"DART-sio ERROR={err:#x}")


# ------------------------------------------------------------------ stage S: V/I sense path
def _s256_stage_sense(hw):
    # S1 ADMAC RX channel (cluster 0 RXB): disable, reset, 16-bit words, 2-word frames, left DISABLED
    hw.rx = hw.admac.chans[S256_SENSE_CHAN]
    if not (hw.rx.ch == S256_SENSE_CHAN and hw.rx.rx):
        raise RuntimeError("ADMAC channel object is not the RX sense channel")
    hw.rx.disable()
    hw.rx.reset()                    # admac.py reset(): rings, burst 0xc00060, 32-bit, 1 word
    hw.rx.read_reports()
    hw.rx.burstsize = 0x00600030     # FIFO limit 0x30 / threshold 0x18 words of 2 bytes
    hw.rx.buswidth = _S256_E_BUSWIDTH.W_16BIT
    hw.rx.framesize = _S256_E_FRAME.F_2_WORDS
    ar = hw.admac.regs
    c = S256_SENSE_CHAN
    _s256_log(f"S1 ADMAC ch{c} (RX) reset: BUSWIDTH={ar.CHAN_BUSWIDTH[c].val:#x} BURST={ar.CHAN_BURSTSIZE[c].val:#x} "
              f"SRAM={ar.CHAN_SRAM_CARVEOUT[c].val:#x} DESC_RING={ar.CHAN_DESC_RING[c].val:#x} "
              f"REPORT_RING={ar.CHAN_REPORT_RING[c].val:#x} RX_EN(read)={ar.RX_EN.val:#x} "
              f"TX ch{S256_ADMAC_CH} SRAM={ar.CHAN_SRAM_CARVEOUT[S256_ADMAC_CH].val:#x}; channel left disabled")
    if (ar.CHAN_BUSWIDTH[c].val & 0x77) != 0x11:
        raise RuntimeError(f"RX BUSWIDTH {ar.CHAN_BUSWIDTH[c].val:#x} != 0x11")

    # S2 MCA cluster 0 RXB: 16 slots x 16 bit on this cluster's SYNC, slots 0/1 (I, V), port 0, EN=0
    hw.rxb = hw.cl.rxb
    hw.rxb.STATUS.set(EN=0)
    hw.rxb.CONF.reg = _s256_mca.R_SERDES_CONF(NSLOTS=15, SLOT_WIDTH=_s256_mca.E_SLOT_WIDTH.W_16BIT, BCLK_POL=1,
                                              UNK1=1, UNK2=1, NO_DATA_FEEDBACK=1, SYNC_SEL=1 + S256_CLUSTER)
    hw.rxb.BITDELAY.val = 0          # same as TXA BITDELAY (amp RX and TX offset 0)
    hw.rxb.CHANMASK[0].val = 0xffff_ffff
    hw.rxb.CHANMASK[1].val = 0xffff_fffc
    hw.rxb.UNK1.val = 1 << S256_CLUSTER   # Asahi mca.c REG_RX_SERDES_PORT = port mask (port 0)
    adb = hw.switch1 + 0x8000 * S256_CLUSTER + 0x4000
    p.write32(adb, S256_SWITCH1_B)
    if hw.rxb.STATUS.reg.EN:
        raise RuntimeError("RXB enabled after configuration")
    _s256_log(f"S2 MCA cluster {S256_CLUSTER} RXB: STATUS={hw.rxb.STATUS.val:#x} CONF={hw.rxb.CONF.val:#x} "
              f"BITDELAY={hw.rxb.BITDELAY.val:#x} MASK={hw.rxb.CHANMASK[0].val:#x},{hw.rxb.CHANMASK[1].val:#x} "
              f"PORT={hw.rxb.UNK1.val:#x} adapterB[{adb:#x}]={p.read32(adb):#x} (EN=0)")

    # S3 amp TX slots (amp still in software shutdown 0x0e)
    if _s256_amp_read(hw, 0x02) != S256_AMP_SHUTDOWN:
        raise RuntimeError("amp not in shutdown before the sense config")
    before = [_s256_amp_read(hw, r) for r in (0x0e, 0x0f, 0x10, 0x31)]
    hw.i2c.write_reg(S256_AMP, 0x0e, S256_AMP_TDM)
    after = [_s256_amp_read(hw, r) for r in (0x0e, 0x0f, 0x10)]
    _s256_log("S3 amp sense TX: before 0x0e..0x10,0x31=" + ",".join(f"{v:#04x}" for v in before) +
              " after 0x0e..0x10=" + ",".join(f"{v:#04x}" for v in after) +
              f" 0x02={_s256_amp_read(hw, 0x02):#x}")
    if after != S256_AMP_TDM:
        raise RuntimeError("amp sense TX config read-back mismatch")
    if _s256_amp_read(hw, 0x02) != S256_AMP_SHUTDOWN:
        raise RuntimeError("amp left shutdown during the sense config")
    _s256_log(f"S4 sense ring window+{S256_SENSE_OFF:#x} IOVA {S256_IOVA_BASE + S256_SENSE_OFF:#x} "
              f"bytes {S256_SENSE_BYTES:#x} ch{S256_SENSE_CHAN}; protection stage {S256_PROT_STAGE} "
              f"(prot_gain={S256_PROT_GAIN:#x} prot_vol_floor={S256_PROT_VOL_FLOOR:#x})")


def _s256_sense_off(hw):
    if hw.rx is not None:
        try:
            hw.rx.disable()
        except Exception as e:
            _s256_log(f"SAFE ADMAC RX disable failed: {e}")
    if hw.rxb is not None:
        try:
            hw.rxb.STATUS.set(EN=0)
        except Exception as e:
            _s256_log(f"SAFE RXB EN=0 failed: {e}")


def _s256_safe_off(hw):
    # amp: mute -> software shutdown, then hardware shutdown (SDZ low); then stop data path
    if hw.i2c is not None:
        for v in (0x0d, 0x0e):
            try:
                hw.i2c.write_reg(S256_AMP, 0x02, [v])
            except Exception as e:
                _s256_log(f"SAFE amp 0x02<-{v:#x} failed: {e}")
    if hw.b0_ok:
        try:
            p.write32(S256_GPIO181, 0x76a02)
            _s256_log(f"SAFE GPIO181 -> {p.read32(S256_GPIO181):#x} (amp in hardware shutdown)")
        except Exception as e:
            _s256_log(f"SAFE GPIO181 low failed: {e}")
    if hw.tx is not None:
        try:
            hw.tx.disable()
        except Exception as e:
            _s256_log(f"SAFE ADMAC ch0 disable failed: {e}")
    if hw.serdes is not None:
        try:
            hw.serdes.STATUS.set(EN=0)
        except Exception as e:
            _s256_log(f"SAFE SERDES TXA EN=0 failed: {e}")
    _s256_sense_off(hw)


_s256_buf = _s256_stage_a()
_s256_hw = _S256Hw()
_s256_ok = False
try:
    _s256_stage_b(_s256_buf, _s256_hw)
    _s256_ok = True
except Exception as _s256_e:
    _s256_log(f"FAIL {type(_s256_e).__name__}: {_s256_e} -> fail closed (header status 0, amp off)")
    _s256_safe_off(_s256_hw)

_s256_sense_ok = False
if _s256_ok:
    try:
        _s256_stage_sense(_s256_hw)
        _s256_sense_ok = True
    except Exception as _s256_e:
        _s256_log(f"SENSE FAIL {type(_s256_e).__name__}: {_s256_e} -> status bit1 = 0 (driver stays static)")
        _s256_sense_off(_s256_hw)
        try:
            if _s256_amp_read(_s256_hw, 0x02) != S256_AMP_SHUTDOWN:
                _s256_hw.i2c.write_reg(S256_AMP, 0x02, [0x0d])
                _s256_hw.i2c.write_reg(S256_AMP, 0x02, [S256_AMP_SHUTDOWN])
        except Exception as _s256_e2:
            _s256_log(f"SENSE FAIL amp check: {_s256_e2}")
_s256_status = (1 if _s256_ok else 0) | (S256_STATUS_SENSE if _s256_sense_ok else 0)
iface.writemem(_s256_buf, _s256_header(_s256_status))
p.dc_civac(_s256_buf, S256_HDR_SIZE)
_s256_hdr = iface.readmem(_s256_buf, S256_HDR_LEN)
_s256_require(_s256_hdr == _s256_header(_s256_status), "final header read-back mismatch")
_s256_log(f"LAYOUT window IPA {S256_WIN_IPA:#x}+{S256_WIN_SIZE:#x} -> PA {_s256_buf:#x}; "
          f"ring IPA {S256_WIN_IPA + S256_RING_OFF:#x} IOVA {S256_IOVA_BASE + S256_RING_OFF:#x} "
          f"bytes {S256_RING_BYTES:#x}; DART-sio stream 2")
_s256_log(f"HEADER magic={S256_MAGIC:#x} version={S256_VERSION} ring_iova={S256_IOVA_BASE + S256_RING_OFF:#x} "
          f"ring_bytes={S256_RING_BYTES:#x} rate={S256_RATE} format={S256_FORMAT} admac_ch={S256_ADMAC_CH} "
          f"cluster={S256_CLUSTER} amp={S256_AMP:#x} vol_limit={S256_VOL_LIMIT:#x} status={_s256_status:#x} "
          f"sense_iova={S256_IOVA_BASE + S256_SENSE_OFF:#x} sense_bytes={S256_SENSE_BYTES:#x} "
          f"sense_chan={S256_SENSE_CHAN} sense_format={S256_SENSE_FORMAT} prot_gain={S256_PROT_GAIN:#x} "
          f"prot_vol_floor={S256_PROT_VOL_FLOOR:#x} stage={S256_PROT_STAGE}")
_s256_log("READY: hardware prepared, nothing playing" if _s256_ok else
          "NOT READY: status bit0 = 0, the NWAU0001 driver must refuse to start")
