#!/usr/bin/env python3
"""S231: pre-create the Windows ESP / MSR / data partitions inside WINTEST and hide NWSTUB from Windows.

Why: the S231 guest chain refuses every guest GPT write (writable_namespace / hv_vm fast path), so
Windows Setup or diskpart cannot create the ESP and MSR. The table is therefore written once from the
host, through the m1n1 host NVMe path whose C guard (nvme_write_allowed) only admits LBA 0-5 and
61279339-61279343 outside the Windows zone.

Modes
  plan  <capture_dir> <out_dir>   offline. Builds the new table from a read-only capture
                                  (ans2-s87/run-*/lba-{1..5,61279343}.bin), validates it, writes the
                                  planned blocks and plan.json. No device access.
  restore <before-apply dir>      online. Writes the saved original blocks back (same order), verifies.
  apply <plan_dir> <plan_sha256>  online, mini idle in an m1n1 proxy with no guest running. Re-reads
                                  every GPT block, refuses unless the disk still equals the capture the
                                  plan was built from, saves the old blocks, writes backup set then
                                  primary set, reads everything back and compares byte for byte.

Fixed facts (2026-11-15 capture run-20261115-174016): 4096-byte blocks, 61279344 blocks,
entries at LBA 2-5 (128 x 128 B), backup entries 61279339-61279342, backup header 61279343.
"""
import hashlib, json, os, struct, sys, uuid, zlib
from pathlib import Path

BLOCK = 4096
DISK_BLOCKS = 61279344
DISK_GUID = uuid.UUID('898f172d-77bf-4007-b865-ac81a141eb3c')
PRIMARY = [1, 2, 3, 4, 5]
BACKUP_ENTRIES = [61279339, 61279340, 61279341, 61279342]
BACKUP_HEADER = 61279343
ZONE = (39944960, 59968629)          # S231 Windows write zone (C guard + Python guard)

T_BASIC = uuid.UUID('ebd0a0a2-b9e5-4433-87c0-68b6b72699c7')
T_ESP = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b')
T_MSR = uuid.UUID('e3c9e316-0b5c-4db8-817d-f92df00215ae')

# Current entries that must be found exactly (unique GUID -> (type, start, end)).
ISC = uuid.UUID('56f38f44-5c55-48da-aff0-97af9bb0a578')
APFS = uuid.UUID('c624fd70-53bd-4c27-adc7-1ac2f9288cbc')
NWSTUB = uuid.UUID('8d17c4ed-0de2-4bf0-884f-5477b004da66')
WINTEST = uuid.UUID('e1d36b9e-d05c-4030-aec5-26d402086781')
RECOVERY = uuid.UUID('59130d35-2432-4fcb-8206-7efdf9903d4d')
EXPECT = {ISC: (6, 128005), APFS: (128006, 38968337), NWSTUB: (38968576, 39944703),
          WINTEST: (39944960, 59968511), RECOVERY: (59968630, 61279338)}

# New partitions (1 MiB = 256 blocks alignment).
ESP_FIRST, ESP_BLOCKS = 39944960, 131072            # 512 MiB FAT32 (4 KiB sectors need >= 256 MiB)
MSR_FIRST, MSR_BLOCKS = ESP_FIRST + ESP_BLOCKS, 4096  # 16 MiB
WIN_FIRST = MSR_FIRST + MSR_BLOCKS
WIN_LAST = 59968511                                 # unchanged WINTEST end
NWSTUB_ATTRS = (1 << 63) | (1 << 62) | (1 << 60)    # no drive letter, hidden, read-only (Windows)

# Deterministic new unique GUIDs so a re-plan reproduces identical bytes.
def _guid(tag):
    return uuid.UUID(bytes=hashlib.sha256(b'NWOAS-S231-' + tag.encode()).digest()[:16], version=4)
G_ESP, G_MSR, G_WIN = _guid('ESP'), _guid('MSR'), _guid('WINDOWS')


