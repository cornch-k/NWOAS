#!/usr/bin/env python3
"""S245: clear the GPT attribute bits (0xD000000000000000, inherited from the NWSTUB Basic Data
placeholder) on slot 3, now the 13.5 stub APFS container. Nothing else changes.

Offline: builds a plan dir in the exact format gpt_partition_s231.apply() consumes
(plan.json + new-lba-*.bin for all 10 GPT blocks), from the read-only capture
stub-135/results/gpt-after-20261116-003638. apply() refuses unless the disk still equals that capture.
"""
import hashlib, json, struct, sys, zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'layout-s231'))
import gpt_partition_s231 as g

STUB_ATTRS_OLD = 0xD000000000000000
T_APFS = g.uuid.UUID('7c3457ef-0000-11aa-aa11-00306543ecac')

def main(capture, out):
    capture, out = Path(capture), Path(out)
    blk = {l: (capture / f'lba-{l}.bin').read_bytes() for l in g.PRIMARY + g.BACKUP_ENTRIES + [g.BACKUP_HEADER]}
    assert all(len(v) == g.BLOCK for v in blk.values())
    ph = g.parse_header(blk[1], 1); bh = g.parse_header(blk[g.BACKUP_HEADER], g.BACKUP_HEADER)
    arr = b''.join(blk[l] for l in (2, 3, 4, 5))
    assert zlib.crc32(arr) == ph['ecrc'] == bh['ecrc'], 'entries CRC'
    assert b''.join(blk[l] for l in g.BACKUP_ENTRIES) == arr, 'backup entries differ'
    ents = g.entries_list(arr)
    assert len(ents) == 7, len(ents)
    e3 = [e for e in ents if e['slot'] == 3][0]
    assert e3['guid'] == g.NWSTUB and e3['type'] == T_APFS, 'slot 3 is not the stub APFS entry'
    assert (e3['start'], e3['end'], e3['attrs']) == (38968576, 39944703, STUB_ATTRS_OLD)
    for e in ents:
        if e['slot'] != 3: assert e['attrs'] == 0, f"slot {e['slot']} attrs {e['attrs']:#x}"
    new_arr = bytearray(arr)
    struct.pack_into('<Q', new_arr, 2 * 128 + 48, 0)
    new_arr = bytes(new_arr)
    diff = [i for i in range(len(arr)) if arr[i] != new_arr[i]]
    assert diff and all(2 * 128 + 48 <= i < 2 * 128 + 56 for i in diff), 'changed outside slot 3 attributes'
    ecrc = zlib.crc32(new_arr)
    blocks = {1: g.build_header(blk[1], ecrc), g.BACKUP_HEADER: g.build_header(blk[g.BACKUP_HEADER], ecrc)}
    for j, l in enumerate((2, 3, 4, 5)): blocks[l] = new_arr[j * g.BLOCK:(j + 1) * g.BLOCK]
    for j, l in enumerate(g.BACKUP_ENTRIES): blocks[l] = new_arr[j * g.BLOCK:(j + 1) * g.BLOCK]
    h1 = g.parse_header(blocks[1], 1); h2 = g.parse_header(blocks[g.BACKUP_HEADER], g.BACKUP_HEADER)
    assert h1['ecrc'] == h2['ecrc'] == ecrc
    for l in (1, g.BACKUP_HEADER):
        d = [i for i in range(g.BLOCK) if blocks[l][i] != blk[l][i]]
        assert all(16 <= i < 20 or 88 <= i < 92 for i in d), f'header {l} changed outside CRC fields'
    changed = sorted(l for l in blocks if blocks[l] != blk[l])
    assert changed == [1, 2, g.BACKUP_ENTRIES[0], g.BACKUP_HEADER], changed
    out.mkdir(parents=True, exist_ok=False)
    for l, b in blocks.items(): (out / f'new-lba-{l}.bin').write_bytes(b)
    meta = dict(stage='S245-STUB-ATTR', capture=str(capture),
                capture_sha256={l: hashlib.sha256(blk[l]).hexdigest() for l in g.PRIMARY + [g.BACKUP_HEADER]},
                new_sha256={l: hashlib.sha256(b).hexdigest() for l, b in sorted(blocks.items())},
                changed_blocks=changed, slot3_attrs_old=hex(STUB_ATTRS_OLD), slot3_attrs_new='0x0',
                slot3_entry_new=new_arr[256:384].hex())
    (out / 'plan.json').write_text(json.dumps(meta, indent=2) + '\n')
    print('changed blocks', changed)
    print('PLAN OK', out, 'plan.json sha256', hashlib.sha256((out / 'plan.json').read_bytes()).hexdigest())

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
