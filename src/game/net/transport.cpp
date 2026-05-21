#include "transport.hpp"
#include "netutil.hpp"

#include <cstring>
#include <cstdio>
#include <vector>
#include <atomic>

#define ENET_IMPLEMENTATION
#include <enet.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

using netutil::nowMs;

static constexpr int NUM_CHANNELS = 2;
static constexpr int CHANNEL_RELIABLE = 0;
static constexpr int CHANNEL_UNRELIABLE = 1;

constexpr uint8_t NetTransport::PROBE_MAGIC[4];

// Single active transport pointer. Only one ENet host exists per process.
static std::atomic<NetTransport*> sActiveTransport{nullptr};

static void registerTransport(_ENetHost*, NetTransport* t) {
  sActiveTransport.store(t, std::memory_order_release);
}

static void unregisterTransport(_ENetHost*) {
  sActiveTransport.store(nullptr, std::memory_order_release);
}

static NetTransport* getTransportFromHost(_ENetHost*) {
  return sActiveTransport.load(std::memory_order_acquire);
}

NetTransport::NetTransport()
    : enetHost_(nullptr), peer_(nullptr), state_(Disconnected),
      punchState_(PunchIdle), punchLocalNonce_(0), punchPeerNonce_(0),
      punchStartMs_(0), punchLastProbeMs_(0), punchTimeoutMs_(5000),
      relayAuthenticated_(false), relayLastTokenMs_(0), relayTokenAttempts_(0) {
  enet_initialize();
}

NetTransport::NetTransport(NetTransport&& other) noexcept
    : enetHost_(other.enetHost_), peer_(other.peer_), state_(other.state_),
      punchState_(other.punchState_), punchCandidates_(std::move(other.punchCandidates_)),
      punchLocalNonce_(other.punchLocalNonce_), punchPeerNonce_(other.punchPeerNonce_),
      punchStartMs_(other.punchStartMs_), punchLastProbeMs_(other.punchLastProbeMs_),
      punchTimeoutMs_(other.punchTimeoutMs_), punchResult_(std::move(other.punchResult_)),
      relayToken_(std::move(other.relayToken_)), relayHost_(std::move(other.relayHost_)),
      relayPort_(other.relayPort_), relayAuthenticated_(other.relayAuthenticated_),
      relayLastTokenMs_(other.relayLastTokenMs_), relayTokenAttempts_(other.relayTokenAttempts_) {
  // Update registry to point to new instance
  if (enetHost_) {
    registerTransport(enetHost_, this);
  }
  // Nullify source
  other.enetHost_ = nullptr;
  other.peer_ = nullptr;
  other.state_ = Disconnected;
  other.punchState_ = PunchIdle;
}

NetTransport& NetTransport::operator=(NetTransport&& other) noexcept {
  if (this != &other) {
    disconnect();
    enetHost_ = other.enetHost_;
    peer_ = other.peer_;
    state_ = other.state_;
    punchState_ = other.punchState_;
    punchCandidates_ = std::move(other.punchCandidates_);
    punchLocalNonce_ = other.punchLocalNonce_;
    punchPeerNonce_ = other.punchPeerNonce_;
    punchStartMs_ = other.punchStartMs_;
    punchLastProbeMs_ = other.punchLastProbeMs_;
    punchTimeoutMs_ = other.punchTimeoutMs_;
    punchResult_ = std::move(other.punchResult_);
    relayToken_ = std::move(other.relayToken_);
    relayHost_ = std::move(other.relayHost_);
    relayPort_ = other.relayPort_;
    relayAuthenticated_ = other.relayAuthenticated_;
    relayLastTokenMs_ = other.relayLastTokenMs_;
    relayTokenAttempts_ = other.relayTokenAttempts_;
    if (enetHost_) {
      registerTransport(enetHost_, this);
    }
    other.enetHost_ = nullptr;
    other.peer_ = nullptr;
    other.state_ = Disconnected;
    other.punchState_ = PunchIdle;
  }
  return *this;
}

