#pragma once

#include <string>
#include "constants.hpp"
#include "gfx/font.hpp"
#include "gfx/palette.hpp"
#include "gfx/sprite.hpp"
#include "material.hpp"
#include "mixer/mixer.hpp"
#include "nobject.hpp"
#include "settings.hpp"
#include "sobject.hpp"
#include "weapon.hpp"

#define NUM_AIPARAMS_KEYS 7
#define NUM_AIPARAMS_VALUES 2
#define MAX_MATERIALS 256
#define NUM_TEXTURES 9
#define NUM_BONUS_SOBJECTS 2
#define NUM_BONUS_TIMER_VALUES 2
#define NUM_COLOR_ANIM 4
#define FIRE_CONE_OFFSET_DIRECTION 2
#define FIRE_CONE_OFFSET_ANGLE_FRAME 7
#define FIRE_CONE_OFFSET_XY 2
#define NUM_WEAPON_STATES 3
#define NUM_GAME_MODES 4
#define NUM_ON_OFF 2

extern int stoneTab[3][4];

/* Textures sourced from [[constants.textures]] in tc.cfg */
struct Texture {
  bool nDrawBack;  // 1C208
  int mFrame;      // 1C1EA
  int sFrame;      // 1C1F4
  int rFrame;      // 1C1FE
};

struct Texts {
  Texts();

  std::string gameModes[Settings::GameMode::MaxGameModes];
  std::string onoff[NUM_ON_OFF];
  std::string controllers[3];

  static char const* keyNames[177];

  std::string weapStates[NUM_WEAPON_STATES];

  int copyrightBarFormat;
};

/* Colour animations sourced from [[constants.colorAnim]] in tc.cfg */
struct ColourAnim {
  int from;
  int to;
};

/* AI parameters sourced from [[constants.aiparams]] in tc.cfg */
struct AIParams {
  int k[NUM_AIPARAMS_VALUES][NUM_AIPARAMS_KEYS];  // 0x1AEEE, contiguous words
};

struct SfxSample : gvl::noncopyable {
  // SfxSample(SfxSample const&) = delete;
  // SfxSample& operator=(SfxSample const&) = delete;

  SfxSample() : sound(0) {}

  SfxSample(SfxSample&& other)
      : name(std::move(other.name)),
        sound(other.sound),
        originalData(std::move(other.originalData)) {
    other.sound = 0;
  }

  SfxSample& operator=(SfxSample&& other) {
    name = std::move(other.name);
    sound = other.sound;
    sound = 0;
    originalData = std::move(other.originalData);
    return *this;
  }

  SfxSample(std::string name, int length)
      : name(std::move(name)), originalData(length) {
    sound = sfx_new_sound(length * 2);
  }

  ~SfxSample() {
    if (sound)
      sfx_free_sound(sound);
  }

  void createSound();

  std::string name;
  sfx_sound* sound;
  std::vector<uint8_t> originalData;
};

struct Bitmap;
struct FsNode;

using std::vector;

struct Common {
  Common();

  ~Common() {}

  static int fireConeOffset[FIRE_CONE_OFFSET_DIRECTION]
                           [FIRE_CONE_OFFSET_ANGLE_FRAME][FIRE_CONE_OFFSET_XY];

  void load(FsNode node);
  void drawTextSmall(const Bitmap& scr, char const* str, int x, int y);
  void precompute();

  std::string guessName() const;

  PalIdx* wormSprite(int f, int dir, int w) {
    return wormSprites.spritePtr(f + dir * 7 * 3 + w * 2 * 7 * 3);
  }

  Sprite wormSpriteObj(int f, int dir, int w) {
    return wormSprites[f + dir * 7 * 3 + w * 2 * 7 * 3];
  }

  PalIdx* fireConeSprite(int f, int dir) {
    return fireConeSprites.spritePtr(f + dir * 7);
  }

  // Computed
  Texts texts;
  vector<int> weapOrder;
  SpriteSet wormSprites;
  SpriteSet fireConeSprites;

  Material materials[MAX_MATERIALS];
  Texture textures[NUM_TEXTURES];
  vector<Weapon> weapons;
  vector<SObjectType> sobjectTypes;
  vector<NObjectType> nobjectTypes;
  /* Randomized timer values for Bonus SObjects. Sourced from
   * [[constants.bonuses]] in tc.cfg (timer/timerV) */
  int bonusRandTimer[NUM_BONUS_SOBJECTS][NUM_BONUS_TIMER_VALUES];
  /* Bonus SObjects. Sourced from [[constants.bonuses]] in tc.cfg (sobj) */
  int bonusSObjects[NUM_BONUS_SOBJECTS];
  /* AI parameters. Sourced from [[constants.aiparams.$KEY]] in tc.cfg */
  AIParams aiParams;
  /* Colour Animations. Sourced from [[constants.colorAnim]] in tc.cfg (from/to)
   */
  ColourAnim colorAnim[NUM_COLOR_ANIM];
  /* Bonus frames. Sourced from [[constants.bonuses]] in tc.cfg (frame) */
  int bonusFrames[NUM_BONUS_SOBJECTS];
  // all sprite sets sourced from TC/$NAME/sprites

  SpriteSet smallSprites;
  SpriteSet largeSprites;
  SpriteSet textSprites;
  Palette exepal;
  Font font;
  vector<SfxSample> sounds;

  int32_t C[CONST_DEF_T::MaxC];
  std::string S[STRING_DEF_T::MaxS];
  bool H[HACK_DEF_T::MaxH];
};
