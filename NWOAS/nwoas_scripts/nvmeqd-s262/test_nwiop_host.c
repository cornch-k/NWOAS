/* S262S host test for nwiop.c (stubs the Windows/ntdll calls; macOS clang):
 *   clang -std=gnu11 -O1 -w "-D__declspec(x)=" -Dmemset=nw_memset -c nwiop.c -o /tmp/nwiop.o &&
 *   clang -O1 -w test_nwiop_host.c /tmp/nwiop.o -o /tmp/t && /tmp/t     -> HOSTTEST PASS */
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
typedef unsigned short U16; typedef unsigned int U32; typedef long NTSTATUS; typedef void *H;
static U16 cmd[256]; static jmp_buf jb; static U32 exitcode; static char created[256]; static U32 cflags;
static U32 pprio=1, tprio=1, cpp=1, ctp=1; int deny_thread=0;
void tow(const char *s){int i=0;for(;s[i];i++)cmd[i]=(U16)s[i];cmd[i]=0;}
H GetStdHandle(U32 x){(void)x;return 0;}
int WriteFile(H h,const void*b,U32 n,U32*w,void*o){(void)h;(void)o;fwrite(b,1,n,stdout);*w=n;return 1;}
void ExitProcess(U32 c){exitcode=c;longjmp(jb,1);}
U16 *GetCommandLineW(void){return cmd;}
U32 GetLastError(void){return 5;}
H GetCurrentProcess(void){return (H)1;} H GetCurrentThread(void){return (H)2;}
U32 GetPriorityClass(H h){return h==(H)1?0x4000:0x20;}
typedef struct { H process, thread; U32 pid, tid; } PI;
int CreateProcessW(const U16*a,U16*c,void*x,void*y,int inh,U32 fl,void*e,const U16*d,void*si,void*pi){
 (void)a;(void)x;(void)y;(void)e;(void)d;(void)si;int i=0;for(;c[i];i++)created[i]=(char)c[i];created[i]=0;cflags=fl;(void)inh;
 PI*p=pi;p->process=(H)3;p->thread=(H)4;p->pid=77;return 1;}
U32 ResumeThread(H h){(void)h;return 1;} U32 WaitForSingleObject(H h,U32 t){(void)h;(void)t;return 0;}
int GetExitCodeProcess(H h,U32*c){(void)h;*c=0;return 1;} int TerminateProcess(H h,U32 c){(void)h;(void)c;return 1;} int CloseHandle(H h){(void)h;return 1;}
NTSTATUS NtQueryInformationProcess(H h,U32 k,void*v,U32 n,U32*g){(void)n;(void)g;if(k!=33)return -1;*(U32*)v=h==(H)1?pprio:cpp;return 0;}
NTSTATUS NtSetInformationProcess(H h,U32 k,void*v,U32 n){(void)h;(void)n;if(k!=33)return -1;cpp=*(U32*)v;return 0;}
NTSTATUS NtQueryInformationThread(H h,U32 k,void*v,U32 n,U32*g){(void)n;(void)g;if(k!=22)return -1;*(U32*)v=h==(H)2?tprio:ctp;return 0;}
NTSTATUS NtSetInformationThread(H h,U32 k,void*v,U32 n){(void)h;(void)n;if(k!=22)return -1;if(deny_thread)return (NTSTATUS)0xC0000061;ctp=*(U32*)v;return 0;}
void mainCRTStartup(void);
static int run(const char*s){tow(s);created[0]=0;cpp=ctp=1;if(!setjmp(jb))mainCRTStartup();printf("-> exit=%u created=[%s] flags=%#x\n",exitcode,created,cflags);return exitcode;}
int main(void){int bad=0;
 bad|=run("\"C:\\NWOAS\\s262\\NWIOP.EXE\" 2 \"C:\\NWOAS\\s260b\\NWPWR.EXE\" io 10")!=0||strcmp(created,"\"C:\\NWOAS\\s260b\\NWPWR.EXE\" io 10")||cflags!=0x24||ctp!=2||cpp!=2;
 bad|=run("NWIOP.EXE 2 X.EXE a")!=0||strcmp(created,"X.EXE a");
 bad|=run("NWIOP.EXE 7 X.EXE")!=92; bad|=run("NWIOP.EXE 2")!=92; bad|=run("NWIOP.EXE 22 X")!=92;
 deny_thread=1; bad|=run("NWIOP.EXE 2 X.EXE")!=91; deny_thread=0;
 printf(bad?"HOSTTEST FAIL\n":"HOSTTEST PASS\n");return bad;}
