#!/usr/bin/env python3
"""Fault injection of EOF repair ordering using host API stubs; no disks accessed."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent
s=(root/'nwtrim.c').read_text().replace('typedef unsigned long U32;','typedef unsigned int U32;').replace('typedef long S32;','typedef int S32;').replace('#define IMP __declspec(dllimport)','#define IMP')
stubs=r"""
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
static jmp_buf jump;static int test,phase,opens,eof_calls,flush_calls;static U64 pos,length,hashed;static U32 result;
H GetStdHandle(U32 v){(void)v;return (H)1;}
int WriteFile(H h,const void *b,U32 n,U32 *w,void *o){(void)h;(void)b;(void)o;*w=n;return 1;}
int ReadFile(H h,void *b,U32 n,U32 *got,void *o){(void)h;(void)b;(void)o;if(test==4)return 0;*got=length-pos<n?(U32)(length-pos):n;pos+=*got;return 1;}
H CreateFileW(const W *p,U32 a,U32 sh,void *sa,U32 d,U32 f,H t){(void)p;(void)sh;(void)sa;(void)d;(void)t;
 opens++;if(opens==1&&a!=0x80000000)abort();if(opens==2&&(a!=0xc0000000||f&0x20000000||hashed!=564691107ULL))abort();return (H)1;}
int GetFileSizeEx(H h,long long *s){(void)h;*s=(long long)length;return 1;}
int CloseHandle(H h){(void)h;return 1;}
W *GetCommandLineW(void){static W out[256];char line[256];snprintf(line,sizeof(line),"g C:\\S117SRC\\%s %s direct",test==8?"other.swm":"install2.swm",test==9?"bad":"62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963");for(unsigned i=0;i<=strlen(line);i++)out[i]=(unsigned char)line[i];return out;}
U32 GetLastError(void){return 5;} U64 GetTickCount64(void){return 0;}
H GetProcessHeap(void){return (H)1;} H HeapAlloc(H h,U32 f,U64 n){(void)h;(void)f;return malloc((size_t)n);}
int HeapFree(H h,U32 f,H p){(void)h;(void)f;free(p);return 1;}
void ExitProcess(U32 r){result=r;longjmp(jump,1);}
H VirtualAlloc(H a,U64 n,U32 t,U32 p){(void)a;(void)t;(void)p;return aligned_alloc(65536,(size_t)n);}
int VirtualFree(H p,U64 n,U32 f){(void)n;(void)f;free(p);return 1;}
int GetDiskFreeSpaceW(const W *r,U32 *s,U32 *b,U32 *f,U32 *t){(void)r;*s=1;*b=4096;*f=1;*t=1;return 1;}
S32 BCryptOpenAlgorithmProvider(H *h,const W *a,const W *b,U32 f){(void)a;(void)b;(void)f;*h=(H)1;return 0;}
S32 BCryptGetProperty(H h,const W *n,U8 *b,U32 z,U32 *got,U32 f){(void)h;(void)n;(void)z;(void)f;*(U32 *)b=16;*got=4;return 0;}
S32 BCryptCreateHash(H a,H *h,U8 *o,U32 n,U8 *s,U32 z,U32 f){(void)a;(void)o;(void)n;(void)s;(void)z;(void)f;phase++;*h=(H)1;return 0;}
S32 BCryptHashData(H h,U8 *b,U32 n,U32 f){(void)h;(void)b;(void)f;if(phase==2)hashed+=n;return 0;}
S32 BCryptFinishHash(H h,U8 *b,U32 n,U32 f){(void)h;(void)n;(void)f;if(phase==1){memcpy(b,abc_digest,32);return 0;}
 const char *hex="62512ee80dafe30ad5bb7db430395eeaa9080473e43c29ea327b5eaf5bfe1963";for(int i=0;i<32;i++){unsigned x;sscanf(hex+i*2,"%2x",&x);b[i]=(U8)x;}if(test==2)b[0]^=1;return 0;}
S32 BCryptDestroyHash(H h){(void)h;return 0;} S32 BCryptCloseAlgorithmProvider(H h,U32 f){(void)h;(void)f;return 0;}
int SetFilePointerEx(H h,long long n,long long *o,U32 f){(void)h;(void)o;(void)f;if(test==5)return 0;pos=n;return 1;}
int SetEndOfFile(H h){(void)h;eof_calls++;if(test==6)return 0;length=pos;return 1;}
int FlushFileBuffers(H h){(void)h;flush_calls++;return test!=7;}
int main(void){for(test=0;test<10;test++){phase=opens=eof_calls=flush_calls=0;pos=hashed=0;length=test==1?564691107:test==3?564695041:564695040;
 if(!setjmp(jump))mainCRTStartup();int expected=test<2?0:test==2?1:2;
 if(result!=(U32)expected||(test>=2&&test!=6&&test!=7&&eof_calls)||(test<2&&(eof_calls!=1||flush_calls!=1||length!=564691107||hashed!=564691107))){printf("FAIL case%d rc%u eof%d flush%d hashed%llu\n",test,result,eof_calls,flush_calls,hashed);return 1;}}
 puts("PASS 10 EOF repair ordering/failure cases; digest must match before mutation, unaligned EOF uses buffered handle");return 0;}
"""
with tempfile.TemporaryDirectory(prefix='nwoas-s120-test-') as d:
 p=Path(d);(p/'test.c').write_text(s+'\n'+stubs)
 subprocess.run(['/usr/bin/clang','-O0',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
