#pragma once

#include "gvl/support/platform.hpp"

#if GVL_WINDOWS
#undef  NOMINMAX
#define NOMINMAX
#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NONAMELESSUNION
#define NONAMELESSUNION
#undef  NOKERNEL
#define NOKERNEL
#undef  NONLS
#define NONLS


#ifndef _WIN32_WINDOWS
#if GVL_WIN32
#define _WIN32_WINDOWS 0x0410
#endif
#endif

#ifndef WINVER
#define WINVER 0x0410
#endif

#include <windows.h>

#endif
