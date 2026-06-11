#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "game/gfx/palette.hpp"
#include "game/io/stream.hpp"
#include "game/worm.hpp"

namespace {

// Golden reference for the VGA-era pipeline: palette files carry 6-bit
// channels which reach the screen as (v & 63) << 2. The 8-bit palette
// refactor must keep steady-state output byte-identical to this.
uint8_t Legacy6BitToScreen(uint8_t raw) { return static_cast<uint8_t>((raw & 63) << 2); }

// Golden reference for the worm colour ramp: ScaleAdd computed in 6-bit,
// expanded to screen range by Activate.
uint8_t LegacyScaleAddToScreen(int c6, int scale, int add) {
  return static_cast<uint8_t>(((add + c6 * scale) / 64) << 2);
}

struct Rgb {
  uint8_t r, g, b;
};

// The hand-tuned 5-entry gradient from Palette::SetWormColoursSpan,
// evaluated with the legacy 6-bit math.
std::vector<Rgb> LegacyWormRamp(int const (&rgb)[3]) {
  struct {
    int scale, add;
  } const kSteps[5] = {{38, 0}, {50, 0}, {64, 0}, {47, 1008}, {28, 2205}};

  std::vector<Rgb> ramp;
  for (auto const& step : kSteps) {
    ramp.push_back({LegacyScaleAddToScreen(rgb[0], step.scale, step.add),
                    LegacyScaleAddToScreen(rgb[1], step.scale, step.add),
                    LegacyScaleAddToScreen(rgb[2], step.scale, step.add)});
  }
  return ramp;
}

void RequireEntry(Color const& actual, Rgb const& expected) {
  REQUIRE(static_cast<int>(actual.r) == static_cast<int>(expected.r));
  REQUIRE(static_cast<int>(actual.g) == static_cast<int>(expected.g));
  REQUIRE(static_cast<int>(actual.b) == static_cast<int>(expected.b));
}

}  // namespace

TEST_CASE("classic palette load and activate matches the VGA pipeline", "[palette]") {
  // Cover the full byte range, including values above 63 that legacy
  // palette files would have had masked off.
  std::vector<uint8_t> raw(256 * 3);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    raw[i] = static_cast<uint8_t>((i * 7 + 3) & 0xff);
  }

  io::MemReader r(raw.data(), raw.size());
  Palette pal;
  pal.Read(r);

  Color real_pal[256];
  pal.Activate(real_pal);

  for (int i = 0; i < 256; ++i) {
    REQUIRE(static_cast<int>(real_pal[i].r) == static_cast<int>(Legacy6BitToScreen(raw[i * 3])));
    REQUIRE(static_cast<int>(real_pal[i].g) ==
            static_cast<int>(Legacy6BitToScreen(raw[i * 3 + 1])));
    REQUIRE(static_cast<int>(real_pal[i].b) ==
            static_cast<int>(Legacy6BitToScreen(raw[i * 3 + 2])));
  }
}

TEST_CASE("worm colour ramps match the legacy shading", "[palette]") {
  // Default worm colours from WormSettings / settings.cpp.
  int const kWormRgb[2][3] = {{26, 26, 63}, {15, 43, 15}};
  // Sprite ramp bases and secondary copy locations for each worm index.
  int const kBase[2] = {32, 41};
  int const kColourIndex[2] = {0x58, 0x78};

  for (int w = 0; w < 2; ++w) {
    WormSettings ws;
    for (int j = 0; j < 3; ++j) {
      ws.rgb[j] = kWormRgb[w][j];
    }

    Palette pal;
    pal.Clear();
    pal.SetWormColour(w, ws);

    Color real_pal[256];
    pal.Activate(real_pal);

    auto const kRamp = LegacyWormRamp(kWormRgb[w]);

    // 5-entry sprite ramp at base-2 .. base+2.
    for (int j = 0; j < 5; ++j) {
      RequireEntry(real_pal[kBase[w] - 2 + j], kRamp[j]);
    }

    // Secondary 6-entry copy cycles through ramp entries 1..3.
    for (int j = 0; j < 6; ++j) {
      RequireEntry(real_pal[kColourIndex[w] + j], kRamp[1 + j % 3]);
    }

    // Minimap / status colours copy ramp entries 2..4.
    for (int j = 0; j < 3; ++j) {
      RequireEntry(real_pal[129 + w * 4 + j], kRamp[2 + j]);
    }
  }
}

TEST_CASE("fade endpoints are exact", "[palette]") {
  std::vector<uint8_t> raw(256 * 3);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    raw[i] = static_cast<uint8_t>(i & 63);
  }
  io::MemReader r(raw.data(), raw.size());
  Palette pal;
  pal.Read(r);

  Palette faded = pal;
  faded.Fade(32);  // full brightness: must be a no-op
  Color a[256], b[256];
  pal.Activate(a);
  faded.Activate(b);
  for (int i = 0; i < 256; ++i) {
    REQUIRE(a[i].r == b[i].r);
    REQUIRE(a[i].g == b[i].g);
    REQUIRE(a[i].b == b[i].b);
  }

  faded.Fade(0);  // fully faded: everything black
  faded.Activate(b);
  for (int i = 0; i < 256; ++i) {
    REQUIRE(static_cast<int>(b[i].r) == 0);
    REQUIRE(static_cast<int>(b[i].g) == 0);
    REQUIRE(static_cast<int>(b[i].b) == 0);
  }
}

TEST_CASE("lightup at zero amount is an identity", "[palette]") {
  std::vector<uint8_t> raw(256 * 3);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    raw[i] = static_cast<uint8_t>((i * 5) & 63);
  }
  io::MemReader r(raw.data(), raw.size());
  Palette pal;
  pal.Read(r);

  Palette lit = pal;
  lit.LightUp(0);
  Color a[256], b[256];
  pal.Activate(a);
  lit.Activate(b);
  for (int i = 0; i < 256; ++i) {
    REQUIRE(a[i].r == b[i].r);
    REQUIRE(a[i].g == b[i].g);
    REQUIRE(a[i].b == b[i].b);
  }
}

TEST_CASE("rotatefrom permutes entries without touching channel values", "[palette]") {
  std::vector<uint8_t> raw(256 * 3);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    raw[i] = static_cast<uint8_t>((i * 11 + 1) & 63);
  }
  io::MemReader r(raw.data(), raw.size());
  Palette source;
  source.Read(r);

  Palette rotated = source;
  rotated.RotateFrom(source, 168, 174, 2);

  for (int i = 168; i <= 174; ++i) {
    int const kCount = 174 - 168 + 1;
    int const kSrc = 168 + ((i - 168) + kCount - 2) % kCount;
    REQUIRE(rotated.entries[i].r == source.entries[kSrc].r);
    REQUIRE(rotated.entries[i].g == source.entries[kSrc].g);
    REQUIRE(rotated.entries[i].b == source.entries[kSrc].b);
  }
}
