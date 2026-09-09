/* S106: read-only Windows ARM64 file hash diagnostic, using Windows CNG SHA256.
 * No CRT, disk management, registry, network or write access to the input file.
 * Self-test SHA256("abc") before file I/O; verify full file length. */
typedef unsigned char U8;
typedef unsigned short W;
typedef unsigned long U32;
typedef unsigned long long U64;
typedef long S32;
typedef void *H;
#define IMP __declspec(dllimport)
IMP H GetStdHandle(U32);
IMP int WriteFile(H,const void *,U32,U32 *,void *);
IMP int ReadFile(H,void *,U32,U32 *,void *);
IMP H CreateFileW(const W *,U32,U32,void *,U32,U32,H);
IMP int GetFileSizeEx(H,long long *);
IMP int CloseHandle(H);
IMP W *GetCommandLineW(void);
IMP U32 GetLastError(void);
IMP U64 GetTickCount64(void);
IMP H GetProcessHeap(void);
IMP H HeapAlloc(H,U32,U64);
IMP int HeapFree(H,U32,H);
IMP void ExitProcess(U32);
IMP S32 BCryptOpenAlgorithmProvider(H *,const W *,const W *,U32);
IMP S32 BCryptGetProperty(H,const W *,U8 *,U32,U32 *,U32);
IMP S32 BCryptCreateHash(H,H *,U8 *,U32,U8 *,U32,U32);
IMP S32 BCryptHashData(H,U8 *,U32,U32);
IMP S32 BCryptFinishHash(H,U8 *,U32,U32);
IMP S32 BCryptDestroyHash(H);
IMP S32 BCryptCloseAlgorithmProvider(H,U32);
static H output;
static void emit(const char *s){U32 n=0,written;while(s[n])n++;WriteFile(output,s,n,&written,0);}
static void number(U64 x){char b[24];unsigned i=23;b[i]=0;do{b[--i]='0'+x%10;x/=10;}while(x);emit(b+i);}
static void hex(U8 *b){const char *digits="0123456789abcdef";char s[65];for(int i=0;i<32;i++){s[2*i]=digits[b[i]>>4];s[2*i+1]=digits[b[i]&15];}s[64]=0;emit(s);}
static int arg(W **p,W *out,unsigned limit){unsigned n=0;while(**p==' '||**p=='\t')(*p)++;int quoted=**p=='"';if(quoted)(*p)++;while(**p && (quoted?**p!='"':(**p!=' '&&**p!='\t'))){if(n+1>=limit)return 0;out[n++]=*(*p)++;}if(quoted){if(**p!='"')return 0;(*p)++;}out[n]=0;return n!=0;}
static const W sha[]={ 'S','H','A','2','5','6',0 };
static const W object_len[]={ 'O','b','j','e','c','t','L','e','n','g','t','h',0 };
static const U8 abc_digest[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
void mainCRTStartup(void){
 H alg=0,hash=0,file=(H)-1,heap=GetProcessHeap();U8 *object=0,*buffer=0,digest[32];U32 len=0,got=0,rc=2;long long size=0;U64 total=0,started=GetTickCount64(),next=268435456;static W path[2048],skip[2048],expect[65];W *cmd=GetCommandLineW();
 output=GetStdHandle((U32)-11);emit("NWOAS S106 ARM64 SHA256, input read-only\r\n");
 if(!arg(&cmd,skip,2048)||!arg(&cmd,path,2048)){emit("Usage: NWHASH.exe filename [expected-sha256]\r\n");goto end;}
 arg(&cmd,expect,65);
 if(BCryptOpenAlgorithmProvider(&alg,sha,0,0)<0||BCryptGetProperty(alg,object_len,(U8 *)&len,4,&got,0)<0||got!=4||!len||len>1048576){emit("FAIL CNG initialization\r\n");goto end;}
 object=HeapAlloc(heap,0,len);buffer=HeapAlloc(heap,0,1048576);if(!object||!buffer)goto end;
 if(BCryptCreateHash(alg,&hash,object,len,0,0,0)<0||BCryptHashData(hash,(U8 *)"abc",3,0)<0||BCryptFinishHash(hash,digest,32,0)<0)goto end;
 for(int i=0;i<32;i++)if(digest[i]!=abc_digest[i]){emit("FAIL SHA256 self-test\r\n");goto end;}
 BCryptDestroyHash(hash);hash=0;emit("PASS SHA256 self-test\r\n");
 if(BCryptCreateHash(alg,&hash,object,len,0,0,0)<0)goto end;
 file=CreateFileW(path,0x80000000,7,0,3,0x08000000,0);
 if(file==(H)-1||!GetFileSizeEx(file,&size)||size<0){emit("FAIL open/size error=");number(GetLastError());emit("\r\n");goto end;}
 for(;;){
  if(!ReadFile(file,buffer,1048576,&got,0)){emit("FAIL ReadFile error=");number(GetLastError());emit("\r\n");goto end;}
  if(!got)break;
  if(BCryptHashData(hash,buffer,got,0)<0){emit("FAIL hash update\r\n");goto end;}
  total+=got;
  if(total>=next){emit("PROGRESS bytes=");number(total);emit(" elapsed_ms=");number(GetTickCount64()-started);emit("\r\n");next+=268435456;}
 }
 if(total!=(U64)size){emit("FAIL incomplete file bytes=");number(total);emit("\r\n");goto end;}
 if(BCryptFinishHash(hash,digest,32,0)<0)goto end;
 emit("SHA256 ");hex(digest);emit(" bytes=");number(total);emit(" elapsed_ms=");number(GetTickCount64()-started);emit("\r\n");
 rc=0;
 if(expect[0]){
  const char *digits="0123456789abcdef";
  for(int i=0;i<64;i++){W e=expect[i];if(e>='A'&&e<='F')e+=32;U8 v=(i&1)?digest[i/2]&15:digest[i/2]>>4;if(e!=(W)digits[v]){rc=1;break;}}
  if(expect[64])rc=1;
  emit(rc?"MISMATCH expected SHA256\r\n":"MATCH expected SHA256\r\n");
 }
end:
 if(hash)BCryptDestroyHash(hash);
 if(alg)BCryptCloseAlgorithmProvider(alg,0);
 if(file!=(H)-1)CloseHandle(file);
 if(object)HeapFree(heap,0,object);
 if(buffer)HeapFree(heap,0,buffer);
 emit("EXIT ");number(rc);emit("\r\n");ExitProcess(rc);
}
