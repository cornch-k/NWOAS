#include "read_mirror.h"
#include <assert.h>
int main(void) {
    struct nwoas_read_mirror m={0}; uint64_t out;
    nwoas_mirror_publish(&m,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX);
    for(unsigned w=0;w<70;w++) {
        for(uint64_t a=0x6fffffffcULL;a<0x700000010ULL;a++)
            nwoas_mirror_read(&m,a,w,true,true,UINT32_MAX,&out);
        for(uint64_t a=0x700000ff8ULL;a<0x700001010ULL;a++)
            nwoas_mirror_read(&m,a,w,true,true,UINT32_MAX,&out);
        for(uint64_t a=0x700103ff8ULL;a<0x700104010ULL;a++)
            nwoas_mirror_read(&m,a,w,true,true,UINT32_MAX,&out);
    }
    assert(!nwoas_mirror_read(&m,UINT64_MAX,3,true,true,0,&out));
    assert(!nwoas_mirror_read(&m,0x700000000ULL,3,true,true,0,0));
    nwoas_mirror_publish(&m,0,0,0,0,0);
    assert(!nwoas_mirror_read(&m,0x700000000ULL,3,false,false,0,&out));
    return 0;
}
