#include "worm.hpp"
#include <cstdlib>
#include <random>
#include "console.hpp"
#include "constants.hpp"
#include "filesystem.hpp"  // For joinPath
#include "game.hpp"
#include "gfx/renderer.hpp"
#include "mixer/player.hpp"

#include <gvl/serialization/archive.hpp>
#include <gvl/serialization/context.hpp>
#include "replay.hpp"

#include <gvl/crypt/gash.hpp>
#include <gvl/io2/fstream.hpp>

struct Point {
  int x, y;
};

gvl::gash::value_type& WormSettings::updateHash() {
  GameSerializationContext context;
  gvl::hash_accumulator<gvl::gash> ha;

  archive(
      gvl::out_archive<
          gvl::hash_accumulator<gvl::gash>, GameSerializationContext>(
          ha, context),
      *this);

  ha.flush();
  hash = ha.final();
  return hash;
}

void WormSettings::saveProfile(FsNode node) {
  try {
    auto writer = node.toOctetWriter();

    profileNode = node;
    GameSerializationContext context;
    archive(
        gvl::out_archive<gvl::octet_writer, GameSerializationContext>(
            writer, context),
        *this);
  } catch (gvl::stream_error& e) {
    Console::writeWarning(
        std::string("Stream error saving profile: ") + e.what());
  }
}

void WormSettings::loadProfile(FsNode node) {
  int oldColor = color;
  try {
    auto reader = node.toOctetReader();

    profileNode = node;
    GameSerializationContext context;
    archive(
        gvl::in_archive<gvl::octet_reader, GameSerializationContext>(
            reader, context),
        *this);
  } catch (gvl::stream_error& e) {
    Console::writeWarning(
        std::string("Stream error loading profile: ") + e.what());
    Console::writeWarning(
        "The profile may just be old, in which case there is nothing to worry "
        "about");
  }

  color = oldColor;  // We preserve the color
}

void Worm::calculateReactionForce(Game& game, int newX, int newY, int dir) {
  static Point const colPoints[4][7] = {
      {// DOWN reaction points
       {-1, -4},
       {0, -4},
       {1, -4},
       {0, 0},
       {0, 0},
       {0, 0},
       {0, 0}},
      {// LEFT reaction points
       {1, -3},
       {1, -2},
       {1, -1},
       {1, 0},
       {1, 1},
       {1, 2},
       {1, 3}},
      {// UP reaction points
       {-1, 4},
       {0, 4},
       {1, 4},
       {0, 0},
       {0, 0},
       {0, 0},
       {0, 0}},
      {// RIGHT reaction points
       {-1, -3},
       {-1, -2},
       {-1, -1},
       {-1, 0},
       {-1, 1},
       {-1, 2},
       {-1, 3}}

  };

  static int const colPointCount[4] = {3, 7, 3, 7};

  reacts[dir] = 0;

  // newX should be x + velX at the first call

  for (int i = 0; i < colPointCount[dir]; ++i) {
    int colX = newX + colPoints[dir][i].x;
    int colY = newY + colPoints[dir][i].y;

    if (!game.level.checkedMatWrap(colX, colY).background()) {
      ++reacts[dir];
    }
  }
}

void Worm::processPhysics(Game& game) {
  const Common& common = *game.common;

  if (reacts[static_cast<int>(Worm::ReactionForce::Up)] > 0)
    vel.x = (vel.x * LC(WormFricMult)) / LC(WormFricDiv);

  fixedvec absvel(std::abs(vel.x), std::abs(vel.y));

  int32_t rh, rv, mbh, mbv;

  rh = reacts
      [vel.x >= 0 ? static_cast<int>(Worm::ReactionForce::Left)
                  : static_cast<int>(Worm::ReactionForce::Right)];
  rv = reacts
      [vel.y >= 0 ? static_cast<int>(Worm::ReactionForce::Up)
                  : static_cast<int>(Worm::ReactionForce::Down)];
  mbh = vel.x > 0 ? LC(MinBounceRight) : -LC(MinBounceLeft);
  mbv = vel.y > 0 ? LC(MinBounceDown) : -LC(MinBounceUp);

  // TODO: We wouldn't need the vel.x check if we knew that mbh/mbv were always
  // non-zero
  if (vel.x && rh) {
    if (absvel.x > mbh) {
      if (common.H[HFallDamage])
        health -= LC(FallDamageRight);
      else
        game.soundPlayer->play(14);
      vel.x = -vel.x / 3;
    } else
      vel.x = 0;
  }

  if (vel.y && rv) {
    if (absvel.y > mbv) {
      if (common.H[HFallDamage])
        health -= LC(FallDamageDown);
      else
        game.soundPlayer->play(14);
      vel.y = -vel.y / 3;
    } else
      vel.y = 0;
  }

  if (reacts[static_cast<int>(Worm::ReactionForce::Up)] == 0) {
    vel.y += LC(WormGravity);
  }

  // No, we can't use rh/rv here, they are out of date
  if (reacts
          [vel.x >= 0 ? static_cast<int>(Worm::ReactionForce::Left)
                      : static_cast<int>(Worm::ReactionForce::Right)] < 2)
    pos.x += vel.x;

  if (reacts
          [vel.y >= 0 ? static_cast<int>(Worm::ReactionForce::Up)
                      : static_cast<int>(Worm::ReactionForce::Down)] < 2)
    pos.y += vel.y;
}

