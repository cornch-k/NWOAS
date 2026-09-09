#ifndef S171_PATTERN_H
#define S171_PATTERN_H
#include <stdint.h>
static inline uint64_t s171_pattern(uint64_t index, uint32_t pass) {
 uint64_t x=index ^ (0x9e3779b97f4a7c15ULL*(pass+1));
 x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
 x=(x^(x>>27))*0x94d049bb133111ebULL;
 return x^(x>>31);
}
static inline int s171_parse(const uint16_t *s,uint32_t *mib,uint32_t *passes) {
 uint32_t vals[2]={4096,3}; unsigned n=0;
 if(*s=='"'){s++;while(*s && *s!='"')s++;if(*s!='"')return 0;s++;}
 else {while(*s && *s!=' ' && *s!='\t')s++;}
 while(*s){
  while(*s==' '||*s=='\t')s++;
  if(!*s)break;
  if(n==2 || *s<'0'||*s>'9')return 0;
  uint32_t v=0;
  while(*s>='0'&&*s<='9'){if(v>10240)return 0;v=v*10+(*s++-'0');}
  if(*s && *s!=' ' && *s!='\t')return 0;
  vals[n++]=v;
 }
 if(vals[0]<64||vals[0]>10240||vals[1]<1||vals[1]>10)return 0;
 *mib=vals[0];*passes=vals[1];return 1;
}
#endif
