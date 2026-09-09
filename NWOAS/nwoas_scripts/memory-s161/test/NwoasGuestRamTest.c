/*
 * NwoasGuestRamTest.c - native unit test for Include/Library/NwoasGuestRam.h (NWOAS S161).
 *
 * Compiles the REAL header (../Include/Library/NwoasGuestRam.h) against test/stub/Base.h.
 * Built twice by the Makefile: once with -DNWOAS_WINDOW_AFTER_RAM=1 (S102, current default)
 * and once with -DNWOAS_WINDOW_AFTER_RAM=0 (legacy) so the compile-time entry point
 * NwoasComputeGuestRamLayout() is exercised in both modes, not just the explicit-mode Ex().
 *
 * Reference vector (S158 live log usb-s158-20260909-234547):
 *   phys_base = 0x83CA9C000, mem_size = 0x2A4530000
 *   mode 1  -> backing 0xAE0FCC000  (== phys_base + mem_size, nothing subtracted)
 *   legacy  -> backing 0x9E0FCC000  (== phys_base + mem_size - 4 GiB, subtracted once)
 *
 * Exit status 0 == all checks passed.
 */

#include <stdio.h>
#include <string.h>

#include <Library/NwoasGuestRam.h>

/* ------------------------------------------------------------------------ */
/* Tiny check harness                                                        */
/* ------------------------------------------------------------------------ */

static int g_checks = 0;
static int g_fails  = 0;

#define CHECK(cond)                                                              \
  do {                                                                           \
    g_checks++;                                                                  \
    if (!(cond)) {                                                               \
      g_fails++;                                                                 \
      printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                    \
    }                                                                            \
  } while (0)

#define CHECK_EQ64(actual, expected)                                             \
  do {                                                                           \
    UINT64 _a = (UINT64)(actual);                                                \
    UINT64 _e = (UINT64)(expected);                                              \
    g_checks++;                                                                  \
    if (_a != _e) {                                                              \
      g_fails++;                                                                 \
      printf ("FAIL %s:%d: %s == 0x%llx, expected 0x%llx\n", __FILE__, __LINE__, \
              #actual, (unsigned long long)_a, (unsigned long long)_e);          \
    }                                                                            \
  } while (0)

#define CHECK_STATUS(actual, expected)                                           \
  do {                                                                           \
    NWOAS_GUEST_RAM_STATUS _a = (actual);                                        \
    NWOAS_GUEST_RAM_STATUS _e = (expected);                                      \
    g_checks++;                                                                  \
    if (_a != _e) {                                                              \
      g_fails++;                                                                 \
      printf ("FAIL %s:%d: %s -> %s, expected %s\n", __FILE__, __LINE__,         \
              #actual, NwoasGuestRamStatusName (_a), NwoasGuestRamStatusName (_e)); \
    }                                                                            \
  } while (0)

/* ------------------------------------------------------------------------ */
/* Reference inputs                                                          */
/* ------------------------------------------------------------------------ */

#define REF_PHYS_BASE      0x83CA9C000ULL
#define REF_MEM_SIZE       0x2A4530000ULL
#define REF_BACKING_MODE1  0xAE0FCC000ULL
#define REF_BACKING_LEGACY 0x9E0FCC000ULL
#define GIB4               0x100000000ULL
#define PG                 0x4000ULL

/* ------------------------------------------------------------------------ */
/* Tests                                                                     */
/* ------------------------------------------------------------------------ */

static void
TestConstants (void)
{
  CHECK_EQ64 (NWOAS_DMA_WIN_BASE, 0);
  CHECK_EQ64 (NWOAS_DMA_WIN_SIZE, GIB4);
  CHECK_EQ64 (NWOAS_DMA_WIN_END,  GIB4);              /* window top pinned at 4 GiB */
  CHECK_EQ64 (NWOAS_GUEST_PHYS_BASE_MIN, 0x800000000ULL);
  CHECK (NWOAS_DMA_WIN_END <= NWOAS_GUEST_PHYS_BASE_MIN); /* window and guest RAM cannot overlap */
  CHECK_EQ64 (NWOAS_DART_PAGE_SIZE, PG);
}

static void
TestReferenceVectorMode1 (void)
{
  NWOAS_GUEST_RAM_LAYOUT  L;

  memset (&L, 0xA5, sizeof (L));
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, REF_MEM_SIZE, TRUE, &L), NwoasGuestRamOk);

  CHECK_EQ64 (L.PhysBase,        REF_PHYS_BASE);
  CHECK_EQ64 (L.BootArgsMemSize, REF_MEM_SIZE);
  CHECK_EQ64 (L.GuestRamSize,    REF_MEM_SIZE);                 /* nothing subtracted */
  CHECK_EQ64 (L.GuestRamEnd,     REF_BACKING_MODE1);
  CHECK_EQ64 (L.BackingPa,       REF_BACKING_MODE1);
  CHECK_EQ64 (L.BackingPa,       L.GuestRamEnd);                /* backing directly follows RAM */
  CHECK_EQ64 (L.BackingEnd,      REF_BACKING_MODE1 + GIB4);
  CHECK_EQ64 (L.WindowBase,      0);
  CHECK_EQ64 (L.WindowSize,      GIB4);
  CHECK (L.WindowAfterRam == TRUE);

  /* The EBS identity end and the WIDE-DART table bound must now agree: L1Hi = (backing-1) >> 25. */
  CHECK_EQ64 ((L.BackingPa - 1) >> 25, 1392);
  CHECK_EQ64 (L.PhysBase >> 25, 1054);
}

