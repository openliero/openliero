// Rollback Step 11b — NetSession integration with RollbackController.
//
// Step 11a put the PacketInputBatch wire format and protocol-version
// byte on the transport. Step 11b wires NetSession's setup paths so
// that constructing it with useRollback=true builds a RollbackController
// and routes its batched send/receive through NetTransport.
//
// This test stands two NetSession instances up over loopback in
// rollback mode, drives the handshake → settings → mapdata → playing
// transition, and runs a few simulation ticks. The peers must reach
// Playing, expose rollbackController() (not controller()), and stay
// frame-locked enough that a clean (zero-jitter) loopback never
// triggers a rollback or a desync.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <thread>

#include "game.hpp"
#include "math.hpp"
#include "net/session.hpp"

namespace {

struct Env {
  std::shared_ptr<Common> common;
  std::shared_ptr<Settings> settings;
  FsNode tcRoot;

  Env() {
    precomputeTables();
    common = std::make_shared<Common>();
    tcRoot = FsNode("data") / "TC" / "openliero";
    common->load(tcRoot);
    settings = std::make_shared<Settings>();
    settings->lives = 10;
    settings->loadingTime = 0;
    settings->loadChange = true;
    settings->randomLevel = true;
    settings->gameMode = Settings::GMKillEmAll;
    // Step 11c — NetSession reads useRollback from settings now.
    settings->useRollback = true;
  }
};

template <typename Pred>
bool pollUntil(NetSession& a, NetSession& b, Pred pred, int maxMs = 5000) {
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(maxMs);
  while (!pred() && std::chrono::steady_clock::now() < deadline) {
    a.update();
    b.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return pred();
}

}  // namespace

TEST_CASE("NetSession in rollback mode reaches Playing and runs ticks",
          "[session][rollback]") {
  Env e;

  NetSession host(e.common, e.settings, e.tcRoot);
  NetSession client(e.common, e.settings, e.tcRoot);

  // Step 11c — useRollback comes from settings (set in the fixture),
  // not from a ctor flag.
  REQUIRE(host.hostGame(0));
  uint16_t port = host.transport().listeningPort();
  REQUIRE(client.joinGame("127.0.0.1", port));
  REQUIRE(host.useRollback());
  REQUIRE(client.useRollback());

  bool ready = pollUntil(host, client, [&]() {
    return host.sessionState() == NetSession::Playing &&
           client.sessionState() == NetSession::Playing;
  });
  REQUIRE(ready);

  // In rollback mode the lockstep accessor is null; the rollback
  // accessor returns the live controller. This is the contract the
  // Step 11e default flip will rely on.
  REQUIRE(host.controller() == nullptr);
  REQUIRE(client.controller() == nullptr);
  REQUIRE(host.rollbackController() != nullptr);
  REQUIRE(client.rollbackController() != nullptr);

  // Both peers seeded the same RNG via handshake — identical game
  // state at frame 0.
  REQUIRE(host.rollbackController()->game.rand ==
          client.rollbackController()->game.rand);

  // Run a few sim ticks. Process pumps the controller (which sends
  // input batches via the wired NetTransport callback), and
  // session.update() polls the transport so received batches reach
  // the other controller. With zero loopback jitter prediction
  // should never trip a rollback.
  host.rollbackController()->focus();
  client.rollbackController()->focus();
  for (int i = 0; i < 50; ++i) {
    host.rollbackController()->process();
    client.rollbackController()->process();
    host.update();
    client.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Step 11c — both peers ended up with the host's settings.
  REQUIRE(host.rollbackController()->rollbackCount() == 0);
  REQUIRE(client.rollbackController()->rollbackCount() == 0);
  REQUIRE(host.rollbackController()->currentFrame() > 0);
  // With the new inputDelay=1 default (Step 11c) the peers can be up
  // to kFrameAdvantage frames apart at any snapshot — the Step 8
  // time-sync stall holds the gap within that bound but doesn't
  // guarantee frame-by-frame parity mid-loop.
  int32_t gap = static_cast<int32_t>(host.rollbackController()->currentFrame()) -
                static_cast<int32_t>(client.rollbackController()->currentFrame());
  REQUIRE(std::abs(gap) <= RollbackController::kFrameAdvantage);
  REQUIRE(!host.desyncDetected());
  REQUIRE(!client.desyncDetected());
}

TEST_CASE("Rollback weapon select transitions to game over a real session",
          "[session][rollback][weapsel]") {
  // End-to-end version of test_rollback_weapsel's "jitter" case: spin up
  // two NetSessions in rollback mode over loopback ENet, drive both
  // peers' navigation inputs through weapon select, and confirm both
  // sides transition to StateGame at the same simFrame. Guards the
  // weapon-select-rollback integration with the session/transport layer
  // (not just the controller in isolation).
  Env e;
  e.settings->useRollback = true;
  e.settings->inputDelay = 1;
  e.settings->selectBotWeapons = 0;

  auto clientSettings = std::make_shared<Settings>(*e.settings);
  // Settings's default copy ctor shallow-copies the wormSettings vector
  // of shared_ptrs, leaving both peers' local worm pointing at the same
  // wormSettings[NetworkPlayerIdx] object. In production this is fine
  // (host and client are separate processes), but in this single-process
  // test it means host's worm0 and client's worm1 share state — every
  // weapon-select mutation on host bleeds into client and vice versa,
  // making determinism impossible. Replace each entry with a fresh
  // shared_ptr so the two peers have fully independent profiles.
  for (auto& ws : clientSettings->wormSettings) {
    if (ws) ws = std::make_shared<WormSettings>(*ws);
  }

  NetSession host(e.common, e.settings, e.tcRoot);
  NetSession client(e.common, clientSettings, e.tcRoot);

  REQUIRE(host.hostGame(0));
  uint16_t port = host.transport().listeningPort();
  REQUIRE(client.joinGame("127.0.0.1", port));

  bool ready = pollUntil(host, client, [&]() {
    return host.sessionState() == NetSession::Playing &&
           client.sessionState() == NetSession::Playing;
  });
  REQUIRE(ready);

  auto* hc = host.rollbackController();
  auto* cc = client.rollbackController();
  REQUIRE(hc != nullptr);
  REQUIRE(cc != nullptr);

  hc->focus();
  cc->focus();

  constexpr uint8_t BIT_DOWN = uint8_t{1} << Worm::Down;
  constexpr uint8_t BIT_FIRE = uint8_t{1} << Worm::Fire;

  // Same script the controller-level test uses: 6× Down (each press is
  // on/off to produce a clean rising edge), a short idle pad, then a
  // single Fire press.
  std::vector<uint8_t> script;
  for (int i = 0; i < 6; ++i) {
    script.push_back(BIT_DOWN);
    script.push_back(0);
  }
  script.push_back(0);
  script.push_back(BIT_FIRE);
  script.push_back(0);

  uint32_t hostTransitionFrame = 0;
  uint32_t clientTransitionFrame = 0;
  bool hostTransitioned = false;
  bool clientTransitioned = false;

  auto runTick = [&](uint8_t inByte) {
    hc->setLocalControlState(inByte);
    cc->setLocalControlState(inByte);
    bool hostInWs = !hostTransitioned;
    bool clientInWs = !clientTransitioned;
    hc->process();
    cc->process();
    host.update();
    client.update();
    if (hostInWs && hc->gameState() == StateGame) {
      hostTransitioned = true;
      hostTransitionFrame = hc->currentFrame();
    }
    if (clientInWs && cc->gameState() == StateGame) {
      clientTransitioned = true;
      clientTransitionFrame = cc->currentFrame();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  };

  for (uint8_t inByte : script) runTick(inByte);
  // Idle tail to let promote loops drain and the transition fire on
  // both peers under loopback's small but non-zero RTT.
  for (int i = 0; i < 200 && !(hostTransitioned && clientTransitioned); ++i) {
    runTick(0);
  }

  REQUIRE(hostTransitioned);
  REQUIRE(clientTransitioned);
  REQUIRE(hostTransitionFrame == clientTransitionFrame);

  // Both peers picked the same weapons.
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < Settings::selectableWeapons; ++j) {
      REQUIRE(hc->game.worms[i]->settings->weapons[j] ==
              cc->game.worms[i]->settings->weapons[j]);
    }
  }

  REQUIRE(!host.desyncDetected());
  REQUIRE(!client.desyncDetected());
}

TEST_CASE("Host's useRollback / inputDelay sync to the client over MatchSettings",
          "[session][rollback]") {
  Env e;
  // Host: rollback ON, inputDelay 2.
  e.settings->useRollback = true;
  e.settings->inputDelay = 2;
  e.settings->maxRollback = 7;

  // Client starts with lockstep settings — host's values must overwrite
  // them via MatchSettings before tryStartGame builds the controller.
  auto clientSettings = std::make_shared<Settings>(*e.settings);
  clientSettings->useRollback = false;
  clientSettings->inputDelay = 5;
  clientSettings->maxRollback = 99;

  NetSession host(e.common, e.settings, e.tcRoot);
  NetSession client(e.common, clientSettings, e.tcRoot);

  REQUIRE(host.hostGame(0));
  uint16_t port = host.transport().listeningPort();
  REQUIRE(client.joinGame("127.0.0.1", port));

  bool ready = pollUntil(host, client, [&]() {
    return host.sessionState() == NetSession::Playing &&
           client.sessionState() == NetSession::Playing;
  });
  REQUIRE(ready);

  // Host's settings flowed through MatchSettingsData.
  REQUIRE(clientSettings->useRollback);
  REQUIRE(clientSettings->inputDelay == 2);
  REQUIRE(clientSettings->maxRollback == 7);

  // The client built a RollbackController, not a NetworkController.
  REQUIRE(client.useRollback());
  REQUIRE(client.rollbackController() != nullptr);
  REQUIRE(client.controller() == nullptr);
}