NetTransport::~NetTransport() {
  disconnect();
  enet_deinitialize();
}

void NetTransport::disconnect() {
  stopPunch();
  if (peer_) {
    enet_peer_disconnect_now(peer_, 0);
    peer_ = nullptr;
  }
  if (enetHost_) {
    unregisterTransport(enetHost_);
    enet_host_destroy(enetHost_);
    enetHost_ = nullptr;
  }
  state_ = Disconnected;
  relayAuthenticated_ = false;
  relayToken_.clear();
}

uint16_t NetTransport::listeningPort() const {
  return enetHost_ ? enetHost_->address.port : 0;
}

bool NetTransport::createHost(uint16_t port) {
  ENetAddress address = {};
  address.port = port;

  enetHost_ = enet_host_create(&address, 1, NUM_CHANNELS, 0, 0);
  if (!enetHost_) return false;

  setupIntercept();
  return true;
}

void NetTransport::setupIntercept() {
  if (!enetHost_) return;
  registerTransport(enetHost_, this);
  enet_host_set_intercept(enetHost_, &NetTransport::interceptCallback);
}

int NetTransport::interceptCallback(_ENetHost* host, void* /*event*/) {
  NetTransport* self = getTransportFromHost(host);
  if (!self) return 0;

  uint8_t* data = host->receivedData;
  size_t len = host->receivedDataLength;

  if (len < 4) return 0;

  // Check for hole-punch probe (magic: OLHP)
  if (len >= 8 && std::memcmp(data, PROBE_MAGIC, 4) == 0) {
    if (self->punchState_ == Punching) {
      uint32_t recvNonce = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                           ((uint32_t)data[6] << 8) | (uint32_t)data[7];

      if (recvNonce == self->punchLocalNonce_) {
        // Our own reflected probe — ignore
        return 1;
      }
      if (self->punchPeerNonce_ != 0 && recvNonce != self->punchPeerNonce_) {
        // Unknown nonce
        return 1;
      }

      // Valid probe from peer!
      char fromIP[INET6_ADDRSTRLEN] = {};
      enet_address_get_host_ip(&host->receivedAddress, fromIP, sizeof(fromIP));

      fprintf(stderr, "[transport] punch SUCCESS from %s:%u (nonce=%08x)\n",
              fromIP, host->receivedAddress.port, recvNonce);

      self->punchResult_.peerIP = fromIP;
      self->punchResult_.peerPort = host->receivedAddress.port;
      self->punchState_ = PunchSucceeded;
      if (self->onPunchSuccess) self->onPunchSuccess(self->punchResult_);
    }
    return 1; // consumed (don't pass to ENet)
  }

  // Check for relay ACK (single byte 0x01)
  if (len == 1 && data[0] == RELAY_ACK) {
    if (!self->relayAuthenticated_) {
      fprintf(stderr, "[transport] relay ACK received — authenticated\n");
      self->relayAuthenticated_ = true;
    }
    return 1;
  }

  // Let user-provided handler try (e.g., STUN responses)
  if (self->onInterceptedPacket && self->onInterceptedPacket(data, len)) {
    return 1;
  }

  return 0; // Not consumed — let ENet process it
}

bool NetTransport::host(uint16_t port) {
  if (enetHost_) return false;

  if (!createHost(port)) {
    state_ = Failed;
    return false;
  }

  state_ = Listening;
  return true;
}

bool NetTransport::connect(const std::string& address, uint16_t port) {
  if (enetHost_) return false;

  // Create host on ephemeral port
  if (!createHost(0)) {
    state_ = Failed;
    return false;
  }

  ENetAddress addr = {};
  addr.port = port;
  if (enet_address_set_host(&addr, address.c_str()) != 0) {
    enet_host_destroy(enetHost_);
    enetHost_ = nullptr;
    state_ = Failed;
    return false;
  }

  peer_ = enet_host_connect(enetHost_, &addr, NUM_CHANNELS, 0);
  if (!peer_) {
    enet_host_destroy(enetHost_);
    enetHost_ = nullptr;
    state_ = Failed;
    return false;
  }

  state_ = Connecting;
  return true;
}

