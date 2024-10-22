#include "nobject.hpp"
#include "bobject.hpp"
#include "game.hpp"
#include "gfx/renderer.hpp"
#include "mixer/player.hpp"

NObject& NObjectType::create(
    Game& game,
    fixedvec vel,
    fixedvec pos,
    int color,
    int ownerIdx,
    WormWeapon* firedBy) {
  NObject& obj = *game.nobjects.newObjectReuse();

  obj.type = this;
  obj.ownerIdx = ownerIdx;
  obj.pos = pos;

  obj.vel = vel;

  // STATS
  obj.firedBy = firedBy;
  obj.hasHit = false;

  Worm* owner = game.wormByIdx(ownerIdx);
  game.statsRecorder->damagePotential(owner, firedBy, hitDamage);

  if (startFrame > 0) {
    obj.curFrame = std::uniform_int_distribution<int>(0, numFrames)(game.rand);
  } else if (color != 0) {
    obj.curFrame = color;
  } else {
    obj.curFrame = colorBullets;
  }

  obj.timeLeft = timeToExplo;

  if (timeToExploV)
    obj.timeLeft -=
        std::uniform_int_distribution<int>(0, timeToExploV - 1)(game.rand);

  return obj;
}

NObject& NObjectType::create1(
    Game& game,
    fixedvec vel,
    fixedvec pos,
    int color,
    int ownerIdx,
    WormWeapon* firedBy) {
  if (distribution) {
    vel.x += distribution - std::uniform_int_distribution<int>(
                                0, (distribution * 2) - 1)(game.rand);
    vel.y += distribution - std::uniform_int_distribution<int>(
                                0, (distribution * 2) - 1)(game.rand);
  }

  return create(game, vel, pos, color, ownerIdx, firedBy);
}

void NObjectType::create2(
    Game& game,
    int angle,
    fixedvec vel,
    fixedvec pos,
    int color,
    int ownerIdx,
    WormWeapon* firedBy) {
  int realSpeed =
      speed - std::uniform_int_distribution<int>(0, speedV - 1)(game.rand);

  vel += cossinTable[angle] * realSpeed / 100;

  // TODO: !REPLAYS Make the distributions use the same code
  if (distribution) {
    vel.x += std::uniform_int_distribution<int>(
                 0, (distribution * 2) - 1)(game.rand) -
             distribution;
    vel.y += std::uniform_int_distribution<int>(
                 0, (distribution * 2) - 1)(game.rand) -
             distribution;
  }

  auto& obj = create(game, vel, pos, color, ownerIdx, firedBy);

  obj.pos += obj.vel;
}

