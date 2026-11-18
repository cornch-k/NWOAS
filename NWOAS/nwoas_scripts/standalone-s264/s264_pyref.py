# SPDX-License-Identifier: MIT
"""NWOAS S264: run the REAL Python launcher code path (proxyclient hv/__init__.py HV.init,
load_raw, hv.start and the S259 boot modules) with a recorder in front of the transport.

Two transports:
  * FakeMachine (offline, this file): no hardware.  Answers proxy calls from an ADT dump,
    a boot-args blob, a synthetic GPT and a memory dict.  Used by s264_offline_test.py.
  * real proxy objects (s264_compare.py on the MacBook, later): every call goes to the
    Mac mini; the recorder only SUPPRESSES what would start or change the running state at
    hv.start time (iodev changes, fb shutdown, SPRR/GXF, hv_start) so the guest never runs.

The recorder produces the same "launch record" as s264_common.c_record() for the C plan.
"""
import os
import struct
import sys
import types
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PROXYCLIENT = ROOT / "m1n1_windows" / "proxyclient"
SCRIPTS = ROOT / "nwoas_scripts"
for _p in (str(PROXYCLIENT), str(SCRIPTS), str(HERE)):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import s264_common as C  # noqa: E402

# launcher environment of agxdcp-s259-guest-test.sh (only what the boot path reads)
S259_ENV = {
    "NWOAS_AGX_MODE": "mediate", "NWOAS_AGX_LOCAL_IPA": "0xa00000000", "NWOAS_AGX_BASE_PSTATE": "3",
    "NWOAS_AGX_SCANOUT_INTID": "889", "NWOAS_AGX_POOL_IPA": "0", "NWOAS_AGX_DCP_SERVICE": "1",
    "NWOAS_WIFI": "0", "NWOAS_HV_READ_TIMEOUT": "30", "M1N1_SPLIT_CONSOLE": "1",
    "NWOAS_CONTROLLER_DIR": str(SCRIPTS / "nvme-s139"), "NWOAS_MAX_TRANSFER": "1048576",
    "NWOAS_NVME_FAST_MASK": "1", "NWOAS_EXCLUDE_WINDOW": "1", "NWOAS_WIN_BASE": "0",
    "NWOAS_WIN_SIZE": "0x100000000", "NWOAS_CPU_PSTATE": "7", "NWOAS_LATE_P12": "0",
    "NWOAS_DELIVER_CINEBENCH": "0", "NWOAS_TARGET_QUERY": "1", "M1N1_KEEP_BAUD": "1",
}


