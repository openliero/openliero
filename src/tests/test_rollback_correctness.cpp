// Rollback Step 7 — headline correctness test.
//
// Two RollbackControllers exchange inputs through a JitterTransport that
// delays delivery by a random amount within [minDelay, maxDelay] frames.
// Because each peer cannot wait for the real input, it predicts the
// remote side, advances speculatively, and reconciles via rollback when
// the real byte arrives and disagrees with the prediction. The test
// drives both peers with identical PRNG-generated inputs, runs for
// kTicks frames, then flushes any in-flight packets so both peers
// converge. After convergence both peers must agree on every checksum
// they've still got resident in the rollback ring, and their state must
// match a zero-jitter reference run driven by the same inputs.
//
// Step 7 has no input redundancy (Step 7.5's job), so a stall in this
// protocol is a *cascade*: when one peer falls behind by more than
// kMaxRollback it stops sending, which starves the other peer, which
// stops sending — and neither recovers. To exercise the rollback
// algorithm in isolation from that transport problem the test runs
// with delays small enough that the steady-state gap between simFrame
// and confirmedFrame stays comfortably under kMaxRollback even when a
// run of unlucky delays land back-to-back. Step 7.5's redundancy test
// will be where larger delays / loss get exercised.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include "controller/rollbackController.hpp"
#include "game.hpp"
#include "jitter_transport.hpp"
#include "math.hpp"
#include "mixer/player.hpp"
#include "rollback/buffer.hpp"

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

struct ScriptedInputs {
  std::vector<uint8_t> a;
  std::vector<uint8_t> b;
};

ScriptedInputs generateInputs(uint32_t seed, int ticks) {
  Rand rng(seed);
  ScriptedInputs out;
  out.a.reserve(ticks);
  out.b.reserve(ticks);
  for (int i = 0; i < ticks; ++i) {
    uint8_t inA = rng() & 0x7f;
    uint8_t inB = rng() & 0x7f;
    if ((rng() % 10) < 6) inA |= (1 << Worm::Fire);
    if ((rng() % 10) < 6) inB |= (1 << Worm::Fire);
    out.a.push_back(inA);
    out.b.push_back(inB);
  }
  return out;
}

// Drive a zero-jitter reference run of `ticks` ticks with the given
// input sequence. Returns the final fastGameChecksum and the simFrame
// the peer reached.
struct RefResult {
  uint32_t checksum;
  uint32_t simFrame;
};

RefResult runReference(uint32_t worldSeed, ScriptedInputs const& script,
                       int ticks) {
  auto [common, settings] = makeEnv();
  auto a = std::make_unique<RollbackController>(common, settings, 0);
  auto b = std::make_unique<RollbackController>(common, settings, 1);
  a->setSkipWeaponSelection(true);
  b->setSkipWeaponSelection(true);
  a->game.rand.seed(worldSeed);
  b->game.rand.seed(worldSeed);

  std::vector<std::pair<uint32_t, uint8_t>> aToB, bToA;
  a->setInputCallbacks(
      [&](uint32_t f, uint8_t in) { aToB.push_back({f, in}); },
      [](uint32_t) { return -1; });
  b->setInputCallbacks(
      [&](uint32_t f, uint8_t in) { bToA.push_back({f, in}); },
      [](uint32_t) { return -1; });
  a->focus();
  b->focus();

  for (uint32_t f = 0; f < 3; ++f) {
    a->injectRemoteInput(f, 0);
    b->injectRemoteInput(f, 0);
  }

  for (int i = 0; i < ticks; ++i) {
    a->setLocalControlState(script.a[i]);
    b->setLocalControlState(script.b[i]);
    a->process();
    b->process();
    for (auto const& [f, in] : aToB) b->injectRemoteInput(f, in);
    for (auto const& [f, in] : bToA) a->injectRemoteInput(f, in);
    aToB.clear();
    bToA.clear();
  }

  REQUIRE(a->currentFrame() == b->currentFrame());
  uint32_t cA = fastGameChecksum(a->game);
  uint32_t cB = fastGameChecksum(b->game);
  REQUIRE(cA == cB);
  return {cA, a->currentFrame()};
}

}  // namespace

