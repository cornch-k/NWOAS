#include "read_mirror.h"
static struct nwoas_read_mirror mirror;
void publish(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{ nwoas_mirror_publish(&mirror,a,b,c,d,e); }
int read_projection(uint64_t address, unsigned width, int armed, int faulted, uint32_t mask, uint64_t *out)
{ return nwoas_mirror_read(&mirror,address,width,armed,faulted,mask,out); }
uint64_t counter(unsigned which)
{ return which == 0 ? mirror.pci_reads : which == 1 ? mirror.register_reads : mirror.publications; }
