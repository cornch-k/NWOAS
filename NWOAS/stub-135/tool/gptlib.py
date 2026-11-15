"""GPT checks for turning the NWSTUB slot (GPT slot 3) into an APFS stub container.

Pure functions only: no device access, no subprocesses. nwstub.py feeds raw 4096-byte blocks read
from /dev/rdisk0; the tests feed the saved S231 blocks. Python 3.9+ (the Mac mini runs the Python 3.13
bundled with the asahi-installer v0.9.2 release package).

Expected disk = the S231 layout written 2026-11-15 18:02 (nwoas_scripts/layout-s231/gpt-plan/plan.json,
read back identical in ans2-s87/run-20261115-180239). Entry bytes below are copied from plan.json
new_entry_bytes; tests/test_gptlib.py re-checks them against plan.json and guest_module.py PROTECTED.
"""
import struct, uuid, zlib

BLOCK = 4096
DISK_BLOCKS = 61279344
DISK_BYTES = DISK_BLOCKS * BLOCK
DISK_GUID = uuid.UUID('898f172d-77bf-4007-b865-ac81a141eb3c')
FIRST_USABLE, LAST_USABLE = 6, 61279338
PRIMARY_ENTRIES = [2, 3, 4, 5]
BACKUP_ENTRIES = [61279339, 61279340, 61279341, 61279342]
BACKUP_HEADER = 61279343
GPT_LBAS = [0, 1] + PRIMARY_ENTRIES + BACKUP_ENTRIES + [BACKUP_HEADER]   # every block nwstub reads/saves

T_BASIC = uuid.UUID('ebd0a0a2-b9e5-4433-87c0-68b6b72699c7')
T_APFS = uuid.UUID('7c3457ef-0000-11aa-aa11-00306543ecac')

STUB_SLOT = 3
STUB_GUID = uuid.UUID('8d17c4ed-0de2-4bf0-884f-5477b004da66')
STUB_FIRST, STUB_LAST = 38968576, 39944703
STUB_ATTRS = 0xD000000000000000
STUB_BLOCKS = STUB_LAST - STUB_FIRST + 1            # 976128 blocks = 3,998,220,288 bytes

EXPECTED_ENTRIES = [bytes.fromhex(h) for h in (
    '616964690067aa11aa1100306543ecac448ff356555cda48aff097af9bb0a578060000000000000005f40100000000000000000000000000690042006f006f007400530079007300740065006d0043006f006e007400610069006e00650072000000000000000000000000000000000000000000000000000000000000000000',
    'ef57347c0000aa11aa1100306543ecac70fd24c6bd53274cadc71ac2f9288cbc06f4010000000000119c5202000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000',
    'a2a0d0ebe5b9334487c068b6b72699c7edc4178de20df04b884f5477b004da66009d520200000000ff8161020000000000000000000000d0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000',
    '28732ac11ff8d211ba4b00a0c93ec93b0330e7a34ac17b489f2e8854fe1191920083610200000000ff8263020000000000000000000000004500460049002000730079007300740065006d00200070006100720074006900740069006f006e000000000000000000000000000000000000000000000000000000000000000000',
    '16e3c9e35c0bb84d817df92df00215ae9f89a6381afd3a44862bfb07ef8a886a0083630200000000ff9263020000000000000000000000004d006900630072006f0073006f0066007400200072006500730065007200760065006400200070006100720074006900740069006f006e0000000000000000000000000000000000',
    'a2a0d0ebe5b9334487c068b6b72699c78561e2fc871ded49968e3e11c24127b70093630200000000ff0b93030000000000000000000000004200610073006900630020006400610074006100200070006100720074006900740069006f006e000000000000000000000000000000000000000000000000000000000000000000',
    '727663520079aa11aa1100306543ecac350d13593224cb4f82067efdf9903d4d760c9303000000006a0ca7030000000000000000000000005200650063006f0076006500720079004f00530043006f006e007400610069006e006500720000000000000000000000000000000000000000000000000000000000000000000000',
)]
TYPE_NAMES = {
    uuid.UUID('69646961-6700-11aa-aa11-00306543ecac'): 'Apple_APFS_ISC',
    T_APFS: 'Apple_APFS',
    T_BASIC: 'Microsoft Basic Data',
    uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b'): 'EFI',
    uuid.UUID('e3c9e316-0b5c-4db8-817d-f92df00215ae'): 'Microsoft Reserved',
    uuid.UUID('52637672-7900-11aa-aa11-00306543ecac'): 'Apple_APFS_Recovery',
}