static void
TestReferenceVectorLegacy (void)
{
  NWOAS_GUEST_RAM_LAYOUT  L;

  memset (&L, 0xA5, sizeof (L));
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, REF_MEM_SIZE, FALSE, &L), NwoasGuestRamOk);

  CHECK_EQ64 (L.GuestRamSize,  REF_MEM_SIZE - GIB4);            /* subtracted exactly once */
  CHECK_EQ64 (L.GuestRamEnd,   REF_BACKING_LEGACY);
  CHECK_EQ64 (L.BackingPa,     REF_BACKING_LEGACY);
  CHECK_EQ64 (L.BackingEnd,    REF_PHYS_BASE + REF_MEM_SIZE);   /* legacy backing ends at RAM top */
  CHECK_EQ64 (L.BackingEnd,    REF_BACKING_MODE1);              /* == the mode-1 backing START */
  CHECK (L.WindowAfterRam == FALSE);

  /* Documents the S158 observation: legacy/local arithmetic gave L1Hi 1264, 4 GiB short. */
  CHECK_EQ64 ((L.BackingPa - 1) >> 25, 1264);
}

static void
TestCompileTimeMode (void)
{
  NWOAS_GUEST_RAM_LAYOUT  L;
  NWOAS_GUEST_RAM_LAYOUT  Lex;

  CHECK_STATUS (NwoasComputeGuestRamLayout (REF_PHYS_BASE, REF_MEM_SIZE, &L), NwoasGuestRamOk);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, REF_MEM_SIZE,
                                              (BOOLEAN)(NWOAS_WINDOW_AFTER_RAM != 0), &Lex),
                NwoasGuestRamOk);
  /* Compare members: struct tail padding is not initialized by the API. */
  CHECK_EQ64 (L.PhysBase, Lex.PhysBase);
  CHECK_EQ64 (L.BootArgsMemSize, Lex.BootArgsMemSize);
  CHECK_EQ64 (L.GuestRamSize, Lex.GuestRamSize);
  CHECK_EQ64 (L.GuestRamEnd, Lex.GuestRamEnd);
  CHECK_EQ64 (L.BackingPa, Lex.BackingPa);
  CHECK_EQ64 (L.BackingEnd, Lex.BackingEnd);
  CHECK_EQ64 (L.WindowBase, Lex.WindowBase);
  CHECK_EQ64 (L.WindowSize, Lex.WindowSize);
  CHECK (L.WindowAfterRam == Lex.WindowAfterRam);

#if NWOAS_WINDOW_AFTER_RAM
  CHECK_EQ64 (L.BackingPa, REF_BACKING_MODE1);
  CHECK_EQ64 (NwoasGuestRamBackingPa (REF_PHYS_BASE, REF_MEM_SIZE), REF_BACKING_MODE1);
#else
  CHECK_EQ64 (L.BackingPa, REF_BACKING_LEGACY);
  CHECK_EQ64 (NwoasGuestRamBackingPa (REF_PHYS_BASE, REF_MEM_SIZE), REF_BACKING_LEGACY);
