#include <SDL.h>
#include <cstdlib>
#include <ctime>

#include <algorithm>
#include "ai/predictive_ai.hpp"
#include "constants.hpp"
#include "filesystem.hpp"
#include "game.hpp"
#include "gfx/renderer.hpp"
#include "spectatorviewport.hpp"
#include "viewport.hpp"
#include "weapsel.hpp"
#include "worm.hpp"

Game::Game(
    std::shared_ptr<Common> common,
    std::shared_ptr<Settings> settingsInit,
    std::shared_ptr<SoundPlayer> soundPlayer)
    : common(common),
      soundPlayer(soundPlayer),
      settings(settingsInit),
      statsRecorder(new NormalStatsRecorder),
      level(*common),
      screenFlash(0),
      gotChanged(false),
      lastKilledIdx(-1),
      paused(true),
      quickSim(false) {
  // https://stackoverflow.com/questions/76745282/c-mt19937-getting-the-same-sequence-multiple-times
  // for deterministic values, the value passed to mt19937 needs to be constant
  // TODO stuff the random value somewhere we can access it for replays/etc
  rand_seed = uint32_t(std::time(0));
  rand = std::mt19937(rand_seed);

  cycles = 0;
}

Game::~Game() {
  clearViewports();
  clearWorms();
}

void Game::onKey(uint32_t key, bool state) {
  for (std::size_t i = 0; i < worms.size(); ++i) {
    Worm& w = *worms[i];

    for (std::size_t control = 0;
         control < static_cast<int>(WormSettings::Control::MaxControl);
         ++control) {
      if (w.settings->controls[control] == key) {
        w.setControlState(static_cast<Worm::Control>(control), state);
      }
    }
  }
}

Worm* Game::findControlForKey(uint32_t key, Worm::Control& control) {
  for (std::size_t i = 0; i < worms.size(); ++i) {
    Worm& w = *worms[i];

    const uint32_t* controls =
        settings->extensions ? w.settings->controlsEx : w.settings->controls;
    std::size_t maxControl =
        settings->extensions
            ? static_cast<int>(WormSettings::Control::MaxControlEx)
            : static_cast<int>(WormSettings::Control::MaxControl);
    for (std::size_t c = 0; c < maxControl; ++c) {
      if (controls[c] == key) {
        control = static_cast<Worm::Control>(c);
        return &w;
      }
    }
  }

  return 0;
}

void Game::releaseControls() {
  for (std::size_t i = 0; i < worms.size(); ++i) {
    Worm& w = *worms[i];

    for (std::size_t control = 0;
         control < static_cast<int>(WormSettings::Control::MaxControl);
         ++control) {
      w.release(static_cast<Worm::Control>(control));
    }
  }
}

void Game::clearViewports() {
  viewports.clear();
  spectatorViewports.clear();
}

void Game::addViewport(Viewport* vp) {
  // vp->worm->viewport = vp;
  viewports.push_back(vp);
}

void Game::addSpectatorViewport(SpectatorViewport* vp) {
  spectatorViewports.push_back(vp);
}

void Game::processViewports() {
  for (std::size_t i = 0; i < viewports.size(); ++i) {
    viewports[i]->process(*this);
  }
  for (std::size_t i = 0; i < spectatorViewports.size(); ++i) {
    spectatorViewports[i]->process(*this);
  }
}

void Game::drawViewports(Renderer& renderer, GameState state, bool isReplay) {
  for (std::size_t i = 0; i < viewports.size(); ++i) {
    viewports[i]->draw(*this, renderer, state, isReplay);
  }
}

void Game::drawSpectatorViewports(
    Renderer& renderer,
    GameState state,
    bool isReplay) {
  for (std::size_t i = 0; i < spectatorViewports.size(); ++i) {
    spectatorViewports[i]->draw(*this, renderer, state, isReplay);
  }
}

void Game::clearWorms() {
  worms.clear();
}

void Game::resetWorms() {
  for (std::size_t i = 0; i < worms.size(); ++i) {
    Worm& w = *worms[i];
    w.health = w.settings->health;
    w.lives = settings->lives;  // Not in the original!
    w.kills = 0;
    w.visible = false;
    w.killedTimer = 150;

    w.currentWeapon = 1;
  }
}

void Game::addWorm(Worm* worm) {
  worms.push_back(worm);
}