void Worm::process(Game& game) {
  Common& common = *game.common;

  if (health > settings->health)
    health = settings->health;

  if ((game.settings->gameMode != Settings::GameMode::KillEmAll &&
       game.settings->gameMode != Settings::GameMode::ScalesOfJustice) ||
      lives > 0) {
    if (visible) {
      // Liero.exe: 291C

      auto next = pos + vel;
      auto iNext = ftoi(next);

      // Calculate reaction forces
      {
        for (int i = 0; i < 4; i++) {
          calculateReactionForce(game, iNext.x, iNext.y, i);

          // Yes, Liero does this in every iteration. Keep it this way.

          if (iNext.x < 4) {
            reacts[static_cast<int>(Worm::ReactionForce::Right)] += 5;
          } else if (iNext.x > game.level.width - 5) {
            reacts[static_cast<int>(Worm::ReactionForce::Left)] += 5;
          }

          if (iNext.y < 5) {
            reacts[static_cast<int>(Worm::ReactionForce::Down)] += 5;
          } else {
            if (common.H[HWormFloat]) {
              if (iNext.y > LC(WormFloatLevel))
                vel.y -= LC(WormFloatPower);
            } else if (iNext.y > game.level.height - 6) {
              reacts[static_cast<int>(Worm::ReactionForce::Up)] += 5;
            }
          }
        }

        if (reacts[static_cast<int>(Worm::ReactionForce::Down)] < 2) {
          if (reacts[static_cast<int>(Worm::ReactionForce::Up)] > 0) {
            if (reacts[static_cast<int>(Worm::ReactionForce::Left)] > 0 ||
                reacts[static_cast<int>(Worm::ReactionForce::Right)] > 0) {
              // Low or none push down,
              // Push up and
              // Push left or right

              pos.y -= itof(1);
              next.y = pos.y + vel.y;
              iNext.y = ftoi(next.y);

              calculateReactionForce(
                  game, iNext.x, iNext.y,
                  static_cast<int>(Worm::ReactionForce::Left));
              calculateReactionForce(
                  game, iNext.x, iNext.y,
                  static_cast<int>(Worm::ReactionForce::Right));
            }
          }
        }

        if (reacts[static_cast<int>(Worm::ReactionForce::Up)] < 2) {
          if (reacts[static_cast<int>(Worm::ReactionForce::Down)] > 0) {
            if (reacts[static_cast<int>(Worm::ReactionForce::Left)] > 0 ||
                reacts[static_cast<int>(Worm::ReactionForce::Right)] > 0) {
              // Low or none push up,
              // Push down and
              // Push left or right

              pos.y += itof(1);
              next.y = pos.y + vel.y;
              iNext.y = ftoi(next.y);

              calculateReactionForce(
                  game, iNext.x, iNext.y,
                  static_cast<int>(Worm::ReactionForce::Left));
              calculateReactionForce(
                  game, iNext.x, iNext.y,
                  static_cast<int>(Worm::ReactionForce::Right));
            }
          }
        }
      }

      auto ipos = ftoi(pos);

      auto br = game.bonuses.all();
      for (Bonus* i; (i = br.next());) {
        if (ipos.x + 5 > ftoi(i->x) && ipos.x - 5 < ftoi(i->x) &&
            ipos.y + 5 > ftoi(i->y) && ipos.y - 5 < ftoi(i->y)) {
          if (i->frame == 1) {
            if (health < settings->health) {
              game.bonuses.free(br);

              game.doHealing(
                  *this, (std::uniform_int_distribution<int>(
                              0, LC(BonusHealthVar) - 1)(game.rand) +
                          LC(BonusMinHealth)) *
                             settings->health / 100);
            }
          } else if (i->frame == 0) {
            if (std::uniform_int_distribution<int>(
                    0, LC(BonusExplodeRisk) - 1)(game.rand) > 1) {
              WormWeapon& ww = weapons[currentWeapon];

              if (!common.H[HBonusReloadOnly]) {
                fireCone = 0;

                ww.type = &common.weapons[i->weapon];
                ww.ammo = ww.type->ammo;
              }

              game.soundPlayer->play(24);

              game.bonuses.free(br);

              ww.loadingLeft = 0;
            } else {
              int bix = ftoi(i->x);
              int biy = ftoi(i->y);
              game.bonuses.free(br);
              common.sobjectTypes[0].create(game, bix, biy, index, 0);
            }
          }
        }
      }

      processSteerables(game);

      if (!movable && !pressed(Worm::Control::Left) &&
          !pressed(Worm::Control::Right)) {
        // processSteerables sets movable to false, does this interfere?
        movable = true;
      }  // 2FB1

      processAiming(game);
      processTasks(game);
      processWeapons(game);

      if (pressed(Worm::Control::Fire) && !pressed(Worm::Control::Change) &&
          weapons[currentWeapon].available() &&
          weapons[currentWeapon].delayLeft <= 0) {
        fire(game);
      } else {
        if (weapons[currentWeapon].type->loopSound)
          game.soundPlayer->stop(&weapons[currentWeapon]);
      }

      processPhysics(game);
      processSight(game);

      if (pressed(Worm::Control::Change)) {
        processWeaponChange(game);
      } else {
        keyChangePressed = false;
        processMovement(game);
      }

      // what exactly does this code do?
      if (health < settings->health / 4) {
        // if health is negative, uniform_int_distribution(a, b) blows up
        // because b > a
        if (health < 0) {
          // skip
        } else if (
            std::uniform_int_distribution<int>(0, health + 6 - 1)(game.rand) ==
            0) {
          if (std::uniform_int_distribution<int>(0, 3 - 1)(game.rand) == 0) {
            // NOTE: MUST be outside the unpredictable branch below
            int snd =
                18 + std::uniform_int_distribution<int>(0, 3 - 1)(game.rand);
            if (!game.soundPlayer->isPlaying(this)) {
              game.soundPlayer->play(snd, this);
            }
          }

          common.nobjectTypes[6].create1(game, vel, pos, 0, index, 0);
        }
      }

      if (health <= 0) {
        leaveShellTimer = 0;
        makeSightGreen = false;

        Weapon const& w = *weapons[currentWeapon].type;
        if (w.loopSound) {
          game.soundPlayer->stop(&weapons[currentWeapon]);
        }

        int deathSnd =
            15 + std::uniform_int_distribution<int>(0, 3 - 1)(game.rand);
        game.soundPlayer->play(deathSnd, this);

        fireCone = 0;
        ninjarope.out = false;

        if (game.settings->gameMode == Settings::GameMode::ScalesOfJustice) {
          while (health <= 0) {
            health += settings->health;
            --lives;
          }
        } else {
          --lives;
        }

        int oldLastKilled = game.lastKilledIdx;
        // For GameOfTag, 'it' doesn't change if the killer
        // was not 'it', itself, unknown or there were no 'it'.
        if (game.settings->gameMode != Settings::GameMode::GameOfTag ||
            game.lastKilledIdx < 0 || lastKilledByIdx < 0 ||
            lastKilledByIdx == index || lastKilledByIdx == game.lastKilledIdx) {
          game.lastKilledIdx = index;
        }
        game.gotChanged = (oldLastKilled != game.lastKilledIdx);

        if (lastKilledByIdx >= 0 && lastKilledByIdx != index) {
          ++game.wormByIdx(lastKilledByIdx)->kills;
        }

        visible = false;
        killedTimer = 150;

        int max = 120 * game.settings->blood / 100;

        if (max > 1) {
          for (int i = 1; i <= max; ++i) {
            common.nobjectTypes[6].create2(
                game, std::uniform_int_distribution<int>(0, 128 - 1)(game.rand),
                vel / 3, pos, 0, index, 0);
          }
        }

        for (int i = 7; i <= 105; i += 14) {
          common.nobjectTypes[index].create2(
              game,
              i + std::uniform_int_distribution<int>(0, 14 - 1)(game.rand),
              vel / 3, pos, 0, index, 0);
        }

        game.statsRecorder->afterDeath(this);

        release(Worm::Control::Fire);
      }

      // Update frame
      int animFrame = animate ? ((game.cycles & 31) >> 3) : 0;
      currentFrame = angleFrame() + game.settings->wormAnimTab[animFrame];
    } else {
      // Worm is dead
      steerableCount = 0;

      if (pressedOnce(Worm::Control::Fire))
        ready = true;

      if (killedTimer > 0)
        --killedTimer;

      // Don't respawn in quicksim
      if (killedTimer == 0 && !game.quickSim)
        beginRespawn(game);

      if (killedTimer < 0)
        doRespawning(game);
    }
  }
}

