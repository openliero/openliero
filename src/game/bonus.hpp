#pragma once

#include "exactObjectList.hpp"
#include "math.hpp"

struct Game;

struct Bonus : ExactObjectListBase {
  Bonus() : frame(-1) {}

  fixed x;
  fixed y;
  fixed velY;
  int frame;
  int timer;
  int weapon;

  void process(Game& game);
};
