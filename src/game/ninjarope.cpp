#include "constants.hpp"
#include "game.hpp"
#include "gfx/color.hpp"
#include "math.hpp"
#include "worm.hpp"

void NinjaRope::process(Worm& owner, Game& game) {
  Common& common = *game.common;

  if (out) {
    pos += vel;

    auto ipos = ftoi(pos);

    anchor = 0;
    for (std::size_t i = 0; i < game.worms.size(); ++i) {
      Worm& w = *game.worms[i];

      if (&w != &owner && checkForSpecWormHit(game, ipos.x, ipos.y, 1, w)) {
        anchor = &w;
        break;
      }
    }

    fixedvec diff = pos - owner.pos;

    fixedvec force(
        (diff.x << LC(NRForceShlX)) / LC(NRForceDivX),
        (diff.y << LC(NRForceShlY)) / LC(NRForceDivY));

    curLen = (vectorLength(ftoi(diff.x), ftoi(diff.y)) + 1)
             << LC(NRForceLenShl);

    if (ipos.x <= 0 || ipos.x >= game.level.width - 1 || ipos.y <= 0 ||
        ipos.y >= game.level.height - 1 || game.level.mat(ipos).dirtRock()) {
      if (!attached) {
        length = LC(NRAttachLength);
        attached = true;

        if (game.level.inside(ipos)) {
          if (game.level.mat(ipos).anyDirt()) {
            PalIdx pix = game.level.pixel(ipos);

            // TODO: Check 11 and read from exe
            for (int i = 0; i < 11; ++i) {
              common.nobjectTypes[2].create2(
                  game,
                  std::uniform_int_distribution<int>(0, 128 - 1)(game.rand),
                  fixedvec(), pos, pix, owner.index, 0);
            }
          }
        }
      }

      vel.zero();
    } else if (anchor) {
      if (!attached) {
        // TODO: Should this value be separate from the non-worm attaching?
        length = LC(NRAttachLength);
        attached = true;
      }

      if (curLen > length) {
        anchor->vel -= force / curLen;
      }

      vel = anchor->vel;
      pos = anchor->pos;
    } else {
      attached = false;
    }

    if (attached) {
      // curLen can't be 0

      if (curLen > length) {
        owner.vel += force / curLen;
      }
    } else {
      vel.y += LC(NinjaropeGravity);

      if (curLen > length) {
        vel -= force / curLen;
      }
    }
  }
}
