"""S244: pure checks for running the S243 stack on macOS 13.5 OS firmware (iBoot-8422.141.2).

No m1n1/proxy imports: every function takes an ADT-like object (m1n1.adt node API: adt['/path'],
getattr(node, 'prop-name')) and a BootArgs-like object (ba.phys_base, ba.mem_size, ba.video.*).
Used by probe_fw135_s244.py (D4 proxy, before chainload) and fw135_preflight.py (HV pre-init, before
hv.init()). Nothing here reads or writes the Mac mini by itself.

What is modelled and why (source citations are in README.md):
  * guest skew: the guest m1n1 prefix (S215, built from bddf7f06 + loader-s197 patch) lowers
    boot_args.mem_size by top_of_memory_alloc() before UEFI runs. layout-s231/guest_module.py:44,52 and
    usb-s174/guest_map.py:13 hard-code that reduction as 0x34000. On Tahoe it is 16K guard + 16K
    kboot log buffer + SIO firmware data (sio.c, MERGE_SIO_FWDATA). The SIO part depends on the ADT
    (OS-paired DeviceTree, which changes to 13.5), and two more terms appear only off Tahoe:
    a guest framebuffer relocation (display.c "current framebuffer is too small") and an ISP heap
    (isp.c, V13_5 case). If the real reduction differs from 0x34000, UEFI's backing (phys_base +
    mem_size) and the HV low-window backing disagree -> silent guest memory aliasing.
  * FBPROBE: S233b hv_exc.c:240 runs `dc ivac` (invalidate, no clean) over a hard-coded
    0xbe3f60000 + 5120*720 bytes ~120 s into the Windows kernel. That is the Tahoe /vram. It is only
    harmless while that range stays inside /vram.
"""

OS_FW_135 = "iBoot-8422.141.2"
TAHOE_OS_FW = "mBoot-18000.121.3"
SYSTEM_FW_PREFIX = "mBoot-18000"
SKEW_PINNED = 0x34000              # layout-s231/guest_module.py:44,52; usb-s174/guest_map.py:13
FBPROBE_BASE = 0xBE3F60000         # m1n1-usb-s233/src/hv_exc.c:240
FBPROBE_BYTES = 5120 * 720         # hv_exc.c:241-242
SZ_4K = 0x1000
SZ_16K = 0x4000
WINDOW = 0x100000000               # NWOAS_WIN_SIZE in the launcher


def align_up(x, a):
    return (x + a - 1) & ~(a - 1)


def cstr(value):
    """ADT strings come back as bytes (firmware-version) or str (system-firmware-version)."""
    if isinstance(value, str):
        return value.split("\0", 1)[0]
    return bytes(value).split(b"\0", 1)[0].decode("ascii", "replace")


def _prop(node, name):
    try:
        value = getattr(node, name)
    except (AttributeError, KeyError):
        return None
    return value


def _node(adt, path):
    try:
        return adt[path]
    except (KeyError, IndexError, AttributeError):
        return None


def _key(s):
    s = s.encode("latin-1") if isinstance(s, str) else s
    return (s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3]


SPACER = "\xff\xff\xff\xff"
# m1n1-guest-s215/src/sio.c copy_rules[] in order: (prop, blobsize, keyed, keys)
SIO_RULES = [
    ("asio-ascwrap-tunables", 0, False, ()),
    (None, 0x1B80, False, ()),
    (None, 0x1E000, False, ()),
    (None, 0x4000, False, ()),
    ("map-range", 16, True, (SPACER, SPACER, SPACER, "MISC")),
    ("dmashim", 32, True, ("SSPI", "SUAR", "SAUD", "ADMA", "AAUD")),
    ("device-type", 8, True, ("dSPI", "dUAR", "dMCA", "dDPA", "dPDM", "dALE", "dAMC", "dAPD")),
]