void Game::draw(
    Renderer& renderer,
    GameState state,
    bool useSpectatorViewports,
    bool isReplay) {
  if (useSpectatorViewports) {
    drawSpectatorViewports(renderer, state, isReplay);
  } else {
    drawViewports(renderer, state, isReplay);
  }

  // common->font.drawText(toString(cycles / 70), 10, 10, 7);

  renderer.pal = renderer.origpal;

  for (int w = 0; w < 4; ++w)
    renderer.pal.rotateFrom(
        renderer.origpal, common->colorAnim[w].from, common->colorAnim[w].to,
        cycles >> 3);

  renderer.pal.fade(renderer.fadeValue);

  if (screenFlash > 0) {
    renderer.pal.lightUp(screenFlash);
  }
}

bool checkBonusSpawnPosition(Game& game, int x, int y) {
  gvl::rect rect(x - 2, y - 2, x + 3, y + 3);

  rect.intersect(game.level.rect());

  for (int cx = rect.x1; cx < rect.x2; ++cx)
    for (int cy = rect.y1; cy < rect.y2; ++cy) {
      if (game.level.mat(cx, cy).dirtRock())
        return false;
    }

  return true;
}

void Game::createBonus() {
  Common& common = *this->common;

  if (static_cast<int>(bonuses.size()) >= settings->maxBonuses)
    return;

  for (std::size_t i = 0; i < 50000; ++i) {
    int ix =
        std::uniform_int_distribution<int>(0, LC(BonusSpawnRectW) - 1)(rand);
    int iy =
        std::uniform_int_distribution<int>(0, LC(BonusSpawnRectH) - 1)(rand);

    if (common.H[HBonusSpawnRect]) {
      ix += LC(BonusSpawnRectX);
      iy += LC(BonusSpawnRectY);
    }

    if (checkBonusSpawnPosition(*this, ix, iy)) {
      int frame;

      if (common.H[HBonusOnlyHealth])
        frame = 1;
      else if (common.H[HBonusOnlyWeapon])
        frame = 0;
      else
        frame = std::uniform_int_distribution<int>(0, 2 - 1)(rand);

      Bonus* bonus = bonuses.newObject();
      if (!bonus)
        return;

      bonus->x = itof(ix);
      bonus->y = itof(iy);
      bonus->velY = 0;
      bonus->frame = frame;
      bonus->timer = std::uniform_int_distribution<int>(
                         0, common.bonusRandTimer[frame][1] - 1)(rand) +
                     common.bonusRandTimer[frame][0];

      if (frame == 0) {
        do {
          bonus->weapon = std::uniform_int_distribution<int>(
              0, common.weapons.size() - 1)(rand);
        } while (settings->weapTable[bonus->weapon] == 2);
      }

      common.sobjectTypes[7].create(*this, ix, iy, 0, 0);
      return;
    }
  }  // 234F
}

