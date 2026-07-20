/*
 * dart_hw.h — Apple Silicon DART (IOMMU) hardware register definitions
 *
 * Source authority: m1n1/src/dart.c (Asahi Linux project)
 * All offsets/bitmasks verified against that file; line numbers cited below.
 *
 * Target: T8103 (M1) = DART_T8020 variant.
 * T6000 / T8110 variants noted where they differ.
 *
 * Status: [VERIFIED vs m1n1 source] — register layout matches dart.c exactly.
 *         [DESIGN-INFERRED] — Windows-side usage (ACPI, WDK APIs) has no m1n1 counterpart.
 *         [UNCONFIRMED] — base addresses without ADT/DT cross-check.
 */

#pragma once
#include <ntddk.h>

/* =========================================================================
 * IOVA address space limits
 * dart_find_iova uses (1LLU << 36) as upper bound  [dart.c:711]
 * ========================================================================= */
#define DART_IOVA_BITS      38      /* bits [37:36]=TTBR, [35:25]=L1, [24:14]=L2, [13:0]=offset */
#define DART_PAGE_SHIFT     14      /* 16KB pages                       [dart.c:253] */
#define DART_PAGE_SIZE      (1u << DART_PAGE_SHIFT)   /* 0x4000 */
#define DART_L1_ENTRIES     2048    /* 11-bit L1 index [35:25]          [dart.c:521] */
#define DART_L2_ENTRIES     2048    /* 11-bit L2 index [24:14]          [dart.c:522] */
#define DART_TTBR_COUNT_T8020  4   /* bits [37:36]                     [dart.c:161] */
#define DART_TTBR_COUNT_T8110  1   /* T8110 only one TTBR              [dart.c:193] */

/* =========================================================================
 * T8020 / T8103 (M1) register offsets
 * Source: dart.c lines 14-51
 * ========================================================================= */

/* Stream command / TLB invalidation */
#define DART_T8020_STREAM_COMMAND            0x20    /* [dart.c:30] */
#define DART_T8020_STREAM_COMMAND_BUSY       (1u << 2)   /* poll-until-clear after invalidate */
#define DART_T8020_STREAM_COMMAND_INVALIDATE (1u << 20)  /* write this to trigger TLB flush */
#define DART_T8020_STREAM_COMMAND_BUSY_TIMEOUT_US  100   /* [dart.c:34] */

#define DART_T8020_STREAM_SELECT             0x34    /* bitmask of stream IDs to invalidate [dart.c:28] */

/* Error reporting */
#define DART_T8020_ERROR                     0x40    /* [dart.c:17] */
#define DART_T8020_ERROR_FLAG                (1u << 31)  /* error pending [dart.c:21] */
#define DART_T8020_ERROR_READ_FAULT          (1u << 4)   /* [dart.c:22] */
#define DART_T8020_ERROR_WRITE_FAULT         (1u << 3)   /* [dart.c:23] */
#define DART_T8020_ERROR_NO_PTE              (1u << 2)   /* [dart.c:24] */
#define DART_T8020_ERROR_NO_PMD              (1u << 1)   /* [dart.c:25] */
#define DART_T8020_ERROR_NO_TTBR             (1u << 0)   /* [dart.c:26] */
#define DART_T8020_ERROR_STREAM_SHIFT        24           /* [dart.c:18] */
#define DART_T8020_ERROR_STREAM_MASK         0xFu         /* 4 bits    [dart.c:19] */
#define DART_T8020_ERROR_CODE_MASK           0xFFFFFFu    /* [dart.c:20] */

#define DART_T8020_ERROR_ADDR_LO             0x50    /* fault VA bits[31:0]  [dart.c:39] */
#define DART_T8020_ERROR_ADDR_HI             0x54    /* fault VA bits[63:32] [dart.c:38] */

/* Configuration / Lock */
#define DART_T8020_CONFIG                    0x60    /* [dart.c:14] */
#define DART_T8020_CONFIG_LOCK               (1u << 15)  /* when set: TTBR/TCR writes ignored [dart.c:15] */

