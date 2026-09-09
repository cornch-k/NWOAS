#!/usr/bin/env python3
from pathlib import Path
import subprocess
O=Path(__file__).resolve().parent;R=O.parent
s=(R/'NwoasHideHighRamDxe-s167.c').read_text(); start=s.index('EFI_STATUS\nEFIAPI\nNwoasWrappedGetMemoryMap');end=s.index('\n#endif // NWOAS_HIDE_HIGH_RAM',start)
function=s[start:end]
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <Uefi.h>
#include <Library/NwoasSplitMemoryMap.h>
#define DEBUG(x) ((void)0)
#define NWOAS_HIDE_BASE 0x100000000ULL
#define NWOAS_MIN_LOW_FREE_PAGES 0x80000ULL
#define NWOAS_HIDE_FULL 0
static BOOLEAN mWrapLogged;
#if NWOAS_HIDE_KEEP_HIGH_BYTES
static UINT64 mKeepHighStart=0x83B7E0000ULL,mKeepHighEnd=0x93B7E0000ULL;
#endif
static BOOLEAN NwoasTypeIsHideable(UINT32 t) { return t==EfiConventionalMemory; }
typedef struct { EFI_MEMORY_DESCRIPTOR d; UINT64 extension; } Ext;
static Ext original[3];
static EFI_STATUS mOrigGetMemoryMap(UINTN *n,EFI_MEMORY_DESCRIPTOR *m,UINTN *key,UINTN *ds,UINT32 *ver) {
  *ds=sizeof(Ext); *ver=1; *key=0x1234;
  if (!m || *n<sizeof(original)) {*n=sizeof(original);return EFI_BUFFER_TOO_SMALL;}
  memcpy(m,original,sizeof(original));*n=sizeof(original);return EFI_SUCCESS;
}
'''
test=r'''
int main(void) {
 Ext out[5], snapshot[5]; UINTN n,key,ds; UINT32 v; EFI_STATUS st;
 original[0]=(Ext){{EfiConventionalMemory,0,0,0x100000,EFI_MEMORY_WB},0xaabb};
 original[1]=(Ext){{EfiConventionalMemory,0x83B7E0000ULL,0,0x180000,EFI_MEMORY_WB},0xccdd};
 original[2]=(Ext){{EfiACPIReclaimMemory,0xA00000000ULL,0,4,EFI_MEMORY_WB},0xeeff};
 n=0;st=NwoasWrappedGetMemoryMap(&n,NULL,&key,&ds,&v);
 assert(st==EFI_BUFFER_TOO_SMALL && key==0x1234 && ds==sizeof(Ext));
 assert(n==sizeof(original)+(NWOAS_HIDE_KEEP_HIGH_BYTES?sizeof(Ext):0));
 memset(out,0xa5,sizeof(out));n=sizeof(original);
 st=NwoasWrappedGetMemoryMap(&n,(void*)out,&key,&ds,&v);
 if(NWOAS_HIDE_KEEP_HIGH_BYTES) {
  assert(st==EFI_BUFFER_TOO_SMALL && n==4*sizeof(Ext));
  assert(memcmp(out,original,sizeof(original))==0);
 } else { assert(st==EFI_SUCCESS && out[1].d.Type==EfiReservedMemoryType); }
 memset(out,0xa5,sizeof(out));n=sizeof(out);
 st=NwoasWrappedGetMemoryMap(&n,(void*)out,&key,&ds,&v);
 assert(st==EFI_SUCCESS && key==0x1234 && v==1);
 assert(memcmp(&out[0],&original[0],sizeof(Ext))==0);
 if(NWOAS_HIDE_KEEP_HIGH_BYTES) {
  assert(n==4*sizeof(Ext));
  assert(out[1].d.Type==EfiConventionalMemory && out[1].d.NumberOfPages==0x100000);
  assert(out[2].d.Type==EfiReservedMemoryType && out[2].d.PhysicalStart==0x93B7E0000ULL && out[2].d.NumberOfPages==0x80000);
  assert(out[1].extension==0xccdd && out[2].extension==0xccdd);
  assert(memcmp(&out[3],&original[2],sizeof(Ext))==0);
 } else {
  assert(n==sizeof(original)); assert(out[1].d.NumberOfPages==0x180000);
  assert(memcmp(&out[2],&original[2],sizeof(Ext))==0);
 }
 // Insufficient low DMA pool: preserve full original map, including high RAM.
 original[0].d.NumberOfPages=0x7ffff;
 memset(out,0xa5,sizeof(out));memcpy(snapshot,out,sizeof(out));memcpy(snapshot,original,sizeof(original));n=sizeof(out);
 st=NwoasWrappedGetMemoryMap(&n,(void*)out,&key,&ds,&v);
 assert(st==EFI_SUCCESS && n==sizeof(original) && memcmp(out,snapshot,sizeof(out))==0);
 puts("PASS: actual wrapper, probe/retry/split/MapKey/low-DMA guard");
}
'''
(O/'test.c').write_text(prefix+function+test)
for mode in (0,0x100000000):
 exe=O/f'test-{mode}'
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-but-set-variable','-fsanitize=address,undefined',f'-DNWOAS_HIDE_KEEP_HIGH_BYTES={mode}ULL','-I'+str(R/'test/stub'),'-I'+str(R/'Include'),str(O/'test.c'),str(R/'test/stub/BaseMemoryLibStub.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
