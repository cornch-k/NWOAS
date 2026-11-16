/* NWOAS gpu-hv2: compile-time check of the launcher ABI (struct nwoas_agx_config)
 * with the m1n1 target flags. The Python module packs exactly these offsets. */
#include "nwoas_agx.h"
#include "hv_guest_ipa_pa.h"
#define OFF(f) __builtin_offsetof(struct nwoas_agx_config, f)
_Static_assert(sizeof(struct nwoas_agx_config) == 512, "config size");
_Static_assert(OFF(magic) == 0 && OFF(version) == 4 && OFF(size) == 8 && OFF(mode) == 12, "hdr");
_Static_assert(OFF(heap_limit) == 16 && OFF(local_reserve_ipa) == 24 && OFF(pool_window_ipa) == 32, "q");
_Static_assert(OFF(base_pstate_override) == 40 && OFF(flags) == 44 && OFF(guest_entry) == 48, "w");
_Static_assert(OFF(scanout_intid) == 56 && OFF(reserved) == 60 && OFF(memory) == 64, "m");
_Static_assert(sizeof(struct hv_contract_snapshot) == 448, "snapshot");
_Static_assert(sizeof(struct hv_contract_region) == 24, "region");
_Static_assert(sizeof(struct hv_guest_ipa_pa_request) == 16 + 64 * 16, "hvc request");
