#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

struct StunResult {
  std::string ipv4;        // External IPv4, or empty if unavailable
  uint16_t ipv4Port = 0;   // External port for IPv4 mapping (0 if unavailable)
  std::string ipv6;        // External IPv6, or empty if unavailable
  uint16_t ipv6Port = 0;   // External port for IPv6 mapping (0 if unavailable)
};

struct StunMappedAddress {
  std::string ip;
  uint16_t port = 0;
};

// STUN protocol constants (RFC 5389) — exposed for testing
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
  // req is the original request header (needed for XOR unmasking).
  StunMappedAddress parseResponse(const uint8_t* data, size_t len, const Header& req);
}

// Minimal STUN client (RFC 5389) for discovering external IP addresses.
// Queries a public STUN server over both IPv4 and IPv6 to discover
// the external address for each protocol.
class StunQuery {
public:
  void start();

  // Start STUN queries bound to a specific local port.
  // This ensures the NAT mapping corresponds to the game's listening port.
  void start(uint16_t localPort);

  // Returns the results collected so far.
  StunResult result() const;

  // True if the query has completed (success or failure).
  bool done() const { return done_.load(); }

  ~StunQuery();

private:
  // Query a specific STUN server address (IPv4 or IPv6 literal)
  static StunMappedAddress queryServer(const char* serverAddr, uint16_t port,
                                        uint16_t localPort = 0);

  void run();

  std::thread thread_;
  std::atomic<bool> done_{false};
  mutable std::mutex mutex_;
  StunResult result_;
  uint16_t localPort_{0};
};
