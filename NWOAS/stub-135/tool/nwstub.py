#!/usr/bin/env python3
"""stub-135: turn the NWSTUB placeholder (GPT slot 3) into an Asahi-style macOS 13.5 stub whose boot
object will be the NWOAS D4 m1n1. Runs on the Mac mini, in normal macOS (Tahoe), from Terminal with sudo.

  sudo ./nwstub.sh                 dry run (default): reads only, prints the before/after GPT plan
  sudo ./nwstub.sh --apply         does it (asks you to type NWSTUB first; bless asks for the password)
  python3 -I nwstub.py --simulate-gpt <dir>   offline plan from saved GPT blocks (no system access)

Writes (apply only): GPT slot 3 type via `diskutil eraseVolume APFS`, the new container's volumes and files
(asahi-installer v0.9.2 stub.py, unmodified), boot.bin, then `bless --setBoot` for the stub.
Nothing else on the disk is written by this tool; the GPT is re-read after diskutil and the run stops if
any entry other than slot 3, slot 3's LBA range, or the header geometry changed.
This tool never reads, stores or passes a password; bless/bputil/kmutil prompt for it themselves.
"""
import argparse, datetime, hashlib, json, logging, os, plistlib, shutil, subprocess, sys, zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gptlib

BOOT_MD5 = '7bec0225edc101857be3b7f195dd7357'
BOOT_SHA256 = '80a13ca257fcf1d04676e08355b78ffec57cf82fbbbf3b3395f3524daa187e48'
BOOT_SIZE = 2097152
IPSW_NAME = 'UniversalMac_13.5_22G74_j274_stub-parts.ipsw'
IPSW_SHA256 = '9c315566b75ec744e3135d92f3925e4b01f65211641af765e4b84a76201ed419'
IPSW_BUILD = '22G74'
LABEL_DEFAULT = 'Windows'
MODEL, DEVICE_CLASS, CHIP_ID, BOARD_ID = 'Macmini9,1', 'j274ap', 0x8103, 0x22
DISK = 'disk0'
ASAHI_VERSION = 'v0.9.2'


def say(tag, msg):
    print(f'[{tag}] {msg}', flush=True)


def stop(msg):
    print(f'[STOP] {msg}', flush=True)
    raise SystemExit(2)


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for blk in iter(lambda: f.read(16 << 20), b''):
            h.update(blk)
    return h.hexdigest()


def run(*cmd, capture=True):
    logging.info('run: %r', cmd)
    r = subprocess.run(list(cmd), stdout=subprocess.PIPE if capture else None, stderr=subprocess.STDOUT if capture else None)
    if r.returncode != 0:
        out = r.stdout.decode(errors='replace') if capture and r.stdout else ''
        stop(f'command failed ({r.returncode}): {" ".join(cmd)}\n{out}')
    return r.stdout if capture else b''


def plist(*cmd):
    return plistlib.loads(run(*cmd))


# ---------------------------------------------------------------- raw GPT (read-only)

def read_gpt_blocks():
    fd = os.open(f'/dev/r{DISK}', os.O_RDONLY)
    try:
        out = {}
        for l in gptlib.GPT_LBAS:
            b = os.pread(fd, gptlib.BLOCK, l * gptlib.BLOCK)
            if len(b) != gptlib.BLOCK:
                stop(f'short read at LBA {l}')
            out[l] = b
        return out
    finally:
        os.close(fd)


def load_gpt_dir(d):
    d = Path(d)
    return {l: (d / f'lba-{l}.bin').read_bytes() for l in gptlib.GPT_LBAS}


def save_gpt_dir(blocks, d):
    """Same file layout as nwoas_scripts/layout-s231/gpt_partition_s231.py before-apply dirs, so
    `gpt_partition_s231.py restore <d>` can write the saved table back through the m1n1 proxy."""
    d = Path(d); d.mkdir(parents=True, exist_ok=False)
    for l, b in blocks.items():
        (d / f'lba-{l}.bin').write_bytes(b)
    (d / 'sha256.json').write_text(json.dumps({str(l): hashlib.sha256(b).hexdigest() for l, b in blocks.items()}, indent=1))
    return d


def print_plan(tbl, label):
    print()
    print(gptlib.format_table(tbl['entries'], 'BEFORE (on disk now)'))
    print()
    print(gptlib.format_table(gptlib.predict_after(tbl), 'AFTER (planned)'))
    print()
    print('Unchanged by plan: slots 1, 2, 4, 5, 6, 7 (byte-identical), every LBA range, disk GUID 898f172d-...,')
    print('header geometry, backup table position. Checked again from the raw disk after diskutil runs.')
    print()
    print('Commands --apply will run, in order:')
    print(f'  1. save LBA {gptlib.GPT_LBAS} -> out/gpt-before-<time>/')
    print(f'  2. diskutil eraseVolume APFS "{label}" <slot-3 device>      # slot 3 only, offset/size/UUID checked')
    print('  3. re-read raw GPT; STOP unless only slot 3 type/GUID/attrs/name changed; save out/gpt-after-<time>/')
    print(f'  4. asahi stub.py: volumes "{label} - Data"(D) "{label}"(S) Preboot(B) Recovery(R); macOS 13.5 22G74 files')
    print('  5. copy boot.bin (D4 m1n1, MD5 7bec0225...) into the stub "Finish Installation.app"; hide for step 2')
    print('  6. bless --setBoot --device /dev/<stub System volume> --user <admin>   # bless asks the password')


