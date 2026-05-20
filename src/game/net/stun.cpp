#include "stun.hpp"

#include <enet.h>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <random>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

// Google's STUN server has well-known IPv4 and IPv6 addresses
static constexpr const char* STUN_SERVER_IPV4 = "74.125.250.129";
static constexpr const char* STUN_SERVER_IPV6 = "2001:4860:4864:5:8000::1";
static constexpr uint16_t STUN_PORT = 19302;
static constexpr int STUN_TIMEOUT_MS = 2000;
static constexpr int STUN_RETRIES = 2;

void StunQuery::start() {
  // Guard against double-call: assigning a new thread without joining
  // the old one would cause std::terminate.
  if (started_.exchange(true)) return;
  thread_ = std::thread(&StunQuery::run, this);
}

void StunQuery::start(uint16_t localPort) {
  localPort_ = localPort;
  if (started_.exchange(true)) return;
  thread_ = std::thread(&StunQuery::run, this);
}

StunQuery::~StunQuery() {
  if (thread_.joinable())
    thread_.join();
}

StunResult StunQuery::result() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return result_;
}

StunMappedAddress stun::parseResponse(const uint8_t* data, size_t len,
                                  const stun::Header& req) {
  if (len < sizeof(stun::Header)) return {};

  auto* resp = (const stun::Header*)data;
  if (ntohs(resp->type) != stun::BINDING_RESPONSE) return {};
  if (ntohl(resp->magicCookie) != stun::MAGIC_COOKIE) return {};
  if (std::memcmp(resp->transactionId, req.transactionId, 12) != 0) return {};

  uint16_t attrTotalLen = ntohs(resp->length);
  if (sizeof(stun::Header) + attrTotalLen > len) return {};

  const uint8_t* attrs = data + sizeof(stun::Header);
  size_t offset = 0;

  while (offset + 4 <= attrTotalLen) {
    uint16_t attrType = (uint16_t)(attrs[offset] << 8 | attrs[offset + 1]);
    uint16_t attrLen = (uint16_t)(attrs[offset + 2] << 8 | attrs[offset + 3]);
    offset += 4;

    if (offset + attrLen > attrTotalLen) break;

    if (attrType == stun::ATTR_XOR_MAPPED_ADDRESS && attrLen >= 8) {
      uint8_t family = attrs[offset + 1];
      uint16_t xorPort = (uint16_t)(attrs[offset + 2] << 8 | attrs[offset + 3]);
      uint16_t mappedPort = xorPort ^ (uint16_t)(stun::MAGIC_COOKIE >> 16);

      if (family == 0x01 && attrLen >= 8) { // IPv4
        uint32_t xorAddr;
        std::memcpy(&xorAddr, attrs + offset + 4, 4);
        uint32_t addr = ntohl(xorAddr) ^ stun::MAGIC_COOKIE;
        char buf[64];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                 (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
                 (addr >> 8) & 0xFF, addr & 0xFF);
        return {buf, mappedPort};
      } else if (family == 0x02 && attrLen >= 20) { // IPv6
        uint8_t addrBytes[16];
        std::memcpy(addrBytes, attrs + offset + 4, 16);
        uint32_t cookie = htonl(stun::MAGIC_COOKIE);
        for (int i = 0; i < 4; i++)
          addrBytes[i] ^= ((uint8_t*)&cookie)[i];
        for (int i = 0; i < 12; i++)
          addrBytes[4 + i] ^= req.transactionId[i];
        char buf[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, addrBytes, buf, sizeof(buf));
        return {buf, mappedPort};
      }
    } else if (attrType == stun::ATTR_MAPPED_ADDRESS && attrLen >= 8) {
      uint8_t family = attrs[offset + 1];
      uint16_t mappedPort = (uint16_t)(attrs[offset + 2] << 8 | attrs[offset + 3]);

      if (family == 0x01) { // IPv4
        uint32_t addr;
        std::memcpy(&addr, attrs + offset + 4, 4);
        addr = ntohl(addr);
        char buf[64];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                 (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
                 (addr >> 8) & 0xFF, addr & 0xFF);
        return {buf, mappedPort};
      }
    }

    // Attributes are padded to 4-byte boundaries
    offset += (attrLen + 3) & ~3u;
  }
  return {};
}

StunMappedAddress StunQuery::queryServer(const char* serverAddr, uint16_t port,
                                          uint16_t localPort) {
  ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (sock == ENET_SOCKET_NULL) return {};

  // Bind to specific local port if requested (for hole-punching: NAT mapping
  // must correspond to the game's listening port).
  // SO_REUSEADDR is needed so both IPv4 and IPv6 queries can bind the same port.
  if (localPort != 0) {
    enet_socket_set_option(sock, ENET_SOCKOPT_REUSEADDR, 1);
    ENetAddress localAddr = {};
    localAddr.port = localPort;
    memset(&localAddr.host, 0, sizeof(localAddr.host));
    if (enet_socket_bind(sock, &localAddr) != 0) {
      // Fall back to ephemeral port if bind fails
    }
  }

  ENetAddress addr = {};
  addr.port = port;
  if (enet_address_set_host(&addr, serverAddr) != 0) {
    enet_socket_destroy(sock);
    return {};
  }

  // Build STUN Binding Request
  stun::Header req = {};
  req.type = htons(stun::BINDING_REQUEST);
  req.length = 0;
  req.magicCookie = htonl(stun::MAGIC_COOKIE);

  std::random_device rd;
  for (int i = 0; i < 12; i++)
    req.transactionId[i] = (uint8_t)(rd() & 0xFF);

  ENetBuffer sendBuf;
  sendBuf.data = &req;
  sendBuf.dataLength = sizeof(req);

  uint8_t recvData[512];
  ENetBuffer recvBuf;
  recvBuf.data = recvData;
  recvBuf.dataLength = sizeof(recvData);

  StunMappedAddress result;
  for (int attempt = 0; attempt < STUN_RETRIES && result.ip.empty(); ++attempt) {
    int sent = enet_socket_send(sock, &addr, &sendBuf, 1);
    if (sent < 0) break;

    enet_uint32 waitCondition = ENET_SOCKET_WAIT_RECEIVE;
    if (enet_socket_wait(sock, &waitCondition, STUN_TIMEOUT_MS) != 0)
      continue;
    if (!(waitCondition & ENET_SOCKET_WAIT_RECEIVE))
      continue;

    ENetAddress fromAddr = {};
    int recvLen = enet_socket_receive(sock, &fromAddr, &recvBuf, 1);
    if (recvLen < (int)sizeof(stun::Header)) continue;

    result = stun::parseResponse(recvData, (size_t)recvLen, req);
  }

  enet_socket_destroy(sock);
  return result;
}

void StunQuery::run() {
  StunResult res;
  auto v4 = queryServer(STUN_SERVER_IPV4, STUN_PORT, localPort_);
  res.ipv4 = v4.ip;
  res.ipv4Port = v4.port;
  auto v6 = queryServer(STUN_SERVER_IPV6, STUN_PORT, localPort_);
  res.ipv6 = v6.ip;
  res.ipv6Port = v6.port;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    result_ = res;
  }
  done_.store(true);
}
