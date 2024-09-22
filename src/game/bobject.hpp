#pragma once

#include "math.hpp"
#include "fastObjectList.hpp"

struct Game;

/*
 * Blood Object
*/
struct BObject
{
	bool process(Game& game);

	fixedvec pos, vel;
	int color;
};
