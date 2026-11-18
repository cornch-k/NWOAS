#!/usr/bin/env python3
"""Host test for the S262 SMBIOS Type 4 block (no device touched).

Compiles the real AppleDTLib.c and the NWOAS-S262 block of src/SmbiosInfoDxe.c
natively with small stubs, then runs them against saved J274 ADT blobs:
  1. unchanged blob            -> must report Apple M1 / 8 cores (4E+4P) / 3204 / 2988, all from ADT
  2. target-type J274 -> J313   -> must keep the template values (Not Specified / 6 / 3228 / 0)
  3. product-soc-name removed   -> must fall back to the J274 default name "Apple M1"
Usage: run_host_test.py ADT_BLOB [ADT_BLOB ...]
"""
import os, re, subprocess, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
SRC = HERE.parent / 'src/SmbiosInfoDxe.c'
DTLIB = Path('/Users/angigyeom/Desktop/DEV/NWOAS/apple_silicon_platforms_mu/Silicon/Apple/AppleSiliconPkg/Library/AppleDTLib/AppleDTLib.c')
DTH = DTLIB.parents[2] / 'Include/Library/AppleDTLib.h'

STUB = r'''
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
typedef char CHAR8; typedef uint8_t UINT8; typedef uint16_t UINT16; typedef uint32_t UINT32; typedef uint64_t UINT64;
typedef uintptr_t UINTN; typedef int BOOLEAN; typedef void VOID; typedef int EFI_STATUS;
#define TRUE 1
#define FALSE 0
#define STATIC static
#define CONST const
#define EFIAPI
#define EFI_SUCCESS 0
#define DEBUG_INFO 1
#define DEBUG_ERROR 2
#define DEBUG(x) dbg_print x
#define ASSERT(x) do { if (!(x)) { fprintf(stderr, "ASSERT\n"); exit(9); } } while (0)
static void dbg_print(int lvl, const char *fmt, ...) { if (lvl != DEBUG_ERROR) return; char f[512]; size_t j = 0;
  for (size_t i = 0; fmt[i] && j < sizeof(f) - 2; i++) { if (fmt[i] == '%' && fmt[i+1] == 'a') { f[j++] = '%'; f[j++] = 's'; i++; } else f[j++] = fmt[i]; }
  f[j] = 0; va_list ap; va_start(ap, fmt); vprintf(f, ap); va_end(ap); }
static UINT64 g_adt;
#define FixedPcdGet64(x) g_adt
#define PcdGet64(x) 0
static int AsciiStrCmp(const char *a, const char *b) { return strcmp(a, b); }
static int AsciiStrnCmp(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
static char *AsciiStrStr(const char *a, const char *b) { return strstr(a, b); }
static size_t AsciiStrLen(const char *a) { return strlen(a); }
static size_t AsciiStrSize(const char *a) { return strlen(a) + 1; }
static int CompareMem(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
static void *CopyMem(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
static int AsciiStrCpyS(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s); return 0; }
#define AsciiSPrint snprintf
static UINT32 ReadUnaligned32(const UINT32 *p) { UINT32 v; memcpy(&v, p, 4); return v; }
'''
TYPE4 = r'''
struct { UINT16 MaxSpeed, CurrentSpeed; UINT8 CoreCount, EnabledCoreCount, ThreadCount;
         UINT16 CoreCount2, EnabledCoreCount2, ThreadCount2, ThreadEnabled; } mProcessorInfoType4 = {3228, 0, 6, 6, 6, 0, 0, 0, 0};
CHAR8 *mProcessorInfoType4Strings[] = {"Apple", "Apple Inc.", "Not Specified", "Not Specified", NULL};
'''
MAIN = r'''
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  void *b = malloc(n); fread(b, 1, n, f); fclose(f);
  if (dt_check(b, n, NULL) != 0) { printf("BAD ADT\n"); return 2; }
  g_adt = (UINT64)(uintptr_t)b;
  if (NwoasS262IsJ274()) NwoasS262J274ProcessorInfo(); else printf("NOT_J274\n");
  printf("RESULT version=%s max=%u current=%u cores=%u/%u/%u cores2=%u/%u/%u/%u\n", mProcessorInfoType4Strings[2],
    mProcessorInfoType4.MaxSpeed, mProcessorInfoType4.CurrentSpeed, mProcessorInfoType4.CoreCount,
    mProcessorInfoType4.EnabledCoreCount, mProcessorInfoType4.ThreadCount, mProcessorInfoType4.CoreCount2,
    mProcessorInfoType4.EnabledCoreCount2, mProcessorInfoType4.ThreadCount2, mProcessorInfoType4.ThreadEnabled);
  return 0; }
'''

def build(tmp):
    s = SRC.read_text()
    block = s[s.index('/* NWOAS-S262-BEGIN'):s.index('/* NWOAS-S262-END */')]
    hdr = DTH.read_text().replace('#include <stddef.h>', '').replace('#include <stdint.h>', '')
    hdr = re.sub(r'__asm__\("[^"]*"\)', '', hdr)
    lib = DTLIB.read_text()
    lib = lib[lib.index('static void get_cells'):]
    lib = re.sub(r'__asm__\("[^"]*"\)', '', lib)
    c = tmp / 't.c'
    c.write_text(STUB + hdr + '\n' + lib + '\n' + TYPE4 + block + MAIN)
    exe = tmp / 't'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-pointer-sign',
                    '-funsigned-char', '-o', str(exe), str(c)], check=True)
    return exe

def patch(blob, old, new):
    assert blob.count(old) >= 1, old
    return blob.replace(old, new)

def run(exe, blob, tmp, name):
    p = tmp / name
    p.write_bytes(blob)
    r = subprocess.run([str(exe), str(p)], capture_output=True, text=True, check=True)
    return r.stdout

ok = True
with tempfile.TemporaryDirectory() as t:
    tmp = Path(t)
    exe = build(tmp)
    for f in sys.argv[1:]:
        blob = Path(f).read_bytes()
        cases = [
            ('as-is', blob, 'RESULT version=Apple M1 max=3204 current=2988 cores=8/8/8 cores2=8/8/8/8', 'version="Apple M1"(adt) cores=8(adt) E=4 P=4 max=3204(adt) current=2988(adt)'),
            ('j313', patch(blob, b'target-type' + b'\0' * (32 - 11) + b'\x05\x00\x00\x00J274\0',
                           b'target-type' + b'\0' * (32 - 11) + b'\x05\x00\x00\x00J313\0'),
             'RESULT version=Not Specified max=3228 current=0 cores=6/6/6 cores2=0/0/0/0', 'NOT_J274'),
            ('no-soc-name', patch(blob, b'product-soc-name', b'product-soc-nam_'),
             'RESULT version=Apple M1 max=3204 current=2988 cores=8/8/8 cores2=8/8/8/8', 'version="Apple M1"(default)'),
        ]
        for name, b, want, want2 in cases:
            out = run(exe, b, tmp, name + '.adt')
            good = want in out and want2 in out
            ok &= good
            print(('PASS' if good else 'FAIL'), Path(f).name, name)
            print('   ', out.strip().replace('\n', '\n    '))
print('ALL PASS' if ok else 'SOME FAIL')
sys.exit(0 if ok else 1)