# ---------------------------------------------------------------- recorder
class Recorder:
    def __init__(self, p, u, iface, suppress_start=True):
        self.p, self.u, self.iface = p, u, iface
        self.maps, self.msr, self.w64, self.iodev, self.copies = [], {}, [], [], []
        self.writes, self.start, self.spmi, self.agx, self.events = {}, None, [], None, []
        self.phase = "init"
        self.suppress_start = suppress_start
        self.spmi_range = None
        self._patch()

    def _wrap(self, obj, name, fn):
        orig = getattr(obj, name)
        setattr(obj, name, lambda *a, **k: fn(orig, *a, **k))

    def _patch(self):
        r = self

        def hv_map(orig, frm, to, size, incr):
            r.maps.append(("map", frm, to, size, incr))
            return orig(frm, to, size, incr)

        def hv_map_vuart(orig, base, irq, iodev):
            r.maps.append(("vuart", base, irq, iodev))
            return orig(base, irq, iodev)

        def iodev_set_usage(orig, iodev, usage):
            r.iodev.append((int(iodev), int(usage)))
            if r.phase == "start" and r.suppress_start:
                return 0
            return orig(iodev, usage)

        def write64(orig, addr, val):
            r.w64.append((addr, val))
            return orig(addr, val)

        def write32(orig, addr, val):
            if r.spmi_range and r.spmi_range[0] <= addr < r.spmi_range[1]:
                r.spmi.append((addr, val))
            return orig(addr, val)

        def memcpy8(orig, dst, src, n):
            r.copies.append((dst, src, n))
            return orig(dst, src, n)

        def simple(name):
            def f(orig, *a, **k):
                r.events.append((name, a))
                if name == "hv_start":
                    r.start = (a[0], a[1] if len(a) > 1 else 0)
                if r.phase == "start" and r.suppress_start and name in ("hv_start", "fb_improve_logo",
                                                                        "fb_shutdown"):
                    return 0
                return orig(*a, **k)
            return f

        def request(orig, opcode, *a, **k):
            if opcode == 0xc38 and a:
                r.agx = r.writes.get(a[0])
            return orig(opcode, *a, **k)

        for n, f in (("hv_map", hv_map), ("hv_map_vuart", hv_map_vuart),
                     ("iodev_set_usage", iodev_set_usage), ("write64", write64),
                     ("write32", write32), ("memcpy8", memcpy8), ("request", request)):
            self._wrap(self.p, n, f)
        for n in ("hv_start", "fb_improve_logo", "fb_shutdown", "hv_init", "pcie_init", "nvme_init"):
            self._wrap(self.p, n, simple(n))
        # ProxyUtils caches bound proxy methods in _write/_read tables
        if hasattr(self.u, "_write") and isinstance(self.u._write, dict) and 32 in self.u._write:
            self.u._write[32] = self.p.write32
            self.u._write[64] = self.p.write64

        def writemem(orig, addr, data, *a, **k):
            r.writes[addr] = bytes(data)
            return orig(addr, data, *a, **k)
        self._wrap(self.iface, "writemem", writemem)

        def msr(orig, reg, val, *a, **k):
            from m1n1.sysreg import sysreg_parse
            r.msr[C.sysreg_enc(sysreg_parse(reg))] = val
            if r.phase == "start" and r.suppress_start:
                return None
            return orig(reg, val, *a, **k)
        self._wrap(self.u, "msr", msr)

        def cw(orig, dest, data, *a, **k):
            r.copies.append((dest, "payload", len(data)))
            if getattr(r, "skip_payload_upload", False):
                return None
            return orig(dest, data, *a, **k)
        self._wrap(self.u, "compressed_writemem", cw)

    def record(self, hv, audio_buf=None):
        L = {"bootargs_base": hv.guest_base + hv.bootargs_off}
        g0, g1 = hv.phys_base, hv.guest_base + 0x10000000
        rec = dict(maps=self.maps, adt=self.writes.get(hv.adt_base), ba=self.writes.get(L["bootargs_base"]),
                   ba_base=L["bootargs_base"], msr=self.msr,
                   w64=[w for w in self.w64 if (0x210000000 <= w[0] < 0x213000000) or
                        (L["bootargs_base"] <= w[0] < L["bootargs_base"] + 0x4000)],
                   iodev=self.iodev,
                   copies=[c for c in self.copies if g0 <= c[0] < g1],
                   agx=self.agx, audio=(self.writes.get(audio_buf) or b"")[:0x4c] if audio_buf else None,
                   start=self.start, spmi=self.spmi or None)
        return rec


# ---------------------------------------------------------------- partial module loaders
def run_audio_stage_a(glb):
    """sound-s256/audio_module.py up to (not including) stage B, then _s256_stage_a().
    Stage B (power, amp, DMA) is deliberately not run: the S264 milestone has no sound."""
    src = (SCRIPTS / "sound-s256" / "audio_module.py").read_text()
    cut = src.index("# ------------------------------------------------------------------ stage B")
    ns = dict(glb)
    exec(compile(src[:cut], "audio_module.py[stage A]", "exec"), ns)
    return ns["_s256_stage_a"]()


