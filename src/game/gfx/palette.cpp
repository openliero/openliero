#include "palette.hpp"

#include <algorithm>

#include "../gfx.hpp"
#include "../reader.hpp"
#include "../settings.hpp"
#include "io/stream.hpp"

void Palette::Activate(Color real_pal[256]) {
  for (int i = 0; i < 256; ++i) {
    real_pal[i].r = entries[i].r;
    real_pal[i].g = entries[i].g;
    real_pal[i].b = entries[i].b;
  }
}

static int FadeValue(int v, int amount) {
  v = (v * amount) >> 5;
  v = std::max(v, 0);
  return v;
}

static int LightUpValue(int v, int amount) {
  v = (v * (32 - amount) + amount * 255) >> 5;
  v = std::min(v, 255);
  return v;
}

void Palette::Fade(int amount) {
  if (amount >= 32) {
    return;
  }

  for (auto& entrie : entries) {
    entrie.r = FadeValue(entrie.r, amount);
    entrie.g = FadeValue(entrie.g, amount);
    entrie.b = FadeValue(entrie.b, amount);
  }
}

void Palette::LightUp(int amount) {
  for (auto& entrie : entries) {
    entrie.r = LightUpValue(entrie.r, amount);
    entrie.g = LightUpValue(entrie.g, amount);
    entrie.b = LightUpValue(entrie.b, amount);
  }
}

void Palette::RotateFrom(Palette const& source, int from, int to, unsigned dist) {
  int const kCount = (to - from + 1);
  dist %= kCount;

  for (int i = 0; i < kCount; ++i) {
    entries[from + i] = source.entries[from + ((i + kCount - dist) % kCount)];
  }
}

void Palette::Clear() { std::memset(entries, 0, sizeof(entries)); }

void Palette::Read(io::Reader& r) {
  // Classic palette files carry 6-bit VGA channels; expand to the 8-bit
  // range entries hold, preserving the legacy (v & 63) << 2 screen values.
  for (auto& entrie : entries) {
    uint8_t rgb[3];
    r.Get(rgb, 3);

    entrie.r = (rgb[0] & 63) << 2;
    entrie.g = (rgb[1] & 63) << 2;
    entrie.b = (rgb[2] & 63) << 2;
  }
}

// Worm sprites have hardcoded pixel values: 30-34 for worm 0, 39-43 for
// worm 1, with secondary copies at 0x58 / 0x78 and minimap / status colours
// at 129 / 133. TODO: Read from EXE?
ColorBlock const Palette::kWormColorBlocks[2] = {
    {.base = 32, .colour_index = 0x58, .status_index = 129, .width = 5},
    {.base = 41, .colour_index = 0x78, .status_index = 133, .width = 5}};

void Palette::ReadFull(io::Reader& r) {
  for (auto& entrie : entries) {
    uint8_t rgb[3];
    r.Get(rgb, 3);

    entrie.r = rgb[0];
    entrie.g = rgb[1];
    entrie.b = rgb[2];
  }
}

void Palette::SetWormColour(int i, WormSettings const& settings, ColorMode mode) {
  ColorBlock const& block = kWormColorBlocks[i];

  SetWormColoursSpan(block.base, settings.rgb, mode);

  for (int j = 0; j < 6; ++j) {
    entries[block.colour_index + j] = entries[block.base + (j % 3) - 1];
  }

  for (int j = 0; j < 3; ++j) {
    entries[block.status_index + j] = entries[block.base + j];
  }
}

void Palette::SetWormColours(Settings const& settings, ColorMode mode) {
  for (int i = 0; i < 2; ++i) {
    SetWormColour(i, *settings.worm_settings[i], mode);
  }
}
