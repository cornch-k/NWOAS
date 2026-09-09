"""S178 bounded read-only bus transactions. No writes to a PMU register.
Only controller CMD FIFO is written, with EXT_READL and exact RTC addresses.
Response status format is undocumented here; retain raw status in evidence.
"""
import struct,time
BASE=0x23d0d9300
RX_EMPTY=1<<24

def read_rtc_register(read32,write32,reg, *, clock=time.monotonic, budget=0.1):
    if reg not in (0xd002,0xd100):raise ValueError('only RTC counter and offset reads')
    if not 0 < budget <= 0.1:raise ValueError('bounded budget required')
    status=read32(BASE)
    if not status & RX_EMPTY or status & 0xff:
        raise RuntimeError('SPMI FIFO not idle; preserve queued transaction')
    command=(reg<<16)|(1<<15)|(0xf<<8)|0x38|5
    write32(BASE+4,command)
    words=[];deadline=clock()+budget
    for _ in range(3):
        while read32(BASE)&RX_EMPTY:
            if clock()>=deadline:raise TimeoutError('SPMI reply deadline')
        words.append(read32(BASE+8))
        if clock()>deadline:raise TimeoutError('SPMI transaction exceeded budget')
    if not read32(BASE)&RX_EMPTY:raise RuntimeError('Unexpected extra SPMI reply; left untouched')
    return struct.pack('<II',*words[1:])[:6], words[0]
