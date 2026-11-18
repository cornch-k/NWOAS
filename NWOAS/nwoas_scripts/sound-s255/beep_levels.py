#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# NWOAS S258: level-step comparison (copy of S255 beep_tone.py: same register sequence),
# plus a read-only amp register dump before/after.  Original header:
# NWOAS S255: first sound from the Mac mini (J274) internal speaker, run on the
# D4 m1n1 proxy BEFORE the hypervisor is chainloaded (no Windows involved).
#
# Based on m1n1 proxyclient/experiments/speaker_amp.py (MIT).  Changes:
#   - the amp digital volume (TAS2770-style reg 0x05, -0.5 dB/step, reset = 0 dB)
#     is set BEFORE the amp leaves shutdown and is read back; abort if it differs
#   - amp volume fixed at -40 dB; three short tones at -20/-10/-4 dBFS, so
#     -60/-50/-44 dB reach the speaker path (J274 thermal limit at 21 dB gain
#     is about -17.7 dB attenuation per research-20261116/sound/hw-path.md)
#   - amp gain reg 0x03 = 0x00 (lowest), fades to avoid clicks
#   - MCA clock mux set like m1n1 clk_set_mca_muxes() (only kboot does it)
#   - DART via DART.from_adt (DARTRegs no longer exists)
#   - mute + software shutdown + DMA/MCA stop in a finally block
import sys, pathlib, math, struct, time
sys.path.append(str(pathlib.Path(__file__).resolve().parents[2] / "m1n1_windows" / "proxyclient"))

from m1n1.setup import *
from m1n1.hw.dart import DART
from m1n1.hw.i2c import I2C
from m1n1.hw.pmgr import PMGR
from m1n1.hw.nco import NCO
from m1n1.hw.admac import *
from m1n1.hw.mca import *

AMP = 0x31
RATE = 48000
VOL = 0x30                             # amp digital volume -24 dB (driver cap), after the S258 pad fix
LEVELS = [-30.0, -20.0, -12.0, -6.0]   # dBFS, same as Windows A4
TONE_S, GAP_S, FREQ = 1.5, 0.6, 440.0
FULL = (1 << 23) - 1                   # 24-bit full scale
FADE = int(0.02 * RATE)


def tone(seconds, dbfs):
    AMPL = FULL * 10 ** (dbfs / 20.0)
    n = int(seconds * RATE)
    out = bytearray()
    for i in range(n):
        g = min(1.0, i / FADE, (n - 1 - i) / FADE)
        s = int(AMPL * g * math.sin(2 * math.pi * FREQ * i / RATE))
        out += struct.pack("<i", s << 8)   # 24-bit, msb-aligned in 32 bits
    return bytes(out)


def silence(seconds):
    return bytes(4 * int(seconds * RATE))


def log(msg):
    print(f"[S258-level] {msg}", flush=True)


for node in ("/arm-io/i2c1", "/arm-io/admac-sio", "/arm-io/dart-sio", "/arm-io/mca-switch",
             "/arm-io/mca0", "/arm-io/mca1"):
    try:
        p.pmgr_adt_clocks_enable(node)
    except Exception as e:
        log(f"clocks {node}: {e}")