def parse_header(b, expect_cur):
    assert b[:8] == b'EFI PART', 'GPT signature'
    size, crc = struct.unpack_from('<II', b, 12)
    assert size == 92, f'header size {size}'
    raw = bytearray(b[:size]); raw[16:20] = b'\0' * 4
    assert zlib.crc32(raw) == crc, 'header CRC'
    cur, alt, first, last = struct.unpack_from('<QQQQ', b, 24)
    ent, cnt, esz, ecrc = struct.unpack_from('<QIII', b, 72)
    assert cur == expect_cur, f'header current_lba {cur} != {expect_cur}'
    assert uuid.UUID(bytes_le=b[56:72]) == DISK_GUID, 'disk GUID'
    assert (cnt, esz) == (128, 128), 'entry geometry'
    assert b[size:] == bytes(BLOCK - size), 'header reserved tail not zero'
    return dict(cur=cur, alt=alt, first=first, last=last, ent=ent, cnt=cnt, esz=esz, ecrc=ecrc)


def entries_list(arr):
    out = []
    for i in range(128):
        e = arr[i * 128:(i + 1) * 128]
        if e[:16] == bytes(16):
            assert e == bytes(128), f'slot {i+1} empty type but non-zero bytes'
            continue
        t, g = uuid.UUID(bytes_le=e[:16]), uuid.UUID(bytes_le=e[16:32])
        s, en, attrs = struct.unpack_from('<QQQ', e, 32)
        out.append(dict(slot=i + 1, type=t, guid=g, start=s, end=en, attrs=attrs,
                        name=e[56:128].decode('utf-16le').rstrip('\0'), raw=e))
    return out


def make_entry(t, g, s, en, attrs, name):
    nm = name.encode('utf-16le'); assert len(nm) <= 72
    return t.bytes_le + g.bytes_le + struct.pack('<QQQ', s, en, attrs) + nm + bytes(72 - len(nm))


def build_header(old, entries_crc):
    b = bytearray(old[:92])
    struct.pack_into('<I', b, 88, entries_crc)
    struct.pack_into('<I', b, 16, 0)
    struct.pack_into('<I', b, 16, zlib.crc32(bytes(b)))
    return bytes(b) + bytes(BLOCK - 92)


def validate_table(arr, first_usable, last_usable):
    """Independent invariant check of a complete 16 KiB entry array."""
    ents = entries_list(arr)
    by = {e['guid']: e for e in ents}
    assert len(by) == len(ents), 'duplicate unique GUID'
    order = sorted(ents, key=lambda e: e['start'])
    assert [e['slot'] for e in order] == list(range(1, len(ents) + 1)), 'slots not contiguous in disk order'
    for a, b in zip(order, order[1:]):
        assert a['end'] < b['start'], 'overlap'
    for e in ents:
        assert first_usable <= e['start'] <= e['end'] <= last_usable, 'outside usable range'
    for g in (ISC, APFS, RECOVERY):
        assert (by[g]['start'], by[g]['end']) == EXPECT[g], f'Apple entry {g} moved'
    assert (by[NWSTUB]['start'], by[NWSTUB]['end']) == EXPECT[NWSTUB]
    assert by[NWSTUB]['type'] == T_BASIC and by[NWSTUB]['attrs'] == NWSTUB_ATTRS
    win = [e for e in ents if e['guid'] not in (ISC, APFS, NWSTUB, RECOVERY)]
    assert {e['type'] for e in win} == {T_ESP, T_MSR, T_BASIC} and len(win) == 3
    for e in win:
        assert ZONE[0] <= e['start'] <= e['end'] <= ZONE[1], 'Windows partition outside zone'
        assert e['start'] % 256 == 0, 'not 1 MiB aligned'
    return ents


