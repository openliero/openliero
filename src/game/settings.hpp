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

struct Settings : gvl::shared, Extensions {
  enum GameMode {
    KillEmAll,
    GameOfTag,
    Holdazone,
    ScalesOfJustice,
    MaxGameModes
  };

  static int const selectableWeapons = 5;
  static int const zoneCaptureTime = 70;

  static int const wormAnimTab[];

  Settings();

  bool load(FsNode node);
  bool loadLegacy(FsNode node);
  void save(FsNode node);
  gvl::gash::value_type& updateHash();

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

  std::shared_ptr<WormSettings> wormSettings[2];

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

template <typename Archive>
void archive_liero(Archive ar, Settings& settings) {
  ar.ui8(settings.maxBonuses)
      .ui16_le(settings.loadingTime)
      .ui16_le(settings.lives)
      .ui16_le(settings.timeToLose)
      .ui16_le(settings.flagsToWin)
      .b(settings.screenSync)
      .b(settings.map)
      .ui8(settings.wormSettings[0]->controller)
      .ui8(settings.wormSettings[1]->controller)
      .b(settings.randomLevel)
      .ui16_le(settings.blood)
      .ui8(settings.gameMode)
      .b(settings.namesOnBonuses)
      .b(settings.regenerateLevel)
      .b(settings.shadow);

  if (ar.in) {
    settings.wormSettings[0]->controller %= 3;
    settings.wormSettings[1]->controller %= 3;
  }

  for (int i = 0; i < 40; ++i) {
    ar.ui8(settings.weapTable[i]);
    if (ar.in)
      settings.weapTable[i] = limit<0, 3>(settings.weapTable[i]);
  }

  for (int i = 0; i < 2; ++i)
    for (int j = 0; j < 3; ++j) {
      ar.ui8(settings.wormSettings[i]->rgb[j]);
      if (ar.in)
        settings.wormSettings[i]->rgb[j] &= 63;
    }

  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 5; ++j) {
      ar.ui8(settings.wormSettings[i]->weapons[j]);
    }
  }

  ar.ui16_le(settings.wormSettings[0]->health);
  ar.ui16_le(settings.wormSettings[1]->health);

  for (int i = 0; i < 2; ++i) {
    if (settings.wormSettings[i]->randomName && ar.out) {
      std::string empty;
      ar.pascal_str(empty, 21);
    } else {
      ar.pascal_str(settings.wormSettings[i]->name, 21);
      if (ar.in) {
        if (settings.wormSettings[i]->name.empty())
          settings.wormSettings[i]->randomName = true;
        else
          settings.wormSettings[i]->randomName = false;
      }
    }
  }

  ar.b(settings.loadChange);

  char lieroStr[] = "\x05LIERO\0\0";
  for (std::size_t i = 0; i < sizeof(lieroStr); ++i) {
    ar.ui8(lieroStr[i]);
  }

  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 7; ++j) {
      uint32_t v;
      if (ar.out) {
        v = settings.wormSettings[i]->controls[j];
        v = limit<0, 177>(v);
      }
      ar.ui8(v);
      if (ar.in) {
        v = limit<0, 177>(v);
        settings.wormSettings[i]->controls[j] = v;
      }
    }
  }

  ar.pascal_str(settings.levelFile, 9);

  // TODO: Slightly bad way to detect whether extensions exist, no?
  try {
    // Extensions
    int fileExtensionVersion = myGameVersion;
    ar.ui8(fileExtensionVersion);

    bool extDummy = true;
    uint8_t extDummy8 = 0;
    uint32_t extDummy16 = 0;
    ar.b(extDummy);
    ar.b(settings.recordReplays);
    ar.b(settings.loadPowerlevelPalette);
    ar.ui8(extDummy8);
    ar.ui16(extDummy16);  // old fullscreenW
    ar.ui16(extDummy16);  // old fullscreenH

    if (fileExtensionVersion >= 4)
      ar.str(settings.levelFile);

    if (fileExtensionVersion >= 2)
      ar.b(extDummy);

    for (int i = 0; i < 2; ++i) {
      for (int c = 0; c < static_cast<int>(WormSettings::Control::MaxControl);
           ++c) {
        int dummy = 0;
        gvl::enable_when(ar, fileExtensionVersion >= 2)
            .ui8(dummy, 255)
            .ui8(dummy, 255);
      }
    }

    for (int i = 0; i < 2; ++i) {
      WormSettings& ws = *settings.wormSettings[i];
      for (int c = 0; c < static_cast<int>(WormSettings::Control::MaxControlEx);
           ++c) {
        gvl::enable_when(ar, fileExtensionVersion >= 3)
            .ui32(ws.controlsEx[c], ws.controls[c]);
      }
    }

    gvl::enable_when(ar, fileExtensionVersion >= 4)
        .ui16(settings.aiMutations, 2)
        .ui16(settings.aiFrames, 140)
        .ui8(settings.selectBotWeapons, uint32_t(0))
        .ui16(settings.zoneTimeout, 30);

    gvl::enable_when(ar, fileExtensionVersion >= 5)
        .b(settings.aiTraces, false)
        .ui16(settings.aiParallels, 3);

    gvl::enable_when(ar, fileExtensionVersion >= 6)
        .b(settings.allowViewingSpawnPoint, false);

    gvl::enable_when(ar, fileExtensionVersion >= 7)
        .b(settings.singleScreenReplay, false);

    gvl::enable_when(ar, fileExtensionVersion >= 8)
        .b(settings.spectatorWindow, false);

    gvl::enable_when(ar, fileExtensionVersion >= 9)
        .b(settings.fullscreen, false);

    gvl::enable_when(ar, fileExtensionVersion >= 10)
        .str(settings.tc, std::string("openliero"));
  } catch (std::runtime_error&) {
    // Reset to default state
    settings.Extensions::operator=(Extensions());

    for (int i = 0; i < 2; ++i) {
      WormSettings& ws = *settings.wormSettings[i];
      ws.WormSettingsExtensions::operator=(WormSettingsExtensions());
    }
  }
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

#undef S

  ar.arr("weapTable", settings.weapTable, [&](uint32_t& v) {
    ar.u32(0, v);
    if (ar.in)
      v = limit<0, 3>(v);
  });

#define S(n) #n, ws->n

  ar.array_obj(
      "worms", settings.wormSettings,
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
      });

#undef S
}
