/* S175: fresh-boot DART1/SID1 translation, no transfer-pointer rewrites. */
#include "s175_dart1_translate.h"
static u32 s175_read(void *c, uint64_t a) { (void)c; return read32(a); }
static void s175_write(void *c, uint64_t a, uint32_t v) { (void)c; write32(a,v); }
static void s175_barrier(void *c) { (void)c; sysop("dsb sy"); }
static void s175_delay(void *c, uint32_t us) { (void)c; udelay(us); }
static bool s175_range(void *c, uint64_t a, uint64_t n)
{ (void)c; return !(a & 0x3fff) && nwoas_in_dram(a,n); }
static int nwoas_s175_ready;
static bool s175_check_mapping(const u32 roots[4], u64 ipa)
{
    if (ipa >> 38) return false;
    u32 t = roots[(ipa >> 36) & 3];
    if (!(t & BIT(31))) return false;
    u64 l1 = (u64)(t & 0x7fffffff) << 12;
    if (!s175_range(NULL,l1,0x4000)) return false;
    u64 e = read64(l1 + ((ipa >> 25) & 2047)*8);
    if (!(e & 1)) return false;
    u64 l2 = e & 0xffffffc000UL;
    if (!s175_range(NULL,l2,0x4000)) return false;
    u64 leaf = read64(l2 + ((ipa >> 14) & 2047)*8);
    return (leaf & 1) && ((leaf & 0xffffffc000UL) | (ipa & 0x3fff)) == nwoas_ipa_to_pa(ipa);
}
static bool nwoas_usbc_s175_start(u64 op)
{
    if (nwoas_s175_ready) return nwoas_s175_ready > 0;
    u32 roots[4];
    for (u32 i=0;i<4;i++) roots[i]=read32(FL_DART_BASE+0x210+4*i);
    u64 rt=S175_XHCI_BASE+(read32(S175_XHCI_BASE+0x18)&~0x1fu);
    u64 checks[]={0x100000,0xfe9d4000,
        (read32(op+0x18)|((u64)read32(op+0x1c)<<32))&~63UL,
        (read32(op+0x30)|((u64)read32(op+0x34)<<32))&~63UL,
        (read32(rt+0x30)|((u64)read32(rt+0x34)<<32))&~63UL};
    for(u32 i=0;i<sizeof(checks)/sizeof(checks[0]);i++) {
        if(checks[i] && !s175_check_mapping(roots,checks[i])) {
            printf("[usb-s175] mapping mismatch i=%u ipa=%lx; Run suppressed\n",i,checks[i]);
            nwoas_s175_ready=-1;return false;
        }
    }
    s175_ops ops={NULL,s175_read,s175_write,s175_barrier,s175_delay,s175_range};
    s175_result result;
    s175_status st=s175_dart1_sid1_translate(&ops,roots,&result);
    printf("[usb-s175] setup=%s restore=%s tcr=%x->%x roots=%x,%x,%x,%x wait=%u err=%x@%x:%x\n",
        s175_status_str(st),s175_restore_str(result.restore),result.before.tcr,
        result.after.tcr,roots[0],roots[1],roots[2],roots[3],result.flush_wait_us,
        result.after.error,result.after.error_hi,result.after.error_lo);
    nwoas_s175_ready=st==S175_OK?1:-1;
    return st==S175_OK;
}
void nwoas_usbc_s175_progress(u64 rt)
{
    static u32 count;
    if(nwoas_s175_ready!=1 || count>=8) return;
    count++;
    u64 erst=(read32(rt+0x30)|((u64)read32(rt+0x34)<<32))&~63UL;
    u64 pa=nwoas_ipa_to_pa(erst),ring=0,evpa=0;
    if(pa && nwoas_in_dram(pa,16)) ring=read64(pa)&~63UL;
    if(ring) evpa=nwoas_ipa_to_pa(ring);
    u32 ctl=0,status=0;
    if(evpa && nwoas_in_dram(evpa,16)) {nwoas_cache_maint(evpa,16,false);status=read32(evpa+8);ctl=read32(evpa+12);}
    printf("[usb-s175] irq%u iman=%x erdp=%x:%x evt0_type=%u cc=%u ctl=%x\n",
        count,read32(rt+0x20),read32(rt+0x3c),read32(rt+0x38),(ctl>>10)&63,status>>24,ctl);
}
