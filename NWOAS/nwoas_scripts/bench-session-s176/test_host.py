from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parent
s=(r/'session.c').read_text().replace('#define IMP __declspec(dllimport)','#define IMP')
test=r'''
#include <assert.h>
#include <setjmp.h>
static jmp_buf finish;static unsigned scenario,closed,envfreed,launched,exitcode;
uint32_t WTSGetActiveConsoleSessionId(void){return scenario==0?0xffffffff:1;}
int WTSQueryUserToken(uint32_t n,H*t){assert(n==1);if(scenario==1)return 0;*t=(H)1;return 1;}
int CreateEnvironmentBlock(H*e,H t,int inherit){assert(t==(H)1&&!inherit);if(scenario==2)return 0;*e=(H)2;return 1;}
int DestroyEnvironmentBlock(H e){assert(e==(H)2);envfreed++;return 1;}
static int eq(const W*a,const W*b){while(*a && *a==*b){a++;b++;}return *a==*b;}
int CreateProcessAsUserW(H t,const W*a,W*c,void*x,void*y,int inherit,uint32_t flags,H e,const W*d,SI*si,PI*pi){
 assert(t==(H)1&&e==(H)2&&!x&&!y&&!inherit&&flags==0x410);
 assert(eq(a,L"C:\\Windows\\System32\\cmd.exe"));
 assert(eq(c,L"cmd.exe /d /c C:\\NWOAS-BENCH\\run-user.cmd"));
 assert(eq(d,L"C:\\NWOAS-BENCH")&&eq(si->desktop,L"winsta0\\default"));
 assert(si->cb==104&&si->flags==1&&si->show==0);
 launched++;if(scenario==3)return 0;pi->process=(H)3;pi->thread=(H)4;pi->pid=123;return 1;
}
int CloseHandle(H h){assert(h==(H)1||h==(H)3||h==(H)4);closed++;return 1;}
uint32_t GetLastError(void){return 1314;}
H GetStdHandle(uint32_t n){assert(n==(uint32_t)-11);return (H)5;}
int WriteFile(H h,const void*b,uint32_t n,uint32_t*w,void*x){assert(h==(H)5&&b&&n&&!x);*w=n;return 1;}
void ExitProcess(uint32_t n){exitcode=n;longjmp(finish,1);}
int main(void){
 for(scenario=0;scenario<5;scenario++){
  closed=envfreed=launched=0;
  if(!setjmp(finish)){mainCRTStartup();assert(0);}
  unsigned exits[]={2,3,4,5,0};assert(exitcode==exits[scenario]);
  assert(closed==(scenario<2?0:scenario<4?1:3));
  assert(envfreed==(scenario>=3));assert(launched==(scenario>=3));
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);c=p/'test.c';c.write_text(s+'\n'+test)
 for name,flags in [('plain',[]),('san',['-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
  subprocess.run(['cc','-std=c11','-fshort-wchar','-O1','-Wall','-Wextra','-Werror',*flags,str(c),'-o',str(p/name)],check=True)
  subprocess.run([str(p/name)],check=True)
print('PASS actual launcher C: five outcomes, fixed target/context, no cross-session handle inheritance, token/environment cleanup; plain and ASan/UBSan')
