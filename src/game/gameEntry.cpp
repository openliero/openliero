#include <SDL.h>

#include "console.hpp"
#include "constants.hpp"
#include "filesystem.hpp"
#include "game.hpp"
#include "gfx.hpp"
#include "keys.hpp"
#include "math.hpp"
#include "reader.hpp"
#include "sfx.hpp"
#include "text.hpp"
#include "viewport.hpp"
#include "worm.hpp"

#include <ctime>
#include <exception>
#include <random>

int gameEntry(int argc, char* argv[]) try {
  // TODO: Validate PRNG seeding
  // why do we have *two* PRNGs (gfx & game)?
  std::random_device r;
  std::mt19937 rand(r());
  gfx.rand = rand;

  bool tcSet = false;

  std::string tcName;
  std::string configPath;  // Default to current dir

  for (int i = 1; i < argc; ++i) {
    if (argv[i][0] == '-') {
      switch (argv[i][1]) {
        case '-':
          if (std::strcmp(argv[i] + 2, "config-root") == 0 && i + 1 < argc) {
            ++i;
            configPath = argv[i];
          }
          break;
      }
    } else {
      tcName = argv[i];
      tcSet = true;
    }
  }

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);

  initKeys();

  precomputeTables();

  gfx.loadMenus();

  gfx.init();
  gfx.setConfigPath(configPath);

  FsNode configNode(gfx.getConfigNode());

  if (!gfx.loadSettings(configNode / "Setups" / "liero.cfg")) {
    if (!gfx.loadSettingsLegacy(configNode / "LIERO.DAT")) {
      gfx.settings.reset(new Settings);
      gfx.saveSettings(configNode / "Setups" / "liero.cfg");
    }
  }

  if (tcSet)
    gfx.settings->tc = tcName;

  // TC loading
  FsNode lieroRoot(configNode / "TC" / gfx.settings->tc);
  std::shared_ptr<Common> common(new Common());
  common->load(std::move(lieroRoot));
  gfx.common = common;
  gfx.playRenderer.loadPalette(*common);  // This gets the palette from common

  gfx.setVideoMode();
  sfx.init();

  gfx.mainLoop();

  gfx.settings->save(configNode / "Setups" / "liero.cfg");

  sfx.deinit();
  SDL_Quit();

  return 0;
} catch (std::exception&) {
  SDL_Quit();
  throw;
}
