"""NWOAS 4-1 read-only probe: is the m1n1 now answering on the proxy port the given image?
Reads len(marker) bytes of the running m1n1 at proxy base + (offset of the marker in the local image file).
Used by run-stage1-rehearsal.sh negative variants: "the proxy that came back is stage 1's own USB fallback
proxy" (marker NWOAS-STAGE1: USB proxy fallback), not D4 and not a rebooted Mac mini.
  M1N1DEVICE=/dev/cu.usbmodemC07HL05SQ6NY1 PYTHONPATH=<m1n1_windows>/proxyclient \
    python3 probe_running_image.py <image file> <marker text>
Exit: 0 match | 1 no match | 2 proxy error.  No memory writes, no chainload.
"""
import signal
import sys
from pathlib import Path


def main():
    image, marker = Path(sys.argv[1]).read_bytes(), sys.argv[2].encode()
    off = image.find(marker)
    if off < 0:
        print(f"STOP: marker not in {sys.argv[1]}")
        return 2
    from m1n1.proxy import UartInterface, M1N1Proxy
    from m1n1.proxyutils import bootstrap_port

    def deadline(*_):
        raise TimeoutError("no proxy answer within 30 s")

    signal.signal(signal.SIGALRM, deadline)
    signal.alarm(30)
    try:
        iface = UartInterface()
        try:
            p = M1N1Proxy(iface, debug=False)
            bootstrap_port(iface, p)
            base = p.get_base()
            got = iface.readmem(base + off, len(marker))
        finally:
            iface.dev.close()
    except Exception as error:
        print("[running-image] STOP: proxy error:", type(error).__name__, error, flush=True)
        return 2
    finally:
        signal.alarm(0)
    ok = got == marker
    print(f"[running-image] base {base:#x} offset {off:#x}: {'MATCH' if ok else 'NO MATCH'} {sys.argv[1]} "
          f"({marker.decode()!r})", flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
