/* S123 explicit installed-Windows foreground worker. Commands arrive only on the host RAM
 * namespace identified by its MBR signature + session mailbox. No network.
 * Never writes another disk: requires exact size, signature and challenge.
 * Child output is streamed through the virtual mailbox, with sequence ACKs. */
typedef unsigned char U8;
typedef unsigned short W;
typedef unsigned long U32;
typedef unsigned long long U64;
typedef void *H;
#define IMP __declspec(dllimport)
IMP H GetStdHandle(U32);
IMP int WriteFile(H,const void *,U32,U32 *,void *);
IMP int ReadFile(H,void *,U32,U32 *,void *);
IMP H CreateFileW(const W *,U32,U32,void *,U32,U32,H);
IMP int SetFilePointerEx(H,long long,long long *,U32);
IMP int DeviceIoControl(H,U32,void *,U32,void *,U32,U32 *,void *);
IMP int CloseHandle(H);
IMP H VirtualAlloc(H,U64,U32,U32);
IMP void ExitProcess(U32);
IMP void Sleep(U32);
IMP U32 GetLastError(void);
IMP int CreateProcessW(const W *,W *,void *,void *,int,U32,void *,const W *,void *,void *);
IMP U32 WaitForSingleObject(H,U32);
IMP int GetExitCodeProcess(H,U32 *);
IMP int CreatePipe(H *,H *,void *,U32);
IMP int SetHandleInformation(H,U32,U32);
IMP int PeekNamedPipe(H,void *,U32,U32 *,U32 *,U32 *);
IMP U32 GetEnvironmentVariableW(const W *,W *,U32);
typedef struct {U32 size;void *descriptor;int inherit;} SA;
typedef struct {U32 cb;W *reserved,*desktop,*title;U32 x,y,xsize,ysize,xchars,ychars,fill,flags;W show,reserved2;U8 *reservedptr;H input,output,error;} SI;
typedef struct {H process,thread;U32 pid,tid;} PI;
_Static_assert(sizeof(SI)==104,"STARTUPINFO64 ABI");
_Static_assert(sizeof(SA)==24,"SECURITY_ATTRIBUTES64 ABI");
static U32 link_disk_number=0xffffffff;
static U8 *buf,*sendbuf,token[16];static H disk=(H)-1;static U32 sequence=0;
static const U8 magic[]="NWOAS-S123-LINK!";
static void zero(void *p,U32 n){U8 *b=p;while(n--)*b++=0;}
static void copy(void *p,const void *q,U32 n){U8 *b=p;const U8 *a=q;while(n--)*b++=*a++;}
static int equal(const void *a,const void *b,U32 n){const U8 *x=a,*y=b;while(n--)if(*x++!=*y++)return 0;return 1;}
static U32 crc(const U8 *s,U32 n){U32 c=0xffffffff;while(n--){c^=*s++;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320&(-(c&1)));}return ~c;}
static void emit(const char *s){U32 n=0,w;while(s[n])n++;WriteFile(GetStdHandle((U32)-11),s,n,&w,0);}
static int rd(H h,U64 at,U8 *b){U32 got=0;return SetFilePointerEx(h,(long long)at,0,0)&&ReadFile(h,b,4096,&got,0)&&got==4096;}
static int frame(void){return rd(disk,128ULL*4096,buf)&&equal(buf,magic,16)&&equal(buf+16,token,16);}
static int send(U32 job,U32 kind,const U8 *data,U32 n,U32 rc){
 U32 got=0;if(n>4032)return 0;
 zero(sendbuf,4096);copy(sendbuf,magic,16);copy(sendbuf+16,token,16);
 U32 *h=(U32 *)(sendbuf+32);h[0]=job;h[1]=kind;h[2]=n;h[3]=crc(data,n);h[4]=++sequence;h[5]=rc;copy(sendbuf+64,data,n);
 if(!SetFilePointerEx(disk,128LL*4096,0,0)||!WriteFile(disk,sendbuf,4096,&got,0)||got!=4096)return 0;
 return frame()&&*(U32 *)(buf+48)==sequence;
}
static int identify(H h){
 U64 length=0;U32 got;
 if(!DeviceIoControl(h,0x7405c,0,0,&length,8,&got,0)||got!=8||length!=8448ULL*4096)return 0;
 if(!rd(h,0,buf)||*(U32 *)(buf+440)!=0x53313233||buf[510]!=0x55||buf[511]!=0xaa)return 0;
 if(!rd(h,128ULL*4096,buf)||!equal(buf,magic,16))return 0;
 return 1;
}

