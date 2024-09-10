#pragma once

#include "math.hpp"
#include "exactObjectList.hpp"

struct Game;

struct Bonus : ExactObjectListBase
{
	Bonus()
	: frame(-1)
	{
	}

	fixed x;
	fixed y;
	fixed velY;
	int frame;
	int timer;
	int weapon;

	void process(Game& game);
};