def run_wifi_disabled(hv, glb):
    """wifi-s259/wifi_module.py effects with NWOAS_WIFI=0 (no SMC): the control page HOOK and
    the BAR BYPASS range.  The ADT fact checks and the run directory are not reproduced here."""
    sys.path.insert(0, str(SCRIPTS / "wifi-s259"))
    import nwoas_wifi_s259 as s259
    from m1n1.utils import irange
    from m1n1.hv.types import TraceMode
    u = glb["u"]
    wlan = s259.wu.wlan_info(u.adt)
    obj = s259.WifiS259(glb["p"], {}, None, enabled=False, log=lambda s: None, run_dir=None, wlan=wlan)
    hv.add_tracer(irange(s259.CTRL_IPA, s259.CTRL_SIZE), "wifi-s259-ctl", TraceMode.HOOK,
                  read=obj.hook_read, write=obj.hook_write)
    b, n = s259.BAR_EMUL
    hv.add_tracer(irange(b, n), "wifi-s259-bar-emul", TraceMode.BYPASS)
    return obj


def layout_module_effect(hv):
    """layout-s231/module.py tracer effect (same zones, replacement handlers)."""
    from m1n1.utils import irange
    from m1n1.hv.types import TraceMode

    def _u(*a):
        raise RuntimeError("unexpected")
    hv.add_tracer(irange(0x700000000, 0x100000), "s93-nvme-ecam", TraceMode.HOOK, read=_u, write=_u)
    hv.add_tracer(irange(0x700100000, 0x4000), "s93-nvme-bar", TraceMode.HOOK, read=_u, write=_u)


# ---------------------------------------------------------------- offline machine
BLOCK = 4096
DISK_BLOCKS = 61279344


def synth_gpt():
    """A GPT that satisfies layout-s231/guest_module.py (disk GUID, protected entries, zone)."""
    import uuid
    gm = (SCRIPTS / "layout-s231" / "guest_module.py").read_text()
    import re
    hexes = re.findall(r"'([0-9a-f]{256})'", gm)
    prot = [bytes.fromhex(h) for h in hexes[:4]]
    stub = bytes.fromhex(hexes[5])
    ents = bytearray(128 * 128)
    slots = {0: prot[0], 1: prot[1], 2: stub, 6: prot[3]}
    for i, e in slots.items():
        ents[i * 128:(i + 1) * 128] = e
    for i, (s, e) in zip((3, 4, 5), ((39944960, 39976959), (39976960, 39981055), (39981056, 59968511))):
        ent = bytearray(128)
        ent[0:16] = uuid.UUID("ebd0a0a2-b9e5-4433-87c0-68b6b72699c7").bytes_le
        ent[16:32] = uuid.uuid5(uuid.NAMESPACE_DNS, f"s264-{i}").bytes_le
        struct.pack_into("<QQ", ent, 32, s, e)
        ents[i * 128:(i + 1) * 128] = ent
    ecrc = zlib.crc32(bytes(ents))
    guid = uuid.UUID("898f172d-77bf-4007-b865-ac81a141eb3c").bytes_le

    def hdr(cur, alt, ent):
        h = bytearray(BLOCK)
        h[0:8] = b"EFI PART"
        struct.pack_into("<IIIIQQQQ16sQIII", h, 8, 0x10000, 92, 0, 0, cur, alt, 6, DISK_BLOCKS - 6,
                         guid, ent, 128, 128, ecrc)
        struct.pack_into("<I", h, 16, zlib.crc32(bytes(h[:92])))
        return bytes(h)
    disk = {1: hdr(1, DISK_BLOCKS - 1, 2), DISK_BLOCKS - 1: hdr(DISK_BLOCKS - 1, 1, DISK_BLOCKS - 5)}
    for k in range(4):
        disk[2 + k] = bytes(ents[k * BLOCK:(k + 1) * BLOCK])
        disk[DISK_BLOCKS - 5 + k] = disk[2 + k]
    return disk


class FakeMem:
    def __init__(self):
        self.pages = {}

    def write(self, addr, data):
        for i, b in enumerate(data):
            pg, off = divmod(addr + i, BLOCK)
            self.pages.setdefault(pg, bytearray(BLOCK))[off] = b

    def read(self, addr, n):
        out = bytearray(n)
        for i in range(n):
            pg, off = divmod(addr + i, BLOCK)
            if pg in self.pages:
                out[i] = self.pages[pg][off]
        return bytes(out)

    def write_fast(self, addr, data):
        # page-granular fast path for large aligned blobs
        if addr % BLOCK == 0:
            for k in range(0, len(data), BLOCK):
                chunk = data[k:k + BLOCK]
                pg = (addr + k) // BLOCK
                buf = self.pages.setdefault(pg, bytearray(BLOCK))
                buf[:len(chunk)] = chunk
        else:
            self.write(addr, data)


