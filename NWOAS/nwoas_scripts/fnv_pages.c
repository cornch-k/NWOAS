#include <stdio.h>
#include <stdint.h>
int main(int c, char**v){
  FILE*f=fopen(v[1],"rb"); if(!f){perror("open");return 1;}
  unsigned char b[0x4000]; size_t n; long pg=0;
  while((n=fread(b,1,0x4000,f))==0x4000){
    uint32_t h=2166136261u; for(size_t i=0;i<0x4000;i++) h=(h^b[i])*16777619u;
    printf("%08x\n",h); pg++;
  }
  fprintf(stderr,"full16k_pages=%ld\n",pg); return 0;
}
