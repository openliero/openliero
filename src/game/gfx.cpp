#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "filesystem.hpp"
#include "game.hpp"
#include "gfx.hpp"
#include "keys.hpp"
#include "metadata.hpp"
#include "reader.hpp"
#include "sfx.hpp"
#include "text.hpp"

#include <gvl/io2/fstream.hpp>

#include "controller/controller.hpp"
#include "controller/localController.hpp"
#include "controller/replayController.hpp"

#include "gfx/macros.hpp"

#include "menu/arrayEnumBehavior.hpp"
#include "menu/fileSelector.hpp"

Gfx gfx;

struct KeyBehavior : ItemBehavior {
  KeyBehavior(
      Common& common,
      uint32_t& key,
      uint32_t& keyEx,
      bool extended = false)
      : common(common), key(key), keyEx(keyEx), extended(extended) {}

  int onEnter(Menu& menu, MenuItem& item) override {
    sfx.play(common, 27);
    uint32_t k;
    bool isEx;
    do {
      k = gfx.waitForKeyEx();
      isEx = isExtendedKey(k);
    } while (!extended && isEx);

    if (k != DkEscape) {
      if (!isEx)
        key = k;
      keyEx = k;

      onUpdate(menu, item);
    }

    gfx.clearKeys();
    return -1;
  }

  void onUpdate(Menu& menu, MenuItem& item) override {
    item.value = gfx.getKeyName(extended ? keyEx : key);
    item.hasValue = true;
  }

  Common& common;
  uint32_t& key;
  uint32_t& keyEx;
  bool extended;
};

struct WormNameBehavior : ItemBehavior {
  WormNameBehavior(Common& common, WormSettings& ws) : common(common), ws(ws) {}

  int onEnter(Menu& menu, MenuItem& item) override {
    sfx.play(common, 27);

    ws.randomName = false;

    int x, y;
    if (!menu.itemPosition(item, x, y))
      return -1;

    x += menu.valueOffsetX + 2;

    gfx.inputString(ws.name, 20, x, y);

    sfx.play(common, 27);
    onUpdate(menu, item);
    return -1;
  }

  void onUpdate(Menu& menu, MenuItem& item) override {
    item.value = ws.name;
    item.hasValue = true;
  }

  Common& common;
  WormSettings& ws;
};

struct ProfileSaveBehavior : ItemBehavior {
  ProfileSaveBehavior(Common& common, WormSettings& ws, bool saveAs = false)
      : common(common), ws(ws), saveAs(saveAs) {}

  int onEnter(Menu& menu, MenuItem& item) override {
    sfx.play(common, 27);

    int x, y;
    if (!menu.itemPosition(item, x, y))
      return -1;

    x += menu.valueOffsetX + 2;

    if (saveAs) {
      std::string name;
      if (gfx.inputString(name, 30, x, y) && !name.empty()) {
        ws.saveProfile(gfx.getConfigNode() / "Profiles" / (name + ".lpf"));
      }

      sfx.play(common, 27);
    } else
      ws.saveProfile(ws.profileNode);

    menu.updateItems(common);
    return -1;
  }

  void onUpdate(Menu& menu, MenuItem& item) override {
    if (!saveAs) {
      item.visible = (bool)ws.profileNode;
    }
  }

  Common& common;
  WormSettings& ws;
  bool saveAs;
};

struct ProfileLoadedBehavior : ItemBehavior {
  ProfileLoadedBehavior(Common& common, WormSettings& ws)
      : common(common), ws(ws) {}

  void onUpdate(Menu& menu, MenuItem& item) override {
    if (ws.profileNode) {
      item.value = getBasename(getLeaf(ws.profileNode.fullPath()));
      item.visible = true;
    } else {
      item.value.clear();
      item.visible = false;
    }

    item.hasValue = true;
  }

  Common& common;
  WormSettings& ws;
};

#define MIN3(a, b, c) \
  ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))

int levenshtein(char const* s1, char const* s2) {
  std::size_t x, y, s1len, s2len;
  s1len = strlen(s1);
  s2len = strlen(s2);
  std::size_t w = s1len + 1;
  std::vector<unsigned> matrix(w * (s2len + 1));
  matrix[0] = 0;
  for (x = 1; x <= s2len; x++)
    matrix[x * w] = matrix[(x - 1) * w] + 1;
  for (y = 1; y <= s1len; y++)
    matrix[y] = matrix[y - 1] + 1;
  for (x = 1; x <= s2len; x++)
    for (y = 1; y <= s1len; y++) {
      int c = std::tolower(s1[y - 1]) == std::tolower(s2[x - 1]) ? 0 : 1;
      matrix[x * w + y] = MIN3(
          matrix[(x - 1) * w + y] + 1, matrix[x * w + y - 1] + 1,
          matrix[(x - 1) * w + y - 1] + c);
    }

  return (matrix[s2len * w + s1len]);
}

struct WeaponEnumBehavior : EnumBehavior {
  WeaponEnumBehavior(Common& common, uint32_t& v)
      : EnumBehavior(
            common,
            v,
            1,
            static_cast<uint32_t>(common.weapons.size()),
            false) {}

  void onUpdate(Menu& menu, MenuItem& item) override {
    item.value = common.weapons[common.weapOrder[v - 1]].name;
    item.hasValue = true;
  }

  int onEnter(Menu& menu, MenuItem& item) override {
    sfx.play(common, 27);

    int x, y;
    if (!menu.itemPosition(item, x, y))
      return -1;

    x += menu.valueOffsetX + 2;

    std::string search;
    if (gfx.inputString(search, 10, x, y)) {
      uint32_t minimumi;
      double minimum = std::numeric_limits<double>::max();
      for (uint32_t i = min; i <= max; ++i) {
        const std::string& name = common.weapons[common.weapOrder[i - 1]].name;

        double dist = levenshtein(name.c_str(), search.c_str()) /
                      static_cast<double>(name.length());
        if (dist < minimum) {
          minimumi = i;
          minimum = dist;
        }
      }

      v = minimumi;
      menu.updateItems(common);
    }

    return -1;
  }
};

Gfx::Gfx()
    : mainMenu(53, 20),
      settingsMenu(178, 20),
      playerMenu(178, 20),
      hiddenMenu(178, 20),
      curMenu(0),
      sdlDrawSurface(0),
      running(true),
      doubleRes(true),
      menuCycles(0),
      windowW(320 * 2),
      windowH(200 * 2),
      prevMag(0),
      keyBufPtr(keyBuf) {
  clearKeys();
  primaryRenderer = &playRenderer;
}

void Gfx::init() {
  SDL_ShowCursor(SDL_DISABLE);
  lastFrame = SDL_GetTicks64();

  playRenderer.init(320, 200);
  singleScreenRenderer.init(640, 400);
  // Joystick init
  SDL_GameControllerEventState(SDL_ENABLE);
  int numJoysticks = SDL_NumJoysticks();
  joysticks.resize(numJoysticks);
  for (int i = 0; i < numJoysticks; ++i) {
    joysticks[i].sdlGameController = SDL_GameControllerOpen(i);
    joysticks[i].clearState();
  }
}