int Worm::angleFrame() const {
  int x = ftoi(aimingAngle) - 12;

  if (direction != static_cast<int>(Worm::Direction::Left))
    x -= 49;

  x >>= 3;
  if (x < 0)
    x = 0;
  else if (x > 6)
    x = 6;

  if (direction != static_cast<int>(Worm::Direction::Left)) {
    x = 6 - x;
  }  // 9581

  return x;
}

int sqrVectorLength(int x, int y) {
  return x * x + y * y;
}

void DumbLieroAI::process(Game& game, Worm& worm) {
  const Common& common = *game.common;

  const Worm* target = 0;
  int minLen = 0;
  for (std::size_t i = 0; i < game.worms.size(); ++i) {
    Worm* w = game.worms[i];
    if (w != &worm) {
      int len = sqrVectorLength(
          ftoi(worm.pos.x) - ftoi(w->pos.x), ftoi(worm.pos.y) - ftoi(w->pos.y));
      // First or closer worm
      if (!target || len < minLen) {
        target = w;
        minLen = len;
      }
    }
  }

  int maxDist;

  const WormWeapon& ww = worm.weapons[worm.currentWeapon];
  Weapon const& w = *ww.type;

  if (w.timeToExplo > 0 && w.timeToExplo < 500) {
    maxDist = (w.timeToExplo - w.timeToExploV / 2) * w.speed / 130;
  } else {
    maxDist = w.speed - w.gravity / 10;
  }  // 4D43

  if (maxDist < 90)
    maxDist = 90;

  fixedvec delta = target->pos - worm.pos;
  auto idelta = ftoi(delta);

  int realDist = vectorLength(idelta.x, idelta.y);

  if (realDist < maxDist || !worm.visible) {
    // The other worm is close enough
    bool fire = worm.pressed(Worm::Control::Fire);
    if (std::uniform_int_distribution<int>(
            0, common.aiParams
                       .k[fire][static_cast<int>(WormSettings::Control::Fire)] -
                   1)(game.rand) == 0) {
      worm.setControlState(Worm::Control::Fire, !fire);
    }  // 4DE7
  } else if (worm.visible) {
    worm.release(Worm::Control::Fire);
  }  // 4DFA

  // In Liero this is a loop with two iterations, that's better maybe
  bool jump = worm.pressed(Worm::Control::Jump);
  if (std::uniform_int_distribution<int>(
          0, common.aiParams
                     .k[jump][static_cast<int>(WormSettings::Control::Jump)] -
                 1)(game.rand) == 0) {
    worm.toggleControlState(Worm::Control::Jump);
  }

  bool change = worm.pressed(Worm::Control::Change);
  if (std::uniform_int_distribution<int>(
          0,
          common.aiParams
                  .k[change][static_cast<int>(WormSettings::Control::Change)] -
              1)(game.rand) == 0) {
    worm.toggleControlState(Worm::Control::Change);
  }

  // l_4E6B:
  //  Moves up

  // l_4EE5:
  if (realDist > 0) {
    delta /= realDist;
  } else {
    delta.zero();
  }  // 4F2F

  int dir = 1;

  for (; dir < 128; ++dir) {
    // The original had 0xC000, which is wrong
    if (std::abs(cossinTable[dir].x - delta.x) < 0xC00 &&
        std::abs(cossinTable[dir].y - delta.y) < 0xC00)
      break;
  }  // 4F93

  fixed adeltaX = std::abs(delta.x);
  fixed adeltaY = std::abs(delta.y);

  if (dir >= 128) {
    if (delta.x > 0) {
      if (delta.y < 0) {
        if (adeltaY > adeltaX)
          dir = 64 + std::uniform_int_distribution<int>(0, 16 - 1)(game.rand);
        else if (adeltaX > adeltaY)
          dir = 80 + std::uniform_int_distribution<int>(0, 16 - 1)(game.rand);
        else
          dir = 80;
      } else  // deltaY >= 0
      {
        if (adeltaX > adeltaY)
          dir = 96 + std::uniform_int_distribution<int>(0, 16 - 1)(game.rand);
        else
          dir = 116;
      }
    } else {
      if (delta.y < 0) {
        if (adeltaY > adeltaX) {
          dir = 48 + std::uniform_int_distribution<int>(0, 16 - 1)(game.rand);
        } else if (adeltaX > adeltaY) {
          dir = 32 + std::uniform_int_distribution<int>(0, 16 - 1)(game.rand);
        } else {
          // This was 56, but that seems wrong
          dir = 48;
        }
      } else {
        // deltaX <= 0 && deltaY >= 0
        if (adeltaX > adeltaY)
          dir = 12 + std::uniform_int_distribution<int>(0, 16 - 1)(game.rand);
        else
          dir = 12;
      }
    }
  }

  change = worm.pressed(Worm::Control::Change);

  if (change) {
    if (std::uniform_int_distribution<int>(
            0,
            common.aiParams.k[worm.pressed(Worm::Control::Left)]
                             [static_cast<int>(WormSettings::Control::Left)] -
                1)(game.rand) == 0) {
      worm.toggleControlState(Worm::Control::Left);
    }

    if (std::uniform_int_distribution<int>(
            0,
            common.aiParams.k[worm.pressed(Worm::Control::Right)]
                             [static_cast<int>(WormSettings::Control::Right)] -
                1)(game.rand) == 0) {
      worm.toggleControlState(Worm::Control::Right);
    }

    if (worm.ninjarope.out && worm.ninjarope.attached) {
      // l_525F:
      bool up = worm.pressed(Worm::Control::Up);

      if (std::uniform_int_distribution<int>(
              0, common.aiParams
                         .k[up][static_cast<int>(WormSettings::Control::Up)] -
                     1)(game.rand) == 0) {
        worm.toggleControlState(Worm::Control::Up);
      }

      bool down = worm.pressed(Worm::Control::Down);
      if (std::uniform_int_distribution<int>(
              0,
              common.aiParams
                      .k[down][static_cast<int>(WormSettings::Control::Down)] -
                  1)(game.rand) == 0) {
        worm.toggleControlState(Worm::Control::Down);
      }
    } else {
      // l_52D2:
      worm.release(Worm::Control::Up);
      worm.release(Worm::Control::Down);
    }  // 52F8
  }  // if(change)
  else {
    if (realDist > maxDist) {
      worm.setControlState(Worm::Control::Right, (delta.x > 0));
      worm.setControlState(Worm::Control::Left, (delta.x <= 0));
    }  // 5347
    else {
      worm.release(Worm::Control::Right);
      worm.release(Worm::Control::Left);
    }

    if (worm.direction != static_cast<int>(Worm::Direction::Left)) {
      if (dir < 64)
        worm.press(Worm::Control::Left);
      // 5369
      worm.setControlState(
          Worm::Control::Up, (dir + 1 < ftoi(worm.aimingAngle)));
      // 5379
      worm.setControlState(
          Worm::Control::Down, (dir - 1 > ftoi(worm.aimingAngle)));
    } else {
      if (dir > 64)
        worm.press(Worm::Control::Right);
      // 53C6
      worm.setControlState(
          Worm::Control::Up, (dir - 1 > ftoi(worm.aimingAngle)));
      // 53E8
      worm.setControlState(
          Worm::Control::Down, (dir + 1 < ftoi(worm.aimingAngle)));
      // 540A
    }

    if (worm.pressed(Worm::Control::Left) &&
        worm.reacts[static_cast<int>(Worm::ReactionForce::Right)]) {
      if (worm.reacts[static_cast<int>(Worm::ReactionForce::Down)] > 0)
        worm.press(Worm::Control::Right);
      else
        worm.press(Worm::Control::Jump);
    }  // 5454

    if (worm.pressed(Worm::Control::Right) &&
        worm.reacts[static_cast<int>(Worm::ReactionForce::Left)]) {
      if (worm.reacts[static_cast<int>(Worm::ReactionForce::Down)] > 0)
        worm.press(Worm::Control::Left);
      else
        worm.press(Worm::Control::Jump);
    }  // 549E
  }
}