#endif

  /* Consumers agree: PrePi size + base == DART backing == HideHighRam log value. */
  CHECK_EQ64 (L.PhysBase + L.GuestRamSize, NwoasGuestRamBackingPa (REF_PHYS_BASE, REF_MEM_SIZE));
}

static void
TestBounds (void)
{
  NWOAS_GUEST_RAM_LAYOUT  L;

  /* NULL out pointer */
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, REF_MEM_SIZE, TRUE,  NULL), NwoasGuestRamErrNullOut);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, REF_MEM_SIZE, FALSE, NULL), NwoasGuestRamErrNullOut);

  /* phys_base floor: one page below 32 GiB fails, exactly 32 GiB passes, zero fails. */
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (NWOAS_GUEST_PHYS_BASE_MIN - PG, REF_MEM_SIZE, TRUE,  &L), NwoasGuestRamErrPhysBaseLow);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (NWOAS_GUEST_PHYS_BASE_MIN - PG, REF_MEM_SIZE, FALSE, &L), NwoasGuestRamErrPhysBaseLow);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (0, REF_MEM_SIZE, TRUE, &L), NwoasGuestRamErrPhysBaseLow);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (NWOAS_GUEST_PHYS_BASE_MIN, REF_MEM_SIZE, TRUE, &L), NwoasGuestRamOk);
  CHECK_EQ64 (L.BackingPa, NWOAS_GUEST_PHYS_BASE_MIN + REF_MEM_SIZE);

  /* Error paths leave the layout zeroed (no stale backing can leak into a consumer). */
  memset (&L, 0xA5, sizeof (L));
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (0, REF_MEM_SIZE, TRUE, &L), NwoasGuestRamErrPhysBaseLow);
  CHECK_EQ64 (L.BackingPa, 0);
  CHECK_EQ64 (L.GuestRamSize, 0);
  CHECK_EQ64 (L.BackingEnd, 0);

  /* 16 KiB alignment of phys_base and mem_size */
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE + 0x1000, REF_MEM_SIZE, TRUE, &L), NwoasGuestRamErrUnaligned);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, REF_MEM_SIZE + 0x2000, TRUE, &L), NwoasGuestRamErrUnaligned);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, REF_MEM_SIZE + 0x2000, FALSE, &L), NwoasGuestRamErrUnaligned);

  /* mem_size zero (both modes) */
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, 0, TRUE,  &L), NwoasGuestRamErrMemSizeZero);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, 0, FALSE, &L), NwoasGuestRamErrMemSizeZero);

  /* Legacy underflow guard: mem_size <= 4 GiB is rejected, 4 GiB + one page yields a 1-page guest. */
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, GIB4 - PG, FALSE, &L), NwoasGuestRamErrMemSizeTooSmall);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, GIB4,      FALSE, &L), NwoasGuestRamErrMemSizeTooSmall);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, GIB4 + PG, FALSE, &L), NwoasGuestRamOk);
  CHECK_EQ64 (L.GuestRamSize, PG);
  CHECK_EQ64 (L.BackingPa,    REF_PHYS_BASE + PG);
  CHECK_EQ64 (L.BackingEnd,   REF_PHYS_BASE + GIB4 + PG);

  /* Mode 1 has no such floor: mem_size == 4 GiB or one page is a valid guest. */
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, GIB4, TRUE, &L), NwoasGuestRamOk);
  CHECK_EQ64 (L.GuestRamSize, GIB4);
  CHECK_EQ64 (L.BackingPa,    REF_PHYS_BASE + GIB4);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, PG, TRUE, &L), NwoasGuestRamOk);
  CHECK_EQ64 (L.BackingPa,    REF_PHYS_BASE + PG);

  /* UINT64 overflow: phys_base + size wraps; phys_base + size + window wraps. */
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (0xFFFFFFFFFFFFC000ULL, PG, TRUE, &L), NwoasGuestRamErrOverflow);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (0xFFFFFFFFFFFFC000ULL, 2 * PG, TRUE, &L), NwoasGuestRamErrOverflow);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (0xFFFFFFFF00000000ULL, PG, TRUE, &L), NwoasGuestRamErrOverflow);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (0xFFFFFFFF00000000ULL, GIB4 + PG, FALSE, &L), NwoasGuestRamErrOverflow);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, MAX_UINT64 - 0x3FFFULL, TRUE, &L), NwoasGuestRamErrOverflow);

  /* PA limit: backing END may equal 64 GiB, not exceed it. phys_base 32 GiB + size + 4 GiB window. */
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (NWOAS_GUEST_PHYS_BASE_MIN,
                                              NWOAS_GUEST_PA_LIMIT - NWOAS_GUEST_PHYS_BASE_MIN - GIB4,
                                              TRUE, &L), NwoasGuestRamOk);
  CHECK_EQ64 (L.BackingEnd, NWOAS_GUEST_PA_LIMIT);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (NWOAS_GUEST_PHYS_BASE_MIN,
                                              NWOAS_GUEST_PA_LIMIT - NWOAS_GUEST_PHYS_BASE_MIN - GIB4 + PG,
                                              TRUE, &L), NwoasGuestRamErrAboveLimit);
  /* legacy: mem_size includes the window, so limit - base is the largest legal mem_size */
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (NWOAS_GUEST_PHYS_BASE_MIN,
                                              NWOAS_GUEST_PA_LIMIT - NWOAS_GUEST_PHYS_BASE_MIN,
                                              FALSE, &L), NwoasGuestRamOk);
  CHECK_EQ64 (L.BackingEnd, NWOAS_GUEST_PA_LIMIT);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (NWOAS_GUEST_PHYS_BASE_MIN,
                                              NWOAS_GUEST_PA_LIMIT - NWOAS_GUEST_PHYS_BASE_MIN + PG,
                                              FALSE, &L), NwoasGuestRamErrAboveLimit);

  /* Convenience wrapper returns 0 on every failure, never a stale/partial value. */
  CHECK_EQ64 (NwoasGuestRamBackingPa (0, REF_MEM_SIZE), 0);
  CHECK_EQ64 (NwoasGuestRamBackingPa (REF_PHYS_BASE, 0), 0);
  CHECK_EQ64 (NwoasGuestRamBackingPa (REF_PHYS_BASE + 1, REF_MEM_SIZE), 0);
  CHECK_EQ64 (NwoasGuestRamBackingPa (0xFFFFFFFFFFFFC000ULL, PG), 0);

  /* Status names are non-NULL for every enum value (used in DEBUG %a). */
  {
    int s;
    for (s = 0; s <= (int)NwoasGuestRamStatusMax; s++) {
      CHECK (NwoasGuestRamStatusName ((NWOAS_GUEST_RAM_STATUS)s) != NULL);
    }
  }
}

