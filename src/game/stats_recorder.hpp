#pragma once

#include <chrono>
#include "gfx/blit.hpp"
#include "worm.hpp"

struct Common;
struct Renderer;

struct StatsRecorder : gvl::shared {
  virtual void damagePotential(Worm* byWorm, WormWeapon* weapon, int hp);
  virtual void damageDealt(
      Worm* byWorm,
      WormWeapon* weapon,
      Worm* toWorm,
      int hp,
      bool hasHit);

  virtual void shot(Worm* byWorm, WormWeapon* weapon);
  virtual void hit(Worm* byWorm, WormWeapon* weapon, Worm* toWorm);

  virtual void afterSpawn(Worm* worm);
  virtual void afterDeath(Worm* worm);
  virtual void preTick(Game& game);
  virtual void tick(Game& game);
  virtual void finish(Game& game);

  virtual void aiProcessTime(Worm* worm, std::chrono::nanoseconds time);
};

struct WeaponStats {
  WeaponStats()
      : potentialHp(0),
        actualHp(0),
        potentialHits(0),
        actualHits(0),
        totalHp(0) {}

  void combine(WeaponStats const& other) {
    potentialHits += other.potentialHits;
    potentialHp += other.potentialHp;
    actualHits += other.actualHits;
    actualHp += other.actualHp;
    totalHp += other.totalHp;
  }

  int potentialHp, actualHp;
  int potentialHits, actualHits;
  int totalHp;
  int index;
};

struct WormFrameStats {
  WormFrameStats() : damage(0), totalHp(0) {}

  int damage;
  int totalHp;
};

struct WormStats {
  WormStats()
      : damage(0),
        damageDealt(0),
        selfDamage(0),
        damageHm(504 / 2, 350 / 2, 504, 350),
        presence(504 / 2, 350 / 2, 504, 350),
        weaponChangeGood(0),
        weaponChangeBad(0),
        spawnTime(-1),
        lives(0),
        timer(0),
        kills(0),
        aiProcessTime(0) {
    for (int i = 0; i < 40; ++i)
      weapons[i].index = i;
  }

  std::vector<std::pair<int, int> > lifeSpans;

  void lifeStats(int& min, int& max) {
    min = 0;
    max = 0;
    if (lifeSpans.empty())
      return;

    min = lifeSpans[0].second - lifeSpans[0].first;
    max = min;

    for (size_t i = 1; i < lifeSpans.size(); ++i) {
      int len = lifeSpans[i].second - lifeSpans[i].first;
      max = std::max(len, max);
      min = std::min(len, min);
    }
  }

  WeaponStats weapons[40];
  int damage, damageDealt, selfDamage;
  Heatmap damageHm, presence;
  int weaponChangeGood, weaponChangeBad;

  std::vector<WormFrameStats> wormFrameStats;

  int spawnTime;
  int index;

  int lives, timer, kills;
  std::chrono::nanoseconds aiProcessTime;
};

struct NormalStatsRecorder : StatsRecorder {
  NormalStatsRecorder()
      : frame(0),
        frameStart(std::chrono::steady_clock::now()),
        processTimeTotal(0),
        gameTime(0),
        presence(504 / 2, 350 / 2, 504, 350) {
    for (int i = 0; i < 2; ++i) {
      worms[i].index = i;
    }
  }

  int frame;
  WormStats worms[2];
  std::chrono::time_point<std::chrono::steady_clock> frameStart;
  int64_t processTimeTotal;
  int gameTime;

  Heatmap presence;

  void damagePotential(Worm* byWorm, WormWeapon* weapon, int hp) override;
  void damageDealt(
      Worm* byWorm,
      WormWeapon* weapon,
      Worm* toWorm,
      int hp,
      bool hasHit) override;

  void shot(Worm* byWorm, WormWeapon* weapon) override;
  void hit(Worm* byWorm, WormWeapon* weapon, Worm* toWorm) override;

  void afterSpawn(Worm* worm) override;
  void afterDeath(Worm* worm) override;
  void preTick(Game& game) override;
  void tick(Game& game) override;

  void finish(Game& game) override;
  void aiProcessTime(Worm* worm, std::chrono::nanoseconds time) override;
};
