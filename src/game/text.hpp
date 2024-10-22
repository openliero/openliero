#pragma once

#include <cstdio>
#include <cstring>
#include <string>

inline std::string toString(int v) {
  const int BUF_MAX = 20;
  char buf[BUF_MAX];
  std::snprintf(buf, BUF_MAX * sizeof(char), "%d", v);
  return buf;
}

char const* timeToString(int sec);
char const* timeToStringEx(int ms, bool forceHours, bool forceMinutes);
char const* timeToStringFrames(int frames);

inline bool endsWith(std::string const& str, char const* end) {
  auto pos = str.find(end);
  return pos != std::string::npos && pos + std::strlen(end) == str.size();
}

bool ciStartsWith(std::string const& a, std::string const& b);
bool ciCompare(std::string const& a, std::string const& b);
bool ciLess(std::string const& a, std::string const& b);
// converts an extremely limited subset of UTF-8 to extended ASCII
char utf8ToDOS(char* str);
