#pragma once

#include <gvl/math/vec.hpp>

using fixedvec = gvl::ivec2;

typedef int fixed;

inline fixed itof(int v) {
  return v << 16;
}

inline int ftoi(fixed v) {
  return v >> 16;
}

inline fixedvec itof(gvl::ivec2 v) {
  return fixedvec(itof(v.x), itof(v.y));
}

inline gvl::ivec2 ftoi(fixedvec v) {
  return gvl::ivec2(ftoi(v.x), ftoi(v.y));
}

extern fixedvec cossinTable[128];

int vectorLength(int x, int y);

void precomputeTables();