def plan(capture, out):
    capture, out = Path(capture), Path(out)
    blk = {l: (capture / f'lba-{l}.bin').read_bytes() for l in PRIMARY + [BACKUP_HEADER]}
    assert all(len(v) == BLOCK for v in blk.values())
    ph = parse_header(blk[1], 1)
    bh = parse_header(blk[BACKUP_HEADER], BACKUP_HEADER)
    assert (ph['alt'], ph['ent'], ph['first'], ph['last']) == (BACKUP_HEADER, 2, 6, 61279338)
    assert (bh['alt'], bh['ent'], bh['first'], bh['last'], bh['ecrc']) == (1, BACKUP_ENTRIES[0], 6, 61279338, ph['ecrc'])
    arr = b''.join(blk[l] for l in (2, 3, 4, 5))
    assert zlib.crc32(arr) == ph['ecrc'], 'entries CRC'
    cur = entries_list(arr)
    got = {e['guid']: (e['start'], e['end']) for e in cur}
    assert got == EXPECT, f'disk layout differs from expectation: {got}'
    by = {e['guid']: e for e in cur}
    nw = by[NWSTUB]
    new = [by[ISC]['raw'], by[APFS]['raw'],
           make_entry(nw['type'], NWSTUB, nw['start'], nw['end'], NWSTUB_ATTRS, nw['name']),
           make_entry(T_ESP, G_ESP, ESP_FIRST, ESP_FIRST + ESP_BLOCKS - 1, 0, 'EFI system partition'),
           make_entry(T_MSR, G_MSR, MSR_FIRST, MSR_FIRST + MSR_BLOCKS - 1, 0, 'Microsoft reserved partition'),
           make_entry(T_BASIC, G_WIN, WIN_FIRST, WIN_LAST, 0, 'Basic data partition'),
           by[RECOVERY]['raw']]
    new_arr = b''.join(new) + bytes(128 * 128 - 128 * len(new))
    ents = validate_table(new_arr, 6, 61279338)
    ecrc = zlib.crc32(new_arr)
    blocks = {1: build_header(blk[1], ecrc), BACKUP_HEADER: build_header(blk[BACKUP_HEADER], ecrc)}
    for j, l in enumerate((2, 3, 4, 5)):
        blocks[l] = new_arr[j * BLOCK:(j + 1) * BLOCK]
    for j, l in enumerate(BACKUP_ENTRIES):
        blocks[l] = new_arr[j * BLOCK:(j + 1) * BLOCK]
    # Re-parse the built headers with the strict parser.
    h1 = parse_header(blocks[1], 1); h2 = parse_header(blocks[BACKUP_HEADER], BACKUP_HEADER)
    assert h1['ecrc'] == h2['ecrc'] == ecrc and h1['alt'] == BACKUP_HEADER and h2['ent'] == BACKUP_ENTRIES[0]
    # Only header CRC/entries-CRC fields may differ from the old headers.
    for l in (1, BACKUP_HEADER):
        diff = [i for i in range(BLOCK) if blocks[l][i] != blk[l][i]]
        assert all(16 <= i < 20 or 88 <= i < 92 for i in diff), f'header {l} changed outside CRC fields'
    out.mkdir(parents=True, exist_ok=False)
    for l, b in blocks.items():
        (out / f'new-lba-{l}.bin').write_bytes(b)
    src = {l: hashlib.sha256(blk[l]).hexdigest() for l in blk}
    meta = dict(stage='S231-GPT', capture=str(capture), capture_sha256=src,
                new_sha256={l: hashlib.sha256(b).hexdigest() for l, b in sorted(blocks.items())},
                partitions=[dict(slot=e['slot'], type=str(e['type']), guid=str(e['guid']), start=e['start'],
                                 end=e['end'], gb=round((e['end'] - e['start'] + 1) * BLOCK / 1e9, 3),
                                 attrs=hex(e['attrs']), name=e['name']) for e in ents],
                new_entry_bytes=[e['raw'].hex() for e in ents])
    (out / 'plan.json').write_text(json.dumps(meta, indent=2) + '\n')
    digest = hashlib.sha256((out / 'plan.json').read_bytes()).hexdigest()
    print(json.dumps(meta['partitions'], indent=1))
    print('PLAN OK', out, 'plan.json sha256', digest)


ORDER = BACKUP_ENTRIES + [BACKUP_HEADER] + [2, 3, 4, 5] + [1]   # backup set first, primary header last


def _connect():
    import signal
    from m1n1.proxy import UartInterface, M1N1Proxy
    from m1n1.proxyutils import ProxyUtils, bootstrap_port
    def deadline(*_): raise TimeoutError('S231 GPT deadline; do not retry automatically')
    signal.signal(signal.SIGALRM, deadline); signal.alarm(60)
    i = UartInterface(); p = M1N1Proxy(i, debug=False); bootstrap_port(i, p); u = ProxyUtils(p)
    assert 'j274' in str(u.adt.compatible).lower(), 'wrong target'
    assert p.nvme_init(), 'NVMe init failed'
    buf = u.memalign(0x4000, 0x10000)
    def rd(l):
        signal.alarm(15); assert p.nvme_read(1, l, buf), f'read {l}'; return i.readmem(buf, BLOCK)
    def wr(l, b):
        assert len(b) == BLOCK and l in ORDER, f'refusing write to LBA {l}'
        signal.alarm(15); i.writemem(buf, b)
        assert p.nvme_write_n(1, l, buf, 1), f'write {l} FAILED'
    def close():
        signal.alarm(30); p.nvme_shutdown(); signal.alarm(0); i.dev.close()
    return p, rd, wr, close


