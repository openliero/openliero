// Rollback Step 14 Task 14.2 — generation drop.
//
// A controller that has crossed the WS→game generation bump must ignore
// any batch carrying the old (pre-transition) generation. The frame
// numbers in such a batch belong to the abandoned simFrame numbering
// and, if accepted, would corrupt a live game-phase slot — the ring
// buffer reuses the same frame-modulo slots after the reset, so an
// inbound stale frame N would re-mark a freshly written slot N as
// having a real remote input from the old phase.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>

#include "controller/rollbackController.hpp"
#include "game.hpp"
#include "math.hpp"

namespace {

std::pair<std::shared_ptr<Common>, std::shared_ptr<Settings>> makeEnv() {
  precomputeTables();
  auto common = std::make_shared<Common>();
  FsNode tcRoot(FsNode("data") / "TC" / "openliero");
  common->load(std::move(tcRoot));
  auto settings = std::make_shared<Settings>();
  settings->lives = 10;
  settings->loadingTime = 0;
  settings->loadChange = true;
  settings->randomLevel = true;
  settings->gameMode = Settings::GMKillEmAll;
  return {common, settings};
}

}  // namespace

TEST_CASE("Rollback controller drops batches from an older generation",
          "[rollback][generation]") {
  auto [common, settings] = makeEnv();
  RollbackController a(common, settings, 0);
  a.setSkipWeaponSelection(true);
  a.game.rand.seed(0xC0FFEE);
  a.focus();

  // Simulate having crossed the WS→game generation bump. Task 14.4 will
  // wire the real bump into finishWeaponSelect; for this task the
  // controller is just put into the post-transition state directly.
  a.setGenerationForTest(1);
  REQUIRE(a.generation() == 1);
  REQUIRE(a.droppedOldGenerationBatches() == 0);

  // Build a stale-generation batch covering frames [0, 7]. With kInputDelay
  // = 1 (rollback default), confirmedFrame() starts at -1 and the ring
  // slots for frames 0..7 are the ones the live game phase is about to
  // use — accepting these would have remoteInputReady[i] = true with a
  // value picked from a previous phase.
  uint8_t inputs[8] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8};

  SECTION("older generation is dropped") {
    a.injectRemoteBatch(/*generation=*/0, /*baseFrame=*/0, /*count=*/8,
                        inputs, /*remoteLocalFrame=*/3);

    REQUIRE(a.droppedOldGenerationBatches() == 1);
    // lastKnownRemoteFrame_ stays at its sentinel — a dropped packet
    // must not advance the frame-advantage estimate either.
    REQUIRE(a.lastKnownRemoteFrame() == -1);
    // confirmedFrame is unchanged.
    REQUIRE(a.confirmedFrame() == -1);
  }

  SECTION("matching generation is accepted") {
    a.injectRemoteBatch(/*generation=*/1, /*baseFrame=*/0, /*count=*/8,
                        inputs, /*remoteLocalFrame=*/3);

    REQUIRE(a.droppedOldGenerationBatches() == 0);
    REQUIRE(a.lastKnownRemoteFrame() == 3);
  }

  SECTION("future generation is dropped (no buffering yet — Task 14.5)") {
    // Task 14.5 will decide whether to buffer or drop future-gen
    // packets. Task 14.2 keeps the simple symmetric drop so neither
    // direction can corrupt the live ring.
    a.injectRemoteBatch(/*generation=*/2, /*baseFrame=*/0, /*count=*/8,
                        inputs, /*remoteLocalFrame=*/3);

    REQUIRE(a.droppedOldGenerationBatches() == 1);
    REQUIRE(a.lastKnownRemoteFrame() == -1);
  }
}