static void
TestDisjointRangesFor (BOOLEAN WindowAfterRam)
{
  NWOAS_GUEST_RAM_LAYOUT  L;
  UINT64                  Probe[12];
  UINTN                   i;

  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, REF_MEM_SIZE, WindowAfterRam, &L), NwoasGuestRamOk);

  /* Window strictly precedes guest RAM; guest RAM abuts backing; no overlaps. */
  CHECK (NWOAS_DMA_WIN_END <= L.PhysBase);
  CHECK (L.PhysBase < L.GuestRamEnd);
  CHECK (L.GuestRamEnd == L.BackingPa);
  CHECK (L.BackingPa < L.BackingEnd);
  CHECK (L.BackingEnd - L.BackingPa == NWOAS_DMA_WIN_SIZE);

  /* Boundary predicates (half-open intervals). */
  CHECK (NwoasPaInLowWindow (0)                == TRUE);
  CHECK (NwoasPaInLowWindow (GIB4 - 1)         == TRUE);
  CHECK (NwoasPaInLowWindow (GIB4)             == FALSE);
  CHECK (NwoasPaInGuestRam (&L, L.PhysBase - 1)      == FALSE);
  CHECK (NwoasPaInGuestRam (&L, L.PhysBase)          == TRUE);
  CHECK (NwoasPaInGuestRam (&L, L.GuestRamEnd - 1)   == TRUE);
  CHECK (NwoasPaInGuestRam (&L, L.GuestRamEnd)       == FALSE);
  CHECK (NwoasPaInBacking  (&L, L.BackingPa - 1)     == FALSE);
  CHECK (NwoasPaInBacking  (&L, L.BackingPa)         == TRUE);
  CHECK (NwoasPaInBacking  (&L, L.BackingEnd - 1)    == TRUE);
  CHECK (NwoasPaInBacking  (&L, L.BackingEnd)        == FALSE);

  /* No probe address is ever a member of two ranges. */
  Probe[0]  = 0;
  Probe[1]  = 0x20000000ULL;
  Probe[2]  = GIB4 - 1;
  Probe[3]  = GIB4;
  Probe[4]  = L.PhysBase - 1;
  Probe[5]  = L.PhysBase;
  Probe[6]  = L.PhysBase + (L.GuestRamSize / 2);
  Probe[7]  = L.GuestRamEnd - 1;
  Probe[8]  = L.GuestRamEnd;
  Probe[9]  = L.BackingPa + 0x40000ULL;
  Probe[10] = L.BackingEnd - 1;
  Probe[11] = L.BackingEnd;
  for (i = 0; i < sizeof (Probe) / sizeof (Probe[0]); i++) {
    int n = 0;
    if (NwoasPaInLowWindow (Probe[i]))      n++;
    if (NwoasPaInGuestRam (&L, Probe[i]))   n++;
    if (NwoasPaInBacking  (&L, Probe[i]))   n++;
    CHECK (n <= 1);
  }

  /* Window IPA -> backing translation (the DART table / per-Map path). */
  CHECK_EQ64 (NwoasWindowIpaToBackingPa (&L, 0),            L.BackingPa);
  CHECK_EQ64 (NwoasWindowIpaToBackingPa (&L, 0x40000ULL),   L.BackingPa + 0x40000ULL);
  CHECK_EQ64 (NwoasWindowIpaToBackingPa (&L, GIB4 - PG),    L.BackingEnd - PG);
  CHECK_EQ64 (NwoasWindowIpaToBackingPa (&L, L.PhysBase),   L.PhysBase);      /* passthrough */
  CHECK_EQ64 (NwoasWindowIpaToBackingPa (&L, GIB4),         GIB4);            /* just outside */
  CHECK (NwoasPaInBacking (&L, NwoasWindowIpaToBackingPa (&L, GIB4 - 1)) == TRUE);

  /* Invalid / NULL layout: predicates are FALSE, translation is identity. */
  {
    NWOAS_GUEST_RAM_LAYOUT Z;
    memset (&Z, 0, sizeof (Z));
    CHECK (NwoasPaInGuestRam (&Z, L.PhysBase) == FALSE);
    CHECK (NwoasPaInBacking  (&Z, L.BackingPa) == FALSE);
    CHECK (NwoasPaInGuestRam (NULL, L.PhysBase) == FALSE);
    CHECK (NwoasPaInBacking  (NULL, L.BackingPa) == FALSE);
    CHECK_EQ64 (NwoasWindowIpaToBackingPa (&Z,   0x1000), 0x1000);
    CHECK_EQ64 (NwoasWindowIpaToBackingPa (NULL, 0x1000), 0x1000);
  }
}