bool NetTransport::connectExisting(const std::string& address, uint16_t port) {
  if (!enetHost_) return false;
  if (peer_) return false;

  ENetAddress addr = {};
  addr.port = port;
  if (enet_address_set_host(&addr, address.c_str()) != 0) {
    state_ = Failed;
    return false;
  }

  peer_ = enet_host_connect(enetHost_, &addr, NUM_CHANNELS, 0);
  if (!peer_) {
    state_ = Failed;
    return false;
  }

  state_ = Connecting;
  return true;
}

bool NetTransport::hostViaRelay(uint16_t localPort, const std::string& relayAddr,
                                uint16_t relayPort, const std::vector<uint8_t>& token) {
  if (enetHost_) return false;

  if (!createHost(localPort)) {
    state_ = Failed;
    return false;
  }

  // Resolve relay address once (avoid DNS on every retry)
  ENetAddress resolved = {};
  resolved.port = relayPort;
  if (enet_address_set_host(&resolved, relayAddr.c_str()) != 0) {
    unregisterTransport(enetHost_);
    enet_host_destroy(enetHost_);
    enetHost_ = nullptr;
    state_ = Failed;
    return false;
  }
  char resolvedIP[INET6_ADDRSTRLEN] = {};
  enet_address_get_host_ip(&resolved, resolvedIP, sizeof(resolvedIP));

  // Store relay info for token retry
  relayToken_ = token;
  relayHost_ = resolvedIP;
  relayPort_ = relayPort;

  relayAuthenticated_ = false;
  relayTokenAttempts_ = 0;
  relayLastTokenMs_ = 0;

  // Send initial token
  sendRelayToken();

  state_ = Listening;
  return true;
}

bool NetTransport::connectViaRelay(const std::string& relayAddr, uint16_t relayPort,
                                   const std::vector<uint8_t>& token) {
  if (enetHost_) return false;

  if (!createHost(0)) {
    state_ = Failed;
    return false;
  }

  // Resolve relay address once
  ENetAddress resolved = {};
  resolved.port = relayPort;
  if (enet_address_set_host(&resolved, relayAddr.c_str()) != 0) {
    unregisterTransport(enetHost_);
    enet_host_destroy(enetHost_);
    enetHost_ = nullptr;
    state_ = Failed;
    return false;
  }
  char resolvedIP[INET6_ADDRSTRLEN] = {};
  enet_address_get_host_ip(&resolved, resolvedIP, sizeof(resolvedIP));

  // Store relay info
  relayToken_ = token;
  relayHost_ = resolvedIP;
  relayPort_ = relayPort;

  relayAuthenticated_ = false;
  relayTokenAttempts_ = 0;
  relayLastTokenMs_ = 0;

  // Send initial token
  sendRelayToken();

  // Connect to relay via ENet (after token)
  peer_ = enet_host_connect(enetHost_, &resolved, NUM_CHANNELS, 0);
  if (!peer_) {
    unregisterTransport(enetHost_);
    enet_host_destroy(enetHost_);
    enetHost_ = nullptr;
    state_ = Failed;
    return false;
  }

  state_ = Connecting;
  return true;
}

void NetTransport::sendRelayToken() {
  if (!enetHost_ || relayToken_.empty()) return;

  ENetAddress addr = {};
  addr.port = relayPort_;
  if (enet_address_set_host(&addr, relayHost_.c_str()) != 0) return;

  relayTokenAttempts_++;
  relayLastTokenMs_ = nowMs();

  ENetBuffer buf;
  buf.data = relayToken_.data();
  buf.dataLength = relayToken_.size();
  int sent = enet_socket_send(enetHost_->socket, &addr, &buf, 1);
  fprintf(stderr, "[transport] relay token sent (attempt %d, %d bytes, result=%d)\n",
          relayTokenAttempts_, (int)relayToken_.size(), sent);
}

