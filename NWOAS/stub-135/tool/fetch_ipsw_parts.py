#!/usr/bin/env python3
"""Fetch only the macOS 13.5 restore-image members that asahi-installer's stub.py needs for one
J274 (Mac mini M1) stub, and write them into a small local zip that stub.py can open in place of
the 12.9 GB IPSW (StubInstaller.load_ipsw accepts a local path).

Runs on the build MacBook only. It touches no disk device; it only does HTTP range GETs to Apple's
public CDN and writes files under the output directory given on the command line.
Downloaded bytes are treated as untrusted data: nothing from the IPSW is executed. Run with -I.

Member selection mirrors asahi-installer v0.9.2 src/stub.py StubInstaller.install_files():
  BuildManifest.plist, usr/standalone/bootcaches.plist, SystemVersion.plist, PlatformSupport.plist,
  RestoreVersion.plist, BootabilityBundle/Restore/Bootability/*,
  BootabilityBundle/Restore/Firmware/Bootability.dmg.trustcache,
  Firmware/Manifests/restore/<variant>/*, every identity Manifest path except
  BaseSystem/OS/Ap,SystemVolumeCanonicalMetadata/RestoreRamDisk/RestoreTrustCache/Cryptex*,
  plus the BaseSystem image (paired recoveryOS).

Usage:
  python3 -I fetch_ipsw_parts.py list  <out_dir>     # sizes only, downloads central directory + manifest
  python3 -I fetch_ipsw_parts.py fetch <out_dir>     # writes <out_dir>/<PARTIAL> and parts.json
  python3 -I fetch_ipsw_parts.py verify <out_dir>    # re-reads the partial zip, checks CRC + sha256
"""
import hashlib, io, json, os, plistlib, sys, time, zipfile
from http.client import HTTPSConnection
from urllib.parse import urlparse

URL = ("https://updates.cdn-apple.com/2023SummerFCS/fullrestores/032-69606/"
       "D3E05CDF-E105-434C-A4A1-4E3DC7668DD0/UniversalMac_13.5_22G74_Restore.ipsw")
IPSW_SIZE = 12893195726           # Content-Length observed 2026-11-15
PARTIAL = "UniversalMac_13.5_22G74_j274_stub-parts.ipsw"
DEVICE_CLASS, CHIP_ID = "j274ap", 0x8103
VARIANT, BEHAVIOR = "macOS Customer", "Erase"
SKIP_KEYS = ("BaseSystem", "OS", "Ap,SystemVolumeCanonicalMetadata", "RestoreRamDisk", "RestoreTrustCache")
FIXED = ["BuildManifest.plist", "usr/standalone/bootcaches.plist", "SystemVersion.plist",
         "PlatformSupport.plist", "RestoreVersion.plist",
         "BootabilityBundle/Restore/Firmware/Bootability.dmg.trustcache"]
TREES = ["BootabilityBundle/Restore/Bootability/", f"Firmware/Manifests/restore/{VARIANT}/"]


class RangeFile(io.RawIOBase):
    """Seekable read-only view of a remote file using HTTP Range requests (1 MiB block cache)."""
    BLOCK = 1 << 20

    def __init__(self, url, size):
        self.u = urlparse(url); self.size = size; self.pos = 0
        self.cache = {}; self.order = []; self.con = None; self.fetched = 0

    def readable(self): return True
    def seekable(self): return True
    def tell(self): return self.pos

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.size + off
        return self.pos

    def _get(self, start, end):
        for attempt in range(6):
            try:
                if self.con is None:
                    self.con = HTTPSConnection(self.u.netloc, timeout=60)
                self.con.request("GET", self.u.path, headers={"Range": f"bytes={start}-{end - 1}",
                                                               "User-Agent": "stub-fetch/1"})
                r = self.con.getresponse()
                data = r.read()
                if r.status != 206 or len(data) != end - start:
                    raise IOError(f"HTTP {r.status} len {len(data)} for {start}-{end}")
                self.fetched += len(data)
                return data
            except Exception as e:
                try: self.con.close()
                except Exception: pass
                self.con = None
                if attempt == 5: raise
                print(f"  retry {attempt + 1}: {e}", file=sys.stderr); time.sleep(2 * (attempt + 1))

    def _block(self, i):
        if i not in self.cache:
            s = i * self.BLOCK; e = min(self.size, s + self.BLOCK)
            self.cache[i] = self._get(s, e); self.order.append(i)
            if len(self.order) > 64:
                self.cache.pop(self.order.pop(0), None)
        return self.cache[i]

    def readinto(self, b):
        n = min(len(b), self.size - self.pos)
        if n <= 0: return 0
        if n >= 8 * self.BLOCK:          # big sequential read: one ranged GET, bypass the cache
            data = self._get(self.pos, self.pos + n)
        else:
            out = bytearray(); p = self.pos
            while len(out) < n:
                i = p // self.BLOCK; blk = self._block(i); o = p - i * self.BLOCK
                take = blk[o:o + n - len(out)]; out += take; p += len(take)
            data = bytes(out)
        b[:n] = data; self.pos += n
        return n


def open_remote():
    rf = RangeFile(URL, IPSW_SIZE)
    return rf, zipfile.ZipFile(io.BufferedReader(rf, buffer_size=1 << 20))


def select_identity(manifest):
    ids = [i for i in manifest["BuildIdentities"]
           if i["Info"]["DeviceClass"] == DEVICE_CLASS and i["ApChipID"] == f"0x{CHIP_ID:04X}"
           and i["Info"]["RestoreBehavior"] == BEHAVIOR and i["Info"]["Variant"] == VARIANT]
    if len(ids) != 1:
        raise SystemExit(f"STOP: expected exactly one {DEVICE_CLASS} identity, found {len(ids)}")
    return ids[0]