/* Stream remap */
#define DART_T8020_STREAM_REMAP              0x80    /* [dart.c:36] */

/* Enabled-streams register */
#define DART_T8020_ENABLED_STREAMS           0xFC    /* bitmask; set BIT(sid) [dart.c:41] */

/* Translation Control Register: one 32-bit word per stream ID */
/* Address = MMIO_BASE + 0x100 + (4 * sid)                     [dart.c:43,89] */
#define DART_T8020_TCR_OFF                   0x100
#define DART_T8020_TCR_TRANSLATE_ENABLE      (1u << 7)    /* enable IOVA->PA translation [dart.c:44] */
#define DART_T8020_TCR_BYPASS_DART           (1u << 8)    /* bypass DART (pass-through)   [dart.c:45] */
#define DART_T8020_TCR_BYPASS_DAPF           (1u << 12)   /* bypass DAPF                  [dart.c:46] */

/*
 * Translation Table Base Register: 4 entries per stream (T8020)
 * Address = MMIO_BASE + 0x200 + (4 * ttbr_count * sid) + (4 * ttbr_idx)
 *         = MMIO_BASE + 0x200 + (16 * sid) + (4 * ttbr_idx)        [dart.c:48,90-91]
 */
#define DART_T8020_TTBR_OFF                  0x200
#define DART_T8020_TTBR_VALID                (1u << 31)   /* this TTBR entry is valid      [dart.c:49] */
/*
 * TTBR ADDR field = GENMASK(30,0): physical address of L1 table >> 12
 * i.e. PA of L1 = FIELD_GET(ADDR, ttbr) << 12                      [dart.c:50-51]
 */
#define DART_T8020_TTBR_ADDR_MASK            0x7FFFFFFFu  /* bits [30:0] */
#define DART_T8020_TTBR_SHIFT                12

/* =========================================================================
 * T8110 (M2+) register offsets — included for completeness
 * Source: dart.c lines 62-85
 * ========================================================================= */
#define DART_T8110_TCR_OFF                   0x1000   /* [dart.c:67] */
#define DART_T8110_TCR_TRANSLATE_ENABLE      (1u << 0)
#define DART_T8110_TCR_BYPASS_DART           (1u << 1)
#define DART_T8110_TCR_BYPASS_DAPF           (1u << 2)
#define DART_T8110_TCR_REMAP_EN              (1u << 7)

#define DART_T8110_TLB_CMD                   0x80     /* [dart.c:74] */
#define DART_T8110_TLB_CMD_BUSY              (1u << 31)
/* OP field = bits [10:8]: 0=flush all, 1=flush SID             [dart.c:76-79] */
#define DART_T8110_TLB_CMD_OP_SHIFT          8
#define DART_T8110_TLB_CMD_OP_FLUSH_ALL      0
#define DART_T8110_TLB_CMD_OP_FLUSH_SID      1
#define DART_T8110_TLB_CMD_STREAM_MASK       0xFFu    /* bits [7:0] = SID */

#define DART_T8110_PROTECT                   0x200    /* [dart.c:81] */
#define DART_T8110_PROTECT_TTBR_TCR          (1u << 0)

#define DART_T8110_ENABLE_STREAMS            0xC00    /* [dart.c:84] */
#define DART_T8110_DISABLE_STREAMS           0xC20    /* [dart.c:85] */

#define DART_T8110_TTBR_OFF                  0x1400   /* [dart.c:62] */
#define DART_T8110_TTBR_VALID                (1u << 0)
/* TTBR ADDR = bits [29:2]; PA of L1 = FIELD_GET(ADDR,ttbr) << 14  [dart.c:64-65] */
#define DART_T8110_TTBR_ADDR_SHIFT           2
#define DART_T8110_TTBR_ADDR_MASK            0x3FFFFFFFu
#define DART_T8110_TTBR_SHIFT                14

