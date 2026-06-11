#pragma once

#include <cassert>
#include <cstdint>
#include "color.hpp"

struct Settings;
struct WormSettings;

namespace io {
struct Reader;
}  // namespace io

struct Palette {
  static int const kWormColourIndexes[2];
  static int const kWormSpriteColorBase[2];

  // Full 8-bit per channel. Classic sources (VGA palette files, sprite TGA
  // palettes) are expanded from 6-bit at load time.
  Color entries[256];

  void Activate(Color real_pal[256]);
  void Fade(int amount);
  void LightUp(int amount);
  void RotateFrom(Palette& source, int from, int to, unsigned dist);
  void Read(io::Reader& r);

  // `c` channels are 0..63. The ramp math runs in 6-bit precision and is
  // expanded to the 8-bit entry range, keeping classic worm shading
  // byte-identical to the VGA pipeline.
  void ScaleAdd(int dest, int const (&c)[3], int scale, int add) {
    int const kR = (add + c[0] * scale) / 64;
    int const kG = (add + c[1] * scale) / 64;
    int const kB = (add + c[2] * scale) / 64;

    assert(kR < 64);
    assert(kG < 64);
    assert(kB < 64);

    entries[dest].r = kR << 2;
    entries[dest].g = kG << 2;
    entries[dest].b = kB << 2;
  }

  void SetWormColoursSpan(int base, int const (&c)[3]) {
    ScaleAdd(base - 2, c, 38, 0);
    ScaleAdd(base - 1, c, 50, 0);
    ScaleAdd(base, c, 64, 0);
    ScaleAdd(base + 1, c, 47, 1008);
    ScaleAdd(base + 2, c, 28, 2205);
  }

  void ResetPalette(Palette const& new_pal, Settings const& /*settings*/) {
    *this = new_pal;
    // setWormColours(settings);
  }

  void SetWormColour(int i, WormSettings const& settings);
  void SetWormColours(Settings const& settings);

  void Clear();
};
