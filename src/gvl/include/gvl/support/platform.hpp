#pragma once

#include "gvl/support/platform.h"

#ifdef __cplusplus

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

#endif