class FakeIface:
    def __init__(self, mem):
        self.mem = mem
        self.dev = types.SimpleNamespace(timeout=None)

    def writemem(self, addr, data, progress=None):
        self.mem.write_fast(addr, bytes(data))

    def readmem(self, addr, n):
        return self.mem.read(addr, n)

    def set_handler(self, *a):
        pass

    def set_event_handler(self, *a):
        pass


class FakeProxy:
    CARVE = 0x10000

    def __init__(self, mem, iface, carveouts, disk, chipid=0x8103):
        self.mem, self.iface, self.disk, self.chipid = mem, iface, disk, chipid
        for i, (b, n) in enumerate(carveouts + [(0, 0)]):
            mem.write(self.CARVE + 16 * i, struct.pack("<QQ", b, n))
        self.spmi_status_empty = True

    # transport-level
    def request(self, opcode, *a, **k):
        if opcode == 0xc38:
            return 1
        if opcode == 0xc30:
            return 0
        raise RuntimeError(f"offline: unexpected request {opcode:#x}")

    def get_chipid(self):
        return self.chipid

    def get_exc_count(self):
        return 0

    def nop(self):
        return 0

    def iodev_whoami(self):
        return 3

    def iodev_set_usage(self, iodev, usage):
        return 0

    def hv_init(self):
        return 0

    def hv_map(self, *a):
        return 0

    def hv_map_vuart(self, *a):
        return 0

    def hv_start(self, *a):
        return 0

    def fb_improve_logo(self):
        return 0

    def fb_shutdown(self, *a):
        return 0

    def mcc_get_carveouts(self):
        return self.CARVE

    def read64(self, a):
        return struct.unpack("<Q", self.mem.read(a, 8))[0]

    def read32(self, a):
        if a & 0xfff == 0x300 and getattr(self, "spmi_base", None) == a:
            return (1 << 24) if self.spmi_status_empty else 0
        return struct.unpack("<I", self.mem.read(a, 4))[0]

    def write64(self, a, v):
        self.mem.write(a, struct.pack("<Q", v))

    def write32(self, a, v):
        if getattr(self, "spmi_base", None) is not None and a == self.spmi_base + 4:
            self.spmi_status_empty = False
        self.mem.write(a, struct.pack("<I", v & 0xffffffff))

    def mask32(self, a, clr, st):
        v = (self.read32(a) & ~clr) | st
        self.write32(a, v)
        return v

    def memcpy8(self, d, s, n):
        self.mem.write_fast(d, self.mem.read(s, n) if n <= 0x100000 else bytes(n))

    def memset8(self, d, v, n):
        self.mem.write_fast(d, bytes([v]) * n)

    def dc_cvau(self, *a):
        pass

    ic_ivau = dc_civac = dc_cvau

    def pcie_init(self):
        return 0

    def nvme_init(self):
        return True

    def nvme_guest_map(self, *a):
        return 0

    def nvme_read_n(self, ns, lba, buf, n):
        for k in range(n):
            self.mem.write_fast(buf + k * BLOCK, self.disk.get(lba + k, bytes(BLOCK)))
        return True

    def nwoas_nvme_fastpath(self, action, *a, **k):
        return {23: 0x5332333000000001}.get(action, 1)


