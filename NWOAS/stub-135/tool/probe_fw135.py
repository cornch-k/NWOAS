"""Read-only check, run on the MacBook after the Mac mini booted the stub (m1n1 D4 waiting in proxy mode).

Prints the ADT /chosen OS-firmware and system-firmware versions and the m1n1 base. Writes nothing to the
Mac mini (no memory writes beyond what ProxyUtils needs to read the ADT, no NVMe, no chainload).
Gate G-1 PASS needs: OS firmware 'iBoot-8422.141.2' (macOS 13.5) while system firmware stays 'mBoot-18000...'.

  cd /Users/angigyeom/Desktop/DEV/NWOAS/m1n1_windows
  M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 M1N1_KEEP_BAUD=1 PYTHONPATH=proxyclient \
    .venv-hv/bin/python3 -B /Users/angigyeom/Desktop/DEV/NWOAS/stub-135/tool/probe_fw135.py
"""
import json, signal, sys
from m1n1.proxy import UartInterface, M1N1Proxy
from m1n1.proxyutils import ProxyUtils, bootstrap_port


def deadline(*_):
    raise TimeoutError('no proxy answer within 30 s')


signal.signal(signal.SIGALRM, deadline)
signal.alarm(30)
iface = UartInterface()
try:
    p = M1N1Proxy(iface, debug=False)
    bootstrap_port(iface, p)
    u = ProxyUtils(p)
    ch = u.adt['/chosen']
    os_fw = bytes(ch.firmware_version).split(b'\0')[0].decode()
    sys_fw = bytes(ch.system_firmware_version).split(b'\0')[0].decode()
    out = dict(model=str(u.adt.compatible), os_firmware=os_fw, system_firmware=sys_fw, m1n1_base=hex(p.get_base()))
    print(json.dumps(out, indent=1))
    ok = os_fw == 'iBoot-8422.141.2' and sys_fw.startswith('mBoot-18000')
    print('G-1 PASS: 13.5 OS firmware on Tahoe system firmware' if ok else 'G-1 NOT MET', flush=True)
    sys.exit(0 if ok else 1)
finally:
    signal.alarm(0)
    iface.dev.close()