class GptError(Exception):
    pass


def _need(cond, msg):
    if not cond:
        raise GptError(msg)


def parse_header(b, expect_cur):
    _need(len(b) == BLOCK, 'header block size')
    _need(b[:8] == b'EFI PART', f'LBA {expect_cur}: no GPT signature')
    size, crc = struct.unpack_from('<II', b, 12)
    _need(size == 92, f'LBA {expect_cur}: header size {size}')
    raw = bytearray(b[:size]); raw[16:20] = b'\0' * 4
    _need(zlib.crc32(raw) == crc, f'LBA {expect_cur}: header CRC')
    cur, alt, first, last = struct.unpack_from('<QQQQ', b, 24)
    ent, cnt, esz, ecrc = struct.unpack_from('<QIII', b, 72)
    _need(cur == expect_cur, f'header current LBA {cur} != {expect_cur}')
    return dict(cur=cur, alt=alt, first=first, last=last, guid=uuid.UUID(bytes_le=b[56:72]),
                ent=ent, cnt=cnt, esz=esz, ecrc=ecrc, raw=bytes(b))


def parse_entry(e, slot):
    t, g = uuid.UUID(bytes_le=e[:16]), uuid.UUID(bytes_le=e[16:32])
    s, en, attrs = struct.unpack_from('<QQQ', e, 32)
    return dict(slot=slot, type=t, guid=g, start=s, end=en, attrs=attrs,
                name=e[56:128].decode('utf-16le', 'replace').rstrip('\0'), raw=bytes(e))


def read_table(blocks):
    """blocks: {lba: 4096 bytes} for GPT_LBAS. Returns dict with headers, entry array and parsed entries."""
    for l in GPT_LBAS:
        _need(l in blocks and len(blocks[l]) == BLOCK, f'missing block {l}')
    mbr = blocks[0]
    _need(mbr[510:512] == b'\x55\xaa' and mbr[446 + 4] == 0xEE, 'LBA 0 is not a protective MBR')
    ph = parse_header(blocks[1], 1)
    bh = parse_header(blocks[BACKUP_HEADER], BACKUP_HEADER)
    _need((ph['alt'], ph['ent'], ph['first'], ph['last'], ph['cnt'], ph['esz']) ==
          (BACKUP_HEADER, 2, FIRST_USABLE, LAST_USABLE, 128, 128), 'primary header geometry differs')
    _need((bh['alt'], bh['ent'], bh['first'], bh['last'], bh['cnt'], bh['esz']) ==
          (1, BACKUP_ENTRIES[0], FIRST_USABLE, LAST_USABLE, 128, 128), 'backup header geometry differs')
    _need(ph['guid'] == DISK_GUID and bh['guid'] == DISK_GUID, 'disk GUID differs')
    arr = b''.join(blocks[l] for l in PRIMARY_ENTRIES)
    barr = b''.join(blocks[l] for l in BACKUP_ENTRIES)
    _need(zlib.crc32(arr) == ph['ecrc'], 'primary entries CRC')
    _need(barr == arr and bh['ecrc'] == ph['ecrc'], 'backup entries differ from primary')
    ents = []
    for i in range(128):
        e = arr[i * 128:(i + 1) * 128]
        if e[:16] == bytes(16):
            _need(e == bytes(128), f'slot {i + 1}: empty type but non-zero bytes')
            continue
        ents.append(parse_entry(e, i + 1))
    return dict(mbr=mbr, ph=ph, bh=bh, arr=arr, entries=ents)


def check_current(tbl):
    """Refuse unless the disk is exactly the S231 layout with the untouched NWSTUB placeholder."""
    got = [e['raw'] for e in tbl['entries']]
    _need([e['slot'] for e in tbl['entries']] == list(range(1, 8)), 'expected exactly slots 1-7')
    for i, (g, x) in enumerate(zip(got, EXPECTED_ENTRIES)):
        _need(g == x, f'slot {i + 1} differs from the S231 layout')
    s3 = tbl['entries'][STUB_SLOT - 1]
    _need((s3['type'], s3['guid'], s3['start'], s3['end'], s3['attrs']) ==
          (T_BASIC, STUB_GUID, STUB_FIRST, STUB_LAST, STUB_ATTRS), 'slot 3 is not the NWSTUB placeholder')
    _need(tbl['arr'][7 * 128:] == bytes(121 * 128), 'slots 8-128 not empty')
    return s3


