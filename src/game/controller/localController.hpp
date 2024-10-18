#pragma once

#include "commonController.hpp"

#include "../game.hpp"

#include <ctime>
#include "../console.hpp"
#include "../replay.hpp"
#include "../weapsel.hpp"
#include "../worm.hpp"

struct WeaponSelection;
struct ReplayWriter;

struct LocalController : CommonController {
  LocalController(
      std::shared_ptr<Common> common,
      std::shared_ptr<Settings> settings);
  ~LocalController() override;
  void onKey(int key, bool keyState) override;

  // Called when the controller loses focus. When not focused, it will not
  // receive key events among other things.
  void unfocus() override;
  // Called when the controller gets focus.
  void focus() override;
  bool process() override;
  void draw(Renderer& renderer, bool useSpectatorViewports) override;
  void changeState(GameState newState);
  void endRecord();
  void swapLevel(Level& newLevel) override;
  Level* currentLevel() override;
  Game* currentGame() override;
  bool running() override;

  Game game;
  std::unique_ptr<WeaponSelection> ws;
  GameState state;
  int fadeValue;
  bool goingToMenu;
  std::unique_ptr<ReplayWriter> replay;
};
