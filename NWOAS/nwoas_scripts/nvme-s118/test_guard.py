#!/usr/bin/env python3
"""Host-side fault injection of the guard with stubbed read-only Windows APIs.
Does not execute the ARM64 PE or access disks; real WinPE validation is separate.
"""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parent
source=(root/'nwguard.c').read_text().replace('typedef unsigned long U32;', 'typedef unsigned int U32;').replace('#define IMP __declspec(dllimport)', '#define IMP')
stubs=r"""
#include <setjmp.h>
#include <stdio.h>
static jmp_buf done;static int scenario;static U32 result;
H GetStdHandle(U32 a){(void)a;return (H)1;}
int WriteFile(H h,const void *b,U32 n,U32 *w,void *o){(void)h;(void)b;(void)o;*w=n;return 1;}
W *GetCommandLineW(void){static W a[]={'g',' ','C',':',' ','r','e','a','d','y',0};return a;}
void ExitProcess(U32 rc){result=rc;longjmp(done,1);}
U32 GetLastError(void){return 0;}
int GetVolumeInformationW(const W *r,W *l,U32 n,U32 *s,U32 *m,U32 *f,W *fs,U32 fn){
 (void)r;(void)n;(void)m;(void)f;(void)fn;const char *name="Windows",*format=scenario==9?"FAT32":"NTFS";
 for(int i=0;i<8;i++)l[i]=name[i];for(int i=0;format[i];i++)fs[i]=format[i];fs[scenario==9?5:4]=0;
 *s=scenario==2?1:0x1ceae590;return scenario!=10;}
int GetDiskFreeSpaceExW(const W *r,U64 *a,U64 *t,U64 *f){(void)r;*a=*f=scenario==8?19838091489ULL:19838091490ULL;*t=scenario==4?24774701055ULL:24774701056ULL;return 1;}
H CreateFileW(const W *n,U32 a,U32 s,void *p,U32 c,U32 f,H t){(void)n;(void)s;(void)p;(void)c;(void)f;(void)t;if(a)result=999;return (H)1;}
static void put(U8 *p,U64 v,int n){for(int i=0;i<n;i++){p[i]=v&255;v>>=8;}}
int DeviceIoControl(H h,U32 c,void *i,U32 ni,void *o,U32 no,U32 *got,void *ov){
 (void)h;(void)i;(void)ni;(void)no;(void)ov;U8 *p=o;
 if(c!=0x70048)return 0;put(p,scenario==6?0:1,4);put(p+8,scenario==3?0:220856320000ULL,8);
 put(p+16,scenario==11?0:24774705152ULL,8);put(p+24,scenario==5?2:5,4);*got=144;return scenario!=7;}
int CloseHandle(H h){(void)h;return 1;}
int main(void){for(scenario=0;scenario<12;scenario++){result=999;if(!setjmp(done))mainCRTStartup();
 int expect=scenario<2?0:2;if(result!=(U32)expect){printf("FAIL scenario %d rc%u expected%d\n",scenario,result,expect);return 1;}}
 puts("PASS 12 guard cases: valid, exact capacity boundary, wrong serial/offset/size/partition/style, IOCTL error, low space, filesystem/query failure, wrong partition length");return 0;}
"""
with tempfile.TemporaryDirectory(prefix='nwoas-s118-guard-') as temp:
 t=Path(temp);(t/'test.c').write_text(source+'\n'+stubs)
 subprocess.run(['/usr/bin/clang','-O0',str(t/'test.c'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
