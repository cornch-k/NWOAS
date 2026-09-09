"""Configure the Tahoe DCP immediately before HV guest entry.

Loaded by run_guest.py as an HV script.  It only installs a callback; the
callback runs after the guest image and ADT have been uploaded, minimizing the
gap between the successful mode/swap transaction and UEFI execution.
"""
import hashlib
import json
import os
import struct
import time
from pathlib import Path

from construct import Container
from m1n1.fw.afk.rbep import AFKRingBufEndpoint
from m1n1.fw.dcp.iboot import DCPIBootClient, IBootLayerInfo
from m1n1.hw.asc import ASC
from m1n1.hw.dart import DART

from tahoe_afk_link import TahoeIBoot, TahoeLink, decode_packet
import sys
sys.path.insert(0, "/Volumes/X31/NWOAS/nwoas_scripts/display-s190")
from hpd_wait import wait_for_mode_counts


def nwoas_ready_counts(label):
    result = wait_for_mode_counts(
        service.getModeCount, time.monotonic, time.sleep,
        timeout_s=8.0, max_attempts=200, poll_interval_s=0.05,
        max_count=(4096-4)//24, log=print, label=label)
    if not result.ready:
        raise RuntimeError("S190 display not ready: " + result.summary())
    return result.last_sample


ROOT = Path("/Volumes/X31/NWOAS")
D4 = ROOT / "experiments/tahoe-afk-20260906/m1n1-dcp-defer.bin"
D4_MD5 = "7bec0225edc101857be3b7f195dd7357"
MARKER = b"NWOAS-DCP-DEFER: automatic display init deferred to host"

FB_WIDTH = 1280
FB_HEIGHT = 720
FB_STRIDE = FB_WIDTH * 4
FB_DEPTH = 32


def choose_timing(blob, count, vram_size, preferred_size=None):
    """Choose a near-60 Hz progressive mode whose framebuffer fits VRAM."""
    assert len(blob) == 4 + struct.unpack_from("<I", blob)[0] * 24
    modes = [struct.unpack_from("<6I", blob, 4 + j * 24) for j in range(count)]
    usable = [m for m in modes
              if m[0] == 1 and m[1] >= 1024 and m[2] >= 720
              and m[1] * m[2] * 4 <= vram_size
              and 50 * 65536 <= m[3] <= 61 * 65536]
    assert usable, f"No usable near-60 Hz timing: {modes!r}"

    def rank(m):
        size = (m[1], m[2])
        preferred = 0 if preferred_size and size == preferred_size else 1
        standard = {(1280, 720): 0, (1920, 1080): 1}.get(size, 2)
        refresh_error = abs(m[3] - 60 * 65536)
        return preferred, standard, refresh_error, m[1] * m[2], m[4], m[5]

    chosen = min(usable, key=rank)
    raw = next(blob[4 + j * 24:28 + j * 24] for j in range(count)
               if struct.unpack_from("<6I", blob, 4 + j * 24) == chosen)
    print("[FACT] advertised timings", modes)
    print("[FACT] selected timing", chosen)
    return raw, chosen


def prepare_guest_framebuffer_bootargs():
    """Make the guest BootArgs describe the surface DCP will scan out.

    This script runs before hv.init(), which copies u.ba into hv.tba.  Updating
    u.ba here therefore reaches the BootArgs blob later uploaded by load_raw().
    Keep the firmware-provided framebuffer base; only replace the geometry.
    """
    video = u.ba.video
    old = (video.base, video.display, video.stride,
           video.width, video.height, video.depth)
    need = FB_STRIDE * FB_HEIGHT
    vram = u.adt["/vram"].reg[0]
    assert video.base == vram.addr
    assert vram.size >= need, (vram.size, need)
    video.display = 1
    video.stride = FB_STRIDE
    video.width = FB_WIDTH
    video.height = FB_HEIGHT
    video.depth = FB_DEPTH
    new = (video.base, video.display, video.stride,
           video.width, video.height, video.depth)
    print("[FIX] guest framebuffer BootArgs prepared before hv.init", {
        "old": old, "new": new, "vram_size": vram.size,
    })


class NewIBootEndpoint(AFKRingBufEndpoint):
    SHORT = "iboot"
    announcement = None

    def handle_ipc(self, data):
        assert self.announcement is None
        packet = decode_packet(data)
        assert (packet["interface"], packet["kind"], packet["category"]) == (3, 0x11, 0)
        assert packet["payload"][:32].split(b"\0", 1)[0] == b"disp0-service"
        self.announcement = packet


class NewClient(DCPIBootClient):
    ENDPOINTS = {**DCPIBootClient.ENDPOINTS, 0x23: NewIBootEndpoint}


def start_tahoe_dcp():
    prefix = Path(os.environ["NWOAS_DCP_LOG"])
    hv_image = Path(os.environ.get("NWOAS_HV_IMAGE", str(D4)))
    binary = hv_image.read_bytes()
    if hv_image.resolve() == D4.resolve():
        assert hashlib.md5(binary).hexdigest() == D4_MD5
    assert MARKER in binary
    base = p.get_base()
    assert iface.readmem(base + binary.index(MARKER), len(MARKER)) == MARKER
    assert u.adt.model == "Macmini9,1"
    assert b"mBoot-18000.121.3" in bytes(u.adt["/chosen"].firmware_version)

    dart = DART.from_adt(u, "/arm-io/dart-dcp")
    asc = u.adt["/arm-io/dcp"].get_reg(0)[0]
    dcp = NewClient(u, asc, dart)
    dcp.dva_offset = u.adt["/arm-io/dcp"][0].asc_dram_mask
    ASC.boot(dcp)
    dcp.mgmt.start()
    dcp.mgmt.wait_boot(30)
    dcp.start_ep(0x23)
    end = time.monotonic() + 10
    while dcp.iboot.announcement is None and time.monotonic() < end:
        dcp.work()
    assert dcp.iboot.announcement is not None

    ep = dcp.iboot
    ptr = (ep.txq.get_rptr(), ep.txq.get_wptr(), ep.rxq.get_rptr(), ep.rxq.get_wptr())
    assert ptr == (0, 0, 256, 256), ptr
    link = TahoeLink(iface, p, ptr, str(prefix), boot_base=base,
                     shared=ep.iobuffer, syslog_base=dcp.syslog.iobuffer,
                     asc_base=asc)
    service = TahoeIBoot(link)
    print("[FACT] Tahoe DCP management/AFK ready before hv.init", {
        "boot_base": base, "shared": ep.iobuffer,
        "syslog_base": dcp.syslog.iobuffer, "asc_base": asc,
        "pointers": link.pointers(),
    })
    return prefix, base, asc, dart, dcp, ep, link, service


prefix, base, asc, dart, dcp, ep, link, service = start_tahoe_dcp()

# Select geometry from the currently attached sink before hv.init() copies u.ba
# into hv.tba. The former hard-coded 720p mode fails on portable panels which do
# not advertise 1280x720 even though their native 1080p mode works.
_boot_hpd, _boot_nt, _boot_nc = nwoas_ready_counts("S190 before hv.init")
_boot_timings = service.send_cmd(4, replen=4096)
_boot_timing, _boot_mode = choose_timing(
    _boot_timings, _boot_nt, u.adt["/vram"].reg[0].size)
FB_WIDTH, FB_HEIGHT = _boot_mode[1], _boot_mode[2]
FB_STRIDE = FB_WIDTH * 4
prepare_guest_framebuffer_bootargs()


def configure_tahoe_display():
    hpd, nt, nc = nwoas_ready_counts("S190 before guest entry")
    print("[FACT] pre-guest HPD/counts", hpd, nt, nc)
    timings = service.send_cmd(4, replen=4096)
    colors = service.send_cmd(5, replen=4096)
    assert len(colors) == 4 + struct.unpack_from("<I", colors)[0] * 24
    timing, selected = choose_timing(
        timings, nt, u.adt["/vram"].reg[0].size, (FB_WIDTH, FB_HEIGHT))
    assert selected[1:3] == (FB_WIDTH, FB_HEIGHT), (selected, FB_WIDTH, FB_HEIGHT)
    color = next(colors[4 + j * 24:28 + j * 24] for j in range(nc)
                 if struct.unpack_from("<IIIII", colors, 4 + j * 24) == (1, 1, 1, 1, 32))

    assert (hv.tba.video.display, hv.tba.video.stride, hv.tba.video.width,
            hv.tba.video.height, hv.tba.video.depth) == (
                1, FB_STRIDE, FB_WIDTH, FB_HEIGHT, FB_DEPTH)
    pa, dva, size = u.ba.video.base, 0x13DC000, FB_STRIDE * FB_HEIGHT
    assert pa == u.adt["/vram"].reg[0].addr
    for path in ("/arm-io/dart-disp0", "/arm-io/dart-dcp"):
        assert DART.from_adt(u, path).iotranslate(0, dva, size) == [(pa, size)]

    palette = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
               (255, 0, 255), (255, 0, 0), (0, 0, 255), (32, 32, 32)]
    row = b"".join(bytes((b, g, r, 255)) * (FB_WIDTH // len(palette))
                   for r, g, b in palette)
    iface.writemem(pa, row * FB_HEIGHT)
    p.dc_cvac(pa, size)

    layer = Container(
        planes=[Container(addr=dva, stride=FB_STRIDE, addr_format=1), Container(), Container()],
        plane_cnt=1, width=FB_WIDTH, height=FB_HEIGHT, surface_fmt=1,
        colorspace=2, eotf=1, transform=0,
    )
    rect = struct.pack("<8I", FB_WIDTH, FB_HEIGHT, 0, 0,
                       FB_WIDTH, FB_HEIGHT, 0, 0)
    set_layer = bytes(8) + IBootLayerInfo.build(layer) + bytes(8) + rect + bytes(4)
    assert len(set_layer) == 216

    def commit_surface(label):
        service.setPower(True)
        service.send_cmd(6, timing + color)
        swap = service.send_cmd(15, replen=128)
        assert len(swap) == 20
        swap_id = struct.unpack_from("<I", swap, 12)[0]
        service.send_cmd(16, set_layer, replen=128)
        service.send_cmd(18, bytes(12), replen=128)
        print(f"[FACT] {label} mode/swap ACK swap_id={swap_id}")
        return swap_id

    first_swap_id = commit_surface("initial")

    # Tahoe performs a delayed HDMI hotplug cycle after the first modeset.
    # Wait for the observed True -> False -> True transition instead of using
    # a guessed delay, then program the newly published interface once.
    settle_end = time.monotonic() + 8.0
    saw_hpd_down = False
    hpd_samples = []
    while time.monotonic() < settle_end:
        hpd2, nt2, nc2 = service.getModeCount()
        sample = (hpd2, nt2, nc2)
        if not hpd_samples or hpd_samples[-1] != sample:
            hpd_samples.append(sample)
            print("[FACT] HDMI settle HPD/counts", sample)
        if not hpd2:
            saw_hpd_down = True
        elif saw_hpd_down:
            break
        time.sleep(.05)
    assert hpd2 and nt2 and nc2, hpd_samples
    if saw_hpd_down:
        print("[FACT] observed HDMI True/False/True cycle", hpd_samples)
        settle_mode = "hotplug-cycle"
        second_label = "post-hotplug reapply"
    else:
        # A powered-off LG sink can keep HPD/EDID asserted without generating
        # Tahoe's usual delayed cycle.  Serial-only overnight tests must not
        # abort solely because the panel is dark.  Recommit the still-valid
        # published interface after the full settle window and keep all DCP
        # rings serviced exactly as in the physical-display path.
        print("[FACT] HDMI HPD remained stable; using headless-stable reapply", hpd_samples)
        settle_mode = "stable-hpd"
        second_label = "stable-HPD reapply"
    second_swap_id = commit_surface(second_label)

    state = {
        "boot_base": base,
        "shared": ep.iobuffer,
        "syslog_base": dcp.syslog.iobuffer,
        "asc_base": asc,
        "pointers": link.pointers(),
        "first_swap_id": first_swap_id,
        "second_swap_id": second_swap_id,
        "settle_mode": settle_mode,
        "timing": struct.unpack("<6I", timing),
    }
    prefix.with_suffix(prefix.suffix + ".state.json").write_text(json.dumps(state, indent=2))
    print("[FACT] pre-guest settled mode/swap ACK; entering guest without DCP shutdown", state)

    # Give the physical HDMI sink a bounded interval to show the test pattern.
    # This also separates a failed DCP scanout from damage during guest entry.
    print("[TEST] servicing DCP and holding color bars for 5 seconds before guest entry")
    hold_end = time.monotonic() + 5.0
    while time.monotonic() < hold_end:
        assert not link.work(), "Unexpected command response during color-bar hold"
        time.sleep(.01)

    # Retain every object backing active DCP rings through hv.start().
    hv._nwoas_dcp_handoff = (dcp, link, service, dart)


hv._nwoas_dcp_handoff = (dcp, link, service, dart)
hv.preserve_framebuffer = True
hv.pre_guest_start = configure_tahoe_display
print("[DESIGN] Tahoe DCP started early; pre-guest mode/swap hook armed; no DCP shutdown")
