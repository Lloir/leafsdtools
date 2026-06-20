#ifndef DDRAW_H
#define DDRAW_H

#include <windows.h>

/*
 * Minimal ddraw.h shim for cegcc/mingw32ce to support DisplayDirectDraw.cpp
 */

#ifdef __cplusplus
#include <unknwn.h>

struct IDirectDraw : public IUnknown {};
struct IDirectDrawSurface : public IUnknown {};

typedef IDirectDraw *LPDIRECTDRAW;
typedef IDirectDrawSurface *LPDIRECTDRAWSURFACE;

extern "C" {
#else
typedef struct IDirectDraw *LPDIRECTDRAW;
typedef struct IDirectDrawSurface *LPDIRECTDRAWSURFACE;
typedef struct IUnknown IUnknown;
#endif

// Structs
typedef struct _DDBLTFX {
    DWORD dwSize;
} DDBLTFX, *LPDDBLTFX;

// Constants used in the code
#ifndef DDSD_CAPS
#define DDSD_CAPS 0x00000001
#endif

#ifdef __cplusplus
}
#endif

#endif // DDRAW_H
