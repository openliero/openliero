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
// Uses ENet raw UDP sockets (same as STUN) — no WebSocket dependency.
class SignalingClient {
public:
  enum State {
    Idle,
    Creating,       // Waiting for room code
    Hosting,        // Room created, waiting for peer
    Joining,        // Sent join, waiting for server ack
    WaitingForPeer, // Join acked, waiting for host addresses
    Punching,       // Received StartPunch signal
    Relaying,       // Received UseRelay
    Failed,
    Done,           // Punch succeeded
  };

  SignalingClient();
  ~SignalingClient();

  // Connect to signaling server and create a room (host flow).
  bool createRoom(const std::string& serverAddr, uint16_t serverPort);

  // Connect to signaling server and join a room (client flow).
  bool joinRoom(const std::string& serverAddr, uint16_t serverPort,
                const std::string& roomCode);

  // Report our STUN-discovered addresses to the server.
  void reportAddress(uint8_t addrType, const std::string& ip, uint16_t port);

  // Report hole-punch result.
  void reportPunchOK();
  void reportPunchFail();

  // Send keepalive (call periodically while waiting).
  void sendKeepalive();

  // Poll for incoming messages. Call once per frame.
  void poll();

  // Disconnect and cleanup.
  void disconnect();

  State state() const { return state_; }
  const std::string& roomCode() const { return roomCode_; }
  const std::vector<PeerCandidate>& peerCandidates() const { return peerCandidates_; }
  uint16_t relayPort() const { return relayPort_; }
  const std::vector<uint8_t>& relayToken() const { return relayToken_; }

  // Callbacks
  std::function<void(const std::string& code)> onRoomCreated;
  std::function<void()> onPeerJoined;
  std::function<void()> onJoinAcked;  // Client: server confirmed we're in the room
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

  // Retry state for unacknowledged messages
  uint64_t lastSendMs_ = 0;
  int retryCount_ = 0;
  static constexpr int kRetryIntervalMs = 2000;
  static constexpr int kMaxRetries = 5;

  // Cached resolved server address
  ENetAddress resolvedAddr_ = {};
};
