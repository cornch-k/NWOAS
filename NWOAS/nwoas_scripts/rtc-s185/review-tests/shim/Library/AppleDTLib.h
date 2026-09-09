#ifndef NWOAS_REVIEW_SHIM_APPLEDTLIB_H
#define NWOAS_REVIEW_SHIM_APPLEDTLIB_H
#include <PiDxe.h>
/* Mock ADT: opaque node, same call shapes as AppleSiliconPkg AppleDTLib.h. */
typedef struct shim_dt_node dt_node_t;
dt_node_t *dt_get (const char *name);
void      *dt_node_prop (dt_node_t *node, const char *prop, size_t *size);
#endif
