#ifndef NDEBUG
#include "gvl/support/debug.hpp"
#include <cstdlib>
#include <string>

void gvl::passert_fail(
    char const* cond,
    char const* file,
    int line,
    char const* msg) {
  std::string s;

  s += "ASSERT FAILED: ";
  s += file;
  s += ":";
  s += line;
  s += ": !(";
  s += cond;
  s += "), ";
  s += msg;

  fprintf(stderr, "%s\n", s.c_str());
  abort();
}
#endif