def sio_alloc(adt):
    """Bytes taken from the top of memory by sio_setup_fwdata('/arm-io/sio') (MERGE_SIO_FWDATA on).

    Returns (bytes, notes). Guard page not included. Mirrors alloc_mapped_data(): a request that
    fits in the 16K-rounded previous mapping is merged; otherwise top_of_memory_alloc(size).
    """
    notes = []
    sio = _node(adt, "/arm-io/sio")
    if sio is None:
        return 0, ["no /arm-io/sio: dt_reserve_asc_firmware bails, no SIO data"]
    if _prop(sio, "segment-ranges") is None:
        return 0, ["/arm-io/sio has no segment-ranges: SIO data not modelled as allocated"]
    mappings = []
    total = 0

    def alloc(size):
        nonlocal total
        if mappings and align_up(mappings[-1], SZ_16K) >= mappings[-1] + size:
            mappings[-1] = align_up(mappings[-1] + size, SZ_4K)
            return
        mappings.append(align_up(size, SZ_4K))
        total += align_up(size, SZ_16K)

    for prop, blobsize, keyed, keys in SIO_RULES:
        if prop is None:
            alloc(blobsize)
            continue
        blob = _prop(sio, prop)
        if blob is None:
            notes.append(f"missing ADT property {prop!r}: sio_setup_fwdata stops here")
            return total, notes
        blob = bytes(blob)
        if not keyed:
            alloc(len(blob))
            notes.append(f"{prop} len={len(blob):#x}")
            continue
        alloc(len(keys) * blobsize)
        if len(blob) % (blobsize + 4):
            notes.append(f"{prop} bad length {len(blob)}: sio_setup_fwdata stops here")
            return total, notes
        known = [_key(k) for k in keys]
        for off in range(0, len(blob) - blobsize + 1, blobsize + 4):
            k = int.from_bytes(blob[off:off + 4], "little")
            if k not in known:
                notes.append(f"{prop} unknown key {k:#x}: sio_setup_fwdata stops here")
                return total, notes
    return total, notes


