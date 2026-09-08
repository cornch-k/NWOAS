"""Bounded, read-only UART readiness check before a chainload starts."""
import signal
from m1n1.proxy import UartInterface, M1N1Proxy

def timeout(signum, frame):
    raise TimeoutError('proxy did not answer within 10 seconds')

signal.signal(signal.SIGALRM, timeout)
signal.alarm(10)
interface = None
try:
    interface = UartInterface()
    M1N1Proxy(interface, debug=False).nop()
    print('Proxy readiness PASS')
except Exception as error:
    print('STOP: proxy readiness failed:', type(error).__name__, str(error))
    raise SystemExit(2)
finally:
    signal.alarm(0)
    if interface is not None:
        interface.dev.close()
