#pragma once

#include <stdexcept>

namespace gvl {

  void
  passert_fail(char const* cond, char const* file, int line, char const* msg);

#ifndef NDEBUG
#define GVL_PASSERT(cond, msg) \
  if (!(cond))                 \
  gvl::passert_fail(#cond, __FILE__, __LINE__, msg)
#define GVL_SASSERT(cond) \
  if (!(cond))            \
  gvl::passert_fail(#cond, __FILE__, __LINE__, "")
#else
#define GVL_PASSERT(cond, msg) ((void)0)
#define GVL_SASSERT(cond) ((void)0)
#endif

#define passert(cond, msg) GVL_PASSERT(cond, msg)
#define sassert(cond) GVL_SASSERT(cond)
}
