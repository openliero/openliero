#pragma once

#include <cstdint>

struct Material {
  enum Type {
    Dirt = 1 << 0,
    Dirt2 = 1 << 1,
    Rock = 1 << 2,
    Background = 1 << 3,
    SeeShadow = 1 << 4,
    WormM = 1 << 5
  };

  bool dirt() { return (flags & Material::Type::Dirt) != 0; }
  bool dirt2() { return (flags & Material::Type::Dirt2) != 0; }
  bool rock() { return (flags & Material::Type::Rock) != 0; }
  bool background() { return (flags & Material::Type::Background) != 0; }
  bool seeShadow() { return (flags & Material::Type::SeeShadow) != 0; }

  // Constructed
  bool dirtRock() {
    return (flags & (Material::Type::Dirt | Material::Type::Dirt2 |
                     Material::Type::Rock)) != 0;
  }
  bool anyDirt() {
    return (flags & (Material::Type::Dirt | Material::Type::Dirt2)) != 0;
  }
  bool dirtBack() {
    return (flags & (Material::Type::Dirt | Material::Type::Dirt2 |
                     Material::Type::Background)) != 0;
  }
  bool worm() { return (flags & Material::Type::WormM) != 0; }

  uint8_t flags;
};
