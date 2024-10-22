#include "weapon.hpp"
#include <random>
#include "constants.hpp"
#include "game.hpp"
#include "gfx/renderer.hpp"
#include "math.hpp"
#include "mixer/player.hpp"

int Weapon::computedLoadingTime(const Settings& settings) const {
  int ret = (settings.loadingTime * loadingTime) / 100;
  if (ret == 0)
    ret = 1;
  return ret;
}

void Weapon::fire(
    Game& game,
    int angle,
    fixedvec vel,
    int speed,
    fixedvec pos,
    int ownerIdx,
    WormWeapon* ww) const {
  WObject* obj = game.wobjects.newObjectReuse();

  obj->type = this;
  obj->pos = pos;
  obj->ownerIdx = ownerIdx;

  // STATS
  obj->firedBy = ww;
  obj->hasHit = false;

  Worm* owner = game.wormByIdx(ownerIdx);
  game.statsRecorder->damagePotential(owner, ww, hitDamage);
  game.statsRecorder->shot(owner, ww);

  obj->vel = cossinTable[angle] * speed / 100 + vel;

  if (distribution) {
    obj->vel.x += std::uniform_int_distribution<int>(
                      0, (distribution * 2) - 1)(game.rand) -
                  distribution;
    obj->vel.y += std::uniform_int_distribution<int>(
                      0, (distribution * 2) - 1)(game.rand) -
                  distribution;
  }

  if (startFrame >= 0) {
    switch (shotType) {
      case Weapon::ShotType::Normal:
        if (loopAnim) {
          if (numFrames) {
            obj->curFrame =
                std::uniform_int_distribution<int>(0, numFrames)(game.rand);
          } else {
            obj->curFrame = std::uniform_int_distribution<int>(0, 1)(game.rand);
          }
        } else {
          obj->curFrame = 0;
        }
        break;
      case Weapon::ShotType::DType1: {
        if (angle > 64)
          --angle;

        int curFrame = (angle - 12) >> 3;
        if (curFrame < 0) {
          curFrame = 0;
        } else if (curFrame > 12) {
          curFrame = 12;
        }
        obj->curFrame = curFrame;
        break;
      }
      case Weapon::ShotType::DType2:
        //[[fallthrough]]; C++17
      case Weapon::ShotType::Steerable:
        obj->curFrame = angle;
        break;
      case Weapon::ShotType::Laser:
        obj->curFrame = 0;
        break;
    }
  } else {
    obj->curFrame =
        colorBullets - std::uniform_int_distribution<int>(0, 1)(game.rand);
  }

  obj->timeLeft = timeToExplo;

  if (timeToExploV) {
    obj->timeLeft -=
        std::uniform_int_distribution<int>(0, timeToExploV - 1)(game.rand);
  }
}

void WObject::blowUpObject(Game& game, int causeIdx) {
  Common& common = *game.common;
  Weapon const& w = *type;

  fixed x = this->pos.x;
  fixed y = this->pos.y;
  fixed velX = this->vel.x;
  fixed velY = this->vel.y;

  game.wobjects.free(this);

  if (w.createOnExp >= 0) {
    common.sobjectTypes[w.createOnExp].create(
        game, ftoi(x), ftoi(y), causeIdx, firedBy, this);
  }

  if (w.exploSound >= 0) {
    game.soundPlayer->play(w.exploSound);
  }

  int splinters = w.splinterAmount;

  if (splinters > 0) {
    if (w.splinterScatter == 0) {
      for (int i = 0; i < splinters; ++i) {
        int angle = std::uniform_int_distribution<int>(0, 128 - 1)(game.rand);
        int colorSub = std::uniform_int_distribution<int>(0, 1)(game.rand);
        common.nobjectTypes[w.splinterType].create2(
            game, angle, fixedvec(), fixedvec(x, y),
            w.splinterColour - colorSub, causeIdx, firedBy);
      }
    } else {
      for (int i = 0; i < splinters; ++i) {
        int colorSub = std::uniform_int_distribution<int>(0, 1)(game.rand);
        common.nobjectTypes[w.splinterType].create1(
            game, fixedvec(velX, velY), fixedvec(x, y),
            w.splinterColour - colorSub, causeIdx, firedBy);
      }
    }
  }

  if (w.dirtEffect >= 0) {
    int ix = ftoi(x), iy = ftoi(y);
    drawDirtEffect(
        common, game.rand, game.level, w.dirtEffect, ftoi(x) - 7, ftoi(y) - 7);
    if (game.settings->shadow)
      correctShadow(
          common, game.level, gvl::rect(ix - 10, iy - 10, ix + 11, iy + 11));
  }
}