# MCA clock muxes (m1n1 src/clk.c, MIT): bits 27:24 = 5 + min(4, i)
clk_base, clk_size = u.adt["/arm-io/mca-switch"].get_reg(2)
for i in range(clk_size // 4):
    a = clk_base + 4 * i
    v = p.read32(a)
    nv = (v & ~(0xf << 24)) | ((5 + min(4, i)) << 24)
    if nv != v:
        p.write32(a, nv)
log(f"MCA clock mux @ {clk_base:#x}: " + " ".join(f"{p.read32(clk_base + 4*i):#x}" for i in range(clk_size // 4)))

# reset AUDIO_P (shared by MCA and ADMAC)
PS_AUDIO_P = PMGR(u).regs[0].PS4[5]
PS_AUDIO_P.set(DEV_DISABLE=1)
PS_AUDIO_P.set(RESET=1)
PS_AUDIO_P.set(RESET=0)
PS_AUDIO_P.set(DEV_DISABLE=0)

i2c1 = I2C(u, "/arm-io/i2c1")
dart = DART.from_adt(u, "/arm-io/dart-sio")
dart.initialize()

cl_no = 0
admac = ADMAC(u, "/arm-io/admac-sio", dart, debug=False)
tx_chan = admac.chans[4 * cl_no]
tx_chan.disable()
tx_chan.reset()
tx_chan.read_reports()
tx_chan.buswidth = E_BUSWIDTH.W_32BIT
tx_chan.framesize = E_FRAME.F_1_WORD

nco = NCO(u, "/arm-io/nco")
nco[cl_no].set_rate(RATE * 256)
nco[cl_no].enable()

mca_switch1_base = u.adt["/arm-io/mca-switch"].get_reg(1)[0]
mca_cl_base = u.adt["/arm-io/mca-switch"].get_reg(0)[0] + 0x4000 * cl_no
cl = MCACluster(u, mca_cl_base)
regs, serdes = cl.regs, cl.txa

pcm = bytearray(silence(0.2))
for lv in LEVELS:
    pcm += tone(TONE_S, lv) + silence(GAP_S)
pcm += silence(0.3)
pcm = bytes(pcm)
BUF = 32 * 1024
chunks = [pcm[i:i + BUF] for i in range(0, len(pcm), BUF)]

def amp_dump(tag):
    try:
        vals = []
        for r in range(0x00, 0x40):
            vals.append(i2c1.read_reg(AMP, r, 1)[0])
        for b in range(0, 0x40, 16):
            log(f"amp {tag} {b:#04x}: " + " ".join(f"{v:02x}" for v in vals[b:b+16]))
    except Exception as e:
        log(f"amp dump {tag} failed: {e}")

amp_on = False
try:
    regs.SYNCGEN_STATUS.set(RST=1, EN=0)
    regs.SYNCGEN_STATUS.set(RST=0)
    regs.SYNCGEN_MCLK_SEL.val = (1 + cl_no)
    regs.SYNCGEN_HI_PERIOD.val = 0
    regs.SYNCGEN_LO_PERIOD.val = 0xfe

    serdes.STATUS.set(EN=0)
    serdes.CONF.set(NSLOTS=0, SLOT_WIDTH=E_SLOT_WIDTH.W_32BIT, BCLK_POL=1, UNK1=1, UNK2=1,
                    IDLE_UNDRIVEN=1, SYNC_SEL=(1 + cl_no))
    serdes.BITDELAY.val = 0
    serdes.CHANMASK[0].val = 0xffff_fffe
    serdes.CHANMASK[1].val = 0xffff_fffe

    regs.PORT_ENABLES.set(CLOCK1=1, CLOCK2=1, DATA=1)
    regs.PORT_CLK_SEL.set(SEL=(cl_no + 1))
    regs.PORT_DATA_SEL.val = cl_no + 1
    regs.MCLK_STATUS.set(EN=1)
    regs.SYNCGEN_STATUS.set(EN=1)
    p.write32(mca_switch1_base + 0x8000 * cl_no, 0x102040)   # S258 fix: TX_LSB_PAD 0 (msb-aligned data)

    # amp reset GPIO 181 (ADT function-reset)
    p.write32(0x23c1002d4, 0x76a02)
    time.sleep(0.01)
    p.write32(0x23c1002d4, 0x76a03)
    time.sleep(0.01)

    # volume FIRST, while the amp is still in shutdown; verify
    i2c1.write_reg(AMP, 0x05, [VOL])
    vol = i2c1.read_reg(AMP, 0x05, 1)[0]
    log(f"amp volume reg 0x05 = {vol:#x} (want {VOL:#x})")
    if vol != VOL:
        raise RuntimeError("amp volume read-back mismatch, not unmuting")

    amp_dump("before-data")
    ci = 0
    tx_chan.submit(chunks[ci]); ci += 1
    tx_chan.enable()
    while ci < len(chunks) and tx_chan.can_submit():
        tx_chan.submit(chunks[ci]); ci += 1
    serdes.STATUS.set(EN=1)

    i2c1.write_reg(AMP, 0x08, [0x40])
    i2c1.write_reg(AMP, 0x0a, [0x06, 0x00, 0x1a])
    i2c1.write_reg(AMP, 0x1b, [0x01, 0x82, 0x06])
    i2c1.write_reg(AMP, 0x16, [0x50, 0x04])
    i2c1.write_reg(AMP, 0x0d, [0x00])
    i2c1.write_reg(AMP, 0x03, [0x00])        # lowest amp gain
    i2c1.write_reg(AMP, 0x05, [VOL])  # volume again after the config writes
    vol = i2c1.read_reg(AMP, 0x05, 1)[0]
    gain = i2c1.read_reg(AMP, 0x03, 1)[0]
    log(f"before unmute: volume {vol:#x} gain {gain:#x}")
    if vol != VOL or gain != 0x00:
        raise RuntimeError("amp volume/gain read-back mismatch, not unmuting")
    i2c1.write_reg(AMP, 0x02, [0x0c])        # active
    amp_on = True
    log(f"playing 4 tones at {LEVELS} dBFS, amp volume -40 dB")

    while ci < len(chunks):
        while not tx_chan.can_submit():
            tx_chan.poll()
        tx_chan.submit(chunks[ci]); ci += 1
    time.sleep(0.6)
    amp_dump("after-play")
    log("done")
finally:
    try:
        i2c1.write_reg(AMP, 0x02, [0x0d])    # mute
        i2c1.write_reg(AMP, 0x02, [0x0e])    # software shutdown
        log(f"amp state reg 0x02 = {i2c1.read_reg(AMP, 0x02, 1)[0]:#x} (0xe = shutdown)")
    except Exception as e:
        log(f"amp shutdown write failed: {e}")
    try:
        tx_chan.disable()
        serdes.STATUS.set(EN=0)
        regs.SYNCGEN_STATUS.set(EN=0)
        regs.MCLK_STATUS.set(EN=0)
    except Exception as e:
        log(f"stop failed: {e}")
