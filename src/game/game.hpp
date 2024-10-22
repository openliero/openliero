#pragma once

#include <random>
#include <string>
#include <vector>
#include "bobject.hpp"
#include "bonus.hpp"
#include "common.hpp"
#include "constants.hpp"
#include "level.hpp"
#include "mixer/player.hpp"
#include "nobject.hpp"
#include "settings.hpp"
#include "sobject.hpp"
#include "stats_recorder.hpp"
#include "weapon.hpp"

struct SpectatorViewport;
struct Viewport;
struct Worm;
struct Renderer;

enum GameState {
  StateInitial,
  StateWeaponSelection,
  StateGame,
  StateGameEnded,
};

struct Holdazone {
  Holdazone()
      : holderIdx(-1),
        contenderIdx(-1),
        contenderFrames(0),
        timeoutLeft(0),
        zoneWidth(50),
        zoneHeight(34) {}

  gvl::rect rect;
  int holderIdx;

  int contenderIdx, contenderFrames;

  int timeoutLeft;

  int zoneWidth, zoneHeight;
};

struct Game {
  Game(
      std::shared_ptr<Common> common,
      std::shared_ptr<Settings> settings,
      std::shared_ptr<SoundPlayer> soundPlayer);
  ~Game();

  void onKey(uint32_t key, bool state);
  Worm* findControlForKey(uint32_t key, Worm::Control& control);
  void releaseControls();
  void processFrame();
  void focus(Renderer& renderer);
  void updateSettings(Renderer& renderer);

  void createBObject(fixedvec pos, fixedvec vel);
  void createBonus();

  void clearViewports();
  void addViewport(Viewport*);
  void addSpectatorViewport(SpectatorViewport*);
  void processViewports();
  void
  drawViewports(Renderer& renderer, GameState state, bool isReplay = false);
  void drawSpectatorViewports(
      Renderer& renderer,
      GameState state,
      bool isReplay = false);
  void clearWorms();
  void addWorm(Worm*);
  void resetWorms();
  void draw(
      Renderer& renderer,
      GameState state,
      bool useSpectatorViewports,
      bool isReplay = false);
  void startGame();
  bool isGameOver();
  void doDamageDirect(Worm& w, int amount, int byIdx);
  void doHealingDirect(Worm& w, int amount);
  void doDamage(Worm& w, int amount, int byIdx);
  void doHealing(Worm& w, int amount);
  void postClone(const Game& original, bool complete = false);

  void spawnZone();

  Material pixelMat(int x, int y) {
    return common->materials[level.pixel(x, y)];
  }

  Worm* wormByIdx(int idx) {
    if (idx < 0)
      return 0;
    return worms[idx];
  }

  std::shared_ptr<Common> common;
  std::shared_ptr<SoundPlayer> soundPlayer;
  std::shared_ptr<Settings> settings;
  std::shared_ptr<StatsRecorder> statsRecorder;

  Level level;

  int screenFlash;
  bool gotChanged;
  int lastKilledIdx;
  bool paused;
  int cycles;
  std::mt19937 rand;
  uint32_t rand_seed;

  Holdazone holdazone;

  std::vector<Viewport*> viewports;
  std::vector<SpectatorViewport*> spectatorViewports;
  std::vector<Worm*> worms;

  typedef ExactObjectList<Bonus, 99> BonusList;
  typedef ExactObjectList<WObject, 600> WObjectList;
  typedef ExactObjectList<SObject, 700> SObjectList;
  typedef ExactObjectList<NObject, 600> NObjectList;
  typedef FastObjectList<BObject> BObjectList;
  BonusList bonuses;
  WObjectList wobjects;
  SObjectList sobjects;
  NObjectList nobjects;
  BObjectList bobjects;

  bool quickSim;
};

bool checkRespawnPosition(
    Game& game,
    int x2,
    int y2,
    int oldX,
    int oldY,
    int x,
    int y);
