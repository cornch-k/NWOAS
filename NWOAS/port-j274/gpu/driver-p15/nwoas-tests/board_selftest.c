/* NWOAS-J274: compile-time check of the board switch (apple_agx_board.h).
 * Compile only; nothing here runs on, or proves anything about, hardware.
 *   clang -fsyntax-only -std=c11 -I../drivers/apple-agx/shared/include \
 *         -I../drivers/apple-agx/render-admission/include board_selftest.c
 *   (add -DAPPLE_AGX_BOARD_J274=1 for the J274 expectations) */
#include "j313_agx_g2.generated.h"
#include "j313_agx_abi_admission.generated.h"
#include "apple_agx_hwdata_profile.generated.h"

#if defined(APPLE_AGX_BOARD_J274) && APPLE_AGX_BOARD_J274
#define EXPECT_GPU 0xbfffb8000ULL
#define EXPECT_SHARED 0xbfff78000ULL
#define EXPECT_HANDOFF 0xbfff70000ULL
#define EXPECT_BOARD 0x22u
#define EXPECT_TOP_BYTE 0x0b
#else
#define EXPECT_GPU 0x9fffb8000ULL
#define EXPECT_SHARED 0x9fff78000ULL
#define EXPECT_HANDOFF 0x9fff70000ULL
#define EXPECT_BOARD 0x26u
#define EXPECT_TOP_BYTE 0x09
#endif

_Static_assert(J313_AGX_G2_GPU_BASE == EXPECT_GPU, "g2 gpu base");
_Static_assert(J313_AGX_G2_SHARED_BASE == EXPECT_SHARED, "g2 shared base");
_Static_assert(J313_AGX_G2_HANDOFF_BASE == EXPECT_HANDOFF, "g2 handoff base");
_Static_assert(J313_AGX_ABI_ADMISSION_GPU_BASE == EXPECT_GPU, "abi gpu base");
_Static_assert(J313_AGX_ABI_ADMISSION_HANDOFF_BASE == EXPECT_HANDOFF, "abi handoff");
_Static_assert(APPLE_AGX_BOARD_ID == EXPECT_BOARD, "board id");
/* Layout invariants the J313 contract relies on, kept for J274. */
_Static_assert(J313_AGX_G2_SHARED_BASE + J313_AGX_G2_SHARED_SIZE == J313_AGX_G2_GPU_BASE, "shared ends at gpu region");
_Static_assert(J313_AGX_G2_HANDOFF_BASE + 0x8000ULL == J313_AGX_G2_SHARED_BASE, "handoff +0x8000 = shared");
_Static_assert((J313_AGX_G2_GPU_BASE & 0x3fffULL) == 0, "16 KiB aligned");
_Static_assert(J313_AGX_G2_GPU_BASE < (1ULL << 40), "below 40-bit PA limit");
_Static_assert(APPLE_AGX_BOARD_HWDATA_B_GPU_REGION_BASE_OFFSET + 8u <= AGX_HWDATA_B_BYTES, "hwdataB patch in bounds");

/* gpu-region-base ADT input bytes (little endian) */
int main(void) { return AgxHwdataInputBytes39[4] == EXPECT_TOP_BYTE ? 0 : 1; }
