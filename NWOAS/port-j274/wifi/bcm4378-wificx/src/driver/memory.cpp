// memory.cpp - C++ allocation for the WiFiCx TLV generator/parser library
// (WificxTLVGenParse.lib expects operator new(size_t, ULONG_PTR) and delete[]).
// AllocationContext 0 = pool allocation (all our calls use 0).
#include "precomp.h"

#define NWOA_NEW_TAG 'N4WN'

static void* Alloc(size_t n)
{
    if (!n)
        n = 1;
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, n, NWOA_NEW_TAG);  // zeroed
}

static void Free(void* p)
{
    if (p)
        ExFreePoolWithTag(p, NWOA_NEW_TAG);
}

void* __cdecl operator new(size_t n) noexcept { return Alloc(n); }
void* __cdecl operator new[](size_t n) noexcept { return Alloc(n); }
void* __cdecl operator new(size_t n, ULONG_PTR ctx) noexcept { UNREFERENCED_PARAMETER(ctx); return Alloc(n); }
void* __cdecl operator new[](size_t n, ULONG_PTR ctx) noexcept { UNREFERENCED_PARAMETER(ctx); return Alloc(n); }
void __cdecl operator delete(void* p) noexcept { Free(p); }
void __cdecl operator delete[](void* p) noexcept { Free(p); }
void __cdecl operator delete(void* p, ULONG_PTR) noexcept { Free(p); }
void __cdecl operator delete[](void* p, ULONG_PTR) noexcept { Free(p); }