void Gfx::setVideoMode() {
  int flags;

  if (sdlSpectatorRenderer) {
    SDL_DestroyRenderer(sdlSpectatorRenderer);
    sdlSpectatorRenderer = NULL;
  }
  if (settings->spectatorWindow) {
    flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (spectatorFullscreen) {
      flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    if (!sdlSpectatorWindow) {
      std::string spectatorWindowTitle =
          std::string("Liero Spectator Window - ") + build_version();
      sdlSpectatorWindow = SDL_CreateWindow(
          spectatorWindowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED,
          SDL_WINDOWPOS_UNDEFINED, windowW, windowH, flags);
    } else {
      if (spectatorFullscreen) {
        SDL_SetWindowFullscreen(
            sdlSpectatorWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
      } else {
        SDL_SetWindowFullscreen(sdlSpectatorWindow, 0);
      }
    }
    sdlSpectatorRenderer = SDL_CreateRenderer(
        sdlSpectatorWindow, -1, 0 /*SDL_RENDERER_PRESENTVSYNC*/);
    onWindowResize(SDL_GetWindowID(sdlSpectatorWindow));
  } else {
    if (sdlSpectatorTexture) {
      SDL_DestroyTexture(sdlSpectatorTexture);
      sdlSpectatorTexture = NULL;
    }
    if (sdlSpectatorDrawSurface) {
      SDL_FreeSurface(sdlSpectatorDrawSurface);
      sdlSpectatorDrawSurface = NULL;
    }
    if (sdlSpectatorWindow) {
      SDL_DestroyWindow(sdlSpectatorWindow);
      sdlSpectatorWindow = NULL;
    }
  }

  flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

  if (settings->fullscreen) {
    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  }

  if (!sdlWindow) {
    int x = SDL_WINDOWPOS_UNDEFINED;
    int y = SDL_WINDOWPOS_UNDEFINED;
    if (sdlSpectatorWindow) {
      SDL_GetWindowPosition(sdlSpectatorWindow, &x, &y);
    }
    std::string windowTitle = std::string("Liero ") + build_version();
    sdlWindow = SDL_CreateWindow(
        windowTitle.c_str(), x + 100, y + 50, windowW, windowH, flags);

    // The Mac app will automatically use the .icns icon file located in the
    // .app bundle, so don't override that here.
#ifndef __APPLE__
    std::string s = (getConfigNode() / "Resources" / "icon.png").fullPath();
    SDL_Surface* icon = IMG_Load(s.c_str());
    if (icon) {
      SDL_SetWindowIcon(sdlWindow, icon);
      SDL_FreeSurface(icon);
    }
#endif
  } else {
    if (settings->fullscreen) {
      SDL_SetWindowFullscreen(sdlWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
      SDL_SetWindowFullscreen(sdlWindow, 0);
    }
  }
  if (sdlRenderer) {
    SDL_DestroyRenderer(sdlRenderer);
    sdlRenderer = NULL;
  }
  // vertical sync is always disabled. Frame limiting is done manually below,
  // to keep the correct speed
  sdlRenderer =
      SDL_CreateRenderer(sdlWindow, -1, 0 /*SDL_RENDERER_PRESENTVSYNC*/);
  onWindowResize(SDL_GetWindowID(sdlWindow));

  // Set the spectator window's icon after the main window has been initialized.
  // On Windows, this makes sure the icon in the stacked taskbar is the main
  // icon. On MacOS this is commented out, because it only allows one icon and
  // the spectator icon will override the main icon
#ifndef __APPLE__
  if (sdlSpectatorWindow) {
    std::string s =
        (getConfigNode() / "Resources" / "spectator_icon.png").fullPath();
    SDL_Surface* spectator_icon = IMG_Load(s.c_str());
    if (spectator_icon) {
      SDL_SetWindowIcon(sdlSpectatorWindow, spectator_icon);
      SDL_FreeSurface(spectator_icon);
    }
  }
#endif
}

void Gfx::onWindowResize(Uint32 windowID) {
  if (windowID == SDL_GetWindowID(sdlWindow)) {
    if (sdlTexture) {
      SDL_DestroyTexture(sdlTexture);
      sdlTexture = NULL;
    }
    sdlTexture = SDL_CreateTexture(
        sdlRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        doubleRes ? 640 : 320, doubleRes ? 400 : 200);

    if (sdlDrawSurface) {
      SDL_FreeSurface(sdlDrawSurface);
      sdlDrawSurface = NULL;
    }
    sdlDrawSurface = SDL_CreateRGBSurface(
        0, doubleRes ? 640 : 320, doubleRes ? 400 : 200, 32, 0, 0, 0, 0);
    // linear for that old-school chunky look, but consider adding a user
    // option for this
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_RenderSetLogicalSize(
        sdlRenderer, doubleRes ? 640 : 320, doubleRes ? 400 : 200);
  } else {
    if (sdlSpectatorTexture) {
      SDL_DestroyTexture(sdlSpectatorTexture);
      sdlSpectatorTexture = NULL;
    }
    if (sdlSpectatorDrawSurface) {
      SDL_FreeSurface(sdlSpectatorDrawSurface);
      sdlSpectatorDrawSurface = NULL;
    }

    if (settings->spectatorWindow) {
      sdlSpectatorTexture = SDL_CreateTexture(
          sdlSpectatorRenderer, SDL_PIXELFORMAT_ARGB8888,
          SDL_TEXTUREACCESS_STREAMING, 640, 400);
      sdlSpectatorDrawSurface =
          SDL_CreateRGBSurface(0, 640, 400, 32, 0, 0, 0, 0);
      SDL_RenderSetLogicalSize(sdlSpectatorRenderer, 640, 400);
    }
  }
}

void Gfx::loadMenus() {
  hiddenMenu.addItem(
      MenuItem(48, 7, "FULLSCREEN (F11)", HiddenMenu::Option::Fullscreen));
  hiddenMenu.addItem(
      MenuItem(48, 7, "DOUBLE SIZE", HiddenMenu::Option::DoubleRes));
  hiddenMenu.addItem(MenuItem(
      48, 7, "POWERLEVEL PALETTES", HiddenMenu::Option::LoadPowerLevels));
  hiddenMenu.addItem(MenuItem(48, 7, "SHADOWS", HiddenMenu::Option::Shadows));
  hiddenMenu.addItem(MenuItem(
      48, 7, "AUTO-RECORD REPLAYS", HiddenMenu::Option::RecordReplays));
  hiddenMenu.addItem(
      MenuItem(48, 7, "AI FRAMES", HiddenMenu::Option::AiFrames));
  hiddenMenu.addItem(
      MenuItem(48, 7, "AI MUTATIONS", HiddenMenu::Option::AiMutations));
  hiddenMenu.addItem(
      MenuItem(48, 7, "AI PARALLELS", HiddenMenu::Option::AiParallels));
  hiddenMenu.addItem(
      MenuItem(48, 7, "AI TRACES", HiddenMenu::Option::AiTraces));
  hiddenMenu.addItem(
      MenuItem(48, 7, "PALETTE", HiddenMenu::Option::PaletteSelect));
  hiddenMenu.addItem(
      MenuItem(48, 7, "BOT WEAPONS", HiddenMenu::Option::SelectBotWeapons));
  hiddenMenu.addItem(MenuItem(
      48, 7, "SEE SPAWN POINT", HiddenMenu::Option::AllowViewingSpawnPoint));
  hiddenMenu.addItem(MenuItem(
      48, 7, "SINGLE SCREEN REPLAY", HiddenMenu::Option::SingleScreenReplay));
  hiddenMenu.addItem(
      MenuItem(48, 7, "SPECTATOR WINDOW", HiddenMenu::Option::SpectatorWindow));

  playerMenu.addItem(
      MenuItem(3, 7, "PROFILE LOADED", PlayerMenu::Option::LoadedProfile));
  playerMenu.addItem(
      MenuItem(3, 7, "SAVE PROFILE", PlayerMenu::Option::SaveProfile));
  playerMenu.addItem(
      MenuItem(3, 7, "SAVE PROFILE AS...", PlayerMenu::Option::SaveProfileAs));
  playerMenu.addItem(
      MenuItem(3, 7, "LOAD PROFILE", PlayerMenu::Option::LoadProfile));
  playerMenu.addItem(MenuItem(48, 7, "NAME", PlayerMenu::Option::Name));
  playerMenu.addItem(MenuItem(48, 7, "HEALTH", PlayerMenu::Option::Health));
  playerMenu.addItem(MenuItem(48, 7, "Red", PlayerMenu::Option::Red));
  playerMenu.addItem(MenuItem(48, 7, "Green", PlayerMenu::Option::Green));
  playerMenu.addItem(MenuItem(48, 7, "Blue", PlayerMenu::Option::Blue));
  playerMenu.addItem(MenuItem(48, 7, "AIM UP", PlayerMenu::Option::Up));
  playerMenu.addItem(MenuItem(48, 7, "AIM DOWN", PlayerMenu::Option::Down));
  playerMenu.addItem(MenuItem(48, 7, "MOVE LEFT", PlayerMenu::Option::Left));
  playerMenu.addItem(MenuItem(48, 7, "MOVE RIGHT", PlayerMenu::Option::Right));
  playerMenu.addItem(MenuItem(48, 7, "FIRE", PlayerMenu::Option::Fire));
  playerMenu.addItem(MenuItem(48, 7, "CHANGE", PlayerMenu::Option::Change));
  playerMenu.addItem(MenuItem(48, 7, "JUMP", PlayerMenu::Option::Jump));
  playerMenu.addItem(MenuItem(48, 7, "DIG", PlayerMenu::Option::Dig));

  for (int i = 0; i < 5; ++i)
    playerMenu.addItem(MenuItem(
        48, 7, std::string("WEAPON ") + (char)(i + '1'),
        PlayerMenu::Option::Weap0 + i));

  playerMenu.addItem(
      MenuItem(48, 7, "CONTROLLER", PlayerMenu::Option::Controller));

  settingsMenu.addItem(
      MenuItem(48, 7, "GAME MODE", SettingsMenu::Option::GameMode));
  settingsMenu.addItem(
      MenuItem(48, 7, "TIME TO LOSE", SettingsMenu::Option::TimeToLose));
  settingsMenu.addItem(
      MenuItem(48, 7, "TIME TO WIN", SettingsMenu::Option::TimeToWin));
  settingsMenu.addItem(
      MenuItem(48, 7, "ZONE TIMEOUT", SettingsMenu::Option::ZoneTimeout));
  settingsMenu.addItem(
      MenuItem(48, 7, "FLAGS TO WIN", SettingsMenu::Option::FlagsToWin));
  settingsMenu.addItem(MenuItem(48, 7, "LIVES", SettingsMenu::Option::Lives));
  settingsMenu.addItem(MenuItem(48, 7, "LEVEL", SettingsMenu::Option::Level));
  settingsMenu.addItem(
      MenuItem(48, 7, "LOADING TIMES", SettingsMenu::Option::LoadingTimes));
  settingsMenu.addItem(
      MenuItem(48, 7, "WEAPON OPTIONS", SettingsMenu::Option::WeaponOptions));
  settingsMenu.addItem(
      MenuItem(48, 7, "MAX BONUSES", SettingsMenu::Option::MaxBonuses));
  settingsMenu.addItem(MenuItem(
      48, 7, "NAMES ON BONUSES", SettingsMenu::Option::NamesOnBonuses));
  settingsMenu.addItem(MenuItem(48, 7, "MAP", SettingsMenu::Option::Map));
  settingsMenu.addItem(
      MenuItem(48, 7, "AMOUNT OF BLOOD", SettingsMenu::Option::AmountOfBlood));
  settingsMenu.addItem(
      MenuItem(48, 7, "LOAD+CHANGE", SettingsMenu::Option::LoadChange));
  settingsMenu.addItem(MenuItem(
      48, 7, "REGENERATE LEVEL", SettingsMenu::Option::RegenerateLevel));
  settingsMenu.addItem(
      MenuItem(48, 7, "SAVE SETUP AS...", SettingsMenu::Option::SaveOptions));
  settingsMenu.addItem(
      MenuItem(48, 7, "LOAD SETUP", SettingsMenu::Option::LoadOptions));

  mainMenu.addItem(MenuItem(
      10, 10, "", MainMenu::Option::ResumeGame));  // string set in menuLoop
  mainMenu.addItem(MenuItem(
      10, 10, "", MainMenu::Option::NewGame));  // string set in menuLoop
  mainMenu.addItem(
      MenuItem(48, 48, "OPTIONS (F2)", MainMenu::Option::Advanced));
  mainMenu.addItem(MenuItem(48, 48, "REPLAYS (F3)", MainMenu::Option::Replays));
  mainMenu.addItem(MenuItem(48, 48, "TC", MainMenu::Option::TC));
  mainMenu.addItem(MenuItem(6, 6, "QUIT TO OS", MainMenu::Option::Quit));
  mainMenu.addItem(MenuItem::space());
  mainMenu.addItem(
      MenuItem(48, 48, "LEFT PLAYER (F5)", MainMenu::Option::Player1Settings));
  mainMenu.addItem(
      MenuItem(48, 48, "RIGHT PLAYER (F6)", MainMenu::Option::Player2Settings));
  mainMenu.addItem(
      MenuItem(48, 48, "MATCH SETUP (F7)", MainMenu::Option::Settings));

  settingsMenu.valueOffsetX = 100;
  playerMenu.valueOffsetX = 95;
  hiddenMenu.valueOffsetX = 120;
}

void Gfx::setSpectatorFullscreen(bool newFullscreen) {
  if (newFullscreen == spectatorFullscreen)
    return;
  spectatorFullscreen = newFullscreen;

  if (!spectatorFullscreen) {
    if (doubleRes) {
      windowW = 640;
      windowH = 400;
    } else {
      windowW = 320;
      windowH = 200;
    }
  }
  setVideoMode();
}

void Gfx::setFullscreen(bool newFullscreen) {
  if (newFullscreen == settings->fullscreen)
    return;
  settings->fullscreen = newFullscreen;

  // fullscreen will automatically set window size
  if (!settings->fullscreen) {
    if (doubleRes) {
      windowW = 640;
      windowH = 400;
    } else {
      windowW = 320;
      windowH = 200;
    }
  }
  setVideoMode();
  hiddenMenu.updateItems(*common);
}

void Gfx::setDoubleRes(bool newDoubleRes) {
  if (newDoubleRes == doubleRes)
    return;
  doubleRes = newDoubleRes;

  if (!newDoubleRes) {
    windowW = 320;
    windowH = 200;
  } else {
    windowW = 640;
    windowH = 400;
  }
  setVideoMode();
  hiddenMenu.updateItems(*common);
}

void Gfx::processEvent(SDL_Event& ev, Controller* controller) {
  switch (ev.type) {
    case SDL_KEYDOWN: {
      SDL_Scancode s = ev.key.keysym.scancode;

      if (keyBufPtr < keyBuf + 32)
        *keyBufPtr++ = ev.key.keysym;

      Uint32 dosScan = SDLToDOSKey(ev.key.keysym.scancode);
      if (dosScan) {
        dosKeys[dosScan] = true;
        if (controller)
          controller->onKey(dosScan, true);
      }

      if (s == SDL_SCANCODE_F11) {
        if (SDL_GetWindowFromID(ev.key.windowID) == sdlWindow) {
          setFullscreen(!settings->fullscreen);
        } else {
          setSpectatorFullscreen(!spectatorFullscreen);
        }
      }
    } break;

    case SDL_KEYUP: {
      SDL_Scancode s = ev.key.keysym.scancode;

      Uint32 dosScan = SDLToDOSKey(s);
      if (dosScan) {
        dosKeys[dosScan] = false;
        if (controller)
          controller->onKey(dosScan, false);
      }
    } break;

    case SDL_WINDOWEVENT: {
      switch (ev.window.event) {
        case SDL_WINDOWEVENT_RESIZED: {
          onWindowResize(ev.window.windowID);
        } break;

        default:
          break;
      }
    } break;

    case SDL_QUIT: {
      running = false;
    } break;

    case SDL_JOYAXISMOTION: {
      Joystick& js = joysticks[ev.jaxis.which];
      int jbtnBase = 4 + 2 * ev.jaxis.axis;

      bool newBtnStates[2];
      newBtnStates[0] = (ev.jaxis.value > JoyAxisThreshold);
      newBtnStates[1] = (ev.jaxis.value < -JoyAxisThreshold);

      for (int i = 0; i < 2; ++i) {
        int jbtn = jbtnBase + i;
        bool newState = newBtnStates[i];

        if (newState != js.btnState[jbtn]) {
          js.btnState[jbtn] = newState;
          if (controller)
            controller->onKey(joyButtonToExKey(ev.jaxis.which, jbtn), newState);
        }
      }
    } break;

    case SDL_JOYHATMOTION: {
      Joystick& js = joysticks[ev.jhat.which];

      bool newBtnStates[4];
      newBtnStates[0] = (ev.jhat.value & SDL_HAT_UP) != 0;
      newBtnStates[1] = (ev.jhat.value & SDL_HAT_DOWN) != 0;
      newBtnStates[2] = (ev.jhat.value & SDL_HAT_LEFT) != 0;
      newBtnStates[3] = (ev.jhat.value & SDL_HAT_RIGHT) != 0;

      for (int jbtn = 0; jbtn < 4; ++jbtn) {
        bool newState = newBtnStates[jbtn];
        if (newState != js.btnState[jbtn]) {
          js.btnState[jbtn] = newState;
          if (controller)
            controller->onKey(joyButtonToExKey(ev.jhat.which, jbtn), newState);
        }
      }
    } break;

    case SDL_JOYBUTTONDOWN:
      // [[fallthrough]]; C++17
    case SDL_JOYBUTTONUP: {
      Joystick& js = joysticks[ev.jbutton.which];
      int jbtn = 16 + ev.jbutton.button;
      js.btnState[jbtn] = (ev.jbutton.state == SDL_PRESSED);
      if (controller)
        controller->onKey(
            joyButtonToExKey(ev.jbutton.which, jbtn), js.btnState[jbtn]);
    } break;

    default:
      break;
  }
}

void Gfx::process(Controller* controller) {
  SDL_Event ev;
  keyBufPtr = keyBuf;
  while (SDL_PollEvent(&ev)) {
    processEvent(ev, controller);
  }
}

SDL_Keysym Gfx::waitForKey() {
  SDL_Event ev;
  while (SDL_WaitEvent(&ev)) {
    processEvent(ev);
    if (ev.type == SDL_KEYDOWN) {
      return ev.key.keysym;
    }
  }

  // Dummy
  return SDL_Keysym();
}

uint32_t Gfx::waitForKeyEx() {
  SDL_Event ev;
  while (SDL_WaitEvent(&ev)) {
    processEvent(ev);
    switch (ev.type) {
      case SDL_KEYDOWN:
        return SDLToDOSKey(ev.key.keysym);

      case SDL_JOYAXISMOTION:
        if (ev.jaxis.value > JoyAxisThreshold)
          return joyButtonToExKey(ev.jaxis.which, 4 + 2 * ev.jaxis.axis);
        else if (ev.jaxis.value < -JoyAxisThreshold)
          return joyButtonToExKey(ev.jaxis.which, 5 + 2 * ev.jaxis.axis);

        break;
      case SDL_JOYHATMOTION:
        if (ev.jhat.value & SDL_HAT_UP)
          return joyButtonToExKey(ev.jhat.which, 0);
        else if (ev.jhat.value & SDL_HAT_DOWN)
          return joyButtonToExKey(ev.jhat.which, 1);
        else if (ev.jhat.value & SDL_HAT_LEFT)
          return joyButtonToExKey(ev.jhat.which, 2);
        else if (ev.jhat.value & SDL_HAT_RIGHT)
          return joyButtonToExKey(ev.jhat.which, 3);

        break;
      case SDL_JOYBUTTONDOWN:
        return joyButtonToExKey(ev.jbutton.which, 16 + ev.jbutton.button);
      default:
        break;
    }
  }

  // Dummy
  return 0;
}

std::string Gfx::getKeyName(uint32_t key) {
  if (key < MaxDOSKey) {
    return common->texts.keyNames[key];
  } else if (key >= JoyKeysStart) {
    key -= JoyKeysStart;
    int joyNum = key / MaxJoyButtons;
    key -= joyNum * MaxJoyButtons;
    return "J" + toString(joyNum) + "_" + toString(key);
  }

  return "";
}

void Gfx::clearKeys() {
  std::memset(dosKeys, 0, sizeof(dosKeys));
}

void Gfx::preparePalette(
    SDL_PixelFormat* format,
    Color realPal[256],
    uint32_t (&pal32)[256]) {
  for (int i = 0; i < 256; ++i) {
    pal32[i] = SDL_MapRGB(format, realPal[i].r, realPal[i].g, realPal[i].b);
  }
}

void Gfx::menuFlip(bool quitting) {
  if (playRenderer.fadeValue < 32 && !quitting)
    ++playRenderer.fadeValue;
  if (singleScreenRenderer.fadeValue < 32 && !quitting)
    ++singleScreenRenderer.fadeValue;

  ++menuCycles;
  playRenderer.pal = playRenderer.origpal;
  playRenderer.pal.rotateFrom(playRenderer.origpal, 168, 174, menuCycles);
  playRenderer.pal.setWormColours(*settings);
  playRenderer.pal.fade(playRenderer.fadeValue);
  singleScreenRenderer.pal = singleScreenRenderer.origpal;
  singleScreenRenderer.pal.rotateFrom(
      singleScreenRenderer.origpal, 168, 174, menuCycles);
  singleScreenRenderer.pal.setWormColours(*settings);
  singleScreenRenderer.pal.fade(singleScreenRenderer.fadeValue);
  flip();
}

void Gfx::draw(
    SDL_Surface& surface,
    SDL_Texture& texture,
    SDL_Renderer& sdlRenderer,
    Renderer& renderer) {
  gvl::rect updateRect;
  Color realPal[256];
  renderer.pal.activate(realPal);
  int offsetX, offsetY;
  int mag = fitScreen(
      surface.w, surface.h, renderer.renderResX, renderer.renderResY, offsetX,
      offsetY);

  gvl::rect newRect(
      offsetX, offsetY, renderer.renderResX * mag, renderer.renderResY * mag);

  if (mag != prevMag) {
    // Clear background if magnification is decreased to
    // avoid leftovers.
    SDL_FillRect(&surface, 0, 0);
    updateRect = lastUpdateRect | newRect;
  } else
    updateRect = newRect;
  prevMag = mag;

  std::size_t destPitch = surface.pitch;
  std::size_t srcPitch = renderer.bmp.pitch;

  PalIdx* dest = reinterpret_cast<PalIdx*>(surface.pixels) +
                 offsetY * destPitch + offsetX * surface.format->BytesPerPixel;
  PalIdx* src = renderer.bmp.pixels;

  uint32_t pal32[256];
  preparePalette(surface.format, realPal, pal32);
  scaleDraw(
      src, renderer.renderResX, renderer.renderResY, srcPitch, dest, destPitch,
      mag, pal32);

  SDL_UpdateTexture(&texture, NULL, surface.pixels, surface.w * 4);
  SDL_RenderClear(&sdlRenderer);
  SDL_RenderCopy(&sdlRenderer, &texture, NULL, NULL);
  SDL_RenderPresent(&sdlRenderer);

  lastUpdateRect = updateRect;
}

void Gfx::flip() {
  // draw into the play window. This uses either the normal split screen
  // renderer or the single screen renderer if this is a replay and single
  // screen replay is turned on
  draw(*sdlDrawSurface, *sdlTexture, *sdlRenderer, *primaryRenderer);
  if (settings->spectatorWindow) {
    draw(
        *sdlSpectatorDrawSurface, *sdlSpectatorTexture, *sdlSpectatorRenderer,
        singleScreenRenderer);
  }

  static unsigned int const delay = 14u;

  auto wantedTime = lastFrame + delay;

  while (true) {
    auto now = SDL_GetTicks64();
    if (now >= wantedTime)
      break;

    SDL_Delay(wantedTime - now);
  }

  lastFrame = wantedTime;
}

void playChangeSound(const Common& common, int change) {
  if (change > 0) {
    sfx.play(common, 25);
  } else {
    sfx.play(common, 26);
  }
}

void resetLeftRight() {
  gfx.releaseSDLKey(SDL_SCANCODE_LEFT);
  gfx.releaseSDLKey(SDL_SCANCODE_RIGHT);
}

template <typename T>
void changeVariable(T& var, T change, T min, T max, T scale) {
  if (change < 0 && var > min) {
    var += change * scale;
  }
  if (change > 0 && var < max) {
    var += change * scale;
  }
}

struct ProfileLoadBehavior : ItemBehavior {
  ProfileLoadBehavior(Common& common, WormSettings& ws)
      : common(common), ws(ws) {}

  int onEnter(Menu& menu, MenuItem& item) override {
    sfx.play(common, 27);
    gfx.selectProfile(ws);
    sfx.play(common, 27);
    menu.updateItems(common);
    return -1;
  }

  Common& common;
  WormSettings& ws;
};

struct PlayerSettingsBehavior : ItemBehavior {
  PlayerSettingsBehavior(Common& common, int player)
      : common(common), player(player) {}

  int onEnter(Menu& menu, MenuItem& item) override {
    sfx.play(common, 27);
    gfx.playerSettings(player);
    return -1;
  }

  Common& common;
  int player;
};

struct LevelSelectBehavior : ItemBehavior {
  LevelSelectBehavior(Common& common) : common(common) {}

  int onEnter(Menu& menu, MenuItem& item) override {
    sfx.play(common, 27);
    gfx.selectLevel();
    sfx.play(common, 27);
    onUpdate(menu, item);
    return -1;
  }

  void onUpdate(Menu& menu, MenuItem& item) override {
    item.hasValue = true;
    if (!gfx.settings->randomLevel) {
      item.value = '"' + getBasename(getLeaf(gfx.settings->levelFile)) + '"';
      menu.itemFromId(SettingsMenu::Option::RegenerateLevel)->string =
          LS(ReloadLevel);  // Not string?
    } else {
      item.value = LS(Random2);
      menu.itemFromId(SettingsMenu::Option::RegenerateLevel)->string =
          LS(RegenLevel);
    }
  }

  Common& common;
};

struct WeaponOptionsBehavior : ItemBehavior {
  WeaponOptionsBehavior(Common& common) : common(common) {}

  int onEnter(Menu& menu, MenuItem& item) override {
    sfx.play(common, 27);
    gfx.weaponOptions();
    sfx.play(common, 27);
    return -1;
  }

  Common& common;
};

struct OptionsSaveBehavior : ItemBehavior {
  OptionsSaveBehavior(Common& common) : common(common) {}

  int onEnter(Menu& menu, MenuItem& item) override {
    sfx.play(common, 27);

    int x, y;
    if (!menu.itemPosition(item, x, y))
      return -1;

    x += menu.valueOffsetX + 2;

    std::string name = getBasename(getLeaf(gfx.settingsNode.fullPath()));
    if (gfx.inputString(name, 30, x, y) && !name.empty()) {
      gfx.saveSettings(gfx.getConfigNode() / "Setups" / (name + ".cfg"));
    }

    sfx.play(common, 27);

    onUpdate(menu, item);
    return -1;
  }

  void onUpdate(Menu& menu, MenuItem& item) override {
    item.value = getBasename(getLeaf(gfx.settingsNode.fullPath()));
    item.hasValue = true;
  }

  Common& common;
};

struct OptionsSelectBehavior : ItemBehavior {
  OptionsSelectBehavior(Common& common) : common(common) {}

  int onEnter(Menu& menu, MenuItem& item) override {
    sfx.play(common, 27);
    gfx.selectOptions();
    sfx.play(common, 27);
    menu.updateItems(common);
    return -1;
  }

  Common& common;
};

ItemBehavior* SettingsMenu::getItemBehavior(Common& common, MenuItem& item) {
  switch (item.id) {
    case SettingsMenu::Option::NamesOnBonuses:
      return new BooleanSwitchBehavior(common, gfx.settings->namesOnBonuses);
    case SettingsMenu::Option::Map:
      return new BooleanSwitchBehavior(common, gfx.settings->map);
    case SettingsMenu::Option::RegenerateLevel:
      return new BooleanSwitchBehavior(common, gfx.settings->regenerateLevel);
    case SettingsMenu::Option::LoadingTimes:
      return new IntegerBehavior(
          common, gfx.settings->loadingTime, 0, 9999, 1, true);
    case SettingsMenu::Option::MaxBonuses:
      return new IntegerBehavior(common, gfx.settings->maxBonuses, 0, 99, 1);
    case SettingsMenu::Option::AmountOfBlood: {
      IntegerBehavior* ret = new IntegerBehavior(
          common, gfx.settings->blood, 0, LC(BloodLimit), LC(BloodStepUp),
          true);
      ret->allowEntry = false;
      return ret;
    }

    case SettingsMenu::Option::Lives:
      return new IntegerBehavior(common, gfx.settings->lives, 1, 999, 1);
    case SettingsMenu::Option::TimeToLose:
    case SettingsMenu::Option::TimeToWin:
      return new TimeBehavior(common, gfx.settings->timeToLose, 60, 3600, 10);
    case SettingsMenu::Option::ZoneTimeout:
      return new TimeBehavior(common, gfx.settings->zoneTimeout, 10, 3600, 10);
    case SettingsMenu::Option::FlagsToWin:
      return new IntegerBehavior(common, gfx.settings->flagsToWin, 1, 999, 1);

    case SettingsMenu::Option::Level:
      return new LevelSelectBehavior(common);

    case SettingsMenu::Option::GameMode:
      return new ArrayEnumBehavior(
          common, gfx.settings->gameMode, common.texts.gameModes);
    case SettingsMenu::Option::WeaponOptions:
      return new WeaponOptionsBehavior(common);
    case LoadOptions:
      return new OptionsSelectBehavior(common);
    case SaveOptions:
      return new OptionsSaveBehavior(common);
    case LoadChange:
      return new BooleanSwitchBehavior(common, gfx.settings->loadChange);
    default:
      return Menu::getItemBehavior(common, item);
  }
}

void SettingsMenu::onUpdate() {
  setVisibility(SettingsMenu::Option::Lives, false);
  setVisibility(SettingsMenu::Option::TimeToLose, false);
  setVisibility(SettingsMenu::Option::TimeToWin, false);
  setVisibility(SettingsMenu::Option::ZoneTimeout, false);
  setVisibility(SettingsMenu::Option::FlagsToWin, false);

  switch (gfx.settings->gameMode) {
    case Settings::GameMode::KillEmAll:
    // [[fallthrough]]; C++17
    case Settings::GameMode::ScalesOfJustice:
      setVisibility(SettingsMenu::Option::Lives, true);
      break;
    case Settings::GameMode::GameOfTag:
      setVisibility(SettingsMenu::Option::TimeToLose, true);
      break;
    case Settings::GameMode::Holdazone:
      setVisibility(SettingsMenu::Option::TimeToWin, true);
      setVisibility(SettingsMenu::Option::ZoneTimeout, true);
      break;
  }
}

using std::pair;
using std::shared_ptr;
using std::string;
using std::vector;

void Gfx::selectLevel() {
  Common& common = *this->common;
  FileSelector levSel(common);

  shared_ptr<FileNode> random(
      new FileNode(LS(Random), "", "", false, &levSel.rootNode));

  {
    levSel.fill(getConfigNode(), [](string const& name, string const& ext) {
      return ciCompare(ext, "LEV");
    });

    random->id = 1;
    levSel.rootNode.children.insert(levSel.rootNode.children.begin(), random);
    levSel.setFolder(levSel.rootNode);
    levSel.select(settings->levelFile);
  }

  FileNode* previewNode = 0;

  do {
    playRenderer.bmp.copy(frozenScreen);

    string title = LS(SelLevel);
    if (!levSel.currentNode->fullPath.empty()) {
      title += ' ';
      title += levSel.currentNode->fullPath;
    }

    int wid = common.font.getDims(title);

    drawRoundedBox(playRenderer.bmp, 178, 20, 0, 7, wid);
    common.font.drawText(playRenderer.bmp, title, 180, 21, 50);

    FileNode* sel = levSel.curSel();
    if (previewNode != sel && sel && sel != random.get() && !sel->folder) {
      Level level(common);

      ReaderFile f;

      try {
        if (level.load(common, *settings, sel->getFsNode().toOctetReader())) {
          int centerX = singleScreenRenderer.renderResX / 2;

          level.drawMiniature(frozenScreen, 134, 162, 10);
          level.drawMiniature(
              frozenSpectatorScreen, centerX - 126,
              singleScreenRenderer.renderResY - 208, 2);
        }
      } catch (std::runtime_error&) {
        // Ignore
      }

      previewNode = sel;
    }

    levSel.draw();

    if (!levSel.process())
      break;

    if (testSDLKeyOnce(SDL_SCANCODE_RETURN) ||
        testSDLKeyOnce(SDL_SCANCODE_KP_ENTER)) {
      sfx.play(common, 27);

      const auto* sel = levSel.enter();

      if (sel) {
        if (sel == random.get()) {
          settings->randomLevel = true;
          settings->levelFile.clear();
        } else {
          settings->randomLevel = false;
          settings->levelFile = sel->fullPath;
        }
        break;
      }
    }

    menuFlip();
    process();
  } while (true);
}

void Gfx::selectProfile(WormSettings& ws) {
  FileSelector profileSel(*common, 28);

  {
    profileSel.fill(getConfigNode(), [](string const& name, string const& ext) {
      return ciCompare(ext, "LPF");
    });

    profileSel.setFolder(profileSel.rootNode);
    profileSel.select(joinPath(getConfigNode().fullPath(), "Profiles"));
  }

  do {
    playRenderer.bmp.copy(frozenScreen);

    string title = "Select profile:";
    if (!profileSel.currentNode->fullPath.empty()) {
      title += ' ';
      title += profileSel.currentNode->fullPath;
    }

    common->font.drawFramedText(playRenderer.bmp, title, 178, 20, 50);

    profileSel.draw();

    if (!profileSel.process())
      break;

    if (testSDLKeyOnce(SDL_SCANCODE_RETURN) ||
        testSDLKeyOnce(SDL_SCANCODE_KP_ENTER)) {
      auto* sel = profileSel.enter();

      if (sel) {
        ws.loadProfile(sel->getFsNode());
        return;
      }
    }

    menuFlip();
    process();
  } while (true);

  return;
}

int Gfx::selectReplay() {
  FileSelector replaySel(*common, 28);

  {
    replaySel.fill(getConfigNode(), [](string const& name, string const& ext) {
      return ciCompare(ext, "LRP");
    });

    replaySel.setFolder(replaySel.rootNode);
    if (prevSelectedReplayPath.empty() ||
        !replaySel.select(prevSelectedReplayPath)) {
      replaySel.select(joinPath(getConfigNode().fullPath(), "Replays"));
    }
  }

  do {
    playRenderer.bmp.copy(frozenScreen);

    string title = "Select replay:";
    if (!replaySel.currentNode->fullPath.empty()) {
      title += ' ';
      title += replaySel.currentNode->fullPath;
    }

    common->font.drawFramedText(playRenderer.bmp, title, 178, 20, 50);

    replaySel.draw();

    if (!replaySel.process())
      break;

    if (testSDLKeyOnce(SDL_SCANCODE_RETURN) ||
        testSDLKeyOnce(SDL_SCANCODE_KP_ENTER)) {
      auto* sel = replaySel.enter();

      if (sel) {
        prevSelectedReplayPath = sel->fullPath;

        // Reset controller before opening the replay, since we may be recording
        // it
        controller.reset();

        controller.reset(
            new ReplayController(common, sel->getFsNode().toSource()));

        return MainMenu::Option::Replay;
      }
    }
    menuFlip();
    process();
  } while (true);

  return -1;
}

void Gfx::selectOptions() {
  FileSelector optionsSel(*common, 28);

  {
    optionsSel.fill(getConfigNode(), [](string const& name, string const& ext) {
      return ciCompare(ext, "CFG");
    });

    optionsSel.setFolder(optionsSel.rootNode);
    optionsSel.select(joinPath(getConfigNode().fullPath(), "Setups"));
  }

  do {
    playRenderer.bmp.copy(frozenScreen);

    string title = "Select options:";
    if (!optionsSel.currentNode->fullPath.empty()) {
      title += ' ';
      title += optionsSel.currentNode->fullPath;
    }

    common->font.drawFramedText(playRenderer.bmp, title, 178, 20, 50);

    optionsSel.draw();

    if (!optionsSel.process())
      break;

    if (testSDLKeyOnce(SDL_SCANCODE_RETURN) ||
        testSDLKeyOnce(SDL_SCANCODE_KP_ENTER)) {
      auto* sel = optionsSel.enter();

      if (sel) {
        gfx.loadSettings(sel->getFsNode());
        return;
      }
    }
    menuFlip();
    process();
  } while (true);
}

std::unique_ptr<Common> Gfx::selectTc() {
  FileSelector tcSel(*common, 28);

  {
    tcSel.fill(getConfigNode() / "TC", 0);

    tcSel.setFolder(tcSel.rootNode);

    auto end = std::remove_if(
        tcSel.rootNode.children.begin(), tcSel.rootNode.children.end(),
        [](shared_ptr<FileNode> const& n) {
          auto tc = n->getFsNode() / "tc.cfg";
          return !tc.exists();
        });

    tcSel.rootNode.children.erase(end, tcSel.rootNode.children.end());

    for (const auto& c : tcSel.rootNode.children) {
      c->folder = false;
    }
  }

  do {
    playRenderer.bmp.copy(frozenScreen);

    string title = "Select TC:";
    if (!tcSel.currentNode->fullPath.empty()) {
      title += ' ';
      title += tcSel.currentNode->fullPath;
    }

    common->font.drawFramedText(playRenderer.bmp, title, 178, 20, 50);

    tcSel.draw();

    if (!tcSel.process())
      break;

    if (testSDLKeyOnce(SDL_SCANCODE_RETURN) ||
        testSDLKeyOnce(SDL_SCANCODE_KP_ENTER)) {
      auto* sel = tcSel.enter();

      if (sel) {
        gvl::unique_ptr<Common> common(new Common());
        common->load(sel->getFsNode());
        settings->tc = sel->name;
        return common;
      }
    }
    menuFlip();
    process();
  } while (true);

  return std::unique_ptr<Common>();
}

struct WeaponMenu : Menu {
  WeaponMenu(int x, int y) : Menu(x, y) {}

  ItemBehavior* getItemBehavior(Common& common, MenuItem& item) override {
    int index = common.weapOrder[item.id];
    return new ArrayEnumBehavior(
        common, gfx.settings->weapTable[index], common.texts.weapStates);
  }
};

void Gfx::weaponOptions() {
  Common& common = *this->common;
  WeaponMenu weaponMenu(179, 28);

  weaponMenu.setHeight(14);
  weaponMenu.valueOffsetX = 89;

  for (int i = 0; i < static_cast<int>(common.weapons.size()); ++i) {
    int index = common.weapOrder[i];
    weaponMenu.addItem(MenuItem(48, 7, common.weapons[index].name, i));
  }

  weaponMenu.moveToFirstVisible();
  weaponMenu.updateItems(common);

  while (true) {
    playRenderer.bmp.copy(frozenScreen);

    drawBasicMenu();

    drawRoundedBox(
        playRenderer.bmp, 179, 20, 0, 7, common.font.getDims(LS(Weapon)));
    drawRoundedBox(
        playRenderer.bmp, 249, 20, 0, 7, common.font.getDims(LS(Availability)));

    common.font.drawText(playRenderer.bmp, LS(Weapon), 181, 21, 50);
    common.font.drawText(playRenderer.bmp, LS(Availability), 251, 21, 50);

    weaponMenu.draw(common, playRenderer, false);

    if (testSDLKeyOnce(SDL_SCANCODE_UP)) {
      sfx.play(common, 26);
      weaponMenu.movement(-1);
    }

    if (testSDLKeyOnce(SDL_SCANCODE_DOWN)) {
      sfx.play(common, 25);
      weaponMenu.movement(1);
    }

    if (testSDLKeyOnce(SDL_SCANCODE_LEFT)) {
      weaponMenu.onLeftRight(common, -1);
    }
    if (testSDLKeyOnce(SDL_SCANCODE_RIGHT)) {
      weaponMenu.onLeftRight(common, 1);
    }

    if (settings->extensions) {
      if (testSDLKeyOnce(SDL_SCANCODE_PAGEUP)) {
        sfx.play(common, 26);

        weaponMenu.movementPage(-1);
      }

      if (testSDLKeyOnce(SDL_SCANCODE_PAGEDOWN)) {
        sfx.play(common, 25);

        weaponMenu.movementPage(1);
      }
    }

    weaponMenu.onKeys(gfx.keyBuf, gfx.keyBufPtr);

    menuFlip();
    process();

    if (testSDLKeyOnce(SDL_SCANCODE_ESCAPE)) {
      int count = 0;

      for (int i = 0; i < 40; ++i) {
        if (settings->weapTable[i] == 0)
          ++count;
      }

      // Enough weapons available
      if (count > 0)
        break;

      infoBox(LS(NoWeaps), 223, 68, false);
    }
  }
}

void Gfx::infoBox(std::string const& text, int x, int y, bool clearScreen) {
  static int const bgColor = 0;

  if (clearScreen) {
    playRenderer.pal = common->exepal;
    fill(playRenderer.bmp, bgColor);
  }

  int height;
  int width = common->font.getDims(text, &height);

  int cx = x - width / 2 - 2;
  int cy = y - height / 2 - 2;

  drawRoundedBox(playRenderer.bmp, cx, cy, 0, height + 1, width + 1);
  common->font.drawText(playRenderer.bmp, text, cx + 2, cy + 2, 6);

  flip();
  process();

  waitForKey();
  clearKeys();

  if (clearScreen)
    fill(playRenderer.bmp, bgColor);
}

bool Gfx::inputString(
    std::string& dest,
    std::size_t maxLen,
    int x,
    int y,
    int (*filter)(int),
    std::string const& prefix,
    bool centered) {
  std::string buffer = dest;

  while (true) {
    std::string str = prefix + buffer + '_';

    Font& font = common->font;

    int width = font.getDims(str);

    int adjust = centered ? width / 2 : 0;

    int clrX = x - 10 - adjust;

    SDL_Event ev;

    // int offset = clrX + y*320; // TODO: Unhardcode 320
    // TODO: honestly, all the references to window size *need* to be
    // unhardcoded.

    blitImageNoKeyColour(
        playRenderer.bmp, &frozenScreen.getPixel(clrX, y), clrX, y,
        clrX + 10 + width, 8, frozenScreen.pitch);

    drawRoundedBox(playRenderer.bmp, x - 2 - adjust, y, 0, 7, width);

    font.drawText(playRenderer.bmp, str, x - adjust, y + 1, 50);
    flip();

    SDL_StartTextInput();
    SDL_WaitEvent(&ev);
    processEvent(ev);

    switch (ev.type) {
      case SDL_KEYDOWN:
        switch (ev.key.keysym.scancode) {
          case SDL_SCANCODE_BACKSPACE:
            if (!buffer.empty()) {
              buffer.erase(buffer.size() - 1);
            }
            break;

          case SDL_SCANCODE_RETURN:
          // [[fallthrough]]; C++17
          case SDL_SCANCODE_KP_ENTER:
            dest = buffer;
            sfx.play(*common, 27);
            clearKeys();
            return true;

          case SDL_SCANCODE_ESCAPE:
            clearKeys();
            return false;

          default:
            break;
        }
        break;

      case SDL_TEXTINPUT: {
        int k = utf8ToDOS(ev.text.text);
        if (k && buffer.size() < maxLen && (!filter || (k = filter(k)))) {
          buffer += char(k);
        }
        break;
      }
      case SDL_TEXTEDITING:
        // since there's no support for any characters that can use a
        // complex IME input (like East Asian languages), we naively
        // discard this event
        break;

      default:
        break;
    }
  }
}

int filterDigits(int k) {
  return std::isdigit(k) ? k : 0;
}

void Gfx::inputInteger(
    int& dest,
    int min,
    int max,
    std::size_t maxLen,
    int x,
    int y) {
  std::string str(toString(dest));

  if (inputString(str, maxLen, x, y, filterDigits) && !str.empty()) {
    dest = std::atoi(str.c_str());
    if (dest < min)
      dest = min;
    else if (dest > max)
      dest = max;
  }
}

void PlayerMenu::drawItemOverlay(
    Common& common,
    MenuItem& item,
    int x,
    int y,
    bool selected,
    bool disabled) {
  // Color settings
  if (item.id >= PlayerMenu::Option::Red &&
      item.id <= PlayerMenu::Option::Blue) {
    int rgbcol = item.id - PlayerMenu::Option::Red;

    if (selected) {
      drawRoundedBox(
          gfx.playRenderer.bmp, x + 24, y, 168, 7, ws->rgb[rgbcol] - 1);
    } else  // CE98
    {
      drawRoundedBox(
          gfx.playRenderer.bmp, x + 24, y, 0, 7, ws->rgb[rgbcol] - 1);
    }

    fillRect(
        gfx.playRenderer.bmp, x + 25, y + 1, ws->rgb[rgbcol], 5, ws->color);
  }  // CED9
}

ItemBehavior* PlayerMenu::getItemBehavior(Common& common, MenuItem& item) {
  if (item.id >= PlayerMenu::Option::Weap0 &&
      item.id < PlayerMenu::Option::Weap0 + 5)
    return new WeaponEnumBehavior(
        common, ws->weapons[item.id - PlayerMenu::Option::Weap0]);

  switch (item.id) {
    case PlayerMenu::Option::Name:
      return new WormNameBehavior(common, *ws);
    case PlayerMenu::Option::Health: {
      auto* b = new IntegerBehavior(common, ws->health, 1, 10000, 1, true);
      b->scrollInterval = 4;
      return b;
    }

    case PlayerMenu::Option::Red:
    // [[fallthrough]]; C++17
    case PlayerMenu::Option::Green:
    // [[fallthrough]]; C++17
    case PlayerMenu::Option::Blue: {
      auto* b = new IntegerBehavior(
          common, ws->rgb[item.id - PlayerMenu::Option::Red], 0, 63, 1, false);
      b->scrollInterval = 4;
      return b;
    }

    case PlayerMenu::Option::Up:  // D2AB
    // [[fallthrough]]; C++17
    case PlayerMenu::Option::Down:
    // [[fallthrough]]; C++17
    case PlayerMenu::Option::Left:
    // [[fallthrough]]; C++17
    case PlayerMenu::Option::Right:
    // [[fallthrough]]; C++17
    case PlayerMenu::Option::Fire:
    // [[fallthrough]]; C++17
    case PlayerMenu::Option::Change:
    // [[fallthrough]]; C++17
    case PlayerMenu::Option::Jump:
      return new KeyBehavior(
          common, ws->controls[item.id - PlayerMenu::Option::Up],
          ws->controlsEx[item.id - PlayerMenu::Option::Up],
          gfx.settings->extensions);

    case PlayerMenu::Option::Dig:
      return new KeyBehavior(
          common, ws->controlsEx[item.id - PlayerMenu::Option::Up],
          ws->controlsEx[item.id - PlayerMenu::Option::Up],
          gfx.settings->extensions);

    case PlayerMenu::Option::Controller:
      return new ArrayEnumBehavior(
          common, ws->controller, common.texts.controllers);

    case PlayerMenu::Option::SaveProfile:
      return new ProfileSaveBehavior(common, *ws, false);

    case PlayerMenu::Option::SaveProfileAs:
      return new ProfileSaveBehavior(common, *ws, true);

    case PlayerMenu::Option::LoadProfile:
      return new ProfileLoadBehavior(common, *ws);

    case PlayerMenu::Option::LoadedProfile:
      return new ProfileLoadedBehavior(common, *ws);

    default:
      return Menu::getItemBehavior(common, item);
  }
}

void Gfx::playerSettings(int player) {
  playerMenu.ws = settings->wormSettings[player];

  playerMenu.updateItems(*common);
  playerMenu.moveToFirstVisible();

  curMenu = &playerMenu;
  return;
}

void Gfx::mainLoop() {
restart:
  controller.reset(new LocalController(common, settings));

  {
    Level newLevel(*common);
    newLevel.generateFromSettings(*common, *settings, rand);
    controller->swapLevel(newLevel);
  }

  controller->currentGame()->focus(this->playRenderer);
  controller->currentGame()->focus(this->singleScreenRenderer);

  // TODO: Unfocus game when necessary

  while (true) {
    playRenderer.clear();
    controller->draw(this->playRenderer, false);

    singleScreenRenderer.clear();
    controller->draw(this->singleScreenRenderer, true);

    int selection = menuLoop();

    if (selection == MainMenu::Option::NewGame) {
      std::unique_ptr<Controller> newController(
          new LocalController(common, settings));

      Level* oldLevel = controller->currentLevel();

      if (oldLevel && !settings->regenerateLevel &&
          settings->randomLevel == oldLevel->oldRandomLevel &&
          settings->levelFile == oldLevel->oldLevelFile) {
        // Take level and palette from old game
        newController->swapLevel(*oldLevel);
      } else {
        Level newLevel(*common);
        newLevel.generateFromSettings(*common, *settings, rand);
        newController->swapLevel(newLevel);
      }

      controller = std::move(newController);
    } else if (selection == MainMenu::Option::ResumeGame) {
      if (controller->isReplay()) {
        primaryRenderer = &singleScreenRenderer;
      }
    } else if (selection == MainMenu::Option::Quit) {
      break;
    } else if (selection == MainMenu::Option::Replay) {
      if (settings->singleScreenReplay) {
        primaryRenderer = &singleScreenRenderer;
      }
    } else if (selection == MainMenu::Option::TC) {
      goto restart;
    }

    controller->focus();

    while (true) {
      if (!controller->process())
        break;
      playRenderer.clear();
      controller->draw(this->playRenderer, false);

      singleScreenRenderer.clear();
      controller->draw(this->singleScreenRenderer, true);

      ++gfx.menuCycles;

      flip();
      process(controller.get());
    }

    primaryRenderer = &playRenderer;

    controller->unfocus();

    clearKeys();
  }

  controller.reset();
}

void Gfx::saveSettings(FsNode node) {
  settingsNode = node;
  settings->save(node);
}

bool Gfx::loadSettings(FsNode node) {
  settingsNode = node;
  settings.reset(new Settings);
  return settings->load(node);
}

bool Gfx::loadSettingsLegacy(FsNode node) {
  settings.reset(new Settings);
  return settings->loadLegacy(node);
}

void Gfx::drawBasicMenu(/*int curSel*/) {
  playRenderer.bmp.copy(frozenScreen);

  mainMenu.draw(*common, playRenderer, curMenu != &mainMenu, -1, true);
}

void Gfx::drawSpectatorInfo() {
  Common& common = *this->common;
  int centerX = singleScreenRenderer.renderResX / 2;
  int centerY = singleScreenRenderer.renderResY / 4;

  singleScreenRenderer.bmp.copy(frozenSpectatorScreen);
  if (settings->levelFile.empty()) {
    common.font.drawCenteredText(
        singleScreenRenderer.bmp, LS(LevelRandom), centerX, centerY - 32, 7, 2);
  } else {
    auto levelName = getBasename(getLeaf(gfx.settings->levelFile));
    common.font.drawCenteredText(
        singleScreenRenderer.bmp, LS(LevelIs1) + levelName + LS(LevelIs2),
        centerX, centerY - 32, 7, 2);
  }

  std::string vsText = settings->wormSettings[0]->name + " vs " +
                       settings->wormSettings[1]->name;
  // put worm color boxes on a nice spot even if no player names have been
  // entered
  int textSize = std::max(common.font.getDims(vsText) * 2, 48);
  common.font.drawCenteredText(
      singleScreenRenderer.bmp, vsText, centerX, centerY, 7, 2);
  fillRect(
      singleScreenRenderer.bmp, centerX - (textSize / 2) - 1, centerY + 23 - 1,
      16, 16, 7);
  fillRect(
      singleScreenRenderer.bmp, centerX - textSize / 2, centerY + 23, 14, 14,
      settings->wormSettings[0]->color);
  fillRect(
      singleScreenRenderer.bmp, centerX + (textSize / 2) - 16 - 1,
      centerY + 23 - 1, 16, 16, 7);
  fillRect(
      singleScreenRenderer.bmp, centerX + textSize / 2 - 16, centerY + 23, 14,
      14, settings->wormSettings[1]->color);

  if (controller->running()) {
    common.font.drawCenteredText(
        singleScreenRenderer.bmp, "PAUSED", centerX, centerY + 48, 7, 2);
  } else {
    common.font.drawCenteredText(
        singleScreenRenderer.bmp, "SETUP", centerX, centerY + 48, 7, 2);
  }
}

int upperCaseOnly(int k) {
  k = std::toupper(k);

  if ((k >= 'A' && k <= 'Z') ||
      (k == 0x8f || k == 0x8e || k == 0x99)  // � �and �
      || (k >= '0' && k <= '9'))
    return k;

  return 0;
}

void Gfx::openHiddenMenu() {
  if (curMenu == &hiddenMenu)
    return;
  curMenu = &hiddenMenu;
  curMenu->updateItems(*common);
  curMenu->moveToFirstVisible();
}

int Gfx::menuLoop() {
  Common& common = *this->common;
  int centerX = singleScreenRenderer.renderResX / 2;

  std::memset(playRenderer.pal.entries, 0, sizeof(playRenderer.pal.entries));
  std::memset(
      singleScreenRenderer.pal.entries, 0,
      sizeof(singleScreenRenderer.pal.entries));
  flip();
  process();

  fillRect(playRenderer.bmp, 0, 151, 160, 7, 0);
  common.font.drawText(playRenderer.bmp, LS(Copyright2), 2, 152, 19);

  int startItemId;
  if (controller->running()) {
    mainMenu.setVisibility(MainMenu::Option::ResumeGame, true);
    mainMenu.itemFromId(MainMenu::Option::ResumeGame)->string =
        "RESUME GAME (F1)";
    mainMenu.itemFromId(MainMenu::Option::NewGame)->string = "NEW GAME";
    startItemId = MainMenu::Option::ResumeGame;
  } else {
    mainMenu.setVisibility(MainMenu::Option::ResumeGame, false);
    mainMenu.itemFromId(MainMenu::Option::NewGame)->string = "NEW GAME (F1)";
    startItemId = MainMenu::Option::NewGame;
  }

  mainMenu.moveToFirstVisible();
  settingsMenu.moveToFirstVisible();
  settingsMenu.updateItems(common);

  playRenderer.fadeValue = 0;
  singleScreenRenderer.fadeValue = 0;
  curMenu = &mainMenu;

  frozenScreen.copy(playRenderer.bmp);
  singleScreenRenderer.clear();
  if (controller->currentLevel()) {
    controller->currentLevel()->drawMiniature(
        singleScreenRenderer.bmp, centerX - 126,
        singleScreenRenderer.renderResY - 208, 2);
  }
  frozenSpectatorScreen.copy(singleScreenRenderer.bmp);

  menuCycles = 0;
  int selected = -1;
  do {
    drawBasicMenu();
    drawSpectatorInfo();

    if (curMenu == &mainMenu)
      settingsMenu.draw(common, playRenderer, true);
    else
      curMenu->draw(common, playRenderer, false);

    if (testSDLKeyOnce(SDL_SCANCODE_ESCAPE)) {
      if (curMenu == &mainMenu)
        mainMenu.moveToId(MainMenu::Option::Quit);
      else
        curMenu = &mainMenu;
    }

    if (testSDLKeyOnce(SDL_SCANCODE_UP)) {
      sfx.play(common, 26);
      curMenu->movement(-1);
    }

    if (testSDLKeyOnce(SDL_SCANCODE_DOWN)) {
      sfx.play(common, 25);
      curMenu->movement(1);
    }

    if (testSDLKeyOnce(SDL_SCANCODE_RETURN) ||
        testSDLKeyOnce(SDL_SCANCODE_KP_ENTER)) {
      if (curMenu == &mainMenu) {
        sfx.play(common, 27);

        int s = mainMenu.selectedId();
        switch (s) {
          case MainMenu::Option::Settings: {
            // Go into settings menu
            curMenu = &settingsMenu;
            break;
          }

          case MainMenu::Option::Player1Settings:
          // [[fallthrough]]; C++17
          case MainMenu::Option::Player2Settings: {
            playerSettings(s - MainMenu::Option::Player1Settings);
            break;
          }

          case MainMenu::Option::Advanced: {
            openHiddenMenu();
            break;
          }

          case MainMenu::Option::Replays: {
            selected = curMenu->onEnter(common);
            break;
          }

          case MainMenu::Option::TC: {
            if (curMenu->onEnter(common) == MainMenu::Option::TC)
              return MainMenu::Option::TC;
            break;
          }

          default: {
            curMenu = &mainMenu;
            selected = s;
          }
        }
      } else if (curMenu == &settingsMenu) {
        settingsMenu.onEnter(common);
      } else {
        selected = curMenu->onEnter(common);
      }
    }

    if (testSDLKeyOnce(SDL_SCANCODE_F1)) {
      curMenu = &mainMenu;
      mainMenu.moveToId(startItemId);
      selected = startItemId;
    }
    if (testSDLKeyOnce(SDL_SCANCODE_F2)) {
      mainMenu.moveToId(MainMenu::Option::Advanced);
      openHiddenMenu();
    }
    if (testSDLKeyOnce(SDL_SCANCODE_F3)) {
      curMenu = &mainMenu;
      mainMenu.moveToId(MainMenu::Option::Replays);
      selected = curMenu->onEnter(common);
    }

    if (testSDLKeyOnce(SDL_SCANCODE_F5)) {
      mainMenu.moveToId(MainMenu::Option::Player1Settings);
      playerSettings(0);
    }
    if (testSDLKeyOnce(SDL_SCANCODE_F6)) {
      mainMenu.moveToId(MainMenu::Option::Player2Settings);
      playerSettings(1);
    }
    if (testSDLKeyOnce(SDL_SCANCODE_F7)) {
      mainMenu.moveToId(MainMenu::Option::Settings);
      curMenu = &settingsMenu;  // Go into settings menu
    }

    if (testSDLKeyOnce(SDL_SCANCODE_F8)) {
      // TODO ensure this is deterministic
      std::random_device rd;
      std::mt19937 g(rd());

      Common& common = *this->common;

      vector<std::size_t> nobjMap;

      for (std::size_t i = 0; i < common.nobjectTypes.size(); ++i) {
        nobjMap.push_back(i);
      }
      std::shuffle(nobjMap.begin(), nobjMap.end(), g);

      for (auto& w : common.weapons) {
        w.addSpeed = std::uniform_int_distribution<int>(0, 30 - 1)(g) - 5;
        w.affectByExplosions =
            std::uniform_int_distribution<int>(0, 2 - 1)(g) == 0;
        w.affectByWorm = std::uniform_int_distribution<int>(0, 3 - 1)(g) == 0;
        w.ammo = std::uniform_int_distribution<int>(0, 20 - 1)(g) + 1;
        w.bloodOnHit = std::uniform_int_distribution<int>(0, 50 - 1)(g);
        w.blowAway = std::uniform_int_distribution<int>(0, 10 - 1)(g);
        w.bounce = std::uniform_int_distribution<int>(0, 90 - 1)(g);
        w.collideWithObjects =
            std::uniform_int_distribution<int>(0, 10 - 1)(g) == 0;
        w.colorBullets = 3 + std::uniform_int_distribution<int>(0, 250 - 1)(g);
        w.createOnExp = std::uniform_int_distribution<int>(
            0, common.sobjectTypes.size() - 1)(g);
        w.delay = std::uniform_int_distribution<int>(0, 70 - 1)(g);
        w.detectDistance = std::uniform_int_distribution<int>(0, 20 - 1)(g);
        w.dirtEffect = std::uniform_int_distribution<int>(0, 9 - 1)(g);
        w.distribution =
            std::uniform_int_distribution<int>(0, 5000 - 1)(g) - 2500;
        w.explGround = std::uniform_int_distribution<int>(0, 2 - 1)(g) == 0;
        w.exploSound =
            std::uniform_int_distribution<int>(0, common.sounds.size() - 1)(g);
        w.fireCone = std::uniform_int_distribution<int>(0, 10 - 1)(g);
        w.gravity = std::uniform_int_distribution<int>(0, 2000 - 1)(g) - 1000;
        w.hitDamage = std::uniform_int_distribution<int>(0, 20 - 1)(g);
        w.laserSight = std::uniform_int_distribution<int>(0, 5 - 1)(g) == 0;
        w.launchSound =
            std::uniform_int_distribution<int>(0, common.sounds.size() - 1)(g);
        w.leaveShellDelay = std::uniform_int_distribution<int>(0, 30 - 1)(g);
        w.leaveShells = std::uniform_int_distribution<int>(0, 1)(g) == 0;
        w.loadingTime = std::uniform_int_distribution<int>(0, (70 * 3) - 1)(g);
        w.loopAnim = std::uniform_int_distribution<int>(0, 10 - 1)(g) == 0;
        w.loopSound = false;
        w.multSpeed =
            std::uniform_int_distribution<int>(0, 2 - 1)(g)
                ? 100
                : 99 + std::uniform_int_distribution<int>(0, 5 - 1)(g);
        w.objTrailDelay = 10 + std::uniform_int_distribution<int>(0, 70 - 1)(g);
        w.objTrailType = std::uniform_int_distribution<int>(0, 4 - 1)(g) == 0
                             ? std::uniform_int_distribution<int>(
                                   0, common.sobjectTypes.size() - 1)(g)
                             : -1;
        w.parts = std::uniform_int_distribution<int>(0, 2 - 1)(g) == 0
                      ? std::uniform_int_distribution<int>(0, 10 - 1)(g)
                      : 1;
        w.partTrailDelay =
            10 + std::uniform_int_distribution<int>(0, 70 - 1)(g);
        w.partTrailObj = std::uniform_int_distribution<int>(0, 4 - 1)(g) == 0
                             ? std::uniform_int_distribution<int>(
                                   0, common.nobjectTypes.size() - 1)(g)
                             : -1;
        w.partTrailType = std::uniform_int_distribution<int>(0, 2 - 1)(g);
        w.playReloadSound =
            std::uniform_int_distribution<int>(0, 2 - 1)(g) == 0;
        w.recoil = std::uniform_int_distribution<int>(0, 20 - 1)(g);
        w.shadow = std::uniform_int_distribution<int>(0, 2 - 1)(g) == 0;
        w.shotType = std::uniform_int_distribution<int>(0, 5 - 1)(g);
        w.speed = std::uniform_int_distribution<int>(0, 200 - 1)(g);
        w.splinterAmount =
            std::uniform_int_distribution<int>(0, 5 - 1)(g) == 0
                ? std::uniform_int_distribution<int>(0, 10 - 1)(g)
                : 0;
        w.splinterColour = std::uniform_int_distribution<int>(0, 256 - 1)(g);
        w.splinterScatter = std::uniform_int_distribution<int>(0, 2 - 1)(g);
        w.splinterType = std::uniform_int_distribution<int>(
            0, common.nobjectTypes.size() - 1)(g);
        w.startFrame = std::uniform_int_distribution<int>(
            0, static_cast<uint32_t>(common.smallSprites.count) - 13 - 1)(g);
        w.numFrames = std::uniform_int_distribution<int>(0, 5 - 1)(g);
        w.timeToExplo = 50 + std::uniform_int_distribution<int>(0, 200 - 1)(g);
        w.timeToExploV = 10 + std::uniform_int_distribution<int>(0, 50 - 1)(g);
        w.wormCollide = std::uniform_int_distribution<int>(0, 3 - 1)(g) > 0;
        w.wormExplode = std::uniform_int_distribution<int>(0, 3 - 1)(g) > 0;
      }

      // for (auto& n : common.nobjectTypes)
      for (std::size_t idx = 0; idx < common.nobjectTypes.size(); ++idx) {
        auto& n = common.nobjectTypes[nobjMap[idx]];
        n.affectByExplosions =
            std::uniform_int_distribution<int>(0, 5 - 1)(g) == 0;
        n.bloodOnHit = std::uniform_int_distribution<int>(0, 5 - 1)(g);
        n.bloodTrail = std::uniform_int_distribution<int>(0, 10 - 1)(g) == 0;
        n.bloodTrailDelay =
            std::uniform_int_distribution<int>(0, 20 - 1)(g) + 3;
        n.blowAway = std::uniform_int_distribution<int>(0, 10 - 1)(g);
        n.bounce = std::uniform_int_distribution<int>(0, 90 - 1)(g);
        n.colorBullets = 3 + std::uniform_int_distribution<int>(0, 250 - 1)(g);
        n.createOnExp = std::uniform_int_distribution<int>(0, 3 - 1)(g) == 0
                            ? std::uniform_int_distribution<int>(
                                  0, common.sobjectTypes.size() - 1)(g)
                            : -1;
        n.detectDistance = std::uniform_int_distribution<int>(0, 20 - 1)(g);
        n.dirtEffect = std::uniform_int_distribution<int>(0, 9 - 1)(g);
        n.distribution =
            std::uniform_int_distribution<int>(0, 5000 - 1)(g) - 2500;
        n.drawOnMap = std::uniform_int_distribution<int>(0, 20 - 1)(g) == 0;
        n.explGround = std::uniform_int_distribution<int>(0, 4 - 1)(g) > 0;
        n.gravity = std::uniform_int_distribution<int>(0, 2000 - 1)(g) - 1000;
        n.hitDamage = std::uniform_int_distribution<int>(0, 10 - 1)(g);
        n.leaveObj = std::uniform_int_distribution<int>(0, 5 - 1)(g) == 0
                         ? std::uniform_int_distribution<int>(
                               0, common.sobjectTypes.size() - 1)(g)
                         : -1;
        n.leaveObjDelay = 10 + std::uniform_int_distribution<int>(0, 80 - 1)(g);
        n.startFrame = std::uniform_int_distribution<int>(
            0, static_cast<uint32_t>(common.smallSprites.count) - 13 - 1)(g);
        n.numFrames = std::uniform_int_distribution<int>(0, 5 - 1)(g);
        n.speed = std::uniform_int_distribution<int>(0, 150 - 1)(g);
        n.splinterAmount =
            idx > 0 && std::uniform_int_distribution<int>(0, 5 - 1)(g) == 0
                ? std::uniform_int_distribution<int>(0, 10 - 1)(g)
                : 0;
        n.splinterColour = std::uniform_int_distribution<int>(0, 256 - 1)(g);
        n.splinterType =
            idx > 0 ? nobjMap[std::uniform_int_distribution<int>(0, idx - 1)(g)]
                    : 0;
        n.timeToExplo =
            50 + std::uniform_int_distribution<int>(0, (70 * 3) - 1)(g);
        n.timeToExploV = std::uniform_int_distribution<int>(0, 30 - 1)(g);
        n.wormDestroy = std::uniform_int_distribution<int>(0, 3 - 1)(g) == 0;
        n.wormExplode = std::uniform_int_distribution<int>(0, 2 - 1)(g) == 0;
      }

      for (auto& s : common.sobjectTypes) {
        s.animDelay = 1 + std::uniform_int_distribution<int>(0, 10 - 1)(g);
        s.blowAway = std::uniform_int_distribution<int>(0, 2 - 1)(g) == 0
                         ? std::uniform_int_distribution<int>(0, 10000 - 1)(g)
                         : 0;
        s.damage = std::uniform_int_distribution<int>(0, 30 - 1)(g);
        s.detectRange = std::uniform_int_distribution<int>(0, 20 - 1)(g);
        s.dirtEffect = std::uniform_int_distribution<int>(0, 9 - 1)(g);
        s.flash = std::uniform_int_distribution<int>(0, 5 - 1)(g);
        s.startFrame = std::uniform_int_distribution<int>(
            0, static_cast<uint32_t>(common.largeSprites.count) - 7 - 1)(g);
        s.numFrames = std::uniform_int_distribution<int>(0, 7 - 1)(g);
        s.startSound =
            std::uniform_int_distribution<int>(0, common.sounds.size() - 1)(g);
        s.shake = std::uniform_int_distribution<int>(0, 10 - 1)(g);
        s.shadow = std::uniform_int_distribution<int>(0, 2 - 1)(g);
        s.numSounds = 1;
      }
    }

    if (testSDLKey(SDL_SCANCODE_LEFT)) {
      if (!curMenu->onLeftRight(common, -1))
        resetLeftRight();
    }
    if (testSDLKey(SDL_SCANCODE_RIGHT)) {
      if (!curMenu->onLeftRight(common, 1))
        resetLeftRight();
    }

    if (testSDLKeyOnce(SDL_SCANCODE_PAGEUP)) {
      sfx.play(common, 26);

      curMenu->movementPage(-1);
    }

    if (testSDLKeyOnce(SDL_SCANCODE_PAGEDOWN)) {
      sfx.play(common, 25);

      curMenu->movementPage(1);
    }

    menuFlip();
    process();
  } while (selected < 0);

  for (playRenderer.fadeValue = 32; playRenderer.fadeValue > 0;
       --playRenderer.fadeValue) {
    menuFlip(true);
    process();
  }

  return selected;
}