// --- Hole-punch ---

bool NetTransport::startPunch(const std::vector<PunchCandidate>& candidates,
                              uint32_t localNonce, uint32_t peerNonce) {
  if (!enetHost_) return false;
  if (candidates.empty()) return false;

  fprintf(stderr, "[transport] startPunch: %zu candidates, nonce=%08x peer=%08x\n",
          candidates.size(), localNonce, peerNonce);

  punchCandidates_ = candidates;
  punchLocalNonce_ = localNonce;
  punchPeerNonce_ = peerNonce;
  punchState_ = Punching;
  punchStartMs_ = nowMs();
  punchLastProbeMs_ = 0;
  punchTimeoutMs_ = 5000;

  sendProbes();
  return true;
}

void NetTransport::stopPunch() {
  if (punchState_ == Punching)
    punchState_ = PunchFailed;
  punchCandidates_.clear();
}

void NetTransport::sendProbes() {
  if (!enetHost_) return;

  uint8_t probe[8];
  std::memcpy(probe, PROBE_MAGIC, 4);
  probe[4] = (uint8_t)(punchLocalNonce_ >> 24);
  probe[5] = (uint8_t)(punchLocalNonce_ >> 16);
  probe[6] = (uint8_t)(punchLocalNonce_ >> 8);
  probe[7] = (uint8_t)(punchLocalNonce_);

  ENetBuffer buf;
  buf.data = probe;
  buf.dataLength = sizeof(probe);

  for (auto& cand : punchCandidates_) {
    ENetAddress addr = {};
    addr.port = cand.port;
    if (enet_address_set_host(&addr, cand.ip.c_str()) != 0) continue;
    enet_socket_send(enetHost_->socket, &addr, &buf, 1);
  }

  punchLastProbeMs_ = nowMs();
}

void NetTransport::punchPoll() {
  if (punchState_ != Punching) return;

  uint64_t now = nowMs();

  if (now - punchStartMs_ > (uint64_t)punchTimeoutMs_) {
    fprintf(stderr, "[transport] punch TIMEOUT after %d ms\n", punchTimeoutMs_);
    punchState_ = PunchFailed;
    if (onPunchTimeout) onPunchTimeout();
    return;
  }

  // Send probes every 200ms
  if (now - punchLastProbeMs_ > 200) {
    sendProbes();
  }
}

// --- Poll ---

