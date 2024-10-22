#pragma once

#include "../common.hpp"
#include "bitmap.hpp"

struct Renderer {
  Renderer() : fadeValue(0) {}

  void init(int x, int y);
  void clear();
  void loadPalette(Common const& common);
  void setRenderResolution(int x, int y);

  // the bitmap that is drawn into by this renderer
  Bitmap bmp;
  Palette pal, origpal;
  int fadeValue;
  // Resolution to render the game at. This should be modified via
  // setRenderResolution() to ensure that the bitmap is re-allocated
  int renderResX = 320;
  int renderResY = 200;
};
