/* Launch exactly the prepared benchmark script in the logged-on console user's session.
 * No credentials, token changes, privilege adjustments, or arbitrary command arguments. */
#include <stdint.h>
typedef void *H;typedef uint16_t W;
#define IMP __declspec(dllimport)
IMP uint32_t WTSGetActiveConsoleSessionId(void);
IMP int WTSQueryUserToken(uint32_t,H*);
IMP int CreateEnvironmentBlock(H*,H,int);
IMP int DestroyEnvironmentBlock(H);
typedef struct {uint32_t cb;W *reserved,*desktop,*title;uint32_t x,y,xsize,ysize,xchars,ychars,fill,flags;uint16_t show,reserved2;void *bytes;H in,out,err;} SI;
typedef struct {H process,thread;uint32_t pid,tid;} PI;
_Static_assert(sizeof(SI)==104,"STARTUPINFO ABI");
_Static_assert(sizeof(PI)==24,"PROCESS_INFORMATION ABI");
IMP int CreateProcessAsUserW(H,const W*,W*,void*,void*,int,uint32_t,H,const W*,SI*,PI*);
IMP int CloseHandle(H);
IMP uint32_t GetLastError(void);
IMP H GetStdHandle(uint32_t);
IMP int WriteFile(H,const void*,uint32_t,uint32_t*,void*);
IMP void ExitProcess(uint32_t);
static void emit(const char*s,uint32_t a,uint32_t b){char buf[160],tmp[16],*p=buf;unsigned n=0;while(*s)*p++=*s++;do{tmp[n++]=(char)('0'+a%10);a/=10;}while(a);while(n)*p++=tmp[--n];*p++=' ';do{tmp[n++]=(char)('0'+b%10);b/=10;}while(b);while(n)*p++=tmp[--n];*p++='\r';*p++='\n';uint32_t w=0;WriteFile(GetStdHandle((uint32_t)-11),buf,(uint32_t)(p-buf),&w,0);}
void mainCRTStartup(void){
 uint32_t sid=WTSGetActiveConsoleSessionId();H token=0,env=0;
 if(!sid||sid==0xffffffff){emit("S176 no interactive console session ",sid,0);ExitProcess(2);}
 if(!WTSQueryUserToken(sid,&token)){emit("S176 WTSQueryUserToken failed ",GetLastError(),sid);ExitProcess(3);}
 if(!CreateEnvironmentBlock(&env,token,0)){uint32_t e=GetLastError();CloseHandle(token);emit("S176 environment failed ",e,sid);ExitProcess(4);}
 SI si={0};PI pi={0};si.cb=sizeof(si);si.desktop=L"winsta0\\default";si.flags=1;si.show=0;
 static W cmd[]=L"cmd.exe /d /c C:\\NWOAS-BENCH\\run-user.cmd";
 int ok=CreateProcessAsUserW(token,L"C:\\Windows\\System32\\cmd.exe",cmd,0,0,0,0x410,env,L"C:\\NWOAS-BENCH",&si,&pi);uint32_t e=ok?0:GetLastError();
 DestroyEnvironmentBlock(env);CloseHandle(token);
 if(!ok){emit("S176 launch failed ",e,sid);ExitProcess(5);}
 emit("S176 launched user benchmark cmd pid/session ",pi.pid,sid);
 CloseHandle(pi.thread);CloseHandle(pi.process);ExitProcess(0);
}
