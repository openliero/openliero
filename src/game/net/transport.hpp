#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct _ENetHost;
struct _ENetPeer;

// Handles UDP communication between two peers using ENet.
// Provides reliable ordered delivery of input packets.
//
// NEW: Integrates hole-punch and STUN via ENet's intercept callback.
// The same socket is used for STUN queries, hole-punch probes, and game traffic,
// ensuring NAT mappings are preserved throughout the connection lifecycle.
struct NetTransport {
  // Packet types
  enum PacketType : uint8_t {
    PacketInput = 1,
    PacketHandshake = 2,
    PacketChecksum = 3,
    PacketPlayerInfo = 4,
    PacketMatchSettings = 5,
    PacketMapData = 6,
    PacketPause = 7,
    PacketResume = 8,
    PacketRematchReady = 9,
    PacketRematchLevel = 10,
    PacketEndMatch = 11,
    PacketTcInfo = 12,
    PacketTcResponse = 13,
    PacketTcData = 14,
  };

  struct PlayerInfo {
    uint32_t weapons[5];
    int32_t color;
    int32_t rgb[3];
    char name[24];
  };

  struct MatchSettingsData {
    int32_t lives;
    int32_t loadingTime;
    uint32_t gameMode;
    int32_t blood;
    int32_t maxBonuses;
    int32_t timeToLose;
    int32_t flagsToWin;
    uint8_t loadChange;
    uint32_t weapTable[40];
    uint8_t regenerateLevel;
    uint8_t shadow;
    uint8_t namesOnBonuses;
    int32_t bloodParticleMax;
    int32_t zoneTimeout;
  };

  // Connection state
  enum State {
    Disconnected,
    Listening,     // Host waiting for peer
    Connecting,    // Client connecting to host
    Connected,
    Failed,
  };

  NetTransport();
  ~NetTransport();

  // Movable but not copyable (owns socket)
  NetTransport(NetTransport&& other) noexcept;
  NetTransport& operator=(NetTransport&& other) noexcept;
  NetTransport(const NetTransport&) = delete;
  NetTransport& operator=(const NetTransport&) = delete;

  void disconnect();

  // Host a game on the given port.
  bool host(uint16_t port);

  // Connect to a host directly.
  bool connect(const std::string& address, uint16_t port);

  // Connect to a peer using the existing host (after hole-punch).
  // The host must already be created.
  bool connectExisting(const std::string& address, uint16_t port);

  // Create ENet host using a pre-existing socket (from IceBridge).
  // The socket should be non-blocking with adequate buffer sizes.
  bool createHostOnBridgeSocket(int bridgeSocket);

  // Connect via relay. Sends auth token (with retry), then ENet connects.
  // For host: creates ENet host on localPort, authenticates with relay.
  // For client: creates ENet host on ephemeral port, authenticates with relay.
  bool hostViaRelay(uint16_t localPort, const std::string& relayAddr,
                    uint16_t relayPort, const std::vector<uint8_t>& token);
  bool connectViaRelay(const std::string& relayAddr, uint16_t relayPort,
                       const std::vector<uint8_t>& token);

  // --- Hole-punch support (single-socket) ---
  // Start hole-punching to candidates through the ENet host's socket.
  // The host must already be created (via host() call).
  // localNonce identifies us; peerNonce is expected peer (0 = accept any non-self).
  struct PunchCandidate {
    uint8_t type;      // 4=IPv4, 6=IPv6
    std::string ip;
    uint16_t port;
  };
  bool startPunch(const std::vector<PunchCandidate>& candidates,
                  uint32_t localNonce, uint32_t peerNonce);
  void stopPunch();

  struct PunchResult {
    std::string peerIP;
    uint16_t peerPort;
  };
  bool punchSucceeded() const { return punchState_ == PunchSucceeded; }
  bool punchFailed() const { return punchState_ == PunchFailed; }
  const PunchResult& punchResult() const { return punchResult_; }

  // --- General ---
  // Poll for events. Call once per frame.
  bool poll();

  void sendInput(uint32_t frame, uint8_t input);
  void sendChecksum(uint32_t frame, uint32_t checksum);
  void sendHandshake(uint32_t seed, uint32_t settingsHash);
  void sendPlayerInfo(const PlayerInfo& info);
  void sendMatchSettings(const MatchSettingsData& data);
  void sendMapData(const void* data, size_t len);
  void sendPause();
  void sendResume();
  void sendRematchReady(bool ready);
  void sendRematchLevel(bool randomLevel, const std::string& levelFile);
  void sendEndMatch();
  void sendTcInfo(uint32_t hash, const std::string& name);
  void sendTcResponse(bool needData);
  void sendTcData(const void* data, size_t len);

  State state() const { return state_; }
  uint16_t listeningPort() const;

  // Access the ENet host (for STUN-via-host integration)
  _ENetHost* enetHost() const { return enetHost_; }

  // Callbacks
  std::function<void(uint32_t frame, uint8_t input)> onRemoteInput;
  std::function<void(uint32_t seed, uint32_t settingsHash)> onHandshake;
  std::function<void(uint32_t frame, uint32_t checksum)> onChecksum;
  std::function<void(const PlayerInfo& info)> onPlayerInfo;
  std::function<void(const MatchSettingsData& data)> onMatchSettings;
  std::function<void(const void* data, size_t len)> onMapData;
  std::function<void()> onPause;
  std::function<void()> onResume;
  std::function<void(bool ready)> onRematchReady;
  std::function<void(bool randomLevel, std::string levelFile)> onRematchLevel;
  std::function<void()> onEndMatch;
  std::function<void(uint32_t hash, std::string name)> onTcInfo;
  std::function<void(bool needData)> onTcResponse;
  std::function<void(const void* data, size_t len)> onTcData;
  std::function<void()> onConnected;
  std::function<void()> onDisconnected;
  std::function<void(const PunchResult&)> onPunchSuccess;
  std::function<void()> onPunchTimeout;
  // Called for each non-ENet packet intercepted (STUN, etc.)
  // Return true if consumed.
  std::function<bool(const uint8_t* data, size_t len)> onInterceptedPacket;

 private:
  void sendPacket(const void* data, size_t len);
  bool createHost(uint16_t port);
  void setupIntercept();

  // Hole-punch internals
  enum PunchState { PunchIdle, Punching, PunchGrace, PunchSucceeded, PunchFailed };
  void sendProbes();
  void punchPoll();

  // Relay internals
  void sendRelayToken();

  static int interceptCallback(_ENetHost* host, void* event);

  _ENetHost* enetHost_;
  _ENetPeer* peer_;
  State state_;

  // Hole-punch state
  PunchState punchState_;
  std::vector<PunchCandidate> punchCandidates_;
  uint32_t punchLocalNonce_;
  uint32_t punchPeerNonce_;
  uint64_t punchStartMs_;
  uint64_t punchLastProbeMs_;
  int punchTimeoutMs_;
  uint64_t punchGraceStartMs_ = 0;
  PunchResult punchResult_;

  // Relay state
  std::vector<uint8_t> relayToken_;
  std::string relayHost_;        // resolved IP (not hostname)
  uint16_t relayPort_ = 0;
  bool relayAuthenticated_;
  uint64_t relayLastTokenMs_;
  int relayTokenAttempts_;

  static constexpr uint8_t PROBE_MAGIC[4] = {0x4F, 0x4C, 0x48, 0x50}; // "OLHP"
  static constexpr uint8_t RELAY_ACK = 0x01;
};
