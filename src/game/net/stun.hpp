#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

#include <enet.h>

struct StunResult {
  std::string ipv4;
  uint16_t ipv4Port = 0;
  std::string ipv6;
  uint16_t ipv6Port = 0;
};

struct StunMappedAddress {
  std::string ip;
  uint16_t port = 0;
};

// STUN protocol constants (RFC 5389)
namespace stun {
  constexpr uint16_t BINDING_REQUEST = 0x0001;
  constexpr uint16_t BINDING_RESPONSE = 0x0101;
  constexpr uint32_t MAGIC_COOKIE = 0x2112A442;
  constexpr uint16_t ATTR_XOR_MAPPED_ADDRESS = 0x0020;
  constexpr uint16_t ATTR_MAPPED_ADDRESS = 0x0001;

  struct Header {
    uint16_t type;
    uint16_t length;
    uint32_t magicCookie;
    uint8_t transactionId[12];
  };
  static_assert(sizeof(Header) == 20);

  // Parse a STUN Binding Response, extracting the mapped address.
  StunMappedAddress parseResponse(const uint8_t* data, size_t len, const Header& req);

  // Build a STUN Binding Request. Fills in txnId with random bytes.
  Header buildRequest();

  // Check if a packet is a STUN Binding Response matching our transaction ID.
  bool isResponse(const uint8_t* data, size_t len, const Header& req);
}

// Async STUN query using a background thread and its own socket.
// Use this for initial discovery when no ENet host exists yet.
class StunQuery {
public:
  void start();
  void start(uint16_t localPort);

  StunResult result() const;
  bool done() const { return done_.load(); }

  ~StunQuery();

private:
  static StunMappedAddress queryServer(const char* serverAddr, uint16_t port,
                                        uint16_t localPort = 0);
  void run();

  std::thread thread_;
  std::atomic<bool> done_{false};
  std::atomic<bool> started_{false};
  mutable std::mutex mutex_;
  StunResult result_;
  uint16_t localPort_{0};
};

// Synchronous STUN query through an existing ENet host socket.
// Sends STUN request and relies on the caller to feed received packets
// via feedResponse(). This is the single-socket approach.
class StunViaHost {
public:
  // Start a STUN query through the given ENet host socket.
  // The query sends a binding request to the STUN server.
  void start(ENetHost* host);

  // Feed a received packet (from the intercept callback).
  // Returns true if this packet was a STUN response and was consumed.
  bool feedResponse(const uint8_t* data, size_t len);

  // Returns result. Check done() first.
  StunResult result() const { return result_; }
  bool done() const { return done_; }

  // Call periodically to retry if no response yet.
  void update();

private:
  ENetHost* host_ = nullptr;
  stun::Header ipv4Req_{};
  stun::Header ipv6Req_{};
  bool gotIPv4_ = false;
  bool gotIPv6_ = false;
  bool done_ = false;
  int attempts_ = 0;
  uint64_t lastSendMs_ = 0;
  StunResult result_;

  static constexpr int kMaxAttempts = 3;
  static constexpr int kTimeoutMs = 2000;

  void sendRequests();
};