def member_list(z, identity):
    names = {i.filename: i for i in z.infolist()}
    wanted = list(FIXED)
    for t in TREES:
        wanted += [n for n in names if n.startswith(t)]
    for key, val in identity["Manifest"].items():
        if key in SKIP_KEYS or key.startswith("Cryptex"):
            continue
        wanted.append(val["Info"]["Path"])
    wanted.append(identity["Manifest"]["BaseSystem"]["Info"]["Path"])
    out, seen = [], set()
    for n in wanted:
        if n in seen: continue
        seen.add(n)
        if n not in names:
            raise SystemExit(f"STOP: member missing from IPSW: {n}")
        out.append(names[n])
    return out


def describe(z):
    manifest = plistlib.loads(z.read("BuildManifest.plist"))
    ident = select_identity(manifest)
    members = member_list(z, ident)
    return manifest, ident, members


def cmd_list(out_dir):
    rf, z = open_remote()
    manifest, ident, members = describe(z)
    tot = sum(m.file_size for m in members); comp = sum(m.compress_size for m in members)
    for m in sorted(members, key=lambda m: -m.file_size)[:25]:
        print(f"{m.file_size:>12} {m.compress_size:>12} {m.compress_type} {m.filename}")
    print(f"members={len(members)} uncompressed={tot} compressed(download)={comp}")
    print("identity", ident["ApBoardID"], ident["ApChipID"], ident["Info"]["DeviceClass"],
          ident["Info"]["BuildNumber"], "BaseSystem", ident["Manifest"]["BaseSystem"]["Info"]["Path"])
    print("bytes fetched for listing:", rf.fetched)


def cmd_fetch(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, PARTIAL)
    if os.path.exists(dst):
        raise SystemExit(f"STOP: {dst} exists; remove it first")
    rf, z = open_remote()
    manifest, ident, members = describe(z)
    rec = []
    tmp = dst + ".part"
    with zipfile.ZipFile(tmp, "w", allowZip64=True) as zo:
        for k, m in enumerate(members):
            zi = zipfile.ZipInfo(m.filename, date_time=m.date_time)
            zi.external_attr = m.external_attr; zi.create_system = m.create_system
            zi.compress_type = zipfile.ZIP_STORED if m.compress_type == zipfile.ZIP_STORED else zipfile.ZIP_DEFLATED
            h = hashlib.sha256(); n = 0
            with z.open(m) as src, zo.open(zi, "w", force_zip64=m.file_size > (1 << 31)) as d:
                while True:
                    blk = src.read(16 << 20)       # zipfile checks the CRC-32 at EOF
                    if not blk: break
                    h.update(blk); d.write(blk); n += len(blk)
            assert n == m.file_size, (m.filename, n, m.file_size)
            rec.append(dict(name=m.filename, size=m.file_size, crc32=f"{m.CRC:08x}", sha256=h.hexdigest(),
                            src_compressed=m.compress_size))
            print(f"[{k + 1}/{len(members)}] {m.file_size:>11} {m.filename}", flush=True)
    os.replace(tmp, dst)
    zsha = hashlib.sha256()
    with open(dst, "rb") as f:
        for blk in iter(lambda: f.read(16 << 20), b""): zsha.update(blk)
    meta = dict(source_url=URL, source_size=IPSW_SIZE, build=ident["Info"]["BuildNumber"],
                device_class=DEVICE_CLASS, ap_board_id=ident["ApBoardID"], ap_chip_id=ident["ApChipID"],
                variant=VARIANT, behavior=BEHAVIOR, bytes_downloaded=rf.fetched,
                partial_zip=PARTIAL, partial_zip_size=os.path.getsize(dst), partial_zip_sha256=zsha.hexdigest(),
                fetched_at=time.strftime("%Y-%m-%dT%H:%M:%S%z"), members=rec)
    with open(os.path.join(out_dir, "parts.json"), "w") as f:
        json.dump(meta, f, indent=1)
    print("DONE", dst, meta["partial_zip_size"], meta["partial_zip_sha256"], "downloaded", rf.fetched)


def cmd_verify(out_dir):
    meta = json.load(open(os.path.join(out_dir, "parts.json")))
    dst = os.path.join(out_dir, meta["partial_zip"])
    zsha = hashlib.sha256()
    with open(dst, "rb") as f:
        for blk in iter(lambda: f.read(16 << 20), b""): zsha.update(blk)
    assert zsha.hexdigest() == meta["partial_zip_sha256"], "partial zip sha256 mismatch"
    with zipfile.ZipFile(dst) as z:
        names = [i.filename for i in z.infolist()]
        assert names == [m["name"] for m in meta["members"]], "member list differs"
        for m in meta["members"]:
            h = hashlib.sha256()
            with z.open(m["name"]) as s:
                for blk in iter(lambda: s.read(16 << 20), b""): h.update(blk)
            assert h.hexdigest() == m["sha256"], m["name"]
        ident = select_identity(plistlib.loads(z.read("BuildManifest.plist")))
        assert ident["Info"]["BuildNumber"] == meta["build"]
    print("VERIFY OK", len(meta["members"]), "members", meta["partial_zip_sha256"])


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("list", "fetch", "verify"):
        raise SystemExit(__doc__)
    {"list": cmd_list, "fetch": cmd_fetch, "verify": cmd_verify}[sys.argv[1]](sys.argv[2])