void Worm::initWeapons(Game& game) {
  Common& common = *game.common;
  // It was 1 in OpenLiero A1
  currentWeapon = 0;

  for (int j = 0; j < Settings::selectableWeapons; ++j) {
    WormWeapon& ww = weapons[j];
    ww.type = &common.weapons[common.weapOrder[settings->weapons[j] - 1]];
    ww.ammo = ww.type->ammo;
    ww.delayLeft = 0;
    ww.loadingLeft = 0;
  }
}

void Worm::beginRespawn(Game& game) {
  const Common& common = *game.common;

  auto temp = ftoi(pos);

  logicRespawn = temp - gvl::ivec2(80, 80);

  auto enemy = temp;

  if (game.worms.size() == 2) {
    enemy = ftoi(game.worms[index ^ 1]->pos);
  }

  int trials = 0;
  do {
    pos.x = itof(
        LC(WormSpawnRectX) + std::uniform_int_distribution<int>(
                                 0, LC(WormSpawnRectW) - 1)(game.rand));
    pos.y = itof(
        LC(WormSpawnRectY) + std::uniform_int_distribution<int>(
                                 0, LC(WormSpawnRectH) - 1)(game.rand));

    // The original didn't have + 4 in both, which seems
    // to be done in the exe and makes sense.
    while (ftoi(pos.y) + 4 < game.level.height &&
           game.level.mat(ftoi(pos.x), ftoi(pos.y) + 4).background()) {
      pos.y += itof(1);
    }

    if (++trials >= 50000)
      break;
  } while (!checkRespawnPosition(
      game, enemy.x, enemy.y, temp.x, temp.y, ftoi(pos.x), ftoi(pos.y)));

  killedTimer = -1;
}

