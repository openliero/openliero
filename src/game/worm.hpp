#pragma once

#include <cstring>
#include <functional>
#include <gvl/crypt/gash.hpp>
#include <gvl/serialization/archive.hpp>  // For gvl::enable_when
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include "filesystem.hpp"
#include "math.hpp"
#include "version.hpp"

#define NUM_WEAPONS 5

struct Worm;
struct Game;
struct Weapon;

struct NinjaRope {
  NinjaRope() : out(false), attached(false), anchor(0) {}
  /*
   * Has the ninja rope been deployed?
   */
  bool out;
  /*
   * Is the ninja rope attached to something?
   */
  bool attached;
  /*
   * If non-zero, the Worm that the ninja rope is attached to.
   */
  Worm* anchor;
  /*
   * Position of the ninja rope.
   */
  fixedvec pos;
  /*
   * Velocity of the ninja rope.
   */
  fixedvec vel;
  /*
   * Length of the ninja rope.
   */
  int length;
  /*
   * Current length of the ninja rope.
   */
  int curLen;
  /*
   * Processing function.
   */
  void process(Worm& owner, Game& game);
};

struct WormWeapon {
  WormWeapon() : ammo(0), delayLeft(0), loadingLeft(0) {}
  /*
   * Is the weapon available to use?
   */
  bool available() const { return loadingLeft == 0; }

  // int id;
  /*
   * Type of weapon.
   */
  Weapon const* type;
  /*
   * How much ammo is left.
   */
  int ammo;
  /*
   * How much time is left before weapon can be fired again.
   */
  int delayLeft;
  /* How much time is left before weapon is reloaded.
   */
  int loadingLeft;
};

struct WormSettingsExtensions {
  enum Control {
    Up,
    Down,
    Left,
    Right,
    Fire,
    Change,
    Jump,
    Dig,
    MaxControl = Dig,
    MaxControlEx
  };

  WormSettingsExtensions() { std::memset(controlsEx, 0, sizeof(controlsEx)); }

  uint32_t controlsEx[MaxControlEx];
};

struct WormSettings : gvl::shared, WormSettingsExtensions {
  WormSettings() : health(100), controller(0), randomName(true), color(0) {
    rgb[0] = 26;
    rgb[1] = 26;
    rgb[2] = 62;

    for (int i = 0; i < NUM_WEAPONS; ++i) {
      weapons[i] = 1;
    }
    std::memset(controls, 0, sizeof(controls));
  }

  gvl::gash::value_type& updateHash();

  void saveProfile(FsNode node);
  void loadProfile(FsNode node);

  int health;
  uint32_t controller;  // CPU / Human
  uint32_t controls[MaxControl];
  uint32_t weapons[NUM_WEAPONS];  // TODO: Adjustable
  std::string name;
  int rgb[3];
  bool randomName;

  int color;

  FsNode profileNode;

  gvl::gash::value_type hash;
};

template <typename Archive>
void archive(Archive ar, WormSettings& ws) {
  ar.ui32(ws.color).ui32(ws.health).ui16(ws.controller);
  for (int i = 0; i < WormSettings::MaxControl; ++i)
    ar.ui16(ws.controls[i]);  // TODO: Initialize controlsEx from this earlier
  for (int i = 0; i < NUM_WEAPONS; ++i)
    ar.ui16(ws.weapons[i]);
  for (int i = 0; i < 3; ++i)
    ar.ui16(ws.rgb[i]);
  ar.b(ws.randomName);
  ar.str(ws.name);
  if (ar.context.replayVersion <= 1) {
    ws.WormSettingsExtensions::operator=(WormSettingsExtensions());
    return;
  }

  int wsVersion = myGameVersion;
  ar.ui8(wsVersion);

  for (int c = 0; c < WormSettings::MaxControl; ++c) {
    int dummy = 0;
    gvl::enable_when(ar, wsVersion >= 2).ui8(dummy, 255).ui8(dummy, 255);
  }

  for (int c = 0; c < WormSettings::MaxControlEx; ++c) {
    gvl::enable_when(ar, wsVersion >= 3).ui32(ws.controlsEx[c], ws.controls[c]);
  }
}

struct Renderer;

struct WormAI : gvl::shared {
  virtual void process(Game& game, Worm& worm) = 0;

  virtual void drawDebug(
      Game& game,
      Worm const& worm,
      Renderer& renderer,
      int offsX,
      int offsY) {}
};

struct DumbLieroAI : WormAI {
  void process(Game& game, Worm& worm);

  std::mt19937 rand;
};

struct Worm : gvl::shared {
  enum { RFDown, RFLeft, RFUp, RFRight };

  enum Control {
    Up = WormSettings::Up,
    Down = WormSettings::Down,
    Left = WormSettings::Left,
    Right = WormSettings::Right,
    Fire = WormSettings::Fire,
    Change = WormSettings::Change,
    Jump = WormSettings::Jump,
    MaxControl
  };

  struct ControlState {
    ControlState() : istate(0) {}

    bool operator==(ControlState const& b) const { return istate == b.istate; }

    uint32_t pack() const {
      return istate;  // & ((1 << MaxControl)-1);
    }

    void unpack(uint32_t state) { istate = state & 0x7f; }

    bool operator!=(ControlState const& b) const { return !operator==(b); }

    bool operator[](std::size_t n) const { return ((istate >> n) & 1) != 0; }

    void set(std::size_t n, bool v) {
      if (v)
        istate |= 1 << n;
      else
        istate &= ~(uint32_t(1u) << n);
    }

