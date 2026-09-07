#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# NWOAS: run_guest variant that dumps the GOP framebuffer when interrupted (SIGINT). The guest
# (Windows bootmgfw) fails at the winload handoff, draws an error/recovery screen to the GOP
# (invisible on the headless serial boot), then wedges. Send SIGINT once it's wedged: hv.start()
# is blocked on a serial read, SIGINT raises KeyboardInterrupt, and we then read FB @0xE3F60000
# (640x1136 BGRA) over the still-live proxy so we can render + read bootmgfw's failure code.
import sys, pathlib, traceback
sys.path.append(str(pathlib.Path(__file__).resolve().parents[1]))

import argparse, pathlib

def volumespec(s):
    return tuple(s.split(":", 2))

parser = argparse.ArgumentParser(description='Run a payload under the hypervisor + dump FB on SIGINT')
parser.add_argument('-m', '--script', type=pathlib.Path, action='append', default=[])
parser.add_argument('-r', '--raw', action="store_true")
parser.add_argument('-E', '--entry-point', action="store", type=int, default=0x800)
parser.add_argument('--fbout', type=str, default='/private/tmp/claude-501/-Volumes-X31-NWOAS/08e1ec24-7e98-4f1e-91a5-4c4a8348b6ab/scratchpad/fb_dump.bin')
parser.add_argument('payload', type=pathlib.Path)
parser.add_argument('boot_args', default=[], nargs="*")
args = parser.parse_args()

from m1n1.proxy import *
from m1n1.proxyutils import *
from m1n1.utils import *
from m1n1.hv import HV

iface = UartInterface()
p = M1N1Proxy(iface, debug=False)
bootstrap_port(iface, p)
u = ProxyUtils(p, heap_size = 768 * 1024 * 1024)

hv = HV(iface, p, u)
hv.init()

payload = args.payload.open("rb")
if args.raw:
    hv.load_raw(payload.read(), args.entry_point)
else:
    hv.load_macho(payload)

for i in args.script:
    try:
        hv.run_script(i)
    except:
        traceback.print_exc()

FB_IPA  = 0xE3F60000
FB_SIZE = 0x2C6000

def dump_fb(tag):
    try:
        try:
            pa = p.hv_translate(FB_IPA, False, False)
        except Exception:
            pa = 0
        src = pa if pa else FB_IPA
        data = iface.readmem(src, FB_SIZE)
        nz = sum(1 for b in data if b != 0)
        open(args.fbout, "wb").write(data)
        print(f"[fbdump] {tag}: FB IPA {FB_IPA:#x}->PA {src:#x}, wrote {len(data)} bytes, nonzero={nz}")
    except Exception as e:
        print(f"[fbdump] {tag} FAILED: {e}")
        traceback.print_exc()

print("[fbdump] starting guest; SIGINT once wedged to capture the GOP error screen")
try:
    hv.start()
    dump_fb("post-start")
except KeyboardInterrupt:
    print("[fbdump] SIGINT received - guest wedged, dumping framebuffer")
    dump_fb("sigint")

print("[fbdump] done")