# ---------------------------------------------------------------- system checks (Mac mini, macOS)

def find_slot3_device():
    lst = plist('diskutil', 'list', '-plist')
    parts = {d['DeviceIdentifier']: d for d in lst['AllDisksAndPartitions']}.get(DISK, {}).get('Partitions', [])
    hits = []
    for p in parts:
        info = plist('diskutil', 'info', '-plist', p['DeviceIdentifier'])
        if (info.get('PartitionMapPartitionOffset') == gptlib.STUB_FIRST * gptlib.BLOCK and
                info.get('Size') == gptlib.STUB_BLOCKS * gptlib.BLOCK):
            hits.append(info)
    if len(hits) != 1:
        stop(f'expected exactly one {DISK} partition at offset {gptlib.STUB_FIRST * gptlib.BLOCK}, found {len(hits)}')
    info = hits[0]
    if str(info.get('DiskUUID', '')).lower() != str(gptlib.STUB_GUID):
        stop(f'slot 3 device {info["DeviceIdentifier"]} has partition UUID {info.get("DiskUUID")}, expected {gptlib.STUB_GUID}')
    return info


def check_system(args):
    if os.geteuid() != 0:
        stop('run with sudo (needs raw read of /dev/rdisk0)')
    model = run('sysctl', '-n', 'hw.model').decode().strip()
    if model != MODEL:
        stop(f'this is {model}, not the Mac mini {MODEL}')
    d0 = plist('diskutil', 'info', '-plist', DISK)
    if not (d0.get('Internal') and d0.get('WholeDisk') and d0.get('DeviceBlockSize') == gptlib.BLOCK
            and d0.get('Size') == gptlib.DISK_BYTES and d0.get('VirtualOrPhysical') == 'Physical'):
        stop(f'{DISK} is not the internal 4K-block {gptlib.DISK_BYTES}-byte SSD: '
             f'{ {k: d0.get(k) for k in ("Internal", "WholeDisk", "DeviceBlockSize", "Size", "VirtualOrPhysical")} }')
    say('CHECK', f'{model}, {DISK} internal, 4096-byte blocks, {gptlib.DISK_BLOCKS} blocks')


def load_asahi(asahi_dir):
    sys.path.insert(0, str(asahi_dir))
    vt = (Path(asahi_dir) / 'version.tag').read_text().strip()
    if vt != ASAHI_VERSION:
        stop(f'asahi runtime is {vt}, expected {ASAHI_VERSION}')
    os.environ['DISTRO'] = 'NWOAS Windows stub'
    import system, diskutil, osenum, stub, util   # noqa: E401  (asahi-installer modules, unmodified)
    return system, diskutil, osenum, stub, util


