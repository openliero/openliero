#include "hiddenMenu.hpp"
#include "../filesystem.hpp"
#include "../gfx.hpp"
#include "../sfx.hpp"
#include "arrayEnumBehavior.hpp"

static std::string const botWeaponSel[3] = {"RANDOM", "PICK", "KEEP"};

ItemBehavior* HiddenMenu::getItemBehavior(Common& common, MenuItem& item) {
  switch (item.id) {
    case RecordReplays:
      return new BooleanSwitchBehavior(common, gfx.settings->recordReplays);
    case LoadPowerLevels:
      return new BooleanSwitchBehavior(
          common, gfx.settings->loadPowerlevelPalette);
    case DoubleRes:
      return new BooleanSwitchBehavior(
          common, gfx.doubleRes, [](bool v) { gfx.setDoubleRes(v); });
    case Fullscreen:
      return new BooleanSwitchBehavior(
          common, gfx.settings->fullscreen,
          [](bool v) { gfx.setFullscreen(v); });
    case AiFrames:
      return new IntegerBehavior(common, gfx.settings->aiFrames, 1, 70 * 5);
    case AiMutations:
      return new IntegerBehavior(common, gfx.settings->aiMutations, 1, 20);
    case PaletteSelect:
      return new IntegerBehavior(common, paletteColor, 0, 255);
    case Shadows:
      return new BooleanSwitchBehavior(common, gfx.settings->shadow);
    case ScreenSync:
      return new BooleanSwitchBehavior(common, gfx.settings->screenSync);
    case SelectBotWeapons:
      return new ArrayEnumBehavior(
          common, gfx.settings->selectBotWeapons, botWeaponSel);
    case AiTraces:
      return new BooleanSwitchBehavior(common, gfx.settings->aiTraces);
    case AiParallels:
      return new IntegerBehavior(common, gfx.settings->aiParallels, 1, 16);
    case AllowViewingSpawnPoint:
      return new BooleanSwitchBehavior(
          common, gfx.settings->allowViewingSpawnPoint);
    case SingleScreenReplay:
      return new BooleanSwitchBehavior(
          common, gfx.settings->singleScreenReplay);
    case SpectatorWindow:
      return new BooleanSwitchBehavior(
          common, gfx.settings->spectatorWindow, [](bool v) {
            gfx.settings->spectatorWindow = v;
            gfx.setVideoMode();
          });

    default:
      return Menu::getItemBehavior(common, item);
  }
}

void HiddenMenu::drawItemOverlay(
    Common& common,
    MenuItem& item,
    int x,
    int y,
    bool selected,
    bool disabled) {
  // Color settings
  if (item.id == PaletteSelect) {
    int w = 30;
    int offsX = 44;

    drawRoundedBox(
        gfx.playRenderer.bmp, x + offsX, y, selected ? 168 : 0, 7, w);
    fillRect(
        gfx.playRenderer.bmp, x + offsX + 1, y + 1, w + 1, 5, paletteColor);
  }
}
