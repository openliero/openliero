#pragma once

#include "commonController.hpp"

#include "../game.hpp"
#include "../gfx.hpp"
#include "../keys.hpp"

#include <ctime>
#include "../console.hpp"
#include "../mixer/mixer.hpp"
#include "../replay.hpp"
#include "../weapsel.hpp"
#include "../worm.hpp"

struct Game;

struct ReplayController : CommonController {
  ReplayController(std::shared_ptr<Common> common, gvl::source source);

  bool isReplay() override { return true; };
  void onKey(int key, bool keyState) override;
  // Called when the controller loses focus. When not focused, it will not
  // receive key events among other things.
  void unfocus() override;
  // Called when the controller gets focus.
  void focus() override;
  bool process() override;
  void draw(Renderer& renderer, bool useSpectatorViewports) override;
  void changeState(GameState newState);
  void swapLevel(Level& newLevel) override;
  Level* currentLevel() override;
  Game* currentGame() override;
  bool running() override;

  std::unique_ptr<Game> game;

  std::unique_ptr<Game> initialGame;
  gvl::octet_reader initialReader;

  GameState state;
  int fadeValue;
  bool goingToMenu;
  std::unique_ptr<ReplayReader> replay;
  std::shared_ptr<Common> common;
};
