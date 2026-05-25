// Phase 4 tests: per-type cereal serialize() round-trips through both
// the binary (PortableBinaryArchive) and TOML (ser::TomlOutputArchive)
// archives. The old archive() functions stay in place; this verifies
// the new cereal-based path before Phase 6 swaps the call sites.

#include <catch2/catch_test_macros.hpp>

#include <cereal/archives/portable_binary.hpp>

#include "game/serialization/cereal_types.hpp"
#include "game/serialization/toml_archive.hpp"

#include <sstream>

namespace {

template <typename T>
T roundtripBinary(T const& src) {
  std::stringstream ss;
  {
    cereal::PortableBinaryOutputArchive ar(ss);
    ar(cereal::make_nvp("v", src));
  }
  T dst{};
  {
    cereal::PortableBinaryInputArchive ar(ss);
    ar(cereal::make_nvp("v", dst));
  }
  return dst;
}

template <typename T>
T roundtripToml(T const& src) {
  std::stringstream ss;
  {
    ser::TomlOutputArchive ar(ss);
    ar(cereal::make_nvp("v", src));
  }
  T dst{};
  {
    ser::TomlInputArchive ar(ss);
    ar(cereal::make_nvp("v", dst));
  }
  return dst;
}

}  // namespace

TEST_CASE("cereal_types: BasicVec<int,2> binary round-trip",
          "[cereal_types]") {
  IVec2 src{-12345, 6789};
  IVec2 dst = roundtripBinary(src);
  CHECK(dst.x == src.x);
  CHECK(dst.y == src.y);
}

TEST_CASE("cereal_types: BasicVec<int,2> toml round-trip", "[cereal_types]") {
  IVec2 src{-12345, 6789};
  IVec2 dst = roundtripToml(src);
  CHECK(dst.x == src.x);
  CHECK(dst.y == src.y);
}

TEST_CASE("cereal_types: BasicVec<float,2> binary round-trip",
          "[cereal_types]") {
  FVec2 src{1.5f, -2.25f};
  FVec2 dst = roundtripBinary(src);
  CHECK(dst.x == src.x);
  CHECK(dst.y == src.y);
}

TEST_CASE("cereal_types: BasicRect<int> binary round-trip",
          "[cereal_types]") {
  Rect src{1, 2, 100, 200};
  Rect dst = roundtripBinary(src);
  CHECK(dst.x1 == 1);
  CHECK(dst.y1 == 2);
  CHECK(dst.x2 == 100);
  CHECK(dst.y2 == 200);
}

TEST_CASE("cereal_types: BasicRect<int> toml round-trip", "[cereal_types]") {
  Rect src{1, 2, 100, 200};
  Rect dst = roundtripToml(src);
  CHECK(dst.x1 == 1);
  CHECK(dst.y1 == 2);
  CHECK(dst.x2 == 100);
  CHECK(dst.y2 == 200);
}
