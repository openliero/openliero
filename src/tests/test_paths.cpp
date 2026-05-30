#include <catch2/catch_test_macros.hpp>

#include "game/filesystem.hpp"

#include <cstdlib>
#include <string>

// The test target defines OPENLIERO_TEST_DATA_DIR to ${CMAKE_SOURCE_DIR}/data
// (the in-tree stock data). Tests set the OPENLIERO_DATADIR env var to that
// path so systemDataRoot()'s runtime override picks it up.

namespace {
struct ScopedEnv {
  std::string key;
  std::string prev;
  bool hadPrev;
  ScopedEnv(char const* k, char const* v) : key(k) {
    if (const char* p = std::getenv(k)) { prev = p; hadPrev = true; }
    else { hadPrev = false; }
    setenv(k, v, 1);
  }
  ~ScopedEnv() {
    if (hadPrev) setenv(key.c_str(), prev.c_str(), 1);
    else unsetenv(key.c_str());
  }
};
}

TEST_CASE("paths::systemDataRoot honours OPENLIERO_DATADIR env var when set and existing") {
  ScopedEnv env("OPENLIERO_DATADIR", OPENLIERO_TEST_DATA_DIR);

  FsNode node = paths::systemDataRoot();

  REQUIRE(static_cast<bool>(node));
  REQUIRE(node.exists());

  FsNode tc = node / "TC";
  REQUIRE(tc.exists());
}

TEST_CASE("paths::systemDataRoot fullPath matches OPENLIERO_DATADIR env var") {
  ScopedEnv env("OPENLIERO_DATADIR", OPENLIERO_TEST_DATA_DIR);

  FsNode node = paths::systemDataRoot();
  REQUIRE(static_cast<bool>(node));
  REQUIRE(node.fullPath() == std::string(OPENLIERO_TEST_DATA_DIR));
}

TEST_CASE("paths::systemDataRoot falls back when OPENLIERO_DATADIR points at nonexistent path") {
  ScopedEnv env("OPENLIERO_DATADIR", "/nonexistent/openliero/datadir/for/test");

  FsNode node = paths::systemDataRoot();
  // Falls back to SDL_GetBasePath(), which always returns something on Linux,
  // so we only assert that the env-var path was NOT used.
  if (node) {
    REQUIRE(node.fullPath() != std::string("/nonexistent/openliero/datadir/for/test"));
  }
}
