#pragma once

#include "gvl/support/platform.h"

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