def _write_set(p, rd, wr, target, current, label, saved_dir):
    """Write only the blocks that differ, in ORDER; flush; read back. On any failure report progress."""
    todo = [l for l in ORDER if current[l] != target[l]]
    done = []
    try:
        for l in todo:
            wr(l, target[l]); done.append(l); print(f'{label}: wrote LBA {l}', flush=True)
    finally:
        flushed = bool(p.nvme_flush(1))
        if len(done) != len(todo):
            print(f'{label} INTERRUPTED after {done} of {todo}; flush={flushed}; '
                  f'original blocks saved in {saved_dir}; use restore mode', flush=True)
    assert flushed, 'flush failed'
    back = {l: rd(l) for l in ORDER}
    bad = [l for l in ORDER if back[l] != target[l]]
    assert not bad, f'READBACK MISMATCH {bad}; original blocks saved in {saved_dir}'
    return todo


def apply(plan_dir, plan_sha):
    import datetime
    plan_dir = Path(plan_dir)
    pj = (plan_dir / 'plan.json').read_bytes()
    assert hashlib.sha256(pj).hexdigest() == plan_sha, 'plan.json hash mismatch'
    meta = json.loads(pj)
    new = {int(l): (plan_dir / f'new-lba-{l}.bin').read_bytes() for l in meta['new_sha256']}
    for l, b in new.items():
        assert hashlib.sha256(b).hexdigest() == meta['new_sha256'][str(l)]
    assert sorted(new) == sorted(ORDER)
    p, rd, wr, close = _connect()
    old = {l: rd(l) for l in [0] + ORDER}
    if all(old[l] == new[l] for l in ORDER):
        close(); print('ALREADY APPLIED; nothing written'); return
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    bk = plan_dir / f'before-apply-{stamp}'; bk.mkdir()
    for l, b in old.items():
        (bk / f'lba-{l}.bin').write_bytes(b)
    (bk / 'sha256.json').write_text(json.dumps({l: hashlib.sha256(b).hexdigest() for l, b in old.items()}, indent=1))
    print('original blocks saved to', bk, flush=True)
    # Disk must still be exactly what the plan was built from.
    for l, h in meta['capture_sha256'].items():
        assert hashlib.sha256(old[int(l)]).hexdigest() == h, f'LBA {l} changed since capture; STOP'
    arr = b''.join(old[l] for l in (2, 3, 4, 5))
    assert b''.join(old[l] for l in BACKUP_ENTRIES) == arr, 'backup entries differ from primary; STOP'
    written = _write_set(p, rd, wr, new, old, 'APPLY', bk)
    assert rd(0) == old[0], 'MBR changed?'
    close()
    print(f'APPLY PASS: wrote {written}; all 10 GPT blocks read back identical to plan; controller shut down')


def restore(saved_dir):
    """Write back the blocks saved by apply (same ORDER, only differing blocks), then verify."""
    saved_dir = Path(saved_dir)
    sums = json.loads((saved_dir / 'sha256.json').read_text())
    orig = {}
    for l in ORDER:
        b = (saved_dir / f'lba-{l}.bin').read_bytes()
        assert hashlib.sha256(b).hexdigest() == sums[str(l)], f'saved LBA {l} corrupt'
        orig[l] = b
    parse_header(orig[1], 1); parse_header(orig[BACKUP_HEADER], BACKUP_HEADER)
    p, rd, wr, close = _connect()
    cur = {l: rd(l) for l in ORDER}
    written = _write_set(p, rd, wr, orig, cur, 'RESTORE', saved_dir)
    close()
    print(f'RESTORE PASS: wrote {written}; GPT blocks equal the saved originals')


if __name__ == '__main__':
    if not __debug__: raise SystemExit('asserts required')
    if sys.argv[1:2] == ['plan'] and len(sys.argv) == 4: plan(sys.argv[2], sys.argv[3])
    elif sys.argv[1:2] == ['apply'] and len(sys.argv) == 4: apply(sys.argv[2], sys.argv[3])
    elif sys.argv[1:2] == ['restore'] and len(sys.argv) == 3: restore(sys.argv[2])
    else: raise SystemExit(__doc__)