void NObject::process(Game& game) {
  Common& common = *game.common;

  bool bounced = false;
  bool doExplode = false;

  pos += vel;

  auto inewPos = ftoi(pos + vel);
  auto ipos = ftoi(pos);

  NObjectType const& t = *type;

  if (t.bounce > 0) {
    if (!game.level.inside(inewPos.x, ipos.y) ||
        game.pixelMat(inewPos.x, ipos.y).dirtRock()) {
      vel.x = -vel.x * t.bounce / 100;
      // TODO: Read from EXE
      vel.y = (vel.y * 4) / 5;
      bounced = true;
    }

    if (!game.level.inside(ipos.x, inewPos.y) ||
        game.pixelMat(ipos.x, inewPos.y).dirtRock()) {
      vel.y = -vel.y * t.bounce / 100;
      // TODO: Read from EXE
      vel.x = (vel.x * 4) / 5;
      bounced = true;
    }
  }

  if (t.bloodTrail && t.bloodTrailDelay > 0 &&
      (game.cycles % t.bloodTrailDelay) == 0) {
    // TODO: Read from EXE
    game.createBObject(pos, vel / 4);
  }

  // Yes, we do this again.
  inewPos = ftoi(pos + vel);

  if (inewPos.x < 0)
    pos.x = 0;
  if (inewPos.y < 0)
    pos.y = 0;
  if (inewPos.x >= game.level.width)
    pos.x = itof(game.level.width);
  if (inewPos.y >= game.level.height)
    pos.y = itof(game.level.height);

  if (!game.level.inside(inewPos) ||
      game.pixelMat(inewPos.x, inewPos.y).dirtRock()) {
    vel.zero();

    if (t.explGround) {
      if (t.startFrame > 0 && t.drawOnMap) {
        blitImageOnMap(
            common, game.level,
            common.smallSprites.spritePtr(t.startFrame + curFrame), ipos.x - 3,
            ipos.y - 3, 7, 7);

        if (game.settings->shadow) {
          // This seems like an overly large rectangle
          correctShadow(
              common, game.level,
              gvl::rect(ipos.x - 8, ipos.y - 8, ipos.x + 9, ipos.y + 9));
        }
      }

      doExplode = true;
    }
  } else {
    // NOTE: AFAIK, this doesn't exist in Liero, but some TCs seem to
    // forget to set leaveObjDelay to 0 when not using this trail
    if (!bounced && t.leaveObjDelay != 0 && t.leaveObj >= 0 &&
        (game.cycles % t.leaveObjDelay) == 0) {
      common.sobjectTypes[t.leaveObj].create(
          game, ftoi(pos.x), ftoi(pos.y), ownerIdx, firedBy);
    }

    vel.y += t.gravity;
  }

  if (t.numFrames > 0) {
    // TODO: Read from EXE
    if ((game.cycles & 7) == 0) {
      if (vel.x > 0) {
        ++curFrame;
        if (curFrame > t.numFrames)
          curFrame = 0;
      } else if (vel.x < 0) {
        --curFrame;
        if (curFrame < 0)
          curFrame = t.numFrames;
      }
    }
  }

  if (t.timeToExplo > 0) {
    if (--timeLeft <= 0)
      doExplode = true;
  }

  if (!doExplode) {
    if (t.hitDamage > 0) {
      for (std::size_t i = 0; i < game.worms.size(); ++i) {
        Worm& w = *game.worms[i];

        if (checkForSpecWormHit(
                game, ftoi(pos.x), ftoi(pos.y), t.detectDistance, w)) {
          w.vel += vel * t.blowAway / 100;

          game.doDamage(w, t.hitDamage, ownerIdx);

          Worm* owner = game.wormByIdx(ownerIdx);
          game.statsRecorder->damageDealt(
              owner, firedBy, &w, t.hitDamage, hasHit);
          hasHit = true;

          // NOTE: MUST be outside the unpredictable branch below
          if (t.hitDamage > 0 && w.health > 0 &&
              std::uniform_int_distribution<int>(0, 3 - 1)(game.rand) == 0) {
            int snd =
                18 + std::uniform_int_distribution<int>(0, 3 - 1)(game.rand);
            if (!game.soundPlayer->isPlaying(&w)) {
              game.soundPlayer->play(snd, &w);
            }
          }

          int blood = t.bloodOnHit * game.settings->blood / 100;

          for (int i = 0; i < blood; ++i) {
            int angle =
                std::uniform_int_distribution<int>(0, 128 - 1)(game.rand);
            common.nobjectTypes[6].create2(
                game, angle, vel / 3, pos, 0, ownerIdx, 0);
          }

          if (t.wormExplode)
            doExplode = true;
          else if (t.wormDestroy && used)
            game.nobjects.free(this);
        }
      }
    }
  }

  if (doExplode) {
    if (t.createOnExp >= 0) {
      common.sobjectTypes[t.createOnExp].create(
          game, ftoi(pos.x), ftoi(pos.y), ownerIdx, firedBy);
    }

    if (t.dirtEffect >= 0) {
      drawDirtEffect(
          common, game.rand, game.level, t.dirtEffect, ftoi(pos.x) - 7,
          ftoi(pos.y) - 7);

      if (game.settings->shadow)
        correctShadow(
            common, game.level,
            gvl::rect(
                ftoi(pos.x) - 10, ftoi(pos.y) - 10, ftoi(pos.x) + 11,
                ftoi(pos.y) + 11));
    }

    if (t.splinterAmount > 0) {
      for (int i = 0; i < t.splinterAmount; ++i) {
        int angle = std::uniform_int_distribution<int>(0, 128 - 1)(game.rand);
        int colorSub = std::uniform_int_distribution<int>(0, 2 - 1)(game.rand);
        common.nobjectTypes[t.splinterType].create2(
            game, angle, fixedvec(), pos, t.splinterColour - colorSub, ownerIdx,
            0);
      }
    }

    if (used)
      game.nobjects.free(this);
  }
}
