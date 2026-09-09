/* Owned-machine user-mode RAM integrity check. No physical-address claims. */
#include "pattern.h"
typedef void *H;
#define IMP __declspec(dllimport)
IMP uint16_t *GetCommandLineW(void);
IMP H VirtualAlloc(H,uint64_t,uint32_t,uint32_t);
IMP int VirtualFree(H,uint64_t,uint32_t);
typedef struct {uint32_t length,load;uint64_t total,avail,totalpage,availpage,totalvirt,availvirt,availExtended;} MS;
_Static_assert(sizeof(MS)==64,"MEMORYSTATUSEX ABI");
IMP int GlobalMemoryStatusEx(MS*);
IMP int QueryPerformanceCounter(int64_t*);
IMP int QueryPerformanceFrequency(int64_t*);
IMP H GetStdHandle(uint32_t);
IMP int WriteFile(H,const void*,uint32_t,uint32_t*,H);
IMP void ExitProcess(uint32_t);
static char *txt(char*p,const char*s){while(*s)*p++=*s++;return p;}
static char *num(char*p,uint64_t n){char b[24];unsigned i=0;do{b[i++]=(char)('0'+n%10);n/=10;}while(n);while(i)*p++=b[--i];return p;}
static void output(char*b,char*p){uint32_t n=0;if(!WriteFile(GetStdHandle((uint32_t)-11),b,(uint32_t)(p-b),&n,0)||n!=(uint32_t)(p-b))ExitProcess(9);}
static void line(const char*tag,uint64_t a,uint64_t b,uint64_t c){char s[256],*p=txt(s,tag);p=num(p,a);p=txt(p," b=");p=num(p,b);p=txt(p," c=");p=num(p,c);p=txt(p,"\r\n");output(s,p);}
static int64_t now(void){int64_t n=0;if(!QueryPerformanceCounter(&n))ExitProcess(8);return n;}
void mainCRTStartup(void){
 uint32_t mib=0,passes=0;int64_t hz=0;MS m={0};m.length=sizeof(m);
 if(!s171_parse(GetCommandLineW(),&mib,&passes)){line("S171 INVALID ARGS mib=",0,0,0);ExitProcess(2);}
 uint64_t bytes=(uint64_t)mib*1048576ULL;
 if(!QueryPerformanceFrequency(&hz)||hz<=0||!GlobalMemoryStatusEx(&m)){line("S171 ENV FAIL ",0,0,0);ExitProcess(3);}
 line("S171 BEGIN bytes=",bytes,passes,m.avail);
 if(m.avail<bytes+1073741824ULL){line("S171 INSUFFICIENT available=",m.avail,bytes,1073741824);ExitProcess(4);}
 volatile uint64_t *mem=VirtualAlloc(0,bytes,0x3000,4);
 if(!mem){line("S171 ALLOC FAIL bytes=",bytes,0,0);ExitProcess(5);}
 uint64_t count=bytes/8,chunk=33554432ULL;int64_t begin=now();uint32_t error=0;
 for(uint32_t pass=0;pass<passes && !error;pass++){
  int64_t wb=now();uint64_t expected=0,actual=0;
  for(uint64_t at=0;at<count;at+=chunk){
   uint64_t end=at+chunk;if(end>count)end=count;
   for(uint64_t i=at;i<end;i++){uint64_t v=s171_pattern(i,pass);mem[i]=v;expected+=v;}
   line("S171 WRITE pass=",pass,end*8,bytes);
   if(now()-begin>hz*600){error=10;break;}
  }
  if(error)break;
  int64_t rb=now();
  for(uint64_t at=0;at<count;at+=chunk){
   uint64_t end=at+chunk;if(end>count)end=count;
   for(uint64_t i=at;i<end;i++){uint64_t v=mem[i],wanted=s171_pattern(i,pass);if(v!=wanted){line("S171 MISMATCH word=",i,wanted,v);error=6;break;}actual+=v;}
   if(error)break;
   line("S171 READ pass=",pass,end*8,bytes);
   if(now()-begin>hz*600){error=10;break;}
  }
  if(error)break;
  if(actual!=expected){error=7;break;}
  line("S171 PASS pass=",pass,bytes,actual);
  line("S171 TIMING write_us=",(uint64_t)(rb-wb)*1000000ULL/hz,(uint64_t)(now()-rb)*1000000ULL/hz,0);
 }
 if(!VirtualFree((H)mem,0,0x8000)&&!error)error=11;
 line(error?"S171 FAILED code=":"S171 COMPLETE bytes_per_pass=",error?error:bytes,passes,(uint64_t)(now()-begin)*1000ULL/hz);
 ExitProcess(error);
}
