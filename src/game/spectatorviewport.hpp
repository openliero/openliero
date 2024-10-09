#pragma once

#include <ctime>
#include <gvl/math/rect.hpp>
#include "game.hpp"
#include "viewport.hpp"
#include "worm.hpp"

struct Renderer;

struct SpectatorViewport : Viewport {
  SpectatorViewport(gvl::rect rect, int levwidth, int levheight)
      : Viewport(rect, 0, levwidth, levheight) {}

  void draw(Game& game, Renderer& renderer, GameState state, bool isReplay);
  void process(Game& game);
};