def check_bundle(bundle):
    sums = {}
    for line in (bundle / 'SHA256SUMS').read_text().splitlines():
        h, name = line.split('  ', 1)
        sums[name] = h
    bad = [n for n, h in sums.items() if sha256_file(bundle / n) != h]
    if bad:
        stop(f'bundle files changed: {bad}')
    boot = bundle / 'boot' / 'boot.bin'
    data = boot.read_bytes()
    if len(data) != BOOT_SIZE or hashlib.md5(data).hexdigest() != BOOT_MD5 or hashlib.sha256(data).hexdigest() != BOOT_SHA256:
        stop('boot/boot.bin is not the D4 m1n1')
    ipsw = bundle / 'ipsw' / IPSW_NAME
    if sums.get(f'ipsw/{IPSW_NAME}') != IPSW_SHA256:
        stop('partial IPSW hash is not the recorded one')
    with zipfile.ZipFile(ipsw) as z:
        man = plistlib.loads(z.read('BuildManifest.plist'))
    ids = [i for i in man['BuildIdentities'] if i['Info']['DeviceClass'] == DEVICE_CLASS
           and i['Info']['Variant'] == 'macOS Customer' and i['Info']['RestoreBehavior'] == 'Erase']
    if len(ids) != 1 or ids[0]['Info']['BuildNumber'] != IPSW_BUILD or int(ids[0]['ApBoardID'], 16) != BOARD_ID:
        stop('partial IPSW has no single j274ap 22G74 identity')
    say('CHECK', f'bundle: {len(sums)} files match SHA256SUMS; boot.bin = D4; IPSW 13.5 {IPSW_BUILD} j274ap')
    return boot, ipsw


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--simulate-gpt', metavar='DIR')
    ap.add_argument('--bundle', default=str(HERE.parent))
    ap.add_argument('--label', default=LABEL_DEFAULT)
    a = ap.parse_args()

    if a.simulate_gpt:
        if a.apply:
            stop('--simulate-gpt never applies')
        tbl = gptlib.read_table(load_gpt_dir(a.simulate_gpt))
        gptlib.check_current(tbl)
        say('CHECK', 'saved GPT = S231 layout, slot 3 = NWSTUB placeholder')
        print_plan(tbl, a.label)
        say('SIMULATE', 'offline only; nothing was read from or written to any disk')
        return

    bundle = Path(a.bundle).resolve()
    out = bundle / 'out'; out.mkdir(exist_ok=True)
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    logging.basicConfig(level=logging.DEBUG, filename=str(out / f'nwstub-{stamp}.log'),
                        format='%(asctime)s %(name)s %(levelname)s %(message)s')
    if not all(c.isalnum() or c in ' -_' for c in a.label) or not a.label or len(a.label) > 24:
        stop('label: letters, digits, space, - and _ only (max 24)')

    # ---- read-only preflight (dry run and apply) ----
    check_system(a)
    before = read_gpt_blocks()
    tbl = gptlib.read_table(before)
    gptlib.check_current(tbl)
    say('CHECK', 'raw GPT = S231 layout; slot 3 = NWSTUB (Basic Data, attrs 0xD000000000000000, LBA 38968576-39944703)')
    s3 = find_slot3_device()
    dev = s3['DeviceIdentifier']
    if s3.get('MountPoint'):
        stop(f'{dev} is mounted at {s3["MountPoint"]}; unmount it first')
    if s3.get('APFSContainerReference'):
        stop(f'{dev} already holds an APFS container')
    say('CHECK', f'slot 3 = /dev/{dev} (content {s3.get("Content")}), not mounted')
    boot, ipsw = check_bundle(bundle)
    system, diskutil, osenum, stub, util = load_asahi(bundle / 'asahi')
    sysinfo = system.SystemInfo()
    if (sysinfo.device_class, sysinfo.chip_id, sysinfo.board_id) != (DEVICE_CLASS, CHIP_ID, BOARD_ID):
        stop(f'device {sysinfo.device_class} chip {sysinfo.chip_id:#x} board {sysinfo.board_id:#x}')
    if sysinfo.boot_mode != 'macOS':
        stop(f'boot mode is {sysinfo.boot_mode!r}; run from normal macOS (not recovery)')
    if not sysinfo.login_user or sysinfo.login_user == 'unknown':
        stop('no local console login; run from the Mac mini Terminal, not only over SSH')
    say('CHECK', f'macOS {sysinfo.macos_ver} ({sysinfo.macos_build}); system firmware {sysinfo.sys_firmware}; '
                 f'SFR {sysinfo.sfr_full_ver}; boot VGID {sysinfo.boot_vgid}')
    dutil = diskutil.DiskUtil(); dutil.get_info()
    parts = dutil.get_partitions(DISK)
    osinfo = osenum.OSEnum(sysinfo, dutil, DISK)
    # Like asahi main.py (booted OS = VG whose UUID is boot-uuid) but only the running macOS container is
    # enumerated (asahi's collect_os mounts its Preboot/Recovery volumes; other containers are left alone).
    mac_parts = [p for p in parts if p.container and any(
        vg.get('APFSVolumeGroupUUID') == sysinfo.boot_uuid for vg in p.container.get('VolumeGroups', []))]
    if len(mac_parts) != 1:
        stop(f'running macOS volume group {sysinfo.boot_uuid} not found in exactly one container')
    osinfo.collect_part(mac_parts[0])
    cur = [o for o in (mac_parts[0].os or []) if o.vgid == sysinfo.boot_uuid]
    if len(cur) != 1 or not cur[0].admin_users:
        stop('could not identify the running macOS (or its admin users)')
    cur_os = cur[0]
    say('CHECK', f'running OS: {cur_os.label} {cur_os.version}, admin users {cur_os.admin_users}')
    print_plan(tbl, a.label)

    if not a.apply:
        say('DRY-RUN', 'nothing was changed. Re-run with --apply to do it.')
        return

    # ---- apply ----
    print()
    print(f'This will ERASE /dev/{dev} (GPT slot 3, NWSTUB, 4 GB, no user data) and build the "{a.label}" stub.')
    if input('Type NWSTUB to continue: ').strip() != 'NWSTUB':
        stop('not confirmed; nothing changed')
    bdir = save_gpt_dir(before, out / f'gpt-before-{stamp}')
    say('SAVED', f'GPT blocks before the change -> {bdir}')

    run('diskutil', 'eraseVolume', 'APFS', a.label, dev, capture=False)
    after = read_gpt_blocks()
    adir = save_gpt_dir(after, out / f'gpt-after-{stamp}')
    try:
        rep = gptlib.validate_after(before, after)
    except gptlib.GptError as e:
        stop(f'GPT changed outside the allowed fields: {e}. Do not continue. Saved: {bdir} / {adir}. '
             'Rollback: README section "되돌리기 R2".')
    say('CHECK', f'GPT after diskutil: only slot 3 changed {rep["slot3"]} {rep["changed"]}; saved {adir}')
    print(gptlib.format_table(gptlib.read_table(after)['entries'], 'AFTER (re-read from disk)'))

    dutil = diskutil.DiskUtil(); dutil.get_info()
    s3n = find_slot3_device_after(dutil)
    part = dutil.get_partition_info(s3n, refresh_apfs=True)
    if part.offset != gptlib.STUB_FIRST * gptlib.BLOCK or part.size != gptlib.STUB_BLOCKS * gptlib.BLOCK or part.container is None:
        stop(f'new container on {s3n} not where expected: {part}')
    part.label = a.label
    osinfo = osenum.OSEnum(sysinfo, dutil, DISK)

    os.chdir(bundle / 'stage')               # stub.py reads ./logo.icns and ./step2/ relative to cwd
    ins = stub.StubInstaller(sysinfo, dutil, osinfo)

    class LocalIPSW:
        version = '13.5'
        url = str(ipsw)
    ins.load_ipsw(LocalIPSW)
    assert not ins.is_ota
    ins.prepare_volume(part)
    ins.check_volume()
    ins.install_files(cur_os)
    shutil.copy(boot, ins.boot_obj_path)
    if sha256_file(ins.boot_obj_path) != BOOT_SHA256:
        stop('boot.bin copy mismatch in the stub')
    step2 = Path(ins.step2_sh).read_text()
    if '##' in step2.replace('${system_dir##*/}', '') or BOOT_SHA256 not in step2:
        stop('step2.sh template not filled')
    if not util.fsctl_is_bootable(ins.osi.system):
        stop('stub System volume is not marked bootable (APFSIOC_VOL_BOOTABLE)')
    info = dict(vgid=ins.osi.vgid, system=ins.osi.system, sys_volume=ins.osi.sys_volume, slot3_device=s3n,
                gpt_before=str(bdir), gpt_after=str(adir), slot3_changes=rep['slot3'],
                stub_info={k: v for k, v in ins.stub_info.items() if k != 'admin_users'})
    (out / f'stub-{stamp}.json').write_text(json.dumps(info, indent=1, default=str))
    say('DONE', f'stub files installed: VGID {ins.osi.vgid}, System /dev/{ins.osi.sys_volume}; record out/stub-{stamp}.json')

    admins = cur_os.admin_users
    user = admins[0]
    if len(admins) > 1:
        for i, n in enumerate(admins):
            print(f'  {i}: {n}')
        user = admins[int(input('admin user number for bless: '))]
    say('BLESS', f'bless --setBoot --device /dev/{ins.osi.sys_volume} --user {user}  (bless asks the password)')
    r = subprocess.run(['bless', '--setBoot', '--device', '/dev/' + ins.osi.sys_volume, '--user', user])
    # Same order as asahi main.py step2(): bless while SystemVersion.plist is visible, then hide it so the
    # boot picker offers the stub as "Finish Installation" (.IAPhysicalMedia) for step 2.
    ins.prepare_for_step2()
    say('CHECK', 'stub hidden for step 2 (SystemVersion-disabled.plist, .IAPhysicalMedia present)')
    if r.returncode != 0:
        say('WARN', 'bless failed. The stub is installed but not default. See README section 4, risk 1 '
                    '(asahi-installer issue #404). Nothing else needs undoing.')
        raise SystemExit(3)
    say('NEXT', 'Shut down (Apple menu > Shut Down). Wait 25 s. HOLD the power button until "Loading startup options".'
                f' Choose "{a.label}" -> Finish Installation (step 2).')


def find_slot3_device_after(dutil):
    for p in dutil.disk_parts[DISK]['Partitions']:
        info = plist('diskutil', 'info', '-plist', p['DeviceIdentifier'])
        if info.get('PartitionMapPartitionOffset') == gptlib.STUB_FIRST * gptlib.BLOCK:
            if info.get('Size') != gptlib.STUB_BLOCKS * gptlib.BLOCK or info.get('Content') != 'Apple_APFS':
                stop(f'slot 3 after erase: size {info.get("Size")} content {info.get("Content")}')
            return info['DeviceIdentifier']
    stop('slot 3 partition not found after erase')


if __name__ == '__main__':
    main()
