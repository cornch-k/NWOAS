"""S243: generate an ASCII worker job (.cmd, <= 4032 bytes) that fetches manifest
artifacts into one target directory on C: and verifies them independently.

    python3 -I make_job.py MANIFEST.json 'C:\\NWOAS\\drop\\x' OUT.cmd [--index 0,2] [--delete-after]

The job (run by the NWOAS Agent worker as a cmd batch from the NS2 RAM drive):
  1. refuses a target path whose existing components are files or reparse points,
     then creates the directory;
  2. runs %~d0\\NWGET243.EXE <index> <sha256> <dir\\name> per artifact (the client
     refuses to overwrite and checks the digest itself);
  3. re-hashes every delivered file with PowerShell Get-FileHash and prints
     "S243 READBACK MATCH|MISMATCH <name> <sha256>"; exits 1 on any mismatch;
  4. with --delete-after (throughput self-tests only) removes just those files.
"""
import json
import re
import sys
from pathlib import Path

MAILBOX = 4032
SAFE_NAME = re.compile(r'^[A-Za-z0-9_+-][A-Za-z0-9._+-]{0,99}$')
SAFE_DIR = re.compile(r'^C:\\[A-Za-z0-9._+-]+(\\[A-Za-z0-9._+-]+)*$')
PS = '"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoProfile -NonInteractive -Command'


def build(manifest, target_dir, indices=None, delete_after=False):
    arts = manifest['artifacts']
    if indices is None:
        indices = list(range(len(arts)))
    if not indices or len(set(indices)) != len(indices) or not all(0 <= i < len(arts) for i in indices):
        raise ValueError('bad index selection')
    if not SAFE_DIR.match(target_dir) or any(c in ('.', '..') for c in target_dir.split('\\')[1:]):
        raise ValueError(f'unsafe target dir {target_dir!r}')
    if len(target_dir) > 180:
        raise ValueError('target dir too long')
    names = [arts[i]['name'] for i in indices]
    shas = [arts[i]['sha256'] for i in indices]
    for n, s in zip(names, shas):
        if not SAFE_NAME.match(n) or n.endswith('.') or len(target_dir) + 1 + len(n) > 240:
            raise ValueError(f'unsafe artifact name {n!r}')
        if not re.fullmatch('[0-9a-f]{64}', s):
            raise ValueError('bad sha256')
    if len({n.lower() for n in names}) != len(names):
        raise ValueError('duplicate names (case-insensitive)')
    lines = ['@echo off',
             f'echo S243 DELIVER BEGIN {len(indices)} file(s) to {target_dir}',
             f'set "T={target_dir}"',
             PS + ' "$ErrorActionPreference=\'Stop\'; $p=$env:T; while($p){ if(Test-Path -LiteralPath $p)'
             '{ $x=Get-Item -LiteralPath $p -Force; if(-not $x.PSIsContainer -or ($x.Attributes -band '
             '[IO.FileAttributes]::ReparsePoint)){ throw (\'unsafe path component \'+$p) } }; '
             '$p=Split-Path -Parent $p }; New-Item -ItemType Directory -Force -Path $env:T | Out-Null; '
             '\'S243 DIR READY \'+$env:T" 2>&1',
             'if errorlevel 1 (echo S243 DELIVER FAIL dir & exit /b 1)']
    for i, n, s in zip(indices, names, shas):
        lines.append(f'"%~d0\\NWGET243.EXE" {i} {s} "%T%\\{n}" 2>&1')
        lines.append(f'if errorlevel 1 (echo S243 DELIVER FAIL index {i} & exit /b 1)')
    arr_n = ','.join(f"'{n}'" for n in names)
    arr_h = ','.join(f"'{s}'" for s in shas)
    tail = ''
    if delete_after:
        tail = ('; if($bad -eq 0){ foreach($k in $n){ Remove-Item -LiteralPath (Join-Path $env:T $k) -Force; '
                '\'S243 SELFTEST REMOVED \'+$k } }')
    lines.append(PS + f' "$n=@({arr_n}); $h=@({arr_h}); $bad=0; for($i=0;$i -lt $n.Count;$i++){{ '
                 '$f=Join-Path $env:T $n[$i]; $g=(Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower(); '
                 '$l=(Get-Item -LiteralPath $f).Length; if($g -eq $h[$i]){ \'S243 READBACK MATCH \'+$n[$i]+\' \'+$l+\' \'+$g } '
                 'else { \'S243 READBACK MISMATCH \'+$n[$i]+\' \'+$l+\' \'+$g; $bad=1 } }' + tail + '; exit $bad" 2>&1')
    lines.append('if errorlevel 1 (echo S243 DELIVER FAIL readback & exit /b 1)')
    lines.append('echo S243 DELIVER COMPLETE')
    lines.append('exit /b 0')
    text = '\r\n'.join(lines) + '\r\n'
    data = text.encode('ascii')
    if len(data) > MAILBOX:
        raise ValueError(f'job is {len(data)} bytes > {MAILBOX}; deliver fewer files per job')
    return text


def main(argv):
    args = [a for a in argv if not a.startswith('--')]
    opts = [a for a in argv if a.startswith('--')]
    indices, delete_after = None, False
    for o in opts:
        if o.startswith('--index='):
            indices = [int(x) for x in o.split('=', 1)[1].split(',')]
        elif o == '--delete-after':
            delete_after = True
        else:
            sys.exit(f'unknown option {o}')
    if len(args) != 3:
        sys.exit('usage: make_job.py MANIFEST.json C:\\target\\dir OUT.cmd [--index=0,1] [--delete-after]')
    out = Path(args[2])
    if out.exists():
        sys.exit(f'refusing to overwrite {out}')
    text = build(json.loads(Path(args[0]).read_text()), args[1], indices, delete_after)
    out.write_bytes(text.encode('ascii'))
    print(f'wrote {out} ({len(text)} bytes)')


if __name__ == '__main__':
    main(sys.argv[1:])
