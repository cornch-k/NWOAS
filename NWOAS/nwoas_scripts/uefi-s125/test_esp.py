#!/usr/bin/env python3
"""Host-side fault injection of the guard with stubbed read-only Windows APIs.
Does not execute the ARM64 PE or access disks; real WinPE validation is separate.
"""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parent
source=(root/'nwesp.c').read_text().replace('typedef unsigned long U32;', 'typedef unsigned int U32;').replace('#define IMP __declspec(dllimport)', '#define IMP')
stubs=r"""
#include <setjmp.h>
#include <stdio.h>
static jmp_buf done;static int scenario;static U32 result;static int rwopens,flushes;
H GetStdHandle(U32 a){(void)a;return (H)1;}
int WriteFile(H h,const void *b,U32 n,U32 *w,void *o){(void)h;(void)b;(void)o;*w=n;return 1;}
W *GetCommandLineW(void){static W a[20];const char *s=scenario>=12?"g S: flush":"g S: ready";for(int i=0;;i++){a[i]=s[i];if(!s[i])break;}return a;}
void ExitProcess(U32 rc){result=rc;longjmp(done,1);}
U32 GetLastError(void){return 0;}
int GetVolumeInformationW(const W *r,W *l,U32 n,U32 *s,U32 *m,U32 *f,W *fs,U32 fn){
 (void)r;(void)n;(void)m;(void)f;(void)fn;const char *name="Windows",*format=scenario==9?"NTFS":"FAT32";
 for(int i=0;i<8;i++)l[i]=name[i];for(int i=0;format[i];i++)fs[i]=format[i];fs[scenario==9?4:5]=0;
 *s=scenario==2?1:0x1ceae590;return scenario!=10;}
int GetDiskFreeSpaceExW(const W *r,U64 *a,U64 *t,U64 *f){(void)r;*a=*f=scenario==8?104857599ULL:104857600ULL;*t=scenario==4?314572801ULL:313524224ULL;return 1;}
H CreateFileW(const W *n,U32 a,U32 s,void *p,U32 c,U32 f,H t){(void)n;(void)s;(void)p;(void)c;(void)f;(void)t;if(a){rwopens++;if(a!=0xc0000000)result=999;}return (H)1;}
static void put(U8 *p,U64 v,int n){for(int i=0;i<n;i++){p[i]=v&255;v>>=8;}}
int DeviceIoControl(H h,U32 c,void *i,U32 ni,void *o,U32 no,U32 *got,void *ov){
 (void)h;(void)i;(void)ni;(void)no;(void)ov;U8 *p=o;
 if(c!=0x70048)return 0;put(p,scenario==6?0:1,4);put(p+8,scenario==3?0:220524969984ULL,8);
 put(p+16,scenario==11?0:314572800ULL,8);put(p+24,scenario==5?2:3,4);const U8 guid[16]={0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0,0xa0,0xc9,0x3e,0xc9,0x3b};for(int j=0;j<16;j++)p[32+j]=guid[j];if(scenario==2||scenario==13)p[32]^=1;*got=144;return scenario!=7;}
int CloseHandle(H h){(void)h;return 1;}
int FlushFileBuffers(H h){(void)h;flushes++;return scenario!=14;}
int main(void){for(scenario=0;scenario<15;scenario++){result=999;rwopens=flushes=0;if(!setjmp(done))mainCRTStartup();
 int expect=(scenario<2||scenario==12)?0:2;if(result!=(U32)expect||rwopens!=((scenario==12||scenario==14)?1:0)||flushes!=rwopens){printf("FAIL scenario %d rc%u expected%d\n",scenario,result,expect);return 1;}}
 puts("PASS 15 ESP guard cases including wrong GPT type, offset, size, filesystem, space and gated flush/failure");return 0;}
"""
with tempfile.TemporaryDirectory(prefix='nwoas-s118-guard-') as temp:
 t=Path(temp);(t/'test.c').write_text(source+'\n'+stubs)
 subprocess.run(['/usr/bin/clang','-O0',str(t/'test.c'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
