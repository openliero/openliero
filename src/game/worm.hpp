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
  enum class Control {
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

  uint32_t controlsEx[static_cast<int>(
      WormSettingsExtensions::Control::MaxControlEx)];
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
  uint32_t controls[static_cast<int>(WormSettings::Control::MaxControl)];
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
  for (int i = 0; i < static_cast<int>(WormSettings::Control::MaxControl); ++i)
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

  for (int c = 0; c < static_cast<int>(WormSettings::Control::MaxControl);
       ++c) {
    int dummy = 0;
    gvl::enable_when(ar, wsVersion >= 2).ui8(dummy, 255).ui8(dummy, 255);
  }

  for (int c = 0; c < static_cast<int>(WormSettings::Control::MaxControlEx);
       ++c) {
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
  void process(Game& game, Worm& worm) override;

  std::mt19937 rand;
};

struct Worm : gvl::shared {
  /*
   * Enumerations for Worm Reaction Force.
   */
  enum class ReactionForce { Down, Left, Up, Right };
  /*
   * Possible enumerations for Worm Control.
   */
  enum class Control {
    Up = static_cast<int>(WormSettings::Control::Up),
    Down = static_cast<int>(WormSettings::Control::Down),
    Left = static_cast<int>(WormSettings::Control::Left),
    Right = static_cast<int>(WormSettings::Control::Right),
    Fire = static_cast<int>(WormSettings::Control::Fire),
    Change = static_cast<int>(WormSettings::Control::Change),
    Jump = static_cast<int>(WormSettings::Control::Jump),
    MaxControl
  };
  /*
   * Enumerations for direction the Worm is facing.
   */
  enum class Direction { Left, Right };
  /*
   * Control state for Worm.
   */
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
        movable(true),
        animate(false),
        visible(false),
        ready(true),
        flag(false),
        makeSightGreen(false),
        health(0),
        lives(0),
        kills(0),
        timer(0),
        killedTimer(150),
        currentFrame(0),
        flags(0),
        currentWeapon(0),
        lastKilledByIdx(-1),
        fireCone(0),
        leaveShellTimer(0),
        index(0),
        direction(0),
        steerableCount(0),
        statsX(0) {}
  /*
   * Get control state of pressed key-press.
   * TODO: Add documentation.
   */
  bool pressed(Control control) const {
    return controlStates[static_cast<int>(control)];
  }
  /*
   * Set pressed-once key-press control state.
   * TODO: Add documentation.
   */
  bool pressedOnce(Control control) {
    bool state = controlStates[static_cast<int>(control)];
    controlStates.set(static_cast<int>(control), false);
    return state;
  }
  /*
   * Release key-press control state.
   * TODO: Add documentation.
   */
  void release(Control control) {
    controlStates.set(static_cast<int>(control), false);
  }
  /*
   * Set key-press control state.
   * TODO: Add documentation.
   */
  void press(Control control) {
    controlStates.set(static_cast<int>(control), true);
  }
  /*
   * Set control states.
   */
  void setControlState(Control control, bool state) {
    controlStates.set(static_cast<int>(control), state);
  }
  /*
   * Toggle control states.
   */
  void toggleControlState(Control control) {
    controlStates.set(
        static_cast<int>(control), !controlStates[static_cast<int>(control)]);
  }
  /*
   * Get minimap colour for Worm.
   */
  int minimapColor() const { return 129 + index * 4; }
  /*
   * Start the respawning process for Worm.
   */
  void beginRespawn(Game& game);
  /*
   * Do respawning processing for Worm.
   */
  void doRespawning(Game& game);
  /*
   * General process() function for Worm.
   * TODO: Add documentation.
   */
  void process(Game& game);
  /*
   * Process weapons for Worm.
   */
  void processWeapons(Game& game);
  /*
   * Process physics for Worm.
   */
  void processPhysics(Game& game);
  /*
   * Process movement for Worm.
   */
  void processMovement(Game& game);
  /*
   * Process a bunch of chores, homework, tasks and other assorted things for
   * Worm.
   */
  void processTasks(Game& game);
  /*
   * Process aiming for Worm.
   */
  void processAiming(const Game& game);
  /*
   * Process change of Weapon for Worm.
   */
  void processWeaponChange(Game& game);
  /*
   * Process Steerable ShotType for Worm.
   */
  void processSteerables(Game& game);
  /*
   * Calculate shot for Worm.
   */
  void fire(Game& game);
  /*
   * Calculate whether or not the Worm sight goes green.
   */
  void processSight(Game& game);
  /*
   * Calculate Reaction Force.
   * TODO: Add documentation.
   */
  void calculateReactionForce(Game& game, int newX, int newY, int dir);
  /*
   * Initialize weapons.
   * TODO: Add documentation.
   */
  void initWeapons(Game& game);
  /*
   * Angle Frame? Quite possibly the angle that the Worm is aiming at.
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
