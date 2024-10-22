#pragma once

#include "menu.hpp"

struct Common;
struct ItemBehavior;

struct HiddenMenu : Menu {
  enum Option {
    RecordReplays,
    LoadPowerLevels,
    // ScalingFilter,
    DoubleRes,
    Fullscreen,
    AiFrames,
    AiMutations,
    PaletteSelect,
    Shadows,
    ScreenSync,
    SelectBotWeapons,
    AiTraces,
    AiParallels,
    AllowViewingSpawnPoint,
    SingleScreenReplay,
    SpectatorWindow,
  };

  HiddenMenu(int x, int y) : Menu(x, y), paletteColor(0) {}

  ItemBehavior* getItemBehavior(Common& common, MenuItem& item) override;

  void drawItemOverlay(
      Common& common,
      MenuItem& item,
      int x,
      int y,
      bool selected,
      bool disabled) override;

  int paletteColor;
};