void limitXY(int& x, int& y, int maxX, int maxY) {
  if (x < 0)
    x = 0;
  else if (x > maxX)
    x = maxX;

  if (y < 0)
    y = 0;
  if (y > maxY)
    y = maxY;
}

void Worm::doRespawning(Game& game) {
  Common& common = *game.common;

  for (int c = 0; c < 4; c++) {
    if (logicRespawn.x < ftoi(pos.x) - 80)
      ++logicRespawn.x;
    else if (logicRespawn.x > ftoi(pos.x) - 80)
      --logicRespawn.x;

    if (logicRespawn.y < ftoi(pos.y) - 80)
      ++logicRespawn.y;
    else if (logicRespawn.y > ftoi(pos.y) - 80)
      --logicRespawn.y;
  }

  limitXY(
      logicRespawn.x, logicRespawn.y, game.level.width - 158,
      game.level.height - 158);

  int destX = ftoi(pos.x) - 80;
  int destY = ftoi(pos.y) - 80;
  limitXY(destX, destY, game.level.width - 158, game.level.height - 158);

  // Don't spawn in quicksim
  if (logicRespawn.x < destX + 5 && logicRespawn.x > destX - 5 &&
      logicRespawn.y < destY + 5 && logicRespawn.y > destY - 5 && ready) {
    auto ipos = ftoi(pos);
    drawDirtEffect(common, game.rand, game.level, 0, ipos.x - 7, ipos.y - 7);
    if (game.settings->shadow)
      correctShadow(
          common, game.level,
          gvl::rect(ipos.x - 10, ipos.y - 10, ipos.x + 11, ipos.y + 11));

    ready = false;
    game.soundPlayer->play(21);

    visible = true;
    fireCone = 0;
    vel.zero();
    if (game.settings->gameMode != Settings::GameMode::ScalesOfJustice)
      health = settings->health;

    // NOTE: This was done at death before, but doing it here seems to make more
    // sense
    if (std::uniform_int_distribution<int>(0, 1)(game.rand) & 1) {
      aimingAngle = itof(32);
      direction = static_cast<int>(Worm::Direction::Left);
    } else {
      aimingAngle = itof(96);
      direction = static_cast<int>(Worm::Direction::Right);
    }

    game.statsRecorder->afterSpawn(this);
  }
}

