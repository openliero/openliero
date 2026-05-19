#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "signaling.hpp"

// UDP hole-punch implementation.
// After receiving peer candidate addresses from the signaling server,
// sends simultaneous UDP probes to all candidates and detects when
// a probe is received from the peer (meaning the hole is punched).
class HolePunch {
public:
  enum State {
    Idle,
    Punching,
    Succeeded,
    Failed,
  };

  struct Result {
    std::string peerIP;
    uint16_t peerPort;
  };

  HolePunch();
  ~HolePunch();

  // Start hole-punching from localPort to all candidate addresses.
  // localPort should be the game's ENet listening port.
  bool start(uint16_t localPort, const std::vector<PeerCandidate>& candidates);

  // Poll for incoming probes. Call once per frame.
  // Returns true while still active.
  void poll();

  // Stop punching (timeout or success).
  void stop();

  State state() const { return state_; }
  const Result& result() const { return result_; }

  // Timeout in milliseconds (default 5000)
  void setTimeout(int ms) { timeoutMs_ = ms; }

  // Callbacks
  std::function<void(const Result&)> onSuccess;
  std::function<void()> onTimeout;

private:
  void sendProbes();

  int sock_;  // ENetSocket
  State state_;
  Result result_;
  std::vector<PeerCandidate> candidates_;
  int timeoutMs_;
  uint64_t startTimeMs_;
  uint64_t lastProbeMs_;

  // Magic bytes to identify our probes vs random traffic
  static constexpr uint8_t PROBE_MAGIC[4] = {0x4F, 0x4C, 0x48, 0x50}; // "OLHP"
};
