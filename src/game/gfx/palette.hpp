#pragma once

#include <cassert>
#include <cstdint>
#include "color.hpp"

struct Settings;
struct WormSettings;

namespace io {
struct Reader;
}  // namespace io

// Where a worm's colours live in the palette. Worm sprite art hardcodes
// pixel values in the ramp, so the defaults must match the shipped sprites;
// the indirection exists so future skins can claim different blocks.
struct ColorBlock {
  int base;          // centre of the shaded sprite ramp (pixels use base-2 .. base+2)
  int colour_index;  // start of the secondary 6-entry copy block
  int status_index;  // start of the 3-entry minimap / status copy
  int width;         // ramp entries centred on base
};

struct Palette {
  static ColorBlock const kWormColorBlocks[2];

  // Full 8-bit per channel. Classic sources (VGA palette files, sprite TGA
  // palettes) are expanded from 6-bit at load time.
  Color entries[256];

  void Activate(Color real_pal[256]);
  void Fade(int amount);
  void LightUp(int amount);
  void RotateFrom(Palette& source, int from, int to, unsigned dist);
  void Read(io::Reader& r);

  // `c` channels are 0..255 and are quantized to the VGA grid: the ramp
  // math runs in 6-bit precision and is expanded to the 8-bit entry range,
  // keeping classic worm shading byte-identical to the VGA pipeline.
  void ScaleAdd(int dest, int const (&c)[3], int scale, int add) {
    int const kR = (add + (c[0] >> 2) * scale) / 64;
    int const kG = (add + (c[1] >> 2) * scale) / 64;
    int const kB = (add + (c[2] >> 2) * scale) / 64;

    assert(kR < 64);
    assert(kG < 64);
    assert(kB < 64);

    entries[dest].r = kR << 2;
    entries[dest].g = kG << 2;
    entries[dest].b = kB << 2;
  }

  void SetWormColoursSpan(int base, int const (&c)[3]) {
    // Hand-tuned 64-step gradient deriving the 5-entry shaded ramp from a
    // single base colour.
    static constexpr struct {
      int scale, add;
    } kSteps[5] = {{38, 0}, {50, 0}, {64, 0}, {47, 1008}, {28, 2205}};

    for (int j = 0; j < 5; ++j) {
      ScaleAdd(base - 2 + j, c, kSteps[j].scale, kSteps[j].add);
    }
  }

  void ResetPalette(Palette const& new_pal, Settings const& /*settings*/) {
    *this = new_pal;
    // setWormColours(settings);
  }

  void SetWormColour(int i, WormSettings const& settings);
  void SetWormColours(Settings const& settings);

  void Clear();
};