def guest_fb_need(width, height, bpp=32):
    """display.c display_configure(): size of the framebuffer the chosen mode needs."""
    return align_up(width * height * ((bpp + 7) // 8) + 24 * SZ_16K, SZ_16K)


def evaluate(adt, ba, *, headless_ok=False):
    """Return a JSON-able dict with facts, predictions and named checks (each check: ok + why)."""
    out = {"facts": {}, "predictions": {}, "checks": {}, "notes": []}
    f, pr, ck = out["facts"], out["predictions"], out["checks"]

    ch = adt["/chosen"]
    os_fw = cstr(ch.firmware_version)
    sys_fw = cstr(_prop(ch, "system_firmware_version") or _prop(ch, "system-firmware-version") or "")
    compat = [str(c) for c in adt.compatible] if not isinstance(adt.compatible, str) else [adt.compatible]
    f.update(os_firmware=os_fw, system_firmware=sys_fw, compatible=compat)
    ck["G1_os_fw_13_5"] = (os_fw == OS_FW_135, f"OS firmware {os_fw!r} (need {OS_FW_135!r})")
    ck["G1_system_fw_tahoe"] = (sys_fw.startswith(SYSTEM_FW_PREFIX),
                                f"system firmware {sys_fw!r} (expected {SYSTEM_FW_PREFIX}*)")
    ck["G1_model_j274"] = (any("j274" in c.lower() for c in compat) and "Macmini9,1" in compat,
                           "model J274AP / Macmini9,1")

    v = ba.video
    vram_node = _node(adt, "/vram")
    vram = (vram_node.reg[0].addr, vram_node.reg[0].size) if vram_node is not None else (0, 0)
    ram = (ba.phys_base, ba.phys_base + ba.mem_size)
    f.update(video=dict(base=hex(v.base), display=int(v.display), stride=int(v.stride),
                        width=int(v.width), height=int(v.height), depth=hex(v.depth)),
             vram=dict(addr=hex(vram[0]), size=hex(vram[1])),
             phys_base=hex(ba.phys_base), mem_size=hex(ba.mem_size),
             guest_ram_end=hex(ram[1]))

    geom_ok = (v.base != 0 and v.width > 0 and v.height > 0 and v.stride == v.width * 4
               and (v.depth & 0xFF) == 32)
    ck["G2_video_geometry"] = (geom_ok or headless_ok,
                               f"{v.width}x{v.height} stride {v.stride} depth {v.depth:#x}"
                               + ("" if geom_ok else " (not a 32bpp m1n1 mode)")
                               + (" [headless override]" if headless_ok and not geom_ok else ""))
    pr["resolution"] = f"{v.width}x{v.height}"
    pr["is_1080p"] = (v.width, v.height) == (1920, 1080)
    # iBoot hands Macmini9,1 a 640x1136 placeholder (S243 log, boot_args before S233b); m1n1's
    # display_init() treats it as "Dummy framebuffer found" (display.c:641). Still seeing it means
    # no m1n1 modeset happened yet (no HDMI sink, or display_configure failed).
    pr["m1n1_mode_set"] = (v.width, v.height) != (640, 1136)
    if not pr["m1n1_mode_set"]:
        out["notes"].append("video is still the iBoot 640x1136 placeholder: no m1n1 modeset "
                            "(no HDMI sink or display_configure failed); expect no picture")

    base_is_vram = vram[1] > 0 and v.base == vram[0]
    ck["G2_video_base_is_vram"] = (base_is_vram or headless_ok,
                                   f"video.base {v.base:#x} vs /vram {vram[0]:#x}")
    relocated = v.base == ba.phys_base + ba.mem_size + SZ_16K
    pr["fb_relocated_by_m1n1"] = relocated
    if relocated:
        out["notes"].append("framebuffer sits right above mem_size + guard: an earlier m1n1 ran the "
                            "'framebuffer is too small' path (display.c); FBPROBE check decides")

    vram_hits_ram = vram[1] > 0 and vram[0] < ram[1] and ram[0] < vram[0] + vram[1]
    ck["G2_vram_outside_guest_ram"] = (vram[1] > 0 and not vram_hits_ram,
                                       f"/vram [{vram[0]:#x},{vram[0] + vram[1]:#x}) vs RAM "
                                       f"[{ram[0]:#x},{ram[1]:#x})")
    fbp_in = vram[0] <= FBPROBE_BASE and FBPROBE_BASE + FBPROBE_BYTES <= vram[0] + vram[1]
    ck["G2_fbprobe_inside_vram"] = (fbp_in, f"S233b FBPROBE dc-ivac range [{FBPROBE_BASE:#x},"
                                            f"{FBPROBE_BASE + FBPROBE_BYTES:#x}) inside /vram")

    # Guest m1n1 top-of-memory allocations, in m1n1_main/kboot_prepare_dt order.
    terms = []
    fb_size = vram[1] if base_is_vram else v.stride * v.height * (((v.depth & 0xFF) + 7) // 8)
    need = guest_fb_need(v.width, v.height) if v.width and v.height else 0
    guest_reloc = bool(need) and fb_size < need
    pr["guest_fb_need"] = hex(need)
    pr["guest_fb_relocates"] = guest_reloc
    if guest_reloc:
        terms.append(("guest framebuffer relocation", align_up(need, SZ_16K)))
    isp = _node(adt, "/arm-io/isp") or _node(adt, "/arm-io/isp0")
    pr["isp_present"] = isp is not None
    terms.append(("guard page", SZ_16K))
    terms.append(("kboot log buffer", SZ_16K))
    sio_bytes, sio_notes = sio_alloc(adt)
    terms.append(("SIO firmware data", sio_bytes))
    out["notes"] += sio_notes
    skew = sum(t[1] for t in terms)
    pr["guest_skew_terms"] = [(name, hex(n)) for name, n in terms]
    pr["guest_skew"] = hex(skew)
    ck["G2_guest_skew_matches_pin"] = (skew == SKEW_PINNED and isp is None,
                                       f"predicted guest mem_size reduction {skew:#x} vs pinned "
                                       f"{SKEW_PINNED:#x}" + (" (ISP heap not modelled)" if isp else ""))

    low_backing = ba.phys_base + ba.mem_size - WINDOW - SKEW_PINNED
    pr["low_backing_if_pin_holds"] = hex(low_backing)
    out["ok"] = all(c[0] for c in ck.values())
    out["checks"] = {k: {"ok": bool(a), "why": b} for k, (a, b) in ck.items()}
    return out


def gate_g1(result):
    return all(result["checks"][k]["ok"] for k in result["checks"] if k.startswith("G1_"))


def failed(result):
    return [f"{k}: {c['why']}" for k, c in result["checks"].items() if not c["ok"]]
