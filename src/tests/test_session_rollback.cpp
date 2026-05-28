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

  NetSession host(e.common, e.settings, e.tcRoot, /*useRollback=*/true);
  NetSession client(e.common, e.settings, e.tcRoot, /*useRollback=*/true);

  REQUIRE(host.useRollback());
  REQUIRE(client.useRollback());

  REQUIRE(host.hostGame(0));
  uint16_t port = host.transport().listeningPort();
  REQUIRE(client.joinGame("127.0.0.1", port));

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

  REQUIRE(host.rollbackController()->rollbackCount() == 0);
  REQUIRE(client.rollbackController()->rollbackCount() == 0);
  REQUIRE(host.rollbackController()->currentFrame() > 0);
  REQUIRE(host.rollbackController()->currentFrame() ==
          client.rollbackController()->currentFrame());
  REQUIRE(host.rollbackController()->game.rand ==
          client.rollbackController()->game.rand);
  REQUIRE(!host.desyncDetected());
  REQUIRE(!client.desyncDetected());
}
