#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <enet.h>

// Candidate address for a peer (discovered via STUN or reported by signaling).
struct PeerCandidate {
  uint8_t type;      // 4 = IPv4, 6 = IPv6
  std::string ip;
  uint16_t port;
};

// Client for the openliero signaling server.
// Uses ENet raw UDP sockets — no WebSocket dependency.
class SignalingClient {
public:
  enum State {
    Idle,
    Creating,
    Hosting,
    Joining,
    WaitingForPeer,
    Punching,
    Relaying,
    Failed,
    Done,
  };

  SignalingClient();
  ~SignalingClient();

  bool createRoom(const std::string& serverAddr, uint16_t serverPort);
  bool joinRoom(const std::string& serverAddr, uint16_t serverPort,
                const std::string& roomCode);

  void reportAddress(uint8_t addrType, const std::string& ip, uint16_t port);
  void reportPunchOK();
  void reportPunchFail();
  void sendKeepalive();
  void poll();
  void disconnect();

  State state() const { return state_; }
  const std::string& roomCode() const { return roomCode_; }
  const std::vector<PeerCandidate>& peerCandidates() const { return peerCandidates_; }
  uint16_t relayPort() const { return relayPort_; }
  const std::vector<uint8_t>& relayToken() const { return relayToken_; }

  // Callbacks
  std::function<void(const std::string& code)> onRoomCreated;
  std::function<void()> onPeerJoined;
  std::function<void()> onJoinAcked;
  std::function<void(const PeerCandidate&)> onPeerAddr;
  std::function<void()> onStartPunch;
  std::function<void(uint16_t relayPort)> onUseRelay;
  std::function<void(const std::string& msg)> onError;
  std::function<void()> onRoomExpired;

private:
  bool connect(const std::string& serverAddr, uint16_t serverPort);
  void send(const void* data, size_t len);
  void handleMessage(const uint8_t* data, size_t len);

  ENetSocket sock_;
  State state_;
  std::string roomCode_;
  std::string serverAddr_;
  uint16_t serverPort_;
  std::vector<PeerCandidate> peerCandidates_;
  uint16_t relayPort_;
  std::vector<uint8_t> relayToken_;
  int pollErrCount_ = 0;

  uint64_t lastSendMs_ = 0;
  int retryCount_ = 0;
  static constexpr int kRetryIntervalMs = 2000;
  static constexpr int kMaxRetries = 5;

  ENetAddress resolvedAddr_ = {};
};
