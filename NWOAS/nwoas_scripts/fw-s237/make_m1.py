#!/usr/bin/env python3
"""Regenerate M1-memmap.cmd from M1-memmap.ps1 (PowerShell -EncodedCommand = base64 of UTF-16LE)."""
import base64
from pathlib import Path
D = Path(__file__).resolve().parent
ps = (D / 'M1-memmap.ps1').read_text()
enc = base64.b64encode(ps.encode('utf-16-le')).decode()
cmd = ('@echo off\r\n'
       'rem S237 M1: Windows physical-memory runs vs PCIe bridge window 0xC0000000-0xC00FFFFF (read-only).\r\n'
       'rem The encoded command is exactly fw-s237/M1-memmap.ps1 (UTF-16LE, base64); regenerate with make_m1.py.\r\n'
       'echo S237 M1 MEMMAP START\r\n'
       'bcdedit /enum {badmemory}\r\n'
       'powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand ' + enc + '\r\n'
       'echo S237 M1 RC=%ERRORLEVEL%\r\n'
       'echo S237 M1 COMPLETE\r\n'
       'exit /b 0\r\n')
out = D / 'M1-memmap.cmd'
if out.exists() and out.read_bytes() == cmd.encode('ascii'):
    print('unchanged')
else:
    out.write_bytes(cmd.encode('ascii')); print('written')