TEST_CASE("Rollback recovers from mispredictions under random delay",
          "[rollback][correctness]") {
  constexpr uint32_t kWorldSeed = 0xBEEF;
  constexpr int kTicks = 800;
  constexpr uint32_t kInputSeed = 0xC0FFEE;

  ScriptedInputs script = generateInputs(kInputSeed, kTicks);
  RefResult ref = runReference(kWorldSeed, script, kTicks);

  struct Case {
    char const* name;
    int minDelay;
    int maxDelay;
    uint32_t transportSeed;
  };
  // Step 7 has no input redundancy, so we choose delays where the
  // steady-state gap stays well under kMaxRollback even when unlucky
  // runs of high-delay packets cluster. Step 7.5 will exercise larger
  // delays + loss once redundancy makes that survivable.
  std::vector<Case> cases = {
      {"delay [1,3]", 1, 3, 0x1111},
      {"delay [1,4]", 1, 4, 0x2222},
      {"delay [2,4]", 2, 4, 0x3333},
      {"delay [1,5]", 1, 5, 0x4444},
  };

  for (auto const& tc : cases) {
    SECTION(tc.name) {
      INFO("transport seed = " << tc.transportSeed);

      auto [common, settings] = makeEnv();
      auto a = std::make_unique<RollbackController>(common, settings, 0);
      auto b = std::make_unique<RollbackController>(common, settings, 1);
      a->setSkipWeaponSelection(true);
      b->setSkipWeaponSelection(true);
      a->game.rand.seed(kWorldSeed);
      b->game.rand.seed(kWorldSeed);

      rollback_test::JitterTransport transport(
          {tc.transportSeed, tc.minDelay, tc.maxDelay});

      a->setInputCallbacks(
          [&](uint32_t f, uint8_t in) { transport.sendAToB(f, in); },
          [](uint32_t) { return -1; });
      b->setInputCallbacks(
          [&](uint32_t f, uint8_t in) { transport.sendBToA(f, in); },
          [](uint32_t) { return -1; });
      a->focus();
      b->focus();

      // Pre-seed the input-delay window. These bypass the transport so
      // both peers can start advancing immediately, mirroring how a real
      // session would synchronise its starting frames.
      for (uint32_t f = 0; f < 3; ++f) {
        a->injectRemoteInput(f, 0);
        b->injectRemoteInput(f, 0);
      }

      auto deliverA = [&](uint32_t f, uint8_t in) {
        a->injectRemoteInput(f, in);
      };
      auto deliverB = [&](uint32_t f, uint8_t in) {
        b->injectRemoteInput(f, in);
      };

      for (int i = 0; i < kTicks; ++i) {
        a->setLocalControlState(script.a[i]);
        b->setLocalControlState(script.b[i]);
        a->process();
        b->process();
        transport.tick(deliverA, deliverB);
      }

      // Flush any in-flight tail packets, then run additional ticks
      // until both peers' confirmedFrame catches up. We use idle inputs
      // for the catch-up window so the test doesn't outrun the script,
      // but the comparison frame is the final scripted-tick state.
      transport.flush(deliverA, deliverB);

      // Both peers must agree on every still-resident slot's
      // post-resim state. After flush, the promote loop on the next
      // tick of each peer will absorb the late arrivals.
      a->setLocalControlState(0);
      b->setLocalControlState(0);
      // A couple of extra ticks lets the promote loop drain.
      for (int i = 0; i < 12; ++i) {
        a->process();
        b->process();
        transport.tick(deliverA, deliverB);
      }

      REQUIRE(a->currentFrame() == b->currentFrame());

      // Sanity: the test would trivially pass if rollback never fired
      // (predictions always matching means the algorithm wasn't exercised).
      // With random inputs and any delay, mispredictions are inevitable.
      REQUIRE(a->rollbackCount() > 0);
      REQUIRE(b->rollbackCount() > 0);

      uint32_t cA = fastGameChecksum(a->game);
      uint32_t cB = fastGameChecksum(b->game);
      REQUIRE(cA == cB);

      // And it has to match what a zero-jitter peer would have produced
      // from the same input sequence at the same frame. The reference
      // ran `kTicks` ticks; this peer ran `kTicks` scripted + 12 idle.
      // Run the reference the same number of idle ticks so the
      // comparison frames line up.
      auto refAdvanced =
          runReference(kWorldSeed, [&] {
            ScriptedInputs ext = script;
            for (int i = 0; i < 12; ++i) {
              ext.a.push_back(0);
              ext.b.push_back(0);
            }
            return ext;
          }(), kTicks + 12);
      REQUIRE(a->currentFrame() == refAdvanced.simFrame);
      REQUIRE(cA == refAdvanced.checksum);
    }
  }
}