void Worm::processWeapons(Game& game) {
  Common& common = *game.common;

  for (int i = 0; i < Settings::selectableWeapons; ++i) {
    if (weapons[i].delayLeft >= 0)
      --weapons[i].delayLeft;
  }

  WormWeapon& ww = weapons[currentWeapon];
  Weapon const& w = *ww.type;

  if (ww.ammo <= 0) {
    int computedLoadingTime = w.computedLoadingTime(*game.settings);
    ww.loadingLeft = computedLoadingTime;
    ww.ammo = w.ammo;
  }

  // NOTE: computedLoadingTime is never 0, so this works
  if (ww.loadingLeft > 0) {
    --ww.loadingLeft;
    if (ww.loadingLeft <= 0 && w.playReloadSound) {
      game.soundPlayer->play(24);
    }
  }

  if (fireCone > 0) {
    --fireCone;
  }

  if (leaveShellTimer > 0) {
    if (--leaveShellTimer <= 0) {
      auto velY = std::uniform_int_distribution<int>(-19999, 0)(game.rand);
      auto velX = std::uniform_int_distribution<int>(8000, 15999)(game.rand);
      common.nobjectTypes[7].create1(
          game, fixedvec(velX, velY), pos, 0, index, 0);
    }
  }
}

void Worm::processMovement(Game& game) {
  Common& common = *game.common;

  if (movable) {
    bool left = pressed(Worm::Control::Left);
    bool right = pressed(Worm::Control::Right);

    if (left && !right) {
      if (vel.x > LC(MaxVelLeft))
        vel.x -= LC(WalkVelLeft);

      if (direction != static_cast<int>(Worm::Direction::Left)) {
        aimingSpeed = 0;
        if (aimingAngle >= itof(64))
          aimingAngle = itof(128) - aimingAngle;
        direction = static_cast<int>(Worm::Direction::Left);
      }

      animate = true;
    }

    if (!left && right) {
      if (vel.x < LC(MaxVelRight))
        vel.x += LC(WalkVelRight);

      if (direction != static_cast<int>(Worm::Direction::Right)) {
        aimingSpeed = 0;
        if (aimingAngle <= itof(64))
          aimingAngle = itof(128) - aimingAngle;
        direction = static_cast<int>(Worm::Direction::Right);
      }

      animate = true;
    }

    if (left && right) {
      if (ableToDig) {
        ableToDig = false;

        fixedvec dir(cossinTable[ftoi(aimingAngle)]);

        auto digPos = dir * 2 + pos;

        digPos.x -= itof(7);
        digPos.y -= itof(7);

        auto idigPos = ftoi(digPos);
        drawDirtEffect(common, game.rand, game.level, 7, idigPos.x, idigPos.y);
        if (game.settings->shadow)
          correctShadow(
              common, game.level,
              gvl::rect(
                  idigPos.x - 3, idigPos.y - 3, idigPos.x + 18,
                  idigPos.y + 18));

        digPos += dir * 2;

        // l_43EB:
        idigPos = ftoi(digPos);
        drawDirtEffect(common, game.rand, game.level, 7, idigPos.x, idigPos.y);
        if (game.settings->shadow)
          correctShadow(
              common, game.level,
              gvl::rect(
                  idigPos.x - 3, idigPos.y - 3, idigPos.x + 18,
                  idigPos.y + 18));

        // NOTE! Maybe the shadow corrections can be joined into one? Mmm?
      }  // 4552
    } else {
      ableToDig = true;
    }

    if (!left && !right) {
      // Don't animate the this unless he is moving
      animate = false;
    }  // 458C
  }
}

