// Rollback Step 4 — RollbackController in lockstep mode must produce
// frame-by-frame identical state to NetworkController given identical
// inputs, no jitter, no loss. Two parallel loopback fixtures (one running
// NetworkController × NetworkController, one running RollbackController ×
// RollbackController) are stepped in lockstep with the same scripted
// input sequence; their fastGameChecksum values are compared every frame.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <queue>
#include <type_traits>

#include "controller/networkController.hpp"
#include "controller/rollbackController.hpp"
#include "game.hpp"
#include "math.hpp"
#include "mixer/player.hpp"
#include "rollback/buffer.hpp"

namespace {

template <typename Ctrl>
struct Loopback {
  std::shared_ptr<Common> common;
  std::shared_ptr<Settings> settings;
  std::unique_ptr<Ctrl> a;
  std::unique_ptr<Ctrl> b;
  std::queue<std::pair<uint32_t, uint8_t>> aToB, bToA;

  Loopback(std::shared_ptr<Common> commonIn,
           std::shared_ptr<Settings> settingsIn,
           uint32_t seed)
      : common(std::move(commonIn))
      , settings(std::move(settingsIn))
  {
    a = std::make_unique<Ctrl>(common, settings, 0);
    b = std::make_unique<Ctrl>(common, settings, 1);
    a->setSkipWeaponSelection(true);
    b->setSkipWeaponSelection(true);
    a->game.rand.seed(seed);
    b->game.rand.seed(seed);
    auto popFront = [](std::queue<std::pair<uint32_t, uint8_t>>& q,
                       uint32_t frame) -> int {
      if (!q.empty() && q.front().first == frame) {
        int v = q.front().second;
        q.pop();
        return v;
      }
      return -1;
    };
    if constexpr (std::is_same_v<Ctrl, RollbackController>) {
      a->setInputCallbacks(
          [this](uint8_t /*gen*/, uint32_t bf, uint8_t c, uint8_t const* in, uint32_t /*lf*/) {
            for (uint8_t i = 0; i < c; ++i) aToB.push({bf + i, in[i]});
          },
          [this, popFront](uint32_t frame) { return popFront(bToA, frame); });
      b->setInputCallbacks(
          [this](uint8_t /*gen*/, uint32_t bf, uint8_t c, uint8_t const* in, uint32_t /*lf*/) {
            for (uint8_t i = 0; i < c; ++i) bToA.push({bf + i, in[i]});
          },
          [this, popFront](uint32_t frame) { return popFront(aToB, frame); });
    } else {
      a->setInputCallbacks(
          [this](uint32_t frame, uint8_t input) { aToB.push({frame, input}); },
          [this, popFront](uint32_t frame) { return popFront(bToA, frame); });
      b->setInputCallbacks(
          [this](uint32_t frame, uint8_t input) { bToA.push({frame, input}); },
          [this, popFront](uint32_t frame) { return popFront(aToB, frame); });
    }
    a->focus();
    b->focus();
  }

  void deliverAll() {
    while (!aToB.empty()) {
      auto [frame, input] = aToB.front();
      aToB.pop();
      b->injectRemoteInput(frame, input);
    }
    while (!bToA.empty()) {
      auto [frame, input] = bToA.front();
      bToA.pop();
      a->injectRemoteInput(frame, input);
    }
  }
};

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

TEST_CASE("RollbackController lockstep parity vs NetworkController",
          "[rollback][parity]") {
  constexpr uint32_t kSeed = 42;
  constexpr int kNumTicks = 1000;

  auto [commonN, settingsN] = makeEnv();
  auto [commonR, settingsR] = makeEnv();

  Loopback<NetworkController> net(commonN, settingsN, kSeed);
  Loopback<RollbackController> rb(commonR, settingsR, kSeed);

  // Pre-seed the 3-frame input delay window.
  for (uint32_t i = 0; i < 3; ++i) {
    net.a->injectRemoteInput(i, 0);
    net.b->injectRemoteInput(i, 0);
    rb.a->injectRemoteInput(i, 0);
    rb.b->injectRemoteInput(i, 0);
  }

  Rand inputRng(0xC0FFEE);

  for (int tick = 0; tick < kNumTicks; ++tick) {
    uint8_t inputA = inputRng() & 0x7f;
    uint8_t inputB = inputRng() & 0x7f;
    // Bias toward combat to exercise the sound/stats/damage paths.
    if ((inputRng() % 10) < 6) inputA |= (1 << Worm::Fire);
    if ((inputRng() % 10) < 6) inputB |= (1 << Worm::Fire);

    net.a->setLocalControlState(inputA);
    net.b->setLocalControlState(inputB);
    rb.a->setLocalControlState(inputA);
    rb.b->setLocalControlState(inputB);

    net.a->process();
    net.b->process();
    rb.a->process();
    rb.b->process();

    net.deliverAll();
    rb.deliverAll();

    REQUIRE(net.a->currentFrame() == rb.a->currentFrame());
    REQUIRE(net.b->currentFrame() == rb.b->currentFrame());

    uint32_t frame = rb.a->currentFrame();
    if (frame > 0 && frame == rb.b->currentFrame()) {
      uint32_t cNetA = fastGameChecksum(net.a->game);
      uint32_t cNetB = fastGameChecksum(net.b->game);
      uint32_t cRbA  = fastGameChecksum(rb.a->game);
      uint32_t cRbB  = fastGameChecksum(rb.b->game);
      INFO("tick=" << tick << " frame=" << frame);
      REQUIRE(cNetA == cNetB);
      REQUIRE(cRbA == cRbB);
      REQUIRE(cNetA == cRbA);
    }

    if (rb.a->game.isGameOver())
      break;
  }
}

TEST_CASE("RollbackController writes confirmed snapshots into the ring buffer",
          "[rollback][parity]") {
  auto [common, settings] = makeEnv();
  Loopback<RollbackController> rb(common, settings, 99);

  for (uint32_t i = 0; i < 3; ++i) {
    rb.a->injectRemoteInput(i, 0);
    rb.b->injectRemoteInput(i, 0);
  }

  // Advance enough that the ring has wrapped at least once.
  constexpr int kTicks = 64;
  for (int tick = 0; tick < kTicks; ++tick) {
    rb.a->process();
    rb.b->process();
    rb.deliverAll();
  }

  auto const& buf = rb.a->rollbackBuffer();
  REQUIRE(!buf.empty());
  // After N confirmed frames the newest slot is frame N-1.
  int frame = static_cast<int>(rb.a->currentFrame()) - 1;
  REQUIRE(buf.newestFrame() == frame);

  // Every resident slot should be Confirmed in lockstep mode.
  for (int f = buf.oldestFrame(); f <= buf.newestFrame(); ++f) {
    auto* slot = const_cast<rollback::RollbackBuffer&>(buf).find(f);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->remoteState == rollback::RemoteState::Confirmed);
  }
}
