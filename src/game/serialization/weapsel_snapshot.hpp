#pragma once

// Rollback for the weapon-select phase.
//
// advanceWeaponSelection used to be strict lockstep — every tick waited for
// remote input before advancing. That made the phase as slow as the
// slowest peer + worst-case latency, baking a wall-clock offset into the
// game-phase transition. This snapshot captures the (small) mutable state
// the WeaponSelection state machine touches each frame, so the rollback
// controller can predict + resim weapon-select frames the same way it
// does game frames.
//
// The struct deliberately stays small: the menus, isReady flags, picked
// weapon IDs (= worm.settings->weapons[]), worm.controlStates, plus the
// controller's edge-detection / key-repeat state and game.rand (used by
// the "Randomize" option). Menu item display strings and
// worm.weapons[].type pointers are NOT stored — both are derivable from
// the weapon IDs on restore via Common::weapOrder / Common::weapons.

#include "../rand.hpp"
#include "../settings.hpp"

#include <array>
#include <cstdint>

struct WeaponSelectSnap {
  bool valid = false;
  bool wsDone = false;  // true if WeaponSelection::processFrame returned
                        // true at this frame (both peers pressed Done).

  struct PerPlayer {
    std::array<uint32_t, Settings::selectableWeapons> weapons{};
    bool isReady = false;
    int menuSelection = 0;
    int menuTopItem = 0;
    int menuBottomItem = 0;
    uint16_t wormControlStates = 0;
    int currentWeapon = 0;
  };
  std::array<PerPlayer, 2> players{};

  Rand rand;

  // Edge-detection / key-repeat state on the rollback controller.
  uint8_t localPrevInput = 0;
  uint8_t remotePrevInput = 0;
  std::array<uint16_t, 8> localHeldFrames{};
  std::array<uint16_t, 8> remoteHeldFrames{};
};