void Game::processFrame() {
  statsRecorder->preTick(*this);

  if (screenFlash > 0)
    --screenFlash;

  for (std::size_t i = 0; i < viewports.size(); ++i) {
    if (viewports[i]->shake > 0)
      viewports[i]->shake -= 4000;  // TODO: Read 4000 from exe?
  }

  for (std::size_t i = 0; i < spectatorViewports.size(); ++i) {
    if (spectatorViewports[i]->shake > 0)
      spectatorViewports[i]->shake -= 4000;  // TODO: Read 4000 from exe?
  }

  auto br = bonuses.all();
  for (Bonus* i; (i = br.next());) {
    i->process(*this);
  }

  if ((cycles & 1) == 0) {
    for (std::size_t i = 0; i < viewports.size(); ++i) {
      Viewport& v = *viewports[i];

      bool down = false;

      if (wormByIdx(v.wormIdx)->killedTimer > 16)
        down = true;

      if (down) {
        if (v.bannerY < 2)
          ++v.bannerY;
      } else {
        if (v.bannerY > -8)
          --v.bannerY;
      }
    }
    // FIXME duplicated code
    for (std::size_t i = 0; i < spectatorViewports.size(); ++i) {
      SpectatorViewport& v = *spectatorViewports[i];

      bool down = false;

      if (wormByIdx(0)->killedTimer > 16 || wormByIdx(1)->killedTimer > 16)
        down = true;

      if (down) {
        if (v.bannerY < 2)
          ++v.bannerY;
      } else {
        if (v.bannerY > -8)
          --v.bannerY;
      }
    }
  }

  auto sr = sobjects.all();
  for (SObject* i; (i = sr.next());) {
    i->process(*this);
  }

  auto wr = wobjects.all();
  for (WObject* i; (i = wr.next());) {
    i->process(*this);
  }

  auto nr = nobjects.all();
  for (NObject* i; (i = nr.next());) {
    i->process(*this);
  }

  for (BObjectList::iterator i = bobjects.begin(); i != bobjects.end();) {
    if (i->process(*this))
      ++i;
    else
      bobjects.free(i);
  }

  // NOTE: This was originally the beginning of the processing, but has been
  // rotated down to separate out the drawing
  ++cycles;

  if (!common->H[HBonusDisable] && settings->maxBonuses > 0 &&
      std::uniform_int_distribution<int>(
          0, common->C[CBonusDropChance] - 1)(rand) == 0) {
    createBonus();
  }

  for (std::size_t i = 0; i < worms.size(); ++i) {
    worms[i]->process(*this);
  }

  for (std::size_t i = 0; i < worms.size(); ++i) {
    worms[i]->ninjarope.process(*worms[i], *this);
  }

  switch (settings->gameMode) {
    case Settings::GameMode::GameOfTag: {
      bool someInvisible = false;
      for (std::size_t i = 0; i < worms.size(); ++i) {
        if (!worms[i]->visible) {
          someInvisible = true;
          break;
        }
      }

      Worm* lastKilledBy = wormByIdx(lastKilledIdx);

      if (!someInvisible && lastKilledBy && (cycles % 70) == 0 &&
          lastKilledBy->timer < settings->timeToLose) {
        ++lastKilledBy->timer;
      }
    } break;

    case Settings::GameMode::Holdazone: {
      int contenderIdx = -1;
      int contenders = 0;

      for (Worm* w : worms) {
        int x = ftoi(w->pos.x), y = ftoi(w->pos.y);

        if (w->visible && holdazone.rect.inside(x, y)) {
          contenderIdx = w->index;
          ++contenders;
        }
      }

      if (contenders == 0)
        contenderIdx = holdazone.holderIdx;

      if (contenders <= 1) {
        if (contenderIdx < 0 || (holdazone.contenderIdx != contenderIdx &&
                                 holdazone.contenderFrames != 0)) {
          if (holdazone.contenderFrames == 0 ||
              --holdazone.contenderFrames == 0) {
            holdazone.contenderIdx = contenderIdx;
            holdazone.holderIdx = -1;
          }
        } else {
          holdazone.contenderIdx = contenderIdx;

          if (holdazone.contenderFrames < settings->zoneCaptureTime &&
              ++holdazone.contenderFrames >= settings->zoneCaptureTime &&
              holdazone.holderIdx != holdazone.contenderIdx) {
            // New holder

            int newTimeout = holdazone.timeoutLeft;
            if (holdazone.contenderIdx >= 0)
              newTimeout += settings->zoneTimeout * 70 / 4;
            else
              newTimeout += settings->zoneTimeout * 70 / 8;

            holdazone.timeoutLeft =
                std::min(newTimeout, settings->zoneTimeout * 70);

            holdazone.holderIdx = holdazone.contenderIdx;
          }
        }
      }

      bool dec = false;

      if (holdazone.holderIdx >= 0) {
        auto* holder = wormByIdx(holdazone.holderIdx);

        if ((cycles % 70) == 0)
          ++holder->timer;

        dec = true;
      } else {
        dec = (cycles % 4) == 0;
      }

      if (dec) {
        if (--holdazone.timeoutLeft <= 0) {
          spawnZone();
        }
      }
    } break;
  }

  processViewports();

  // Store old control states so we can see what changes (mainly for replays)
  for (std::size_t i = 0; i < worms.size(); ++i) {
    worms[i]->prevControlStates = worms[i]->controlStates;
  }

  statsRecorder->tick(*this);
}

void Game::focus(Renderer& renderer) {
  updateSettings(renderer);
}

void Game::updateSettings(Renderer& renderer) {
  renderer.origpal = level.origpal;  // Activate the Level palette

  for (std::size_t i = 0; i < worms.size(); ++i) {
    const Worm& worm = *worms[i];
    if (worm.index >= 0 && worm.index < 2)
      renderer.origpal.setWormColour(worm.index, *worm.settings);
  }
}

void Game::spawnZone() {
  gvl::ivec2 pos;

  while (holdazone.zoneWidth >= 5) {
    if (level.selectSpawn(
            rand, holdazone.zoneWidth, holdazone.zoneHeight - 8, pos)) {
      holdazone.rect.x1 = pos.x;
      holdazone.rect.y1 = pos.y;
      holdazone.rect.x2 = pos.x + holdazone.zoneWidth;
      holdazone.rect.y2 = pos.y + holdazone.zoneHeight;
      holdazone.timeoutLeft = settings->zoneTimeout * 70;
      holdazone.contenderIdx = -1;
      holdazone.contenderFrames = 0;
      holdazone.holderIdx = -1;
      break;
    }

    holdazone.zoneWidth /= 2;
    holdazone.zoneHeight /= 2;
  }
}

