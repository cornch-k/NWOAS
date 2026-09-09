from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parent
s=(r/'nwos.c').read_text().replace('typedef unsigned long U32;','typedef unsigned int U32;').replace('#define IMP __declspec(dllimport)','#define IMP')
stub=r'''
#include <assert.h>
#include <string.h>
static unsigned scenario,closes,writes,processes;
static U64 position;static U32 ack,kind,rcode;
static int eq(const W*a,const W*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
H GetStdHandle(U32 x){(void)x;return (H)40;}
int WriteFile(H h,const void*b,U32 n,U32*w,void*x){(void)x;*w=n;
 if(h==(H)6){assert(n==5&&!memcmp(b,"ver\r\n",5));writes++;return 1;}
 if(h==(H)50){const U8*p=b;assert(n==4096);ack=*(const U32*)(p+48);kind=*(const U32*)(p+36);rcode=*(const U32*)(p+52);return 1;}
 return 1;}
int ReadFile(H h,void*b,U32 n,U32*got,void*x){(void)x;assert(h==(H)50&&n==4096&&position==128*4096);zero(b,n);copy(b,magic,16);copy((U8*)b+16,token,16);*(U32*)((U8*)b+48)=ack;*got=n;return 1;}
H CreateFileW(const W*p,U32 a,U32 sh,void*sec,U32 disp,U32 flags,H t){(void)flags;(void)t;
 if(p[0]=='\\'&&p[1]=='\\'&&p[2]=='.'&&p[3]=='\\') {assert(!a&&sh==3&&!sec&&disp==3);return p[4]=='G'||(scenario==4&&p[4]=='H')?(H)(U64)p[4]:(H)-1;}
 if(eq(p,L"G:\\NWOAS177.CMD")){assert(a==0x40000000&&!sh&&!sec&&disp==2);return (H)6;}
 if(eq(p,L"NUL")){assert(a==0x80000000&&sh==3&&sec&&((SA*)sec)->inherit==1&&disp==3);return scenario==5?(H)-1:(H)5;}
 assert(0);return (H)-1;}
int SetFilePointerEx(H h,long long a,long long*b,U32 m){(void)b;assert(h==(H)50&&!m);position=a;return 1;}
int DeviceIoControl(H h,U32 code,void*i,U32 ni,void*o,U32 no,U32*got,void*x){(void)i;(void)ni;(void)x;assert((h==(H)'G'||h==(H)'H')&&code==0x560000&&no==32);zero(o,32);U32 *p=o;p[0]=1;p[2]=scenario==1?3:2;*(U64*)((U8*)o+16)=1048576;*(U64*)((U8*)o+24)=scenario==2?4096:33554432;*got=32;return 1;}
int CloseHandle(H h){assert(h!=(H)-1&&h);closes++;return 1;}
H VirtualAlloc(H a,U64 n,U32 f,U32 p){(void)a;(void)n;(void)f;(void)p;return 0;}
void ExitProcess(U32 n){(void)n;assert(0);}
void Sleep(U32 n){(void)n;assert(0);}
U32 GetLastError(void){return 5;}
int CreateProcessW(const W*a,W*c,void*x,void*y,int inherit,U32 flags,void*e,const W*d,void*sv,void*pv){
 SI*si=sv;PI*pi=pv;assert(eq(a,L"C:\\Windows\\System32\\cmd.exe")&&eq(c,L"cmd.exe /d /c G:\\NWOAS177.CMD")&&eq(d,L"C:\\"));assert(!x&&!y&&inherit&&flags==0x08000000&&!e);assert(si->input==(H)5&&si->output==(H)4&&si->error==(H)4);processes++;if(scenario==6)return 0;pi->process=(H)7;pi->thread=(H)8;return 1;}
U32 WaitForSingleObject(H h,U32 n){assert(h==(H)7&&!n);return 0;}
int GetExitCodeProcess(H h,U32*c){assert(h==(H)7);*c=0;return 1;}
int CreatePipe(H*r,H*w,void*s,U32 n){assert(((SA*)s)->inherit==1&&n==16384);*r=(H)3;*w=(H)4;return 1;}
int SetHandleInformation(H h,U32 mask,U32 f){assert(h==(H)3&&mask==1&&!f);return 1;}
int PeekNamedPipe(H h,void*b,U32 n,U32*x,U32*av,U32*y){assert(h==(H)3&&!b&&!n&&!x&&!y);*av=0;return 1;}
U32 GetEnvironmentVariableW(const W*a,W*b,U32 n){(void)a;(void)b;(void)n;return 0;}
int GetVolumeInformationW(const W*p,W*n,U32 z,U32*serial,U32*a,U32*b,W*c,U32 d){assert((p[0]=='G'||p[0]=='H')&&p[1]==':'&&p[2]=='\\'&&!p[3]);assert(!n&&!z&&!a&&!b&&!c&&!d);*serial=scenario==3?123:0x53313233;return 1;}
int main(void){static U8 a[65536],b[65536];buf=a;sendbuf=b;disk=(H)50;link_disk_number=2;
 for(scenario=0;scenario<7;scenario++){closes=writes=processes=0;sequence=ack=kind=rcode=0;zero(buf,65536);copy(buf+64,"ver\r\n",5);assert(run(1,5));assert(kind==3);assert((rcode==0)==(scenario==0));assert(writes==(scenario==0||scenario>=5));assert(processes==(scenario==0||scenario==6));}
 unsigned char ext[32]={1};ext[8]=2;ext[18]=16;ext[27]=2;assert(s177_extent_matches(ext,32,2));assert(!s177_extent_matches(ext,31,2));assert(!s177_extent_matches(0,32,2));
 return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);c=p/'test.c';c.write_text(s+'\n'+stub)
 for name,flags in [('plain',[]),('san',['-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
  subprocess.run(['cc','-std=c11','-fshort-wchar','-O1','-Wall','-Wextra','-Werror','-I',str(r),*flags,str(c),'-o',str(p/name)],check=True)
  subprocess.run([str(p/name)],check=True)
print('PASS actual worker run/scratch: seven scenarios, exact authenticated disk/extent/serial, ambiguous refusal, RAM-only script, NUL inherited stdin, no-window CMD, exit propagation; plain+ASan/UBSan')
