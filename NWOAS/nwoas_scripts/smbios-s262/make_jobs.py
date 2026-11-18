#!/usr/bin/env python3
"""Write jobs/S2 and jobs/S3 from ps/*.ps1: the PowerShell source is gzip+base64 packed and unpacked by a one-line
-Command stub, so the CMD stays ASCII-only and <= 4032 bytes with CRLF (queue_job.py mailbox limit).
Run on the Mac: python3 -I make_jobs.py"""
import base64, gzip
from pathlib import Path
H = Path(__file__).resolve().parent
for name, title in (('S2-smbios-type4-raw', 'SMBIOS RAW TYPE 4'), ('S3-clock-sources', 'CLOCK SOURCES')):
    ps = (H / 'ps' / (name + '.ps1')).read_text()
    ps.encode('ascii')
    b64 = base64.b64encode(gzip.compress(ps.encode('ascii'), 9, mtime=0)).decode('ascii')
    stub = ("$z=New-Object IO.Compression.GZipStream([IO.MemoryStream][Convert]::FromBase64String('%s'),"
            "[IO.Compression.CompressionMode]::Decompress); $t=(New-Object IO.StreamReader($z)).ReadToEnd(); "
            "Invoke-Expression $t") % b64
    tag = name.split('-')[0]
    cmd = ('@echo off\r\nrem S262 %s: source nwoas_scripts/smbios-s262/ps/%s.ps1 (packed by make_jobs.py). Read-only.\r\n'
           'echo S262 %s %s\r\npowershell -NoProfile -ExecutionPolicy Bypass -Command "%s"\r\n'
           'echo S262 %s DONE\r\nexit /b 0\r\n') % (tag, name, tag, title, stub, tag)
    n = len(cmd.encode('ascii'))
    assert n <= 4032, (name, n)
    assert '%' not in cmd and '!' not in cmd and '"' not in stub
    (H / 'jobs' / (name + '.cmd')).write_bytes(cmd.replace('\r\n', '\n').encode('ascii'))
    # round trip check
    assert gzip.decompress(base64.b64decode(b64)).decode('ascii') == ps
    print(name, n, 'bytes (CRLF)')
