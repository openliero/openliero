#pragma once

#include "menu.hpp"

struct Common;
struct ItemBehavior;

struct MainMenu : Menu {
  enum Option {
    ResumeGame,
    NewGame,
    Settings,
    Player1Settings,
    Player2Settings,
    Advanced,
    Quit,
    Replays,
    Replay,
    TC
  };

  MainMenu(int x, int y) : Menu(x, y) {}

  virtual ItemBehavior* getItemBehavior(Common& common, MenuItem& item)
      override;
};
