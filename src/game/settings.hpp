#pragma once

#include <cstdint>
#include <cstring>
#include <gvl/crypt/gash.hpp>
#include <gvl/serialization/archive.hpp>  // For gvl::enable_when
#include <stdexcept>
#include <string>
#include "version.hpp"
#include "worm.hpp"

// We isolate extensions for the benefit of the .dat loader.
// It can then easily reset the extensions if they fail to load.
struct Extensions {
  static int const myVersion = 9;
  static bool const extensions = true;

  Extensions();

  // Extensions
  bool recordReplays;
  bool loadPowerlevelPalette;
  int32_t bloodParticleMax;

  int aiFrames, aiMutations;
  bool aiTraces;
  int32_t aiParallels;

  bool fullscreen;

  int32_t zoneTimeout;
  uint32_t selectBotWeapons;

  bool allowViewingSpawnPoint;
  bool singleScreenReplay;
  bool spectatorWindow;
  std::string tc;
};

struct Rand;

struct Settings : gvl::shared, Extensions {
  enum GameModes {
    GMKillEmAll,
    GMGameOfTag,
    GMHoldazone,
    GMScalesOfJustice,
    MaxGameModes
  };

  static int const selectableWeapons = 5;
  static int const zoneCaptureTime = 70;

  static int const wormAnimTab[];

  Settings();

  bool load(FsNode node, Rand& rand);
  void save(FsNode node, Rand& rand);
  gvl::gash::value_type& updateHash();

  static void generateName(WormSettings& ws, Rand& rand);

  uint32_t weapTable[40];
  int32_t maxBonuses;
  int32_t blood;
  int32_t timeToLose;
  int32_t flagsToWin;
  uint32_t gameMode;
  bool shadow;
  bool loadChange;
  bool namesOnBonuses;
  bool regenerateLevel;
  int32_t lives;
  int32_t loadingTime;
  bool randomLevel;
  std::string levelFile;
  bool map;
  bool screenSync;

  static int const NumWormSettings = 3;  // 0=left, 1=right, 2=network
  static int const NetworkPlayerIdx = 2;
  std::shared_ptr<WormSettings> wormSettings[NumWormSettings];

  gvl::gash::value_type hash;
};

template <int L, int H, typename T>
inline T limit(T v) {
  if (v >= (T)H)
    return (T)H - 1;
  else if (v < (T)L)
    return (T)L;

  return v;
}

// Settings archiving, not including player (worm) settings
template <typename Archive>
void archive(Archive ar, Settings& settings) {
  for (int i = 0; i < 40; ++i) {
    ar.ui8(settings.weapTable[i]);
  }

  ar.ui16(settings.maxBonuses)
      .ui16(settings.blood)
      .ui16(settings.timeToLose)
      .ui16(settings.flagsToWin)
      .ui16(settings.gameMode)
      .b(settings.shadow)
      .b(settings.loadChange)
      .b(settings.namesOnBonuses)
      .b(settings.regenerateLevel)
      .ui16(settings.lives)
      .ui16(settings.loadingTime)
      .b(settings.randomLevel)
      .b(settings.map)
      .b(settings.screenSync)
      .str(settings.levelFile);

  // Extensions
  int fileExtensionVersion = Extensions::myVersion;

  ar.ui8(fileExtensionVersion);

  bool extDummy = true;
  uint8_t extDummy8 = 0;
  int32_t extDummy16 = 0;

  ar.b(extDummy)
      .b(settings.recordReplays)
      .b(settings.loadPowerlevelPalette)
      .ui8(extDummy8)
      .ui16(extDummy16)   // old fullscreenW
      .ui16(extDummy16);  // old fullscreenH

  if (fileExtensionVersion >= 2)
    ar.b(extDummy);

  gvl::enable_when(ar, fileExtensionVersion >= 4)
      .ui16(settings.zoneTimeout, 30);

  gvl::enable_when(ar, fileExtensionVersion >= 6)
      .b(settings.allowViewingSpawnPoint, false);
  gvl::enable_when(ar, fileExtensionVersion >= 7)
      .b(settings.singleScreenReplay, false);
  gvl::enable_when(ar, fileExtensionVersion >= 8)
      .b(settings.spectatorWindow, false);
  gvl::enable_when(ar, fileExtensionVersion >= 9).b(settings.fullscreen, false);
  gvl::enable_when(ar, fileExtensionVersion >= 10)
      .str(settings.tc, std::string("openliero"));
  ar.check();
}