void WObject::process(Game& game) {
  int iter = 0;
  bool doExplode = false;
  bool doRemove = false;

  Common& common = *game.common;
  Weapon const& w = *type;

  Worm* owner = game.wormByIdx(ownerIdx);

  // As liero would do this while rendering, we try to do it as early as
  // possible
  if (common.H[HRemExp] && type - &common.weapons[0] == LC(RemExpObject) - 1) {
    if (owner->pressed(Worm::Control::Change) &&
        owner->pressed(Worm::Control::Fire)) {
      timeLeft = 0;
    }
  }

  do {
    ++iter;
    pos += vel;

    if (w.shotType == 2) {
      fixedvec dir(cossinTable[curFrame]);
      auto newVel = dir * w.speed / 100;

      if (owner->visible && owner->pressed(Worm::Control::Up)) {
        newVel += dir * w.addSpeed / 100;
      }

      vel = ((vel * 8) + newVel) / 9;
    } else if (w.shotType == 3) {
      fixedvec dir(cossinTable[curFrame]);
      auto addVel = dir * w.addSpeed / 100;

      vel += addVel;

      if (w.distribution) {
        vel.x += std::uniform_int_distribution<int>(
                     0, (w.distribution * 2) - 1)(game.rand) -
                 w.distribution;
        vel.y += std::uniform_int_distribution<int>(
                     0, (w.distribution * 2) - 1)(game.rand) -
                 w.distribution;
      }
    }

    if (w.bounce > 0) {
      auto ipos = ftoi(pos);
      auto inewPos = ftoi(pos + vel);

      if (!game.level.inside(inewPos.x, ipos.y) ||
          game.pixelMat(inewPos.x, ipos.y).dirtRock()) {
        if (w.bounce != 100) {
          vel.x = -vel.x * w.bounce / 100;
          // TODO: Read from EXE
          vel.y = (vel.y * 4) / 5;
        } else
          vel.x = -vel.x;
      }

      if (!game.level.inside(ipos.x, inewPos.y) ||
          game.pixelMat(ipos.x, inewPos.y).dirtRock()) {
        if (w.bounce != 100) {
          vel.y = -vel.y * w.bounce / 100;
          // TODO: Read from EXE
          vel.x = (vel.x * 4) / 5;
        } else
          vel.y = -vel.y;
      }
    }

    if (w.multSpeed != 100) {
      vel = vel * w.multSpeed / 100;
    }

    if (w.objTrailType >= 0 && (game.cycles % w.objTrailDelay) == 0) {
      common.sobjectTypes[w.objTrailType].create(
          game, ftoi(pos.x), ftoi(pos.y), ownerIdx, firedBy);
    }

    if (w.partTrailObj >= 0 && (game.cycles % w.partTrailDelay) == 0) {
      if (w.partTrailType == 1) {
        common.nobjectTypes[w.partTrailObj].create1(
            game, vel / LC(SplinterLarpaVelDiv), pos, 0, ownerIdx, firedBy);
      } else {
        int angle = std::uniform_int_distribution<int>(0, 128 - 1)(game.rand);
        common.nobjectTypes[w.partTrailObj].create2(
            game, angle, vel / LC(SplinterCracklerVelDiv), pos, 0, ownerIdx,
            firedBy);
      }
    }

    if (w.collideWithObjects) {
      auto impulse = vel * w.blowAway / 100;

      auto wr = game.wobjects.all();
      for (WObject* i; (i = wr.next());) {
        if (i->type != type || i->ownerIdx != ownerIdx) {
          if (pos.x >= i->pos.x - itof(2) && pos.x <= i->pos.x + itof(2) &&
              pos.y >= i->pos.y - itof(2) && pos.y <= i->pos.y + itof(2)) {
            i->vel += impulse;
          }
        }
      }

      auto nr = game.nobjects.all();
      for (NObject* i; (i = nr.next());) {
        if (pos.x >= i->pos.x - itof(2) && pos.x <= i->pos.x + itof(2) &&
            pos.y >= i->pos.y - itof(2) && pos.y <= i->pos.y + itof(2)) {
          i->vel += impulse;
        }
      }
    }

    auto inewPos = ftoi(pos + vel);

    if (inewPos.x < 0)
      pos.x = 0;
    if (inewPos.y < 0)
      pos.y = 0;
    if (inewPos.x >= game.level.width)
      pos.x = itof(game.level.width - 1);
    if (inewPos.y >= game.level.height)
      pos.y = itof(game.level.height - 1);

    if (!game.level.inside(inewPos) ||
        game.pixelMat(inewPos.x, inewPos.y).dirtRock()) {
      if (w.bounce == 0) {
        if (w.explGround) {
          doExplode = true;
        } else {
          vel.zero();
        }
      }
    } else {
      // The original tested w.gravity first, which doesn't seem like a gain
      vel.y += w.gravity;

      if (w.numFrames > 0) {
        if ((game.cycles & 7) == 0) {
          if (!w.loopAnim) {
            if (++curFrame > w.numFrames)
              curFrame = 0;
          } else {
            if (vel.x < 0) {
              if (--curFrame < 0)
                curFrame = w.numFrames;
            } else if (vel.x > 0) {
              if (++curFrame > w.numFrames)
                curFrame = 0;
            }
          }
        }
      }
    }

    if (w.timeToExplo > 0) {
      if (--timeLeft < 0)
        doExplode = true;
    }

    for (std::size_t i = 0; i < game.worms.size(); ++i) {
      Worm& worm = *game.worms[i];

      if ((w.hitDamage || w.blowAway || w.bloodOnHit || w.wormCollide) &&
          checkForSpecWormHit(
              game, ftoi(pos.x), ftoi(pos.y), w.detectDistance, worm)) {
        worm.vel += vel * w.blowAway / 100;

        game.doDamage(worm, w.hitDamage, ownerIdx);
        game.statsRecorder->damageDealt(
            owner, firedBy, &worm, w.hitDamage, hasHit);
        if (!hasHit)
          game.statsRecorder->hit(owner, firedBy, &worm);
        hasHit = true;

        int bloodAmount = w.bloodOnHit * game.settings->blood / 100;

        for (int i = 0; i < bloodAmount; ++i) {
          int angle = std::uniform_int_distribution<int>(0, 128 - 1)(game.rand);
          common.nobjectTypes[6].create2(
              game, angle, vel / 3, pos, 0, worm.index, firedBy);
        }

        if (w.hitDamage > 0 && worm.health > 0 &&
            std::uniform_int_distribution<int>(0, 3 - 1)(game.rand) == 0) {
          // NOTE: MUST be outside the unpredictable branch below
          int snd =
              std::uniform_int_distribution<int>(0, 3 - 1)(game.rand) + 18;
          if (!game.soundPlayer->isPlaying(&worm)) {
            game.soundPlayer->play(snd, &worm);
          }
        }

        if (w.wormCollide) {
          // TODO wormCollide is a boolean, why is it being shoved in a PRNG
          if (std::uniform_int_distribution<int>(0, 1)(game.rand) == 0) {
            if (w.wormExplode)
              doExplode = true;

            doRemove = true;
          }
        }
      }
    }

    if (doExplode) {
      blowUpObject(game, ownerIdx);
      break;
    } else if (doRemove) {
      game.wobjects.free(this);
      break;
    }
  } while (w.shotType == Weapon::ShotType::Laser && used  // TEMP
           && (iter < 8 || w.id == 28));
}