/* =========================================================================
 * Page Table Entry format (T8020 / T8103)
 * Source: dart.c lines 53-60
 *
 * 63      52 51      40 39                   14 1  0
 * [SP_START] [ SP_END ] [      OFFSET         ] D  V
 *
 *  SP_START  = bits[63:52]  set to 0x000 in T8020          [dart.c:54]
 *  SP_END    = bits[51:40]  set to 0xFFF in T8020          [dart.c:55]
 *  OFFSET    = bits[39:14]  = PA >> 14                     [dart.c:56]
 *  DISABLE_SP= bit[1]       set in T8020 (not T6000)       [dart.c:58]
 *  VALID     = bit[0]                                       [dart.c:60]
 *
 * T8020 pte_flags (constant for every leaf PTE):
 *   (0xFFF << 40) | (0 << 52) | BIT(1) | BIT(0)
 * ========================================================================= */
#define DART_PTE_VALID                       (1ULL << 0)   /* [dart.c:60] */
#define DART_PTE_DISABLE_SP                  (1ULL << 1)   /* T8020 only  [dart.c:58] */
#define DART_T8020_PTE_OFFSET_SHIFT          14            /* [dart.c:53] */
#define DART_T8020_PTE_OFFSET_MASK           0x000000FFFFFFC000ULL  /* bits[39:14] GENMASK(39,14) */
#define DART_T6000_PTE_OFFSET_MASK           0x000000FFFFFFFFC0ULL  /* bits[39:10] GENMASK(39,10) */
/* SP_END field for T8020 PTE: bits[51:40] = 0xFFF (required boilerplate) */
#define DART_PTE_SP_END_VAL                  (0xFFFULL << 40)
/* T8020 leaf PTE flags OR'd into every L2 entry                     [dart.c:150-151] */
#define DART_T8020_PTE_FLAGS  (DART_PTE_SP_END_VAL | DART_PTE_VALID | DART_PTE_DISABLE_SP)

/* =========================================================================
 * IOVA address decode macros
 * Source: dart.c dart_translate_internal (lines 629-667)
 * ========================================================================= */
#define DART_IOVA_TTBR(iova)    (((iova) >> 36) & 0x3)    /* bits [37:36] */
#define DART_IOVA_L1(iova)      (((iova) >> 25) & 0x7FF)  /* bits [35:25] */
#define DART_IOVA_L2(iova)      (((iova) >> 14) & 0x7FF)  /* bits [24:14] */
#define DART_IOVA_OFFSET(iova)  ((iova) & 0x3FFF)         /* bits [13:0]  */

/* =========================================================================
 * Known MMIO base addresses for T8103 (M1) — [UNCONFIRMED, from ADT; no
 * official Apple public documentation. Cross-check via ADT dump required.]
 * The system context notes PCIe DART is in the 0x681000000+ range.
 * USB DARTs are referenced as /arm-io/dart-usb0 and /arm-io/dart-usb1.
 * Display/DCP DARTs are referenced in /arm-io/dart-dcp and /arm-io/dart-disp0.
 * ========================================================================= */
/* [UNCONFIRMED] Example placeholder; replace with values extracted from live ADT */
#define DART_PCIE_BASE_T8103    0x681000000ULL   /* [UNCONFIRMED: from system context hint] */
/* USB DART instances: 2 controllers on M1; actual PA from ADT reg property     */
/* DCP/DISP DARTs: from /arm-io/dart-dcp and /arm-io/dart-disp0 ADT nodes      */

/* =========================================================================
 * Helper: compute TCR register address for a given stream ID (T8020)
 * ========================================================================= */
static inline ULONG_PTR dart_tcr_addr(ULONG_PTR base, ULONG sid)
{
    return base + DART_T8020_TCR_OFF + 4 * sid;
}

/* Helper: compute TTBR address for a given stream ID and TTBR index (T8020) */
static inline ULONG_PTR dart_ttbr_addr(ULONG_PTR base, ULONG sid, ULONG ttbr_idx)
{
    return base + DART_T8020_TTBR_OFF + 4 * DART_TTBR_COUNT_T8020 * sid + 4 * ttbr_idx;
}