void Worm::processTasks(Game& game) {
  const Common& common = *game.common;

  if (pressed(Worm::Control::Change)) {
    if (ninjarope.out) {
      if (pressed(Worm::Control::Up))
        ninjarope.length -= LC(NRPullVel);
      if (pressed(Worm::Control::Down))
        ninjarope.length += LC(NRReleaseVel);

      if (ninjarope.length < LC(NRMinLength))
        ninjarope.length = LC(NRMinLength);
      if (ninjarope.length > LC(NRMaxLength))
        ninjarope.length = LC(NRMaxLength);
    }

    if (pressedOnce(Worm::Control::Jump)) {
      ninjarope.out = true;
      ninjarope.attached = false;

      game.soundPlayer->play(5);

      ninjarope.pos = pos;
      ninjarope.vel = fixedvec(
          cossinTable[ftoi(aimingAngle)].x << LC(NRThrowVelX),
          cossinTable[ftoi(aimingAngle)].y << LC(NRThrowVelY));

      ninjarope.length = LC(NRInitialLength);
    }
  } else {
    // Jump = remove ninjarope, jump
    if (pressed(Worm::Control::Jump)) {
      ninjarope.out = false;
      ninjarope.attached = false;

      if ((reacts[static_cast<int>(Worm::ReactionForce::Up)] > 0 ||
           common.H[HAirJump]) &&
          (ableToJump || common.H[HMultiJump])) {
        vel.y -= LC(JumpForce);
        ableToJump = false;
      }
    } else
      ableToJump = true;
  }
}

void Worm::processAiming(const Game& game) {
  const Common& common = *game.common;

  bool up = pressed(Worm::Control::Up);
  bool down = pressed(Worm::Control::Down);

  if (aimingSpeed != 0) {
    aimingAngle += aimingSpeed;

    if (!up && !down) {
      aimingSpeed = (aimingSpeed * LC(AimFricMult)) / LC(AimFricDiv);
    }

    if (direction == static_cast<int>(Worm::Direction::Right)) {
      if (ftoi(aimingAngle) > LC(AimMaxRight)) {
        aimingSpeed = 0;
        aimingAngle = itof(LC(AimMaxRight));
      }
      if (ftoi(aimingAngle) < LC(AimMinRight)) {
        aimingSpeed = 0;
        aimingAngle = itof(LC(AimMinRight));
      }
    } else {
      if (ftoi(aimingAngle) < LC(AimMaxLeft)) {
        aimingSpeed = 0;
        aimingAngle = itof(LC(AimMaxLeft));
      }
      if (ftoi(aimingAngle) > LC(AimMinLeft)) {
        aimingSpeed = 0;
        aimingAngle = itof(LC(AimMinLeft));
      }
    }
  }

  if (movable && (!ninjarope.out || !pressed(Worm::Control::Change))) {
    if (up) {
      if (direction == static_cast<int>(Worm::Direction::Left)) {
        if (aimingSpeed < LC(MaxAimVelLeft))
          aimingSpeed += LC(AimAccLeft);
      } else {
        if (aimingSpeed > LC(MaxAimVelRight))
          aimingSpeed -= LC(AimAccRight);
      }
    }

    if (down) {
      if (direction == static_cast<int>(Worm::Direction::Right)) {
        if (aimingSpeed < LC(MaxAimVelLeft))
          aimingSpeed += LC(AimAccLeft);
      } else {
        if (aimingSpeed > LC(MaxAimVelRight))
          aimingSpeed -= LC(AimAccRight);
      }
    }
  }
}

void Worm::processWeaponChange(Game& game) {
  if (!keyChangePressed) {
    release(Worm::Control::Left);
    release(Worm::Control::Right);

    keyChangePressed = true;
  }

  fireCone = 0;
  animate = false;

  if (weapons[currentWeapon].type->loopSound) {
    game.soundPlayer->stop(&weapons[currentWeapon]);
  }

  if (weapons[currentWeapon].available() || game.settings->loadChange) {
    if (pressedOnce(Worm::Control::Left)) {
      if (--currentWeapon < 0)
        currentWeapon = Settings::selectableWeapons - 1;

      hotspotX = ftoi(pos.x);
      hotspotY = ftoi(pos.y);
    }

    if (pressedOnce(Worm::Control::Right)) {
      if (++currentWeapon >= Settings::selectableWeapons)
        currentWeapon = 0;

      hotspotX = ftoi(pos.x);
      hotspotY = ftoi(pos.y);
    }
  }
}