static void
TestModesAreExactlyFourGiBApart (void)
{
  NWOAS_GUEST_RAM_LAYOUT  M1;
  NWOAS_GUEST_RAM_LAYOUT  M0;

  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, REF_MEM_SIZE, TRUE,  &M1), NwoasGuestRamOk);
  CHECK_STATUS (NwoasComputeGuestRamLayoutEx (REF_PHYS_BASE, REF_MEM_SIZE, FALSE, &M0), NwoasGuestRamOk);
  /* S158 symptom reproduced by construction: the two interpretations differ by exactly the window. */
  CHECK_EQ64 (M1.BackingPa - M0.BackingPa, GIB4);
  CHECK_EQ64 (M1.GuestRamSize - M0.GuestRamSize, GIB4);
  /* Legacy backing == the mode-1 guest RAM's last 4 GiB: proves double-subtraction would be wrong. */
  CHECK (NwoasPaInGuestRam (&M1, M0.BackingPa) == TRUE);
}

int
main (void)
{
  printf ("NwoasGuestRam.h unit test, NWOAS_WINDOW_AFTER_RAM=%d\n", NWOAS_WINDOW_AFTER_RAM);

  TestConstants ();
  TestReferenceVectorMode1 ();
  TestReferenceVectorLegacy ();
  TestCompileTimeMode ();
  TestBounds ();
  TestDisjointRangesFor (TRUE);
  TestDisjointRangesFor (FALSE);
  TestModesAreExactlyFourGiBApart ();

  printf ("%s: %d checks, %d failures\n", g_fails == 0 ? "PASS" : "FAIL", g_checks, g_fails);
  return g_fails == 0 ? 0 : 1;
}