bool NetTransport::poll() {
  if (!enetHost_) return false;

  // Retry relay token if not yet authenticated
  if (!relayToken_.empty() && !relayAuthenticated_) {
    uint64_t now = nowMs();
    if (now - relayLastTokenMs_ > 1000 && relayTokenAttempts_ < 10) {
      sendRelayToken();
    }
  }

  // Poll hole-punch
  punchPoll();

  ENetEvent event;
  while (enet_host_service(enetHost_, &event, 0) > 0) {
    switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        peer_ = event.peer;
        state_ = Connected;
        if (onConnected) onConnected();
        break;

      case ENET_EVENT_TYPE_RECEIVE: {
        uint8_t* data = event.packet->data;
        size_t len = event.packet->dataLength;

        static constexpr size_t MaxPacketSize = 10 * 1024 * 1024;
        if (len > MaxPacketSize) {
          enet_packet_destroy(event.packet);
          break;
        }

        if (len >= 1) {
          switch (data[0]) {
            case PacketInput:
              if (len == 6 && onRemoteInput) {
                uint32_t frame;
                std::memcpy(&frame, data + 1, 4);
                onRemoteInput(frame, data[5]);
              }
              break;
            case PacketHandshake:
              if (len == 9 && onHandshake) {
                uint32_t seed, hash;
                std::memcpy(&seed, data + 1, 4);
                std::memcpy(&hash, data + 5, 4);
                onHandshake(seed, hash);
              }
              break;
            case PacketChecksum:
              if (len == 9 && onChecksum) {
                uint32_t frame, checksum;
                std::memcpy(&frame, data + 1, 4);
                std::memcpy(&checksum, data + 5, 4);
                onChecksum(frame, checksum);
              }
              break;
            case PacketPlayerInfo:
              if (len == 1 + sizeof(PlayerInfo) && onPlayerInfo) {
                PlayerInfo info;
                std::memcpy(&info, data + 1, sizeof(PlayerInfo));
                onPlayerInfo(info);
              }
              break;
            case PacketMatchSettings:
              if (len == 1 + sizeof(MatchSettingsData) && onMatchSettings) {
                MatchSettingsData msd;
                std::memcpy(&msd, data + 1, sizeof(MatchSettingsData));
                onMatchSettings(msd);
              }
              break;
            case PacketMapData:
              if (len > 5 && onMapData) {
                onMapData(data + 1, len - 1);
              }
              break;
            case PacketPause:
              if (onPause) onPause();
              break;
            case PacketResume:
              if (onResume) onResume();
              break;
            case PacketRematchReady:
              if (len == 2 && onRematchReady) {
                onRematchReady(data[1] != 0);
              }
              break;
            case PacketRematchLevel:
              if (len >= 2 && onRematchLevel) {
                bool random = data[1] != 0;
                std::string file;
                if (len > 2)
                  file.assign(reinterpret_cast<const char*>(data + 2), len - 2);
                onRematchLevel(random, std::move(file));
              }
              break;
            case PacketEndMatch:
              if (onEndMatch) onEndMatch();
              break;
            case PacketTcInfo:
              if (len >= 5 && onTcInfo) {
                uint32_t hash;
                std::memcpy(&hash, data + 1, 4);
                std::string name;
                if (len > 5)
                  name.assign(reinterpret_cast<const char*>(data + 5), len - 5);
                onTcInfo(hash, std::move(name));
              }
              break;
            case PacketTcResponse:
              if (len == 2 && onTcResponse) {
                onTcResponse(data[1] != 0);
              }
              break;
            case PacketTcData:
              if (len > 1 && onTcData) {
                onTcData(data + 1, len - 1);
              }
              break;
          }
        }

        enet_packet_destroy(event.packet);
        break;
      }

      case ENET_EVENT_TYPE_DISCONNECT:
      case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
        peer_ = nullptr;
        state_ = Disconnected;
        if (onDisconnected) onDisconnected();
        return false;

      case ENET_EVENT_TYPE_NONE:
        break;
    }
  }

  return state_ == Connected || state_ == Listening || state_ == Connecting;
}

// --- Send helpers ---

void NetTransport::sendInput(uint32_t frame, uint8_t input) {
  uint8_t buf[6];
  buf[0] = PacketInput;
  std::memcpy(buf + 1, &frame, 4);
  buf[5] = input;
  sendPacket(buf, sizeof(buf));
}

void NetTransport::sendChecksum(uint32_t frame, uint32_t checksum) {
  uint8_t buf[9];
  buf[0] = PacketChecksum;
  std::memcpy(buf + 1, &frame, 4);
  std::memcpy(buf + 5, &checksum, 4);

  if (!peer_) return;
  ENetPacket* packet =
      enet_packet_create(buf, sizeof(buf), ENET_PACKET_FLAG_UNSEQUENCED);
  if (packet) enet_peer_send(peer_, CHANNEL_UNRELIABLE, packet);
}

