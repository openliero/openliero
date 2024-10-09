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
  ~LocalController();
  void onKey(int key, bool keyState);

  // Called when the controller loses focus. When not focused, it will not
  // receive key events among other things.
  void unfocus();
  // Called when the controller gets focus.
  void focus();
  bool process();
  void draw(Renderer& renderer, bool useSpectatorViewports);
  void changeState(GameState newState);
  void endRecord();
  void swapLevel(Level& newLevel);
  Level* currentLevel();
  Game* currentGame();
  bool running();

  Game game;
  std::unique_ptr<WeaponSelection> ws;
  GameState state;
  int fadeValue;
  bool goingToMenu;
  std::unique_ptr<ReplayWriter> replay;
};
