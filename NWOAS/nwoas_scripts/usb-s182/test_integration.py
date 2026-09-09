from pathlib import Path
import ctypes as C,subprocess,tempfile
r=Path(__file__).resolve().parent;s=(r/'integration.inc').read_text();a=s.index('static bool s175_check_mapping(');b=s.index('\nstatic bool nwoas_usbc_s175_start',a);body=s[a:b]
pre=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint32_t u32;typedef uint64_t u64;
#define BIT(n) (1ULL<<(n))
static u64 input,expected;static int fault;
static bool s175_range(void*c,u64 a,u64 n){(void)c;return n==0x4000 && !(a&0x3fff) && a>=0x100000 && a<0x150000;}
static u64 read64(u64 a){u64 l1=0x100000+((input>>36)&3)*0x4000; if(a==l1+((input>>25)&2047)*8)return 0x140000|(fault==2?0:1);if(a==0x140000+((input>>14)&2047)*8)return (expected&~0x3fffULL)|(fault==3?0:1);return 0;}
static u64 nwoas_ipa_to_pa(u64 a){return expected+(fault==4?0x4000:0)+(a-input);}
'''
post=r'''
int check(uint64_t ipa,uint64_t pa,int bad){input=ipa;expected=pa;fault=bad;u32 roots[4]={0x80000100,0x80000104,0x80000108,0x8000010c};if(bad==1)roots[(ipa>>36)&3]=0;return s175_check_mapping(roots,ipa);}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+body+post)
 subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',str(p/'test.c'),'-o',str(p/'test.dylib')],check=True)
 lib=C.CDLL(str(p/'test.dylib'));lib.check.argtypes=[C.c_uint64,C.c_uint64,C.c_int];lib.check.restype=C.c_int
 count=0
 for ipa in [0,0x100000,0x3fff,0x4000,0xfe9d4000,0xffffffff,0x83c6ec000,0xa3c6ec000,0x1000004000,0x200000c001,0x3fffffffff]:
  pa=0xae0fcc000+ipa if ipa<0x100000000 else ipa
  assert lib.check(ipa,pa,0)==1,hex(ipa)
  for bad in [1,2,3,4]:assert lib.check(ipa,pa,bad)==0,(hex(ipa),bad)
  count+=5
 assert lib.check(1<<38,0,0)==0
candidate=(r/'source-candidate/src/hv_vm.c').read_text();assert 'nwoas_usbc_payload_alias(' not in candidate
assert '#define NWOAS_DART_LOWALIAS 0' in candidate and '#define NWOAS_LOW_EVENT_SEED       0' in candidate
assert 'if (!nwoas_usbc_s175_start(op)) val[0] &= ~1ULL;' in candidate
print('PASS actual mapping-check C:',count+1,'translation/fault cases; integrated fresh-boot alias removal and Run-failure gate present')
