#pragma once

#include "math.hpp"
#include "fastObjectList.hpp"

struct Game;

struct BObject
{
	bool process(Game& game);

	fixedvec pos, vel;
	int color;
};
