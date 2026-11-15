"""S243: append one file to a pristine transport-s123 NS2 FAT16 image (pure, no I/O).

transport.fat_image() lays files out sequentially: root entry ix (1-based) and
contiguous clusters starting at 2. fat_append() continues exactly that scheme,
so fat_append(fat_image(F), name, data) == fat_image({**F, name: data}) byte for
byte (unit-tested). It refuses anything that does not look like an untouched
fat_image() output: wrong MBR/BPB, FAT copies differing, a target root entry or
data cluster that is not zero, or a duplicate 8.3 name. Only the FAT partition
(LBA 256..) is modified; the MBR, the pre-FAT gap (LBA 1..255, incl. the LBA 127
request port and the LBA 128 job mailbox) and the capacity are untouched.
"""
import struct

BLOCK = 4096
CAPACITY = 8448
PART = 256
FAT1 = PART + 1
FAT2 = PART + 1 + 4
ROOT = PART + 1 + 8
DATA = PART + 1 + 8 + 4
ROOT_ENTRIES = 512
CLUSTER_LIMIT = 8181      # transport.fat_image asserts cluster+count < 8181
BPB = struct.pack('<HBHBHHBHHHII', 4096, 1, 1, 2, 512, 8192, 0xf8, 4, 63, 255, 256, 0)


def _name83(name):
    if name.count('.') != 1:
        raise ValueError('need exactly one dot in 8.3 name')
    stem, ext = name.split('.')
    if not (1 <= len(stem) <= 8 and 1 <= len(ext) <= 3):
        raise ValueError('not an 8.3 name')
    if not all(c.isascii() and (c.isupper() or c.isdigit() or c in '_-') for c in stem + ext):
        raise ValueError('8.3 name must be upper-case ASCII letters/digits/_-')
    return (stem.ljust(8) + ext.ljust(3)).encode('ascii')


def fat_append(image, name, content):
    """Return a new image with `name` appended; `image` is not modified."""
    if len(image) != CAPACITY * BLOCK:
        raise ValueError('not an NS2 image (capacity)')
    b = bytearray(image)
    if struct.unpack_from('<I', b, 440)[0] != 0x53313233 or b[510:512] != b'\x55\xaa':
        raise ValueError('MBR signature mismatch')
    if b[446:462] != struct.pack('<B3sB3sII', 0, b'\xfe\xff\xff', 6, b'\xfe\xff\xff', 256, 8192):
        raise ValueError('partition entry mismatch')
    boot = PART * BLOCK
    if b[boot:boot + 11] != b'\xeb\x3c\x90NWOAS123' or b[boot + 11:boot + 36] != BPB:
        raise ValueError('boot sector / BPB mismatch')
    fat1 = b[FAT1 * BLOCK:FAT2 * BLOCK]
    fat2 = b[FAT2 * BLOCK:ROOT * BLOCK]
    if fat1 != fat2:
        raise ValueError('FAT copies differ (image not pristine)')
    if struct.unpack_from('<HH', fat1, 0) != (0xfff8, 0xffff):
        raise ValueError('FAT media entries mismatch')
    n83 = _name83(name)
    root = ROOT * BLOCK
    free_ix = None
    for ix in range(1, ROOT_ENTRIES):
        ent = b[root + ix * 32:root + ix * 32 + 32]
        if ent[0] == 0:
            free_ix = ix
            break
        if ent[0:11] == n83:
            raise ValueError(f'{name} already present in FAT root')
    if free_ix is None:
        raise ValueError('FAT root directory full')
    if any(b[root + free_ix * 32:root + ROOT_ENTRIES * 32]):
        raise ValueError('root directory has entries after the first free slot')
    entries = len(fat1) // 2
    used = [c for c in range(2, entries) if struct.unpack_from('<H', fat1, 2 * c)[0]]
    cluster = (max(used) + 1) if used else 2
    if used and used != list(range(2, cluster)):
        raise ValueError('FAT allocation not contiguous (image not pristine)')
    count = max(1, (len(content) + BLOCK - 1) // BLOCK)
    if not cluster + count < CLUSTER_LIMIT:
        raise ValueError('FAT data area full')
    at = DATA * BLOCK + (cluster - 2) * BLOCK
    if any(b[at:at + count * BLOCK]):
        raise ValueError('target data clusters not zero')
    ent = root + free_ix * 32
    b[ent:ent + 11] = n83
    b[ent + 11] = 0x21
    struct.pack_into('<H', b, ent + 26, cluster)
    struct.pack_into('<I', b, ent + 28, len(content))
    b[at:at + len(content)] = content
    for c in range(cluster, cluster + count):
        v = 0xffff if c == cluster + count - 1 else c + 1
        struct.pack_into('<H', b, FAT1 * BLOCK + 2 * c, v)
        struct.pack_into('<H', b, FAT2 * BLOCK + 2 * c, v)
    assert len(b) == CAPACITY * BLOCK
    assert b[:PART * BLOCK] == bytes(image[:PART * BLOCK])
    return bytes(b)