    void toggle(std::size_t n) { istate ^= 1 << n; }

    uint32_t istate;
  };

  Worm()
      : pos(),
        vel(),
        hotspotX(0),
        hotspotY(0),
        aimingAngle(0),
        aimingSpeed(0),
        ableToJump(false),
        ableToDig(false),
        keyChangePressed(false),
        movable(false),
        animate(false),
        visible(false),
        ready(false),
        flag(false),
        makeSightGreen(false),
        health(0),
        lives(0),
        kills(0),
        timer(0),
        killedTimer(0),
        currentFrame(0),
        flags(0),
        currentWeapon(0),
        lastKilledByIdx(-1),
        fireCone(0),
        leaveShellTimer(0),
        index(0),
        direction(0),
        steerableCount(0),
        statsX(0) {
    makeSightGreen = false;

    ready = true;
    movable = true;

    visible = false;
    killedTimer = 150;
  }

  bool pressed(Control control) const { return controlStates[control]; }

  bool pressedOnce(Control control) {
    bool state = controlStates[control];
    controlStates.set(control, false);
    return state;
  }

  void release(Control control) { controlStates.set(control, false); }

  void press(Control control) { controlStates.set(control, true); }

  void setControlState(Control control, bool state) {
    controlStates.set(control, state);
  }

  void toggleControlState(Control control) {
    controlStates.set(control, !controlStates[control]);
  }

  int minimapColor() const { return 129 + index * 4; }

  void beginRespawn(Game& game);
  void doRespawning(Game& game);
  void process(Game& game);
  void processWeapons(Game& game);
  void processPhysics(Game& game);
  void processMovement(Game& game);
  void processTasks(Game& game);
  void processAiming(Game& game);
  void processWeaponChange(Game& game);
  void processSteerables(Game& game);
  void fire(Game& game);
  void processSight(Game& game);
  void calculateReactionForce(Game& game, int newX, int newY, int dir);
  void initWeapons(Game& game);
  /*
   * Angle Frame? Quite possibly the angle that the worm is aiming at.
   * TODO: Add documentation.
   */
  int angleFrame() const;
  /*
   * Position of the Worm.
   */
  fixedvec pos;
  /*
   * Velocity of the Worm.
   */
  fixedvec vel;
  /*
   * Coordinates to determine location of respawn.
   */
  gvl::ivec2 logicRespawn;
  /*
   * Hotspots for laser, laser sight, etc.
   */
  int hotspotX, hotspotY;
  /*
   * Aiming variables?
   * TODO: Add documentation.
   */
  fixed aimingAngle, aimingSpeed;
  /*
   * The previous state of some keys.
   */
  bool ableToJump, ableToDig;
  /*
   * TODO: Add documentation.
   */
  bool keyChangePressed;
  /*
   * Whether or not the Worm is movable. The Worm cannot be moved when they are
   * operating a Steerable weapon.
   */
  bool movable;
  /*
   * Whether or not the Worm can be animated.
   */
  bool animate;
  /*
   * Whether or not the Worm is visible.
   */
  bool visible;
  /*
   * Whether or not the Worm is ready to play.
   */
  bool ready;
  /*
   * Whether or not the Worm possesses a flag.
   */
  bool flag;
  /*
   * Whether or not to make the sight green.
   */
  bool makeSightGreen;
  /*
   * Health remaining for this Worm.
   */
  int health;
  /*
   * Lives remaining for this Worm.
   */
  int lives;
  /*
   * Kills performed by this Worm.
   */
  int kills;
  /*
   * Timer for Game of Tag.
   */
  int timer;
  /*
   * Time until this Worm can respawn.
   */
  int killedTimer;
  /*
   * The current animation frame for this Worm.
   */
  int currentFrame;
  /*
   *How many flags does this worm have?
   */
  int flags;
  /*
   * This Worm's ninja rope.
   */
  NinjaRope ninjarope;
  /*
   * The currently selected weapon.
   */
  int currentWeapon;
  /*
   * Which Worm was the last one that killed this Worm.
   */
  int lastKilledByIdx;
  /*
   *How much is left of the firecone.
   */
  int fireCone;
  /*
   * Time until next shell drop
   */
  int leaveShellTimer;
  /*
   * Settings for this Worm.
   */
  std::shared_ptr<WormSettings> settings;
  /*
   * Index of this Worm (0 or 1).
   */
  int index;
  /*
   * Some AI stuff?
   * TODO: Add more documentation.
   */
  std::shared_ptr<WormAI> ai;
  /*
   * Reaction forces in all 4 directions.
   * TODO: Add more documentation.
   */
  int reacts[4];
  /*
   * Weapons that the Worm currently has.
   */
  WormWeapon weapons[NUM_WEAPONS];
  /*
   * What direction the Worm is facing.
   * 0 = Left, 1 = Right.
   */
  int direction;
  /*
   * Current control states.
   */
  ControlState controlStates;
  /*
   * Previous control states.
   */
  ControlState prevControlStates;
  /*
   * Temporary state for steerables
   */
  int steerableSumX, steerableSumY, steerableCount;
  /*
   * Which X coordinate to display stats at for this Worm.
   */
  int statsX;
  /*
   * This contains the real state of real & extended controls.
   */
  ControlState cleanControlStates;
};

bool checkForWormHit(Game& game, int x, int y, int dist, Worm* ownWorm);
bool checkForSpecWormHit(Game& game, int x, int y, int dist, Worm& w);
int sqrVectorLength(int x, int y);