void Game::startGame() {
  soundPlayer->play(22);
  bobjects.resize(settings->bloodParticleMax);

  if (settings->gameMode == Settings::GameMode::Holdazone) {
    spawnZone();
  }
}

bool Game::isGameOver() {
  int timeToLose = settings->timeToLose;
  switch (settings->gameMode) {
    case Settings::GameMode::KillEmAll:
    // [[fallthrough]]; C++17
    case Settings::GameMode::ScalesOfJustice:
      return std::any_of(worms.begin(), worms.end(), [](const Worm* w) {
        return w->lives <= 0;
      });
      break;
    case Settings::GameMode::GameOfTag:
    // [[fallthrough]]; C++17
    case Settings::GameMode::Holdazone:
      return std::any_of(
          worms.begin(), worms.end(),
          [timeToLose](const Worm* w) { return w->timer >= timeToLose; });
      break;
    default:
      return false;
      break;
  }
}

void Game::doDamageDirect(Worm& w, int amount, int byIdx) {
  if (amount > 0) {
    w.health -= amount;
    if (w.health <= 0) {
      w.lastKilledByIdx = byIdx;
    }
  }
}

void Game::doHealingDirect(Worm& w, int amount) {
  w.health += amount;
  if (settings->gameMode == Settings::GameMode::ScalesOfJustice) {
    while (w.health > w.settings->health) {
      w.lives += 1;
      w.health -= w.settings->health;
    }
  } else {
    if (w.health > w.settings->health) {
      w.health = w.settings->health;
    }
  }
}

void Game::doDamage(Worm& w, int amount, int byIdx) {
  doDamageDirect(w, amount, byIdx);

  if (amount > 0) {
    if (settings->gameMode == Settings::GameMode::ScalesOfJustice) {
      if (byIdx < 0 || byIdx == w.index) {
        int parts = static_cast<int>(worms.size()) - 1;
        int left = amount;

        for (Worm* other : worms) {
          if (other != &w) {
            int k = left / parts;
            doHealingDirect(*other, k);
            parts -= 1;
            left -= k;
          }
        }
      } else {
        doHealingDirect(*worms[byIdx], amount);
      }
    }
  }
}

void Game::doHealing(Worm& w, int amount) {
  doHealingDirect(w, amount);

  if (settings->gameMode == Settings::GameMode::ScalesOfJustice) {
    int parts = static_cast<int>(worms.size()) - 1;
    int left = amount;

    for (Worm* other : worms) {
      if (other != &w) {
        int k = left / parts;
        doDamageDirect(*other, k, w.index);
        parts -= 1;
        left -= k;
      }
    }
  } else {
    if (w.health > w.settings->health)
      w.health = w.settings->health;
  }
}

bool checkRespawnPosition(
    Game& game,
    int x2,
    int y2,
    int oldX,
    int oldY,
    int x,
    int y) {
  const Common& common = *game.common;

  int deltaX = oldX;
  int deltaY = oldY - y;
  int enemyDX = x2 - x;
  int enemyDY = y2 - y;

  if ((std::abs(deltaX) <= LC(WormMinSpawnDistLast) &&
       std::abs(deltaY) <= LC(WormMinSpawnDistLast)) ||
      (std::abs(enemyDX) <= LC(WormMinSpawnDistEnemy) &&
       std::abs(enemyDY) <= LC(WormMinSpawnDistEnemy)))
    return false;

  int maxX = x + 3;
  int maxY = y + 4;
  int minX = x - 3;
  int minY = y - 4;

  if (maxX >= game.level.width)
    maxX = game.level.width - 1;
  if (maxY >= game.level.height)
    maxY = game.level.height - 1;
  if (minX < 0)
    minX = 0;
  if (minY < 0)
    minY = 0;

  for (int i = minX; i != maxX; ++i)
    for (int j = minY; j != maxY; ++j) {
      if (game.level.mat(i, j)
              .rock())  // TODO: The special rock respawn bug is here, consider
                        // an option to turn it off
        return false;
    }

  return true;
}

void Game::postClone(const Game& original, bool complete) {
  if (!complete) {
    statsRecorder.reset(new StatsRecorder);
    soundPlayer.reset(new NullSoundPlayer);
    viewports.clear();
  } else {
    statsRecorder.reset(new NormalStatsRecorder(
        static_cast<NormalStatsRecorder&>(*statsRecorder)));

    for (auto& vp : viewports) {
      vp = new Viewport(*vp);
    }
  }

  for (auto& w : worms) {
    w = new Worm(*w);
  }
}
