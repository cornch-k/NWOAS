"""Recommit the current framebuffer after HPD loss; run only in live HV shell.

Preserves framebuffer pixels, boot geometry, DART mappings and Windows state.
Uses the DCP objects already owned by tahoe_dcp_guest_hook, never reinitializes ASC.
"""
import struct
import time
from construct import Container
from m1n1.fw.dcp.iboot import IBootLayerInfo
from m1n1.hw.dart import DART

dcp, link, service, dart = hv._nwoas_dcp_handoff
g = hv.pre_guest_start.__globals__
w, h, stride = g['FB_WIDTH'], g['FB_HEIGHT'], g['FB_STRIDE']
assert (hv.tba.video.width, hv.tba.video.height, hv.tba.video.stride) == (w, h, stride)
hpd, nt, nc = service.getModeCount()
print('[S158 display] HPD/modes', hpd, nt, nc)
assert hpd and nt and nc, 'No connected display; leave framebuffer untouched'
timing, selected = g['choose_timing'](service.send_cmd(4, replen=4096), nt,
                                      u.adt['/vram'].reg[0].size, (w, h))
assert selected[1:3] == (w, h), 'Reconnection changed geometry; no live resize'
colors = service.send_cmd(5, replen=4096)
assert len(colors) == 4 + nc * 24
color = next(colors[4+j*24:28+j*24] for j in range(nc)
             if struct.unpack_from('<IIIII', colors, 4+j*24) == (1,1,1,1,32))
pa, dva, size = u.ba.video.base, 0x13DC000, stride*h
assert pa == u.adt['/vram'].reg[0].addr
for path in ('/arm-io/dart-disp0', '/arm-io/dart-dcp'):
    assert DART.from_adt(u, path).iotranslate(0, dva, size) == [(pa, size)]
layer = Container(planes=[Container(addr=dva, stride=stride, addr_format=1),
                           Container(), Container()], plane_cnt=1, width=w, height=h,
                  surface_fmt=1, colorspace=2, eotf=1, transform=0)
body = bytes(8) + IBootLayerInfo.build(layer) + bytes(8) + struct.pack('<8I',w,h,0,0,w,h,0,0) + bytes(4)
assert len(body) == 216

def recommit():
    service.setPower(True)
    service.send_cmd(6, timing+color)
    swap = service.send_cmd(15, replen=128)
    assert len(swap) == 20
    service.send_cmd(16, body, replen=128)
    service.send_cmd(18, bytes(12), replen=128)
    print('[S158 display] existing framebuffer recommitted', w, h,
          'swap', struct.unpack_from('<I',swap,12)[0])

recommit()
end = time.monotonic() + 5
saw_down = False
while time.monotonic() < end:
    hpd, nt, nc = service.getModeCount()
    if not hpd:
        saw_down = True
    elif saw_down:
        recommit()
        break
    time.sleep(.1)
print('[S158 display] final HPD/modes', hpd, nt, nc)
assert hpd, 'Sink disconnected again after modeset'
