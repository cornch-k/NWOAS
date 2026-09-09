"""S122: one physical flush on identified Mac mini, no block writes or repartitioning."""
import signal,subprocess,time
from m1n1.proxy import UartInterface,M1N1Proxy
from m1n1.proxyutils import ProxyUtils,bootstrap_port
port='/dev/cu.usbmodemC07HL05SQ6NY1'
r=subprocess.run(['lsof','-t',port],capture_output=True)
assert r.returncode==1 and not r.stdout and not r.stderr
signal.signal(signal.SIGALRM,lambda *_:(_ for _ in ()).throw(TimeoutError('flush preflight deadline')))
signal.alarm(30)
i=UartInterface();p=M1N1Proxy(i,debug=False);bootstrap_port(i,p);u=ProxyUtils(p)
assert 'j274' in str(u.adt.compatible).lower()
try:
 assert p.nvme_init()
 start=time.monotonic();ok=p.nvme_flush(1)
 print('S122 PHYSICAL FLUSH result=',ok,'host_seconds=',time.monotonic()-start,flush=True)
 assert ok,'physical flush failed'
finally:
 try:p.nvme_shutdown()
 finally:signal.alarm(0);i.dev.close()
