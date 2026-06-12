#include <catch2/catch_test_macros.hpp>

#include "common.hpp"
#include "gfx/bitmap.hpp"
#include "gfx/palette.hpp"
#include "level.hpp"

// Minimal Common stub: real Common is heavyweight (loads assets), but the
// display-layer tests only need materials[0..255] populated.
static void FillMaterials(Common& common) {
  for (int i = 0; i < 256; ++i) {
    common.materials[i].flags = 0;
  }
}

// Build a tiny level with no display layer (classic path).
static Level MakeClassicLevel(Common& common) {
  Level level(common);
  level.width = 4;
  level.height = 4;
  level.material_id.assign(16, 0);
  level.materials.assign(16, common.materials[0]);
  // material_id[5] = palette index 42
  level.material_id[5] = 42;
  level.materials[5] = common.materials[42];
  return level;
}

TEST_CASE("Level::display_data and display_valid are empty for classic levels") {
  Common common;
  FillMaterials(common);
  Level level = MakeClassicLevel(common);

  CHECK(level.display_data.empty());
  CHECK(level.display_valid.empty());
}

TEST_CASE("AppearanceAt classic mode, no display layer — palette path") {
  Common common;
  FillMaterials(common);
  Level level = MakeClassicLevel(common);

  uint32_t pal32[256] = {};
  pal32[42] = 0xFFABCDEF;

  // With empty display layer, classic mode returns pal32[material_id[idx]].
  CHECK(level.AppearanceAt(5, ColorMode::kClassic, pal32) == 0xFFABCDEF);
}

TEST_CASE("AppearanceAt modern mode, no display layer — still palette path") {
  Common common;
  FillMaterials(common);
  Level level = MakeClassicLevel(common);

  uint32_t pal32[256] = {};
  pal32[42] = 0xFFABCDEF;

  // Empty display_valid means always fall back, even in modern mode.
  CHECK(level.AppearanceAt(5, ColorMode::kModern, pal32) == 0xFFABCDEF);
}

TEST_CASE("AppearanceAt modern mode, authored pixel — returns display_data") {
  Common common;
  FillMaterials(common);
  Level level = MakeClassicLevel(common);

  // Install a display layer with pixel 5 authored.
  level.display_data.assign(16, 0);
  level.display_valid.assign(16, 0);
  level.display_data[5] = 0xFF112233;
  level.display_valid[5] = 1;

  uint32_t pal32[256] = {};
  pal32[42] = 0xFFABCDEF;

  // Modern mode + valid → authored ARGB.
  CHECK(level.AppearanceAt(5, ColorMode::kModern, pal32) == 0xFF112233);
  // Another pixel with display_valid==0 → palette fallback.
  pal32[0] = 0xFF000001;
  CHECK(level.AppearanceAt(0, ColorMode::kModern, pal32) == 0xFF000001);
}

TEST_CASE("AppearanceAt classic mode, authored pixel — ignores display_data") {
  Common common;
  FillMaterials(common);
  Level level = MakeClassicLevel(common);

  level.display_data.assign(16, 0);
  level.display_valid.assign(16, 0);
  level.display_data[5] = 0xFF112233;
  level.display_valid[5] = 1;

  uint32_t pal32[256] = {};
  pal32[42] = 0xFFABCDEF;

  // Classic mode ignores display_data entirely.
  CHECK(level.AppearanceAt(5, ColorMode::kClassic, pal32) == 0xFFABCDEF);
}

TEST_CASE("Level::Swap also swaps display layer") {
  Common common;
  FillMaterials(common);

  Level a = MakeClassicLevel(common);
  a.display_data.assign(16, 0xDEADBEEF);
  a.display_valid.assign(16, 1);

  Level b(common);
  b.width = 2;
  b.height = 2;
  b.material_id.assign(4, 0);
  b.materials.assign(4, common.materials[0]);
  // b has no display layer

  a.Swap(b);

  CHECK(a.display_data.empty());
  CHECK(a.display_valid.empty());
  CHECK(b.display_data.size() == 16);
  CHECK(b.display_valid[0] == 1);
}

TEST_CASE("Bitmap has a mode field defaulting to kClassic") {
  Bitmap bmp;
  CHECK(bmp.mode == ColorMode::kClassic);
}
