#!/usr/bin/env python3
"""Prepare (never apply) the GptGuard update needed after GPT slot 3 becomes the 13.5 stub container.

Why: layout-s231/guest_module.py pins the four Apple-side GPT entries byte-for-byte (PROTECTED) and
nvme-s124/gpt_guard.py refuses at init when one of them differs ('protected partition modified in
slot 3' if the unique GUID survived, else 'slot 3 outside Windows zone' + 'protected partition missing').
diskutil eraseVolume changes slot 3's type GUID (Basic Data -> Apple_APFS), so every S231/S242
launcher would stop at GptGuard init after the stub exists.

What this writes (all under --out, nothing else is touched):
  guest_module.py      copy of the input with ONE inserted block right after PROTECTED: it reads LBA 2,
                       and accepts slot 3 if it equals either the NWSTUB placeholder (old bytes) or the
                       recorded stub entry (new bytes); anything else -> AssertionError before GptGuard.
                       So the same launcher works before the change, after it, and after a GPT rollback.
  guest_module.diff    unified diff against the input
  apply-guard-update.sh  NOT RUN. Re-checks hashes, backs up, installs the module, rewrites the sha256
                       pin in every launcher that pins the old guest_module.py hash.
  summary.json

Usage: python3 -I guard_update.py --guest-module <layout-s231/guest_module.py>
           --before <gpt-before dir> --after <gpt-after dir> --scripts <nwoas_scripts dir> --out <dir>
The gpt dirs hold lba-<N>.bin for gptlib.GPT_LBAS (as saved by nwstub.py --apply).
"""
import argparse, difflib, hashlib, json, os, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gptlib

GUEST_MODULE_SHA_S231 = 'e85591e9370c0aaebb5fef77ff65daf0086d12603e76e4eeed454b3174575026'
ANCHOR_END = ")]\n"
MARK = "# stub-135 slot 3 identity"


def load_blocks(d):
    d = Path(d)
    return {l: (d / f'lba-{l}.bin').read_bytes() for l in gptlib.GPT_LBAS}


def sha(b):
    return hashlib.sha256(b).hexdigest()


def patch_module(src, new_slot3_hex):
    old_slot3_hex = gptlib.EXPECTED_ENTRIES[gptlib.STUB_SLOT - 1].hex()
    assert MARK not in src, 'module already patched'
    start = src.index('PROTECTED=[bytes.fromhex(h) for h in (')
    end = src.index(ANCHOR_END, start) + len(ANCHOR_END)
    block = src[start:end]
    hexes = re.findall(r"'([0-9a-f]{256})'", block)
    assert len(hexes) == 4 and hexes[2] == old_slot3_hex, 'PROTECTED list is not the S231 list'
    ins = (
        f"{MARK}: placeholder (S231) or the 13.5 stub APFS entry recorded after diskutil eraseVolume.\n"
        "# Every other PROTECTED entry and the zone are unchanged. Unknown slot 3 bytes still refuse here.\n"
        f"NWSTUB_PLACEHOLDER=bytes.fromhex('{old_slot3_hex}')\n"
        f"NWSTUB_STUB135=bytes.fromhex('{new_slot3_hex}')\n"
        "_slot3=backend(2)[256:384]\n"
        "assert PROTECTED[2]==NWSTUB_PLACEHOLDER,'stub-135: PROTECTED[2] is not the NWSTUB placeholder'\n"
        "if _slot3==NWSTUB_STUB135:PROTECTED[2]=NWSTUB_STUB135\n"
        "assert _slot3 in (NWSTUB_PLACEHOLDER,NWSTUB_STUB135),'stub-135: GPT slot 3 is neither the NWSTUB placeholder nor the recorded 13.5 stub entry'\n"
        "hv.log('[stub-135] GPT slot 3 = '+('13.5 stub APFS' if _slot3==NWSTUB_STUB135 else 'NWSTUB placeholder'))\n"
    )
    return src[:end] + ins + src[end:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--guest-module', required=True)
    ap.add_argument('--before', required=True)
    ap.add_argument('--after', required=True)
    ap.add_argument('--scripts', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()

    gm = Path(a.guest_module).resolve(); src_b = gm.read_bytes()
    if sha(src_b) != GUEST_MODULE_SHA_S231:
        raise SystemExit(f'STOP: {gm} is not the S231 guest_module.py (sha256 {sha(src_b)})')
    before, after = load_blocks(a.before), load_blocks(a.after)
    gptlib.check_current(gptlib.read_table(before))
    rep = gptlib.validate_after(before, after)
    assert gptlib.is_current_or_stub(gptlib.read_table(after)) == 'stub'
    new_hex = rep['slot3_entry_hex']

    src = src_b.decode()
    new = patch_module(src, new_hex)
    out = Path(a.out); out.mkdir(parents=True, exist_ok=False)
    (out / 'guest_module.py').write_text(new)
    diff = ''.join(difflib.unified_diff(src.splitlines(True), new.splitlines(True),
                                        'layout-s231/guest_module.py', 'layout-s231/guest_module.py (stub-135)'))
    (out / 'guest_module.diff').write_text(diff)
    new_sha = sha(new.encode())

    scripts = Path(a.scripts).resolve()
    pinned = sorted(str(p.relative_to(scripts)) for p in scripts.glob('*.sh')
                    if GUEST_MODULE_SHA_S231 in p.read_text(errors='replace'))
    manifest = scripts / 'layout-s231' / 'manifest.json'
    sh = ['#!/bin/bash',
          '# PREPARED BY stub-135/tool/guard_update.py - NOT RUN. Run on the MacBook only, after the stub exists',
          '# and its GPT was re-captured. It edits files only; it does not touch the Mac mini.',
          'set -eu', f'S="{scripts}"', f'NEW="{out.resolve()}/guest_module.py"',
          f'[ "$(shasum -a 256 "$S/layout-s231/guest_module.py" | cut -d" " -f1)" = {GUEST_MODULE_SHA_S231} ]',
          f'[ "$(shasum -a 256 "$NEW" | cut -d" " -f1)" = {new_sha} ]',
          'cp -p "$S/layout-s231/guest_module.py" "$S/layout-s231/guest_module.py.s231-before-stub135"',
          'cp "$NEW" "$S/layout-s231/guest_module.py"']
    for rel in pinned + ([str(manifest.relative_to(scripts))] if GUEST_MODULE_SHA_S231 in manifest.read_text() else []):
        sh.append(f'sed -i "" "s/{GUEST_MODULE_SHA_S231}/{new_sha}/g" "$S/{rel}"')
    sh.append(f'grep -rl {GUEST_MODULE_SHA_S231} "$S" --include="*.sh" && {{ echo "STOP: old pin left"; exit 1; }} || true')
    sh.append('echo "guard update installed; old module kept as guest_module.py.s231-before-stub135"')
    (out / 'apply-guard-update.sh').write_text('\n'.join(sh) + '\n')
    os.chmod(out / 'apply-guard-update.sh', 0o644)   # deliberately not executable: review, then run with bash
    summary = dict(old_guest_module_sha256=GUEST_MODULE_SHA_S231, new_guest_module_sha256=new_sha,
                   slot3_before=gptlib.EXPECTED_ENTRIES[2].hex(), slot3_after=new_hex,
                   slot3_changes=rep['slot3'], other_changes=rep['changed'], launchers_pinned=pinned)
    (out / 'summary.json').write_text(json.dumps(summary, indent=1) + '\n')
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main()
