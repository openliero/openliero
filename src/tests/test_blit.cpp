#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "game/gfx/renderer.hpp"

TEST_CASE("updatepal32 packs the working palette as ARGB8888", "[blit][pal32]") {
  Renderer renderer;
  renderer.pal.Clear();
  renderer.pal.entries[0] = {.r = 0, .g = 0, .b = 0, .unused = 0};
  renderer.pal.entries[1] = {.r = 0x12, .g = 0x34, .b = 0x56, .unused = 0};
  renderer.pal.entries[255] = {.r = 0xff, .g = 0xff, .b = 0xff, .unused = 0};

  renderer.UpdatePal32();

  // Canonical packing: alpha forced opaque, then r<<16 | g<<8 | b. This is
  // what both SDL surfaces (fixed SDL_PIXELFORMAT_ARGB8888) and the video
  // tool's BGRA byte order expect.
  REQUIRE(renderer.pal32[0] == 0xFF000000U);
  REQUIRE(renderer.pal32[1] == 0xFF123456U);
  REQUIRE(renderer.pal32[255] == 0xFFFFFFFFU);
}

TEST_CASE("updatepal32 tracks palette mutations", "[blit][pal32]") {
  Renderer renderer;
  renderer.pal.Clear();
  renderer.pal.entries[7] = {.r = 0x10, .g = 0x20, .b = 0x30, .unused = 0};
  renderer.UpdatePal32();
  REQUIRE(renderer.pal32[7] == 0xFF102030U);

  renderer.pal.entries[7] = {.r = 0x40, .g = 0x50, .b = 0x60, .unused = 0};
  renderer.UpdatePal32();
  REQUIRE(renderer.pal32[7] == 0xFF405060U);
}