class FakeU:
    def __init__(self, p, iface, adt_bytes, ba, heap_base, heap_size, regs):
        from m1n1.adt import load_adt
        self.proxy, self.iface, self._adt_bytes = p, iface, adt_bytes
        self.adt = load_adt(adt_bytes)
        self.ba = ba
        self.heap_base, self.heap_top = heap_base, heap_base + heap_size
        self._brk = heap_base + 0x10000
        self.regs = regs

    def get_adt(self):
        return self._adt_bytes

    def mrs(self, reg, **k):
        from m1n1.sysreg import sysreg_parse
        return self.regs.get(C.sysreg_enc(sysreg_parse(reg)), 0)

    def msr(self, reg, val, **k):
        return None

    def inst(self, *a, **k):
        return 0

    def compressed_writemem(self, dest, data, progress=None):
        return None  # the payload content is not needed by the record

    def memalign(self, align, size):
        self._brk = (self._brk + align - 1) & ~(align - 1)
        a = self._brk
        self._brk += size
        assert self._brk <= self.heap_top
        return a

    def malloc(self, size):
        return self.memalign(0x40, size)

    def free(self, *a):
        pass

    def read(self, addr, width):
        return {32: self.proxy.read32, 64: self.proxy.read64}[width](addr)

    def write(self, addr, data, width):
        return {32: self.proxy.write32, 64: self.proxy.write64}[width](addr, data)


def build_ba_raw(rev, **f):
    from m1n1.tgtypes import BootArgs_r1, BootArgs_r2, BootArgs_r3
    S = {1: BootArgs_r1, 2: BootArgs_r2, 3: BootArgs_r3}[rev]
    v = dict(revision=rev, version=2, virt_base=0, phys_base=0, mem_size=0, top_of_kernel_data=0,
             video=dict(base=0, display=1, stride=0, width=0, height=0, depth=32),
             machine_type=0, devtree=0, devtree_size=0, cmdline="", boot_flags=0, mem_size_actual=0)
    v.update(f)
    raw = S.build(v)
    return raw, S.parse(raw)


def run_python_offline(adt_bytes, ba_rev, ba_fields, heapblock_top, carveouts, payload_size,
                       midr, amx, actlr):
    """Real HV code + real guest_map/pmgr_gate/guest_module/agx modules + stage-A audio +
    NWOAS_WIFI=0 Wi-Fi effects, over the fake machine.  Returns (record, hv, wifi_obj)."""
    os.environ.update(S259_ENV)
    from m1n1.hv import HV
    from m1n1.hw.pmu import PMU
    mem = FakeMem()
    iface = FakeIface(mem)
    p = FakeProxy(mem, iface, carveouts, synth_gpt())
    raw, ba = build_ba_raw(ba_rev, **ba_fields)
    u = FakeU(p, iface, adt_bytes, ba, heapblock_top + (128 << 20), 768 << 20,
              {C.sysreg_enc((3, 0, 0, 0, 0)): midr, C.sysreg_enc((3, 4, 15, 1, 4)): amx,
               C.sysreg_enc((3, 6, 15, 14, 6)): actlr, C.sysreg_enc((3, 5, 1, 0, 1)): actlr})
    rec = Recorder(p, u, iface, suppress_start=True)
    hv = HV(iface, p, u)
    hv.init()
    hv.load_raw(bytes(payload_size), 0x800)
    # PMU(u).reset_panic_counter() as run_guest.py:103
    (path, bus) = PMU.find_primary_pmu(u.adt)
    p.spmi_base = u.adt[path.rpartition('/')[0]].get_reg(0)[0]
    rec.spmi_range = (p.spmi_base, p.spmi_base + 0x100)
    PMU(u).reset_panic_counter()
    glb = hv.shell_locals
    glb.update(dict(p=p, u=u, iface=iface, hv=hv))
    for mod in ("usb-s174/guest_map.py", "smp-s133/pmgr_gate.py", "layout-s231/guest_module.py"):
        hv.run_script(str(SCRIPTS / mod))
        glb = hv.shell_locals
    layout_module_effect(hv)
    hv.run_script(str(ROOT / "port-j274" / "gpu" / "hv" / "tools-hv2" / "agx_hv2_module.py"))
    glb = hv.shell_locals
    audio_buf = run_audio_stage_a(glb)
    wifi = run_wifi_disabled(hv, glb)
    rec.phase = "start"
    hv.start()
    return rec.record(hv, audio_buf), hv, wifi, raw
