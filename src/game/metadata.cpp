#include "metadata.hpp"

#define VERSION_GIT_TAG "build-a01745d-2-gcac3a84"
#define VERSION_GIT_HASH "cac3a84"
#define VERSION_GIT_DATE "2026.05.17"
#define VERSION_GIT_TIME "10:43:08"

const char* build_version(void) {
  return VERSION_GIT_TAG;
}

const char* build_hash(void) {
  return VERSION_GIT_HASH;
}

const char* build_date(void) {
  return VERSION_GIT_DATE;
}

const char* build_time(void) {
  return VERSION_GIT_TIME;
}
