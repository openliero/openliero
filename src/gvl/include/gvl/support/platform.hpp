#pragma once

namespace gvl
{

struct noncopyable
{
	noncopyable() {}
	~noncopyable() {}
	noncopyable(const noncopyable&) = delete;
	noncopyable& operator=(const noncopyable&) = delete;
};

}

// assorted defines from old platform.h
#if !defined(GVL_CPP)
#if defined(__cplusplus)
#define GVL_CPP 1
#else
#define GVL_CPP 0
#endif
#endif

#if !defined(GVL_MSVCPP)
# if defined(_MSC_VER)
#  define GVL_MSVCPP _MSC_VER
# else
#  define GVL_MSVCPP 0
# endif
#endif

#if !defined(GVL_X86_64)
# if defined(_M_X64) || defined(__x86_64__)
#  define GVL_X86_64 1
# endif
#endif

#if defined(__cplusplus) // We don't test GVL_CPP here, because we assume we can always use C++ inline in C++
# define GVL_INLINE inline
# define GVL_FORCE_INLINE __forceinline
#elif GVL_GCC
# define GVL_INLINE static inline
#elif GVL_MSVCPP
# define GVL_INLINE __inline
#else
# define GVL_INLINE static
#endif

#if !defined(GVL_CPP0X)
# if GVL_MSVCPP >= 1600
#  define GVL_CPP0X 1 // C++0x level 1
# else
#  define GVL_CPP0X 0
# endif
#endif