void Worm::fire(Game& game) {
  Common& common = *game.common;
  WormWeapon& ww = weapons[currentWeapon];
  Weapon const& w = *ww.type;

  --ww.ammo;
  ww.delayLeft = w.delay;

  fireCone = w.fireCone;

  fixedvec firing(
      cossinTable[ftoi(aimingAngle)] * (w.detectDistance + 5) + pos -
      fixedvec(0, itof(1)));

  if (w.leaveShells > 0) {
    if (std::uniform_int_distribution<int>(0, w.leaveShells - 1)(game.rand) ==
        0) {
      leaveShellTimer = w.leaveShellDelay;
    }
  }

  if (w.launchSound >= 0) {
    if (w.loopSound) {
      if (!game.soundPlayer->isPlaying(&weapons[currentWeapon])) {
        game.soundPlayer->play(w.launchSound, &weapons[currentWeapon], -1);
      }
    } else {
      game.soundPlayer->play(w.launchSound);
    }
  }

  int speed = w.speed;
  fixedvec firingVel;
  int parts = w.parts;

  if (w.affectByWorm) {
    if (speed < 100)
      speed = 100;

    firingVel = vel * 100 / speed;
  }

  for (int i = 0; i < parts; ++i) {
    w.fire(game, ftoi(aimingAngle), firingVel, speed, firing, index, &ww);
  }

  int recoil = w.recoil;

  if (common.H[HSignedRecoil] && recoil >= 128)
    recoil -= 256;

  vel -= cossinTable[ftoi(aimingAngle)] * recoil / 100;
}

bool checkForWormHit(Game& game, int x, int y, int dist, Worm* ownWorm) {
  for (std::size_t i = 0; i < game.worms.size(); ++i) {
    Worm& w = *game.worms[i];

    if (&w != ownWorm) {
      return checkForSpecWormHit(game, x, y, dist, w);
    }
  }

  return false;
}

bool checkForSpecWormHit(Game& game, int x, int y, int dist, Worm& w) {
  Common& common = *game.common;

  if (!w.visible)
    return false;

  PalIdx* wormSprite = common.wormSprite(w.currentFrame, w.direction, 0);

  int deltaX = x - ftoi(w.pos.x) + 7;
  int deltaY = y - ftoi(w.pos.y) + 5;

  gvl::rect r(
      deltaX - dist, deltaY - dist, deltaX + dist + 1, deltaY + dist + 1);

  r.intersect(gvl::rect(0, 0, 16, 16));

  for (int cy = r.y1; cy < r.y2; ++cy)
    for (int cx = r.x1; cx < r.x2; ++cx) {
      assert(cy * 16 + cx < 16 * 16);
      if (common.materials[wormSprite[cy * 16 + cx]].worm())
        return true;
    }

  return false;
}

void Worm::processSight(Game& game) {
  const Common& common = *game.common;

  const WormWeapon& ww = weapons[currentWeapon];
  Weapon const& w = *ww.type;

  if (ww.available() &&
      (w.laserSight || ww.type - &common.weapons[0] == LC(LaserWeapon) - 1)) {
    fixedvec dir = cossinTable[ftoi(aimingAngle)];
    fixedvec temp = fixedvec(pos.x + dir.x * 6, pos.y + dir.y * 6 - itof(1));

    do {
      temp += dir;
      makeSightGreen =
          checkForWormHit(game, ftoi(temp.x), ftoi(temp.y), 0, this);
    } while (temp.x >= 0 && temp.y >= 0 && temp.x < itof(game.level.width) &&
             temp.y < itof(game.level.height) &&
             game.level.mat(ftoi(temp)).background() && !makeSightGreen);

    hotspotX = ftoi(temp.x);
    hotspotY = ftoi(temp.y);
  } else
    makeSightGreen = false;
}

void Worm::processSteerables(Game& game) {
  steerableCount = 0;
  steerableSumX = 0;
  steerableSumY = 0;

  const WormWeapon& ww = weapons[currentWeapon];
  if (ww.type->shotType == Weapon::ShotType::Steerable) {
    auto wr = game.wobjects.all();
    for (WObject* i; (i = wr.next());) {
      if (i->type == ww.type && i->ownerIdx == index) {
        if (pressed(Worm::Control::Left))
          i->curFrame -= (game.cycles & 1) + 1;

        if (pressed(Worm::Control::Right))
          i->curFrame += (game.cycles & 1) + 1;

        // Wrap
        i->curFrame &= 127;
        movable = false;

        steerableSumX += ftoi(i->pos.x);
        steerableSumY += ftoi(i->pos.y);
        ++steerableCount;
      }
    }
  }
}