template <typename Archive>
void archive_text(Settings& settings, Archive& ar) {
  // TODO: Manage defaults when it becomes necessary

#define S(n) #n, settings.n

  ar.i32(S(maxBonuses));
  ar.i32(S(loadingTime));
  ar.i32(S(lives));
  ar.i32(S(timeToLose));
  ar.i32(S(flagsToWin));
  ar.b(S(screenSync));
  ar.b(S(map));
  ar.b(S(randomLevel));
  ar.i32(S(blood));
  ar.u32(S(gameMode));
  ar.b(S(namesOnBonuses));
  ar.b(S(regenerateLevel));
  ar.b(S(shadow));
  ar.b(S(loadChange));
  ar.str(S(levelFile));

  ar.b(S(recordReplays));
  ar.b(S(loadPowerlevelPalette));

  ar.i32(S(aiMutations))
      .i32(S(aiFrames))
      .u32(S(selectBotWeapons))
      .i32(S(zoneTimeout));

  ar.b(S(aiTraces)).i32(S(aiParallels));

  ar.b(S(allowViewingSpawnPoint));
  ar.b(S(singleScreenReplay));
  ar.b(S(spectatorWindow));
  ar.b(S(fullscreen));
  ar.str(S(tc));
  ar.i32(S(bloodParticleMax));

#undef S

  ar.arr("weapTable", settings.weapTable, [&](uint32_t& v) {
    ar.u32(0, v);
    if (ar.in)
      v = limit<0, 3>(v);
  });

#define S(n) #n, ws->n

  // Serialize the first 2 worms (left/right players) as the "worms" array
  // for backwards compatibility with old config files
  std::shared_ptr<WormSettings> twoWorms[2] = {
      settings.wormSettings[0], settings.wormSettings[1]};
  ar.array_obj(
      "worms", twoWorms,
      [&](std::shared_ptr<WormSettings> const& ws) {
        ar.u32(S(controller));
        if (ar.in)
          ws->controller = limit<0, 3>(ws->controller);
        ar.arr("color", ws->rgb, [&](int& c) {
          ar.i32(0, c);
          if (ar.in)
            c &= 63;
        });
        ar.arr("weapons", ws->weapons, [&](uint32_t& w) { ar.u32(0, w); });
        ar.i32(S(health));

        if (ws->randomName && ar.out) {
          std::string empty;
          ar.str("name", empty);
        } else {
          ar.str(S(name));
          // TODO: Random generation?
        }

        ar.arr("controls", ws->controlsEx, [&](uint32_t& c) { ar.u32(0, c); });

        ar.u32(S(inputDevice));
        ar.str(S(gamepadName));
        ar.str(S(gamepadSerial));
        ar.arr("gamepadControls", ws->gamepadControls,
               [&](uint32_t& c) { ar.u32(0, c); });
      });
  if (ar.in) {
    settings.wormSettings[0] = twoWorms[0];
    settings.wormSettings[1] = twoWorms[1];
  }

  // Serialize network player as a separate [netPlayer] object
  {
    auto& ws = settings.wormSettings[Settings::NetworkPlayerIdx];
    ar.obj("netPlayer", [&] {
      ar.u32("controller", ws->controller);
      if (ar.in)
        ws->controller = limit<0, 3>(ws->controller);
      ar.arr("color", ws->rgb, [&](int& c) {
        ar.i32(0, c);
        if (ar.in)
          c &= 63;
      });
      ar.arr("weapons", ws->weapons, [&](uint32_t& w) { ar.u32(0, w); });
      ar.i32("health", ws->health);

      if (ws->randomName && ar.out) {
        std::string empty;
        ar.str("name", empty);
      } else {
        ar.str("name", ws->name);
      }

      ar.arr("controls", ws->controlsEx, [&](uint32_t& c) { ar.u32(0, c); });
      ar.u32("inputDevice", ws->inputDevice);
      ar.str("gamepadName", ws->gamepadName);
      ar.str("gamepadSerial", ws->gamepadSerial);
      ar.arr("gamepadControls", ws->gamepadControls,
             [&](uint32_t& c) { ar.u32(0, c); });
    });
  }

#undef S
}
