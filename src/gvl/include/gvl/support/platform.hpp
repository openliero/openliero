# pragma once

#include "gvl/support/platform.h"

#if GVL_CPP0X && GVL_CPP
#include <utility>
#define GVL_MOVE(x) (::std::move(x))
#else
#define GVL_MOVE(x) (x)
#endif

#ifdef __cplusplus

namespace gvl
{

struct noncopyable
{
protected:
	noncopyable() {}
    ~noncopyable() {}
private:
	noncopyable(const noncopyable&);
	noncopyable& operator=(const noncopyable&);
};

}

#endif
