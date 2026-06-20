// ---------------------------------------------------------------------------
// Case-insensitive shim for building Leaf SD Tools with cegcc / mingw32ce.
//
// The sources include "stdafx.h" (lowercase), but the real precompiled-header
// file in ../LeafSDTools is named "StdAfx.h". On Windows (eMbedded Visual C++)
// the filesystem is case-insensitive, so this never mattered. On Linux the
// filesystem is case-sensitive and the lowercase include fails to resolve.
//
// build-linux/compat is on the -I include path (ahead of ../LeafSDTools), so
// this lowercase shim is found first and simply forwards to the real header,
// which lives in ../LeafSDTools and is reached via the -I$(SRCDIR) path.
// ---------------------------------------------------------------------------
#include "StdAfx.h"
