#pragma once

#include <cstdint>

#include "gvl/support/platform.hpp"

#if GVL_MSVCPP
# include <stdlib.h>
# include <intrin.h>
# include <limits.h>
# pragma intrinsic(_BitScanReverse)
# pragma intrinsic(_BitScanForward)
# if GVL_X86_64
#  pragma intrinsic(_BitScanReverse64)
# endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if GVL_MSVCPP
inline int gvl_bottom_bit(uint32_t v)
{
	unsigned long r;
	if(!_BitScanForward(&r, v))
		return -1;
	return r;
}
#else
int gvl_bottom_bit(uint32_t v);
#endif

inline int32_t gvl_uint32_as_int32(uint32_t x)
{
	if(x >= 0x80000000)
		return (int32_t)(x - 0x80000000u) - 0x80000000;
	else
		return (int32_t)(x);
}

inline uint32_t gvl_int32_as_uint32(int32_t x)
{
	if(x < 0)
		return (uint32_t)(x + 0x80000000) + 0x80000000u;
	else
		return (uint32_t)(x);
}

#ifdef __cplusplus
} // extern "C"
#endif

#if defined(__cplusplus)
namespace gvl
{

inline int bottom_bit(uint32_t v) { return gvl_bottom_bit(v); }
inline int32_t uint32_as_int32(uint32_t x) { return gvl_uint32_as_int32(x); }
inline uint32_t int32_as_uint32(int32_t x) { return gvl_int32_as_uint32(x); }
}
#endif