void NetTransport::sendHandshake(uint32_t seed, uint32_t settingsHash) {
  uint8_t buf[9];
  buf[0] = PacketHandshake;
  std::memcpy(buf + 1, &seed, 4);
  std::memcpy(buf + 5, &settingsHash, 4);
  sendPacket(buf, sizeof(buf));
}

void NetTransport::sendPlayerInfo(const PlayerInfo& info) {
  uint8_t buf[1 + sizeof(PlayerInfo)];
  buf[0] = PacketPlayerInfo;
  std::memcpy(buf + 1, &info, sizeof(PlayerInfo));
  sendPacket(buf, sizeof(buf));
}

void NetTransport::sendMatchSettings(const MatchSettingsData& data) {
  uint8_t buf[1 + sizeof(MatchSettingsData)];
  buf[0] = PacketMatchSettings;
  std::memcpy(buf + 1, &data, sizeof(MatchSettingsData));
  sendPacket(buf, sizeof(buf));
}

void NetTransport::sendMapData(const void* data, size_t len) {
  std::vector<uint8_t> buf(1 + len);
  buf[0] = PacketMapData;
  std::memcpy(buf.data() + 1, data, len);

  if (!peer_) return;
  ENetPacket* packet =
      enet_packet_create(buf.data(), buf.size(), ENET_PACKET_FLAG_RELIABLE);
  if (!packet) return;
  if (enet_peer_send(peer_, CHANNEL_RELIABLE, packet) < 0)
    enet_packet_destroy(packet);
}

void NetTransport::sendPause() {
  uint8_t buf[1] = {PacketPause};
  sendPacket(buf, sizeof(buf));
}

void NetTransport::sendResume() {
  uint8_t buf[1] = {PacketResume};
  sendPacket(buf, sizeof(buf));
}

void NetTransport::sendRematchReady(bool ready) {
  uint8_t buf[2];
  buf[0] = PacketRematchReady;
  buf[1] = ready ? 1 : 0;
  sendPacket(buf, sizeof(buf));
}

void NetTransport::sendRematchLevel(bool randomLevel, const std::string& levelFile) {
  std::vector<uint8_t> buf(2 + levelFile.size());
  buf[0] = PacketRematchLevel;
  buf[1] = randomLevel ? 1 : 0;
  if (!levelFile.empty())
    std::memcpy(buf.data() + 2, levelFile.data(), levelFile.size());
  sendPacket(buf.data(), buf.size());
}

void NetTransport::sendEndMatch() {
  uint8_t buf[1] = {PacketEndMatch};
  sendPacket(buf, sizeof(buf));
}

void NetTransport::sendTcInfo(uint32_t hash, const std::string& name) {
  std::vector<uint8_t> buf(1 + 4 + name.size());
  buf[0] = PacketTcInfo;
  std::memcpy(buf.data() + 1, &hash, 4);
  std::memcpy(buf.data() + 5, name.data(), name.size());
  sendPacket(buf.data(), buf.size());
}

void NetTransport::sendTcResponse(bool needData) {
  uint8_t buf[2];
  buf[0] = PacketTcResponse;
  buf[1] = needData ? 1 : 0;
  sendPacket(buf, sizeof(buf));
}

void NetTransport::sendTcData(const void* data, size_t len) {
  std::vector<uint8_t> buf(1 + len);
  buf[0] = PacketTcData;
  std::memcpy(buf.data() + 1, data, len);

  if (!peer_) return;
  ENetPacket* packet =
      enet_packet_create(buf.data(), buf.size(), ENET_PACKET_FLAG_RELIABLE);
  if (!packet) return;
  if (enet_peer_send(peer_, CHANNEL_RELIABLE, packet) < 0)
    enet_packet_destroy(packet);
}

void NetTransport::sendPacket(const void* data, size_t len) {
  if (!peer_) return;
  ENetPacket* packet =
      enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
  if (!packet) return;
  if (enet_peer_send(peer_, CHANNEL_RELIABLE, packet) < 0)
    enet_packet_destroy(packet);
}