def predict_after(tbl):
    """Entries as diskutil is expected to leave them: only slot 3's type becomes Apple_APFS.
    Unique GUID / attributes / name of slot 3 are not guaranteed by Apple; they are re-read and
    reported after the change (validate_after)."""
    out = []
    for e in tbl['entries']:
        e = dict(e)
        if e['slot'] == STUB_SLOT:
            e['type'] = T_APFS
            e['note'] = 'type -> Apple_APFS; GUID/attrs/name re-read after change'
        out.append(e)
    return out


def validate_after(before, after):
    """before/after: {lba: block}. Returns a report; raises GptError on any change outside slot 3,
    any change of slot 3's LBA range, a slot 3 type other than Apple_APFS, or header geometry changes."""
    tb, ta = read_table(before), read_table(after)
    report = dict(changed=[], slot3={})
    for k in ('guid', 'first', 'last', 'alt', 'ent', 'cnt', 'esz'):
        _need(tb['ph'][k] == ta['ph'][k] and tb['bh'][k] == ta['bh'][k], f'header field {k} changed')
    for name, hb, ha in (('primary', tb['ph']['raw'], ta['ph']['raw']), ('backup', tb['bh']['raw'], ta['bh']['raw'])):
        diff = [i for i in range(BLOCK) if hb[i] != ha[i]]
        _need(all(16 <= i < 20 or 88 <= i < 92 for i in diff), f'{name} header changed outside CRC fields')
    if tb['mbr'] != ta['mbr']:
        report['changed'].append('LBA0 protective MBR rewritten (still protective)')
    for i in range(128):
        eb, ea = tb['arr'][i * 128:(i + 1) * 128], ta['arr'][i * 128:(i + 1) * 128]
        if i + 1 == STUB_SLOT:
            continue
        _need(eb == ea, f'slot {i + 1} changed')
    s_b = parse_entry(tb['arr'][(STUB_SLOT - 1) * 128:STUB_SLOT * 128], STUB_SLOT)
    s_a = parse_entry(ta['arr'][(STUB_SLOT - 1) * 128:STUB_SLOT * 128], STUB_SLOT)
    _need((s_a['start'], s_a['end']) == (STUB_FIRST, STUB_LAST), f'slot 3 moved/resized: {s_a["start"]}-{s_a["end"]}')
    _need(s_a['type'] == T_APFS, f'slot 3 type is {s_a["type"]}, expected Apple_APFS')
    for k in ('type', 'guid', 'attrs', 'name'):
        if s_b[k] != s_a[k]:
            report['slot3'][k] = (str(s_b[k]) if k != 'attrs' else hex(s_b[k]),
                                  str(s_a[k]) if k != 'attrs' else hex(s_a[k]))
    report['slot3_entry_hex'] = s_a['raw'].hex()
    report['slot3_guid'] = str(s_a['guid'])
    return report


def is_current_or_stub(tbl):
    """'placeholder' if slot 3 is the untouched NWSTUB, 'stub' if it is an Apple_APFS entry at the same
    LBAs with every other slot unchanged, else raises."""
    ents = tbl['entries']
    _need([e['slot'] for e in ents] == list(range(1, 8)), 'expected exactly slots 1-7')
    for i, e in enumerate(ents):
        if i + 1 != STUB_SLOT:
            _need(e['raw'] == EXPECTED_ENTRIES[i], f'slot {i + 1} differs from the S231 layout')
    s3 = ents[STUB_SLOT - 1]
    if s3['raw'] == EXPECTED_ENTRIES[STUB_SLOT - 1]:
        return 'placeholder'
    _need(s3['type'] == T_APFS and (s3['start'], s3['end']) == (STUB_FIRST, STUB_LAST), 'slot 3 is neither placeholder nor stub')
    return 'stub'


def format_table(entries, title):
    lines = [title, f"{'slot':>4}  {'type':<22} {'first LBA':>10} {'last LBA':>10} {'GB':>7}  {'attrs':<18} {'unique GUID':<36}  name"]
    for e in entries:
        gb = (e['end'] - e['start'] + 1) * BLOCK / 1e9
        lines.append(f"{e['slot']:>4}  {TYPE_NAMES.get(e['type'], str(e['type'])):<22} {e['start']:>10} {e['end']:>10} "
                     f"{gb:>7.3f}  {hex(e['attrs']):<18} {str(e['guid']):<36}  {e['name']}"
                     + (f"   <- {e['note']}" if e.get('note') else ''))
    return '\n'.join(lines)
