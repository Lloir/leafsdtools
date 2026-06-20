// ---------------------------------------------------------------------------
// Minimal <ceddk.h> shim for building Leaf SD Tools with cegcc / mingw32ce.
//
// The real Windows CE DDK header (ceddk.h) ships with eMbedded Visual C++ /
// Platform Builder and mostly provides register-access macros, physical
// memory mapping helpers (VirtualCopy/TransBusAddrToStatic, ...) and a few
// DDK typedefs. Leaf SD Tools only pulls it in transitively via StdAfx.h and
// does not actually call those DDK primitives, so this thin stand-in is enough
// to let the project compile when the cegcc SDK does not provide ceddk.h.
//
// If your cegcc SDK DOES provide a real ceddk.h, delete the build-linux/compat
// include from CPPFLAGS (or remove this file) so the genuine header is used.
// ---------------------------------------------------------------------------
#ifndef LEAFSD_CEDDK_SHIM_H
#define LEAFSD_CEDDK_SHIM_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// PHYSICAL_ADDRESS is a LARGE_INTEGER on Windows CE.
#ifndef _PHYSICAL_ADDRESS_DEFINED
#define _PHYSICAL_ADDRESS_DEFINED
typedef LARGE_INTEGER PHYSICAL_ADDRESS;
#endif

// Interface / bus type enums referenced by some DDK signatures.
#ifndef _INTERFACE_TYPE_DEFINED
#define _INTERFACE_TYPE_DEFINED
typedef enum _INTERFACE_TYPE {
    InterfaceTypeUndefined = -1,
    Internal,
    Isa,
    Eisa,
    MicroChannel,
    TurboChannel,
    PCIBus,
    VMEBus,
    NuBus,
    PCMCIABus,
    CBus,
    MPIBus,
    MPSABus,
    ProcessorInternal,
    InternalPowerBus,
    PNPISABus,
    PNPBus,
    MaximumInterfaceType
} INTERFACE_TYPE, *PINTERFACE_TYPE;
#endif

// Register access primitives. On the QY units these resolve to plain memory
// access; provide portable fallbacks for compile compatibility only.
#ifndef READ_REGISTER_ULONG
#define READ_REGISTER_ULONG(p)        (*(volatile ULONG  * const)(p))
#define WRITE_REGISTER_ULONG(p, v)    (*(volatile ULONG  * const)(p) = (v))
#define READ_REGISTER_USHORT(p)       (*(volatile USHORT * const)(p))
#define WRITE_REGISTER_USHORT(p, v)   (*(volatile USHORT * const)(p) = (v))
#define READ_REGISTER_UCHAR(p)        (*(volatile UCHAR  * const)(p))
#define WRITE_REGISTER_UCHAR(p, v)    (*(volatile UCHAR  * const)(p) = (v))
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LEAFSD_CEDDK_SHIM_H
