from pathlib import Path
import subprocess, tempfile, sys
repo=Path('/Volumes/X31/NWOAS/m1n1_windows')
s=(Path(sys.argv[1]) if len(sys.argv)>1 else repo/'src/hv_vm.c').read_text()
a=s.index('static void nwoas_usbc_payload_alias');b=s.index('/* D79 read-only descriptor',a)
pre=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint64_t u64; typedef uint32_t u32;
#define BIT(n) (1ULL<<(n))
#define sysop(x) ((void)0)
#define BASE 0x900000000ULL
#define OP 0x502280020ULL
static unsigned char memory[0x100000];
static bool nwoas_in_dram(u64 a,size_t n) { return a>=BASE && n<=sizeof(memory) && a-BASE<=sizeof(memory)-n; }
static u64 nwoas_ipa_to_pa(u64 a) { return a && a<sizeof(memory)?BASE+a:a; }
static u32 read32(u64 a) {
 if(a==OP+0x30) return (u32)(BASE+0x1000);
 if(a==OP+0x34) return (u32)((BASE+0x1000)>>32);
 if(a==0x502280010ULL) return 0;
 assert(nwoas_in_dram(a,4));u32 v;memcpy(&v,memory+(a-BASE),4);return v;
}
static u64 read64(u64 a) {assert(nwoas_in_dram(a,8));u64 v;memcpy(&v,memory+(a-BASE),8);return v;}
static void write64(u64 a,u64 v) {assert(nwoas_in_dram(a,8));memcpy(memory+(a-BASE),&v,8);}
static void put32(u64 a,u32 v) {assert(nwoas_in_dram(a,4));memcpy(memory+(a-BASE),&v,4);}
static void nwoas_cache_maint(u64 a,size_t n,bool clean) {(void)clean;assert(nwoas_in_dram(a,n));}
static void trb(u64 a,u64 ptr,u32 len,u32 ctl) {write64(a,ptr);put32(a+8,len);put32(a+12,ctl);}
'''
post=r'''
int main(void) {
 const u64 a=BASE+0x3000,b=BASE+0x4000,c=BASE+0x2000;
 write64(BASE+0x1008,c);write64(c+7*32+8,a|1);
 trb(a,0x80000,8,(1<<10)|1);trb(a+16,b,0,(6<<10)|1);
 trb(b,0x81000,8,(1<<10)|1);
 nwoas_usbc_payload_alias(OP,1,7);
 assert(read64(a)==BASE+0x80000 && read64(b)==BASE+0x81000);
 /* Reclaimed old B must not be reused merely because hardware DQ stayed A. */
 memset(memory+0x3000,0,0x1000);
 trb(a,0x82000,8,(1<<10)|1);trb(b,0x83000,8,(1<<10)|1);
 nwoas_usbc_payload_alias(OP,1,7);
 assert(read64(a)==BASE+0x82000);assert(read64(b)==0x83000);
 /* Immediate data and Event Data cookies are never payload addresses. */
 trb(a,0x84000,8,(1<<10)|BIT(6)|1);
 trb(a+16,0x85000,0,(7<<10)|1);
 nwoas_usbc_payload_alias(OP,1,7);
 assert(read64(a)==0x84000 && read64(a+16)==0x85000);
 puts("PASS: aliases current linked rings; avoids persisted retired ring; preserves IDT/Event Data");
}
'''
with tempfile.TemporaryDirectory(prefix='nwoas-alias-test-') as d:
 p=Path(d);(p/'test.c').write_text(pre+s[a:b]+post)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-format',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