#include "scratch.h"
IMP int GetVolumeInformationW(const W*,W*,U32,U32*,U32*,U32*,W*,U32);
static int scratch_drive(void) {
 W volume[]=L"\\\\.\\D:", root[]=L"D:\\"; int chosen=0;
 for (U32 letter='C';letter<='Z';letter++) {
  volume[4]=(W)letter;root[0]=(W)letter;
  H h=CreateFileW(volume,0,3,0,3,0,0);
  if(h==(H)-1)continue;
  U8 ext[32];U32 got=0;zero(ext,sizeof(ext));
  int ok=DeviceIoControl(h,0x560000,0,0,ext,sizeof(ext),&got,0);
  CloseHandle(h);
  if(!ok||!s177_extent_matches(ext,got,link_disk_number))continue;
  U32 serial=0;
  if(!GetVolumeInformationW(root,0,0,&serial,0,0,0,0)||serial!=0x53313233)continue;
  if(chosen)return 0;
  chosen=(int)letter;
 }
 return chosen;
}
static int run(U32 job,U32 n){
 static W script[]=L"D:\\NWOAS177.CMD",app[]=L"C:\\Windows\\System32\\cmd.exe";
 static W command[]=L"cmd.exe /d /c D:\\NWOAS177.CMD";
 H f=(H)-1,r=0,w=0,nul=(H)-1;U32 got,available,rc=2;SA sa={sizeof(SA),0,1};SI si;PI pi;
 int letter=scratch_drive();
 if(!letter)return send(job,3,(const U8 *)"RAM scratch not found",21,3);
 script[0]=(W)letter;command[14]=(W)letter;
 /* The payload is captured before mailbox ACK reads overwrite buf. */
 f=CreateFileW(script,0x40000000,0,0,2,0x80,0);
 if(f==(H)-1)return send(job,3,(const U8 *)"script open failed",18,GetLastError());
 int ok=WriteFile(f,buf+64,n,&got,0)&&got==n;CloseHandle(f);
 if(!ok)return send(job,3,(const U8 *)"script write failed",19,GetLastError());
 nul=CreateFileW(L"NUL",0x80000000,3,&sa,3,0x80,0);
 if(nul==(H)-1)return send(job,3,(const U8 *)"NUL stdin failed",16,GetLastError());
 if(!CreatePipe(&r,&w,&sa,16384)||!SetHandleInformation(r,1,0)){
  if(r)CloseHandle(r);if(w)CloseHandle(w);CloseHandle(nul);return 0;
 }
 zero(&si,sizeof(si));zero(&pi,sizeof(pi));si.cb=sizeof(si);si.flags=0x100;
 si.input=nul;si.output=w;si.error=w;
 if(!CreateProcessW(app,command,0,0,1,0x08000000,0,L"C:\\",&si,&pi)){
  rc=GetLastError();CloseHandle(r);CloseHandle(w);CloseHandle(nul);return send(job,3,(const U8 *)"CreateProcess failed",20,rc);
 }
 CloseHandle(w);CloseHandle(nul);CloseHandle(pi.thread);
 emit("S123: host task running; USB stays connected.\r\n");
 for(;;){
  available=0;
  if(PeekNamedPipe(r,0,0,0,&available,0)&&available){
   static U8 data[4032];U32 want=available<4032?available:4032;
   if(!ReadFile(r,data,want,&got,0)||!got||!send(job,2,data,got,0)){CloseHandle(r);CloseHandle(pi.process);return 0;}
  }else if(WaitForSingleObject(pi.process,0)==0)break;
  else Sleep(100);
 }
 GetExitCodeProcess(pi.process,&rc);CloseHandle(pi.process);CloseHandle(r);
 emit("S123: task completed; waiting for next host task.\r\n");
 return send(job,3,(const U8 *)"complete",8,rc);
}
void mainCRTStartup(void){
 static W path[]=L"\\\\.\\PhysicalDrive0",env[8];U32 last=0;
 emit("NWOAS S177 RAM scratch; installed Windows worker\r\n");
 if(GetEnvironmentVariableW(L"SystemDrive",env,8)!=2||env[0]!='C'||env[1]!=':'){emit("STOP: requires installed Windows C:\r\n");ExitProcess(2);}
 buf=VirtualAlloc(0,65536,0x3000,4);sendbuf=VirtualAlloc(0,65536,0x3000,4);
 if(!buf||!sendbuf)ExitProcess(2);
 for(int i=0;i<10;i++){
  path[17]='0'+i;
  H h=CreateFileW(path,0x80000000,3,0,3,0x20000000,0);
  if(h==(H)-1)continue;
  int found=identify(h);if(found)copy(token,buf+16,16);CloseHandle(h);
  if(!found)continue;
  h=CreateFileW(path,0xc0000000,3,0,3,0x20000000,0);
  if(h==(H)-1)continue;
  if(!identify(h)||!equal(token,buf+16,16)){CloseHandle(h);continue;}
  disk=h;link_disk_number=(U32)i;break;
 }
 if(disk==(H)-1){emit("STOP: host virtual disk not found; no raw writes performed.\r\n");ExitProcess(2);}
 sequence=*(U32 *)(buf+48);
 if(!send(0,4,(const U8 *)"WinOS worker connected",22,0))goto fail;
 emit("S123 LINK READY. Leave USB on Mac mini.\r\n");
 for(;;){
  if(!frame())goto fail;
  U32 *h=(U32 *)(buf+32);U32 job=h[0],kind=h[1],n=h[2];
  if(kind==1&&job>last){
   if(!n||n>4032||crc(buf+64,n)!=h[3])goto fail;
   last=job;if(!run(job,n))goto fail;
  }
  Sleep(500);
 }
fail:emit("STOP: host link failed; no task retry.\r\n");CloseHandle(disk);ExitProcess(2);
}
