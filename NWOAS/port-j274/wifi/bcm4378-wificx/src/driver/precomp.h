// precomp.h - common includes for the NWOAS BCM4378 WiFiCx driver (nwoa4378.sys).
#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <wdmguid.h>          // GUID_BUS_INTERFACE_STANDARD
#include <ntstrsafe.h>
#include <bcrypt.h>           // BCryptGenRandom (firmware random seed)

#include <netadaptercx.h>
#include <netiodef.h>
#include <net/ring.h>
#include <net/ringcollection.h>
#include <net/packet.h>
#include <net/fragment.h>
#include <net/virtualaddress.h>
#include <net/virtualaddresstypes.h>

#include <wificx.h>           // dot11wificxintf.h / dot11wificxtypes.hpp
// allocation used by the TLV library templates (defined in memory.cpp)
void* __cdecl operator new(size_t n, ULONG_PTR ctx) noexcept;
void* __cdecl operator new[](size_t n, ULONG_PTR ctx) noexcept;
void __cdecl operator delete(void* p, ULONG_PTR ctx) noexcept;
void __cdecl operator delete[](void* p, ULONG_PTR ctx) noexcept;
#include <TlvGeneratorParser.hpp>

#include "../core/nwbc_core.h"
#include "nwoa.h"
