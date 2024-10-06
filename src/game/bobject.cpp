#include "bobject.hpp"

#include "gfx/color.hpp"
#include "game.hpp"
#include "constants.hpp"

void Game::createBObject(fixedvec pos, fixedvec vel)
{
	Common& common = *this->common;

	BObject& obj = *bobjects.newObjectReuse();

  // handle negative number + singular colour
  if(LC(NumBloodColours) < 2) {
    obj.color = LC(FirstBloodColour);
  } else {
    int range_end = LC(FirstBloodColour) + LC(NumBloodColours) - 1;

    obj.color = std::uniform_int_distribution<int>(LC(FirstBloodColour), range_end)(rand);
  }

	obj.pos = pos;
	obj.vel = vel;
}

bool BObject::process(Game& game)
{
	Common& common = *game.common;

	pos += vel;

	auto ipos = ftoi(pos);

	if(!game.level.inside(ipos))
	{
		return false;
	}
	else
	{
		PalIdx c = game.level.pixel(ipos);
		Material m = game.level.mat(ipos);

		if(m.background())
			vel.y += LC(BObjGravity);

		if((c >= 1 && c <= 2)
		|| (c >= 77 && c <= 79)) // TODO: Read from EXE
		{
      // 77-79
			game.level.setPixel(ipos, 77 + std::uniform_int_distribution<int>(0, 3 - 1)(game.rand), common);
			return false;
		}
		else if(m.anyDirt())
		{
      // 82-84
			game.level.setPixel(ipos, 82 + std::uniform_int_distribution<int>(0, 3 - 1)(game.rand), common);
			return false;
		}
		else if(m.rock())
		{
      // 85-87
			game.level.setPixel(ipos, 85 + std::uniform_int_distribution<int>(0, 3 - 1)(game.rand), common);
			return false;
		}
	}

	return true;
}
