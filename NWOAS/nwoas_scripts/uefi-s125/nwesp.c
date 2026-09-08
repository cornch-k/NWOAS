/* S120: S118 identity/capacity guard plus explicit flush mode for the
 * fixed Mac mini trial NTFS volume. Flush mode opens that volume read/write. */
typedef unsigned char U8;
typedef unsigned short W;
typedef unsigned long U32;
typedef unsigned long long U64;
typedef void *H;
#define IMP __declspec(dllimport)
IMP H GetStdHandle(U32);
IMP int WriteFile(H,const void *,U32,U32 *,void *);
IMP W *GetCommandLineW(void);
IMP void ExitProcess(U32);
IMP U32 GetLastError(void);
IMP int GetVolumeInformationW(const W *,W *,U32,U32 *,U32 *,U32 *,W *,U32);
IMP int GetDiskFreeSpaceExW(const W *,U64 *,U64 *,U64 *);
IMP H CreateFileW(const W *,U32,U32,void *,U32,U32,H);
IMP int DeviceIoControl(H,U32,void *,U32,void *,U32,U32 *,void *);
IMP int CloseHandle(H);
IMP int FlushFileBuffers(H);
static H output;
static int word(const W *w,const char *s){while(*w&&*s){if(*w++!=(unsigned char)*s++)return 0;}return !*w&&!*s;}
static void emit(const char *s){U32 n=0,written;while(s[n])n++;WriteFile(output,s,n,&written,0);}
static void number(U64 x){char b[24];unsigned i=23;b[i]=0;do{b[--i]='0'+x%10;x/=10;}while(x);emit(b+i);}
static int arg(W **p,W *out,unsigned limit){unsigned n=0;while(**p==' '||**p=='\t')(*p)++;int quoted=**p=='"';if(quoted)(*p)++;while(**p && (quoted?**p!='"':(**p!=' '&&**p!='\t'))){if(n+1>=limit)return 0;out[n++]=*(*p)++;}if(quoted){if(**p!='"')return 0;(*p)++;}out[n]=0;return n!=0;}

static U32 u32(const U8 *p){return (U32)p[0]|((U32)p[1]<<8)|((U32)p[2]<<16)|((U32)p[3]<<24);}
static U64 u64(const U8 *p){return (U64)u32(p)|((U64)u32(p+4)<<32);}
void mainCRTStartup(void){
 static W skip[2048],drive[4],mode[16],fs[32],label[64];
 static U8 info[512];W *cmd=GetCommandLineW();U32 serial=0,max=0,flags=0,got=0,rc=2;
 U64 avail=0,total=0,freeb=0;H h=(H)-1;
 output=GetStdHandle((U32)-11);
 if(!arg(&cmd,skip,2048)||!arg(&cmd,drive,4)||!arg(&cmd,mode,16))goto end;
 if(drive[0]<'C'||drive[0]>'Z'||drive[0]=='X'||drive[1]!=':'||drive[2])goto end;
 if(!word(mode,"identify")&&!word(mode,"ready")&&!word(mode,"flush"))goto end;
 W root[]={drive[0],':','\\',0};W dev[]={'\\','\\','.', '\\',drive[0],':',0};
 if(!GetVolumeInformationW(root,label,64,&serial,&max,&flags,fs,32)||!GetDiskFreeSpaceExW(root,&avail,&total,&freeb))goto end;
 emit("VOLUME serial=");number(serial);emit(" total=");number(total);emit(" free=");number(freeb);emit(" available=");number(avail);emit("\r\n");
 if(!word(fs,"FAT32")||total<256ULL*1024*1024||total>300ULL*1024*1024)goto end;
 h=CreateFileW(dev,0,3,0,3,0,0);if(h==(H)-1)goto end;
 /* IOCTL_DISK_GET_PARTITION_INFO_EX, FILE_ANY_ACCESS, METHOD_BUFFERED.
  * Windows ABI offsets: style0, StartingOffset8, PartitionLength16, number24. */
 if(!DeviceIoControl(h,0x70048,0,0,info,sizeof(info),&got,0)||got<32)goto end;
 emit("PARTITION style=");number(u32(info));emit(" start=");number(u64(info+8));emit(" length=");number(u64(info+16));emit(" number=");number(u32(info+24));emit("\r\n");
 if(got<144||u32(info)!=1||u64(info+8)!=220524969984ULL||u64(info+16)!=300ULL*1024*1024||u32(info+24)!=3)goto end;
 static const U8 esp_type[16]={0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b};
 for(unsigned j=0;j<16;j++)if(info[32+j]!=esp_type[j])goto end;
 if(word(mode,"ready")&&avail<100ULL*1024*1024){emit("FAIL insufficient space for 100MiB for boot files\r\n");goto end;}
 if(word(mode,"flush")){
  CloseHandle(h);h=CreateFileW(dev,0xc0000000,3,0,3,0,0);
  if(h==(H)-1||!FlushFileBuffers(h)){emit("FAIL volume flush\r\n");goto end;}
  emit("PASS S125 ESP VOLUME FLUSH\r\n");
 }
 rc=0;emit("PASS S125 ESP TARGET GUARD\r\n");
end:
 if(h!=(H)-1)CloseHandle(h);
 if(rc){emit("FAIL S125 ESP TARGET/CAPACITY GUARD last_error=");number(GetLastError());emit("\r\n");}
 ExitProcess(rc);
}
