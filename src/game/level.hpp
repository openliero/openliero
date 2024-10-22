#pragma once

#include <cstdio>
#include <gvl/math/rect.hpp>
#include <gvl/math/vec.hpp>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include "common.hpp"
#include "gfx/palette.hpp"
#include "material.hpp"

struct Game;
struct Settings;
struct Common;

struct Level {
  Level(const Common& common)
      : width(0), height(0), zeroMaterial(common.materials[0]) {}

  bool
  load(const Common& common, Settings const& settings, gvl::octet_reader r);

  void generateDirtPattern(Common& common, std::mt19937& rand);
  void
  generateRandom(Common& common, Settings const& settings, std::mt19937& rand);
  void makeShadow(const Common& common);
  void generateFromSettings(
      Common& common,
      Settings const& settings,
      std::mt19937& rand);
  bool selectSpawn(std::mt19937& rand, int w, int h, gvl::ivec2& selected);
  void drawMiniature(Bitmap& dest, int mapX, int mapY, int step);

  unsigned char pixel(int x, int y) { return data[x + y * width]; }

  unsigned char pixel(fixedvec pos) { return data[pos.x + pos.y * width]; }

  unsigned char* pixelp(int x, int y) { return &data[x + y * width]; }

  void setPixel(int x, int y, PalIdx w, const Common& common) {
    data[x + y * width] = w;
    materials[x + y * width] = common.materials[w];
  }

  void setPixel(fixedvec pos, PalIdx w, const Common& common) {
    data[pos.x + pos.y * width] = w;
    materials[pos.x + pos.y * width] = common.materials[w];
  }

  Material& mat(int x, int y) { return materials[x + y * width]; }

  Material& mat(fixedvec pos) { return materials[pos.x + pos.y * width]; }

  Material* matp(int x, int y) { return &materials[x + y * width]; }

  unsigned char checkedPixelWrap(int x, int y) {
    unsigned int idx = static_cast<unsigned int>(x + y * width);
    if (idx < data.size())
      return data[idx];
    return 0;
  }

  Material checkedMatWrap(int x, int y) {
    unsigned int idx = static_cast<unsigned int>(x + y * width);
    if (idx < materials.size())
      return materials[idx];
    return zeroMaterial;
  }

  bool inside(int x, int y) {
    return static_cast<unsigned int>(x) < static_cast<unsigned int>(width) &&
           static_cast<unsigned int>(y) < static_cast<unsigned int>(height);
  }

  bool inside(fixedvec pos) {
    return static_cast<unsigned int>(pos.x) <
               static_cast<unsigned int>(width) &&
           static_cast<unsigned int>(pos.y) < static_cast<unsigned int>(height);
  }

  void swap(Level& other) {
    data.swap(other.data);
    materials.swap(other.materials);
    std::swap(width, other.width);
    std::swap(height, other.height);
    std::swap(origpal, other.origpal);
    std::swap(oldRandomLevel, other.oldRandomLevel);
    std::swap(oldLevelFile, other.oldLevelFile);
    std::swap(zeroMaterial, other.zeroMaterial);
  }

  gvl::rect rect() { return gvl::rect(0, 0, width, height); }

  void resize(int width_new, int height_new);

  std::vector<unsigned char> data;
  std::vector<Material> materials;

  bool oldRandomLevel;
  std::string oldLevelFile;
  int width, height;
  Palette origpal;
  Material zeroMaterial;
};
