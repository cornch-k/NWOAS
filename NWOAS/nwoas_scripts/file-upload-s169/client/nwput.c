/* Fixed installation-source backup over the registered NS2 RAM mailbox.
 * Reads only C:\S117SRC\install.swm; raw writes only LBA160..175.
 * Source is never deleted. Host verifies complete SHA before publishing backup. */
#include "../../file-delivery-s168/client/wire.h"
typedef uint8_t U8;typedef uint16_t W;typedef uint32_t U32;typedef uint64_t U64;typedef void *H;
#define IMP __declspec(dllimport)
IMP H GetStdHandle(U32);IMP int WriteFile(H,const void*,U32,U32*,void*);IMP int ReadFile(H,void*,U32,U32*,void*);
IMP H CreateFileW(const W*,U32,U32,void*,U32,U32,H);IMP int CloseHandle(H);
IMP int SetFilePointerEx(H,long long,long long*,U32);IMP int DeviceIoControl(H,U32,void*,U32,void*,U32,U32*,void*);
IMP H VirtualAlloc(H,U64,U32,U32);IMP void ExitProcess(U32);IMP void Sleep(U32);
IMP U32 GetFileAttributesW(const W*);IMP int GetFileSizeEx(H,long long*);IMP int GetFileInformationByHandle(H,void*);
#define BAD ((H)-1)
#define SIZE 3987720636ULL
#define CAP 65408u
static const U8 digest[32]={0x81,0x18,0xbf,0xe1,0x17,0x3b,0x8f,0x72,0x16,0x1d,0x1e,0xec,0xe7,0xeb,0x76,0xb0,0x9c,0xaa,0x7b,0x30,0xb3,0x33,0xf7,0x8a,0xe0,0x39,0xd3,0x90,0xbb,0x04,0xbc,0x8c};
static const U8 magic[16]={78,87,79,65,83,45,83,49,54,57,45,85,80,76,68,33},ackmagic[16]={78,87,79,65,83,45,83,49,54,57,45,85,65,67,75,33},portmagic[16]={78,87,79,65,83,45,83,49,50,51,45,76,73,78,75,33};
static H disk=BAD,source=BAD;static U8 *frame,*ack,*idbuf,token[16];
static void emit(const char*s){U32 n=0,w;while(s[n])n++;WriteFile(GetStdHandle((U32)-11),s,n,&w,0);}
static void number(U64 v){U8 b[24];U32 i=24,w;do{b[--i]=(U8)('0'+v%10);v/=10;}while(v);WriteFile(GetStdHandle((U32)-11),b+i,24-i,&w,0);}
static int eq(const U8*a,const U8*b,U32 n){for(U32 i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static void stop(const char*s,U32 rc){emit(s);emit("\r\n");if(source!=BAD)CloseHandle(source);if(disk!=BAD)CloseHandle(disk);ExitProcess(rc);}
static int rd(H h,U64 at,U8*b,U32 n){U32 got=0;return SetFilePointerEx(h,(long long)at,0,0)&&ReadFile(h,b,n,&got,0)&&got==n;}
static int identify(H h){U64 n=0;U32 got=0;return DeviceIoControl(h,0x7405c,0,0,&n,8,&got,0)&&got==8&&n==8448ULL*4096&&rd(h,0,idbuf,4096)&&wire_rd32(idbuf+440)==0x53313233&&idbuf[510]==0x55&&idbuf[511]==0xaa&&rd(h,128ULL*4096,idbuf,4096)&&eq(idbuf,portmagic,16);}
static void header(U32 seq,U32 kind,U64 offset,U32 n){
 for(U32 i=0;i<128;i++)frame[i]=0;
 for(U32 i=0;i<16;i++){frame[i]=magic[i];frame[16+i]=token[i];}
 wire_wr32(frame+32,seq);wire_wr32(frame+36,kind);wire_wr64(frame+40,offset);wire_wr64(frame+48,SIZE);wire_wr32(frame+56,n);wire_wr32(frame+60,wire_crc32(frame+128,n));
 for(U32 i=0;i<32;i++)frame[64+i]=digest[i];wire_wr32(frame+124,wire_crc32(frame,124));
 for(U32 i=128+n;i<65536;i++)frame[i]=0;
}
static int ack_ok(U32 seq,U32 kind,U64 expected){
 if(!eq(ack,ackmagic,16)||!eq(ack+16,token,16)||wire_crc32(ack,124)!=wire_rd32(ack+124))return 0;
 return wire_rd32(ack+32)==seq && wire_rd32(ack+36)==(kind==2?1u:0u) && wire_rd64(ack+40)==expected && wire_rd64(ack+48)==SIZE && eq(ack+64,digest,32);
}
static int send(U32 seq,U32 kind,U64 offset,U32 n){
 header(seq,kind,offset,n);
 for(U32 attempt=0;attempt<3;attempt++){
  if(attempt)Sleep(100);U32 wrote=0;
  if(!SetFilePointerEx(disk,160LL*4096,0,0)||!WriteFile(disk,frame,65536,&wrote,0)||wrote!=65536)continue;
  if(!rd(disk,176ULL*4096,ack,4096))continue;
  if(!ack_ok(seq,kind,offset+n))continue;
  return 1;
 }
 return 0;
}
void mainCRTStartup(void){
 U32 attr=GetFileAttributesW((const W*)L"C:\\S117SRC");
 if(attr==0xffffffffu||(attr&0x400)||!(attr&0x10))stop("STOP: source directory shape",2);
 source=CreateFileW((const W*)L"C:\\S117SRC\\install.swm",0x80000000,1,0,3,0x08200000,0);
 if(source==BAD)stop("STOP: source open",2);
 U32 info[13];long long size=0;
 if(!GetFileInformationByHandle(source,info)||(info[0]&(0x400|0x10))||!GetFileSizeEx(source,&size)||size!=(long long)SIZE)stop("STOP: source shape/length",2);
 frame=VirtualAlloc(0,65536,0x3000,4);ack=VirtualAlloc(0,4096,0x3000,4);idbuf=VirtualAlloc(0,4096,0x3000,4);
 if(!frame||!ack||!idbuf)stop("STOP: buffers",3);
 W path[]=L"\\\\.\\PhysicalDrive0";
 for(U32 i=0;i<10;i++){
  path[17]=(W)('0'+i);H h=CreateFileW(path,0x80000000,3,0,3,0x20000000,0);if(h==BAD)continue;
  int found=identify(h);if(found)for(U32 j=0;j<16;j++)token[j]=idbuf[16+j];CloseHandle(h);if(!found)continue;
  h=CreateFileW(path,0xc0000000,3,0,3,0x20000000,0);if(h==BAD)continue;
  if(!identify(h)||!eq(idbuf+16,token,16)){CloseHandle(h);continue;}disk=h;break;
 }
 if(disk==BAD)stop("STOP: host RAM namespace absent; no raw writes",4);
 if(!send(1,0,0,0))stop("STOP: BEGIN rejected",5);
 U64 offset=0,next=32ULL*1024*1024;U32 seq=2;
 while(offset<SIZE){
  U32 n=(SIZE-offset<CAP)?(U32)(SIZE-offset):CAP,got=0;
  if(!ReadFile(source,frame+128,n,&got,0)||got!=n)stop("STOP: source read",6);
  if(!send(seq,1,offset,n))stop("STOP: transfer/ACK",7);
  offset+=n;seq++;
  if(offset>=next||offset==SIZE){emit("S169 bytes=");number(offset);emit("\r\n");while(offset>=next)next+=32ULL*1024*1024;}
 }
 if(!send(seq,2,offset,0))stop("STOP: final host SHA verification",8);
 stop("S169 PASS: host accepted complete known SHA; source retained; host durability verification still required",0);
}
