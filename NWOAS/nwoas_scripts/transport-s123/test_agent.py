"""Native API stubs verify the actual C client's disk gating and mailbox ACKs."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent
s=(root/'nwagent.c').read_text().replace('typedef unsigned long U32;','typedef unsigned int U32;').replace('#define IMP __declspec(dllimport)','#define IMP')
stub=r'''
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
static jmp_buf jump;static int scenario,writes,rwopens;static U64 position;static U32 result,acked;static U8 session[16];
H GetStdHandle(U32 a){(void)a;return (H)1;}
int WriteFile(H h,const void *b,U32 n,U32 *got,void *o){(void)o;*got=n;if(h==(H)1)return 1;
 if(scenario<3||position!=128ULL*4096||n!=4096||((U64)b&4095))abort();
 const U8 *p=b;if(!equal(p,magic,16)||!equal(p+16,session,16))abort();writes++;acked=*(const U32 *)(p+48);return 1;}
int ReadFile(H h,void *b,U32 n,U32 *got,void *o){(void)h;(void)o;if(n!=4096||((U64)b&4095))abort();zero(b,n);*got=n;U8 *p=b;
 if(position==0){*(U32 *)(p+440)=scenario==1?0:0x53313233;p[510]=0x55;p[511]=0xaa;}
 else if(position==128ULL*4096){copy(p,magic,16);if(scenario==2)p[0]^=1;copy(p+16,session,16);*(U32 *)(p+48)=scenario==4?0:acked;}
 else abort();return 1;}
H CreateFileW(const W *p,U32 access,U32 share,void *sa,U32 creation,U32 flags,H t){(void)share;(void)sa;(void)creation;(void)t;
 if(p[17]!='0')return (H)-1;if(flags!=0x20000000)abort();if(access==0xc0000000){rwopens++;if(scenario<3)abort();}return (H)2;}
int SetFilePointerEx(H h,long long at,long long *out,U32 method){(void)h;(void)out;(void)method;position=at;return 1;}
int DeviceIoControl(H h,U32 code,void *in,U32 ins,void *out,U32 n,U32 *got,void *ov){(void)h;(void)in;(void)ins;(void)ov;if(code!=0x7405c||n!=8)abort();*(U64 *)out=scenario==0?251000193024ULL:8448ULL*4096;*got=8;return 1;}
int CloseHandle(H h){(void)h;return 1;}
H VirtualAlloc(H h,U64 n,U32 a,U32 b){(void)h;(void)a;(void)b;return aligned_alloc(65536,n);}
void ExitProcess(U32 rc){result=rc;longjmp(jump,1);}
void Sleep(U32 n){(void)n;result=0;longjmp(jump,1);}
U32 GetLastError(void){return 5;}
int CreateProcessW(const W *a,W *c,void *p,void *t,int i,U32 f,void *e,const W *d,void *s,void *pi){(void)a;(void)c;(void)p;(void)t;(void)i;(void)f;(void)e;(void)d;(void)s;(void)pi;abort();}
U32 WaitForSingleObject(H h,U32 n){(void)h;(void)n;abort();}
int GetExitCodeProcess(H h,U32 *r){(void)h;(void)r;abort();}
int CreatePipe(H *r,H *w,void *s,U32 n){(void)r;(void)w;(void)s;(void)n;abort();}
int SetHandleInformation(H h,U32 a,U32 b){(void)h;(void)a;(void)b;abort();}
int PeekNamedPipe(H h,void *a,U32 n,U32 *b,U32 *c,U32 *d){(void)h;(void)a;(void)n;(void)b;(void)c;(void)d;abort();}
U32 GetEnvironmentVariableW(const W *name,W *b,U32 n){(void)name;(void)n;b[0]='X';b[1]=':';b[2]=0;return 2;}
int main(void){
 for(scenario=0;scenario<5;scenario++){
  writes=rwopens=0;acked=sequence=0;disk=(H)-1;position=0;memset(session,0x53,16);
  if(!setjmp(jump))mainCRTStartup();
  if(result!=(scenario==3?0u:2u)||writes!=(scenario>=3?1:0)||rwopens!=(scenario>=3?1:0)){printf("FAIL scenario %d rc%u writes%d opens%d\n",scenario,result,writes,rwopens);return 1;}
  free(buf);free(sendbuf);
 }
 if(crc((const U8 *)"123456789",9)!=0xcbf43926)abort();
 puts("PASS C client: wrong disk size / signature / mailbox cause zero writes; valid handshake; bad ACK stops; CRC32 reference");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='nwoas-s123-client-') as d:
    p=Path(d);(p/'test.c').write_text(s+'\n'+stub)
    subprocess.run(['/usr/bin/clang','-fshort-wchar','-O0',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
