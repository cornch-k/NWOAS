"""S260: host-side mirror of m1n1-gpu-hv2/src/nwoas_pwr.h (struct nwoas_pwr_snapshot).
Pure Python, no proxy access.  test_power_s260.py checks it against the C header with clang."""
import struct

P_HV_NWOAS_PWR = 0xc3a
MAGIC, VERSION = 0x5057524e, 1
OP_QUERY, OP_CONFIG, OP_SNAPSHOT, OP_SMC_READ, OP_SLC, OP_MCC_STAT = range(6)

F_WFI_SLEEP, F_SEC_TICK, F_TIMER_GAP, F_NVME_SHUT, F_SMC_OFF, F_SMC_SENSE, F_PRINT = (1 << i for i in range(7))
F_ALL = 0x7f
LIVE_MASK = F_WFI_SLEEP | F_SEC_TICK | F_TIMER_GAP     # bits a guest job may toggle (NWOAS_PWR_LIVE=1)

NCPU = 8
HEADER = ['magic', 'version', 'size', 'ncpu', 'cntpct', 'cntfrq', 'flags', 'sec_tick_interval',
          'timer_gap_ticks', 'hv_tick_interval', 'hv_ticks', 'prints', 'smc_reads', 'smc_errors',
          'proxy_ticks', 'reserved1', 'reserved2', 'reserved3']
HEADER_FMT = '<IIIIQQQQQQQQQQQQQQ'
CPU = ['wfi_traps', 'wfe_traps', 'wfi_sleeps', 'wfi_sleep_ticks', 'wfi_skip_pending', 'wfi_skip_route',
       'fiq', 'fiq_tick', 'fiq_fast', 'irq', 'sync', 'timer_inj', 'timer_gated', 'timer_dedup',
       'tick_shortened', 'wake_armed', 'sync_dabort', 'sync_msr', 'sync_smc', 'sync_hvc', 'bhl_wait_ticks',
       'slow_entries', 'ipi_sent', 'proxy_calls', 'ipi_rx', 'wake_ipi', 'wake_tick', 'wake_vtimer', 'wake_irq',
       'wake_other', 'nvme_kicks', 'wfi_skip_nvme']
NVME = ['ns1_cmds', 'ns2_cmds', 'other_cmds', 'doorbell_cmds', 'tick_cmds', 'lat_count', 'lat_sum_ticks',
        'lat_max_ticks'] + ['lat_hist%d' % i for i in range(6)] + \
       ['exec_sum_ticks', 'depth_sum', 'depth_samples', 'depth_max', 'read_blocks', 'write_blocks']
HIST_EDGES_US = ['<20', '<100', '<500', '<2000', '<10000', '>=10000']
SIZE = struct.calcsize(HEADER_FMT) + NCPU * len(CPU) * 8 + len(NVME) * 8 + 4 * 8
assert struct.calcsize(HEADER_FMT) == 128 and SIZE == 2368, SIZE

# Linux arch/arm64/boot/dts/apple/hwmon-common.dtsi, hwmon-fan.dtsi, hwmon-mini.dtsi (key names only)
DEFAULT_SMC_KEYS = ['PSTR', 'PDTR', 'PMVR', 'TH0x', 'TW0P', 'F0Ac']
SMC_LABELS = {'PSTR': 'total system power W', 'PDTR': 'AC input power W', 'PMVR': '3.8 V rail power W',
              'TH0x': 'NAND temp C', 'TW0P': 'WiFi/BT module temp C', 'F0Ac': 'fan rpm'}


def smc_key(name):
    b = name.encode('ascii')
    assert len(b) == 4, name
    return int.from_bytes(b, 'big')


def parse(blob):
    if len(blob) < SIZE:
        raise ValueError('short snapshot %d' % len(blob))
    h = dict(zip(HEADER, struct.unpack_from(HEADER_FMT, blob, 0)))
    if h['magic'] != MAGIC or h['version'] != VERSION or h['size'] != SIZE or h['ncpu'] != NCPU:
        raise ValueError('bad snapshot header %r' % {k: h[k] for k in ('magic', 'version', 'size', 'ncpu')})
    off = 128
    cpus = []
    for _ in range(NCPU):
        cpus.append(dict(zip(CPU, struct.unpack_from('<%dQ' % len(CPU), blob, off))))
        off += 8 * len(CPU)
    nvme = dict(zip(NVME, struct.unpack_from('<%dQ' % len(NVME), blob, off)))
    return {'header': h, 'cpu': cpus, 'nvme': nvme}


def decode_smc(name, word):
    """word = value | status << 32 (HV OP_SMC_READ).  Keys are 4-byte little-endian 'flt '."""
    status, value = word >> 32, word & 0xffffffff
    if status:
        return {'key': name, 'ok': False, 'status': status}
    return {'key': name, 'ok': True, 'raw': '%08x' % value,
            'float': round(struct.unpack('<f', value.to_bytes(4, 'little'))[0], 3)}


def delta(a, b):
    """b - a for two parsed snapshots (b later)."""
    fr = b['header']['cntfrq'] or 24000000
    dt = (b['header']['cntpct'] - a['header']['cntpct']) / fr
    cpus = []
    for x, y in zip(a['cpu'], b['cpu']):
        d = {k: y.get(k, 0) - x.get(k, 0) for k in CPU}   # older records lack the newer fields
        d['sleep_pct'] = round(100.0 * d['wfi_sleep_ticks'] / fr / dt, 1) if dt > 0 else 0.0
        d['bhl_wait_pct'] = round(100.0 * d['bhl_wait_ticks'] / fr / dt, 2) if dt > 0 else 0.0
        cpus.append(d)
    n = {k: b['nvme'][k] - a['nvme'][k] for k in NVME}
    n['depth_max'] = b['nvme']['depth_max']                      # monotonic max, not a delta
    n['lat_max_us'] = round(b['nvme']['lat_max_ticks'] * 1e6 / fr, 1)
    n['lat_mean_us'] = round(n['lat_sum_ticks'] * 1e6 / fr / n['lat_count'], 1) if n['lat_count'] else 0.0
    n['exec_mean_us'] = round(n['exec_sum_ticks'] * 1e6 / fr / n['ns1_cmds'], 1) if n['ns1_cmds'] else 0.0
    n['depth_mean'] = round(n['depth_sum'] / n['depth_samples'], 2) if n['depth_samples'] else 0.0
    n['read_MBps'] = round(n['read_blocks'] * 4096 / 1e6 / dt, 1) if dt > 0 else 0.0
    n['write_MBps'] = round(n['write_blocks'] * 4096 / 1e6 / dt, 1) if dt > 0 else 0.0
    proxy_s = (b['header']['proxy_ticks'] - a['header']['proxy_ticks']) / fr
    return {'seconds': round(dt, 3), 'hv_ticks': b['header']['hv_ticks'] - a['header']['hv_ticks'],
            'proxy_pct': round(100.0 * proxy_s / dt, 2) if dt > 0 else 0.0,
            'cpu': cpus, 'nvme': n}
