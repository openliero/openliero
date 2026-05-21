#include "stun.hpp"

#include <enet.h>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <random>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

static constexpr const char* STUN_SERVER_IPV4 = "74.125.250.129";
static constexpr const char* STUN_SERVER_IPV6 = "2001:4860:4864:5:8000::1";
static constexpr uint16_t STUN_PORT = 19302;
static constexpr int STUN_TIMEOUT_MS = 2000;
static constexpr int STUN_RETRIES = 2;

static uint64_t nowMs() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

// --- stun namespace helpers ---

stun::Header stun::buildRequest() {
  Header req = {};
  req.type = htons(BINDING_REQUEST);
  req.length = 0;
  req.magicCookie = htonl(MAGIC_COOKIE);

  std::random_device rd;
  for (int i = 0; i < 12; i++)
    req.transactionId[i] = (uint8_t)(rd() & 0xFF);

  return req;
}

bool stun::isResponse(const uint8_t* data, size_t len, const Header& req) {
  if (len < sizeof(Header)) return false;
  auto* resp = (const Header*)data;
  if (ntohs(resp->type) != BINDING_RESPONSE) return false;
  if (ntohl(resp->magicCookie) != MAGIC_COOKIE) return false;
  return std::memcmp(resp->transactionId, req.transactionId, 12) == 0;
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

  StunMappedAddress xorResult;
  StunMappedAddress plainResult;

  while (offset + 4 <= attrTotalLen) {
    uint16_t attrType = (uint16_t)(attrs[offset] << 8 | attrs[offset + 1]);
    uint16_t attrLen = (uint16_t)(attrs[offset + 2] << 8 | attrs[offset + 3]);
    offset += 4;

    if (offset + attrLen > attrTotalLen) break;

    if (attrType == stun::ATTR_XOR_MAPPED_ADDRESS && attrLen >= 8) {
      uint8_t family = attrs[offset + 1];
      uint16_t xorPort = (uint16_t)(attrs[offset + 2] << 8 | attrs[offset + 3]);
      uint16_t mappedPort = xorPort ^ (uint16_t)(stun::MAGIC_COOKIE >> 16);

      if (family == 0x01 && attrLen >= 8) {
        uint32_t xorAddr;
        std::memcpy(&xorAddr, attrs + offset + 4, 4);
        uint32_t addr = ntohl(xorAddr) ^ stun::MAGIC_COOKIE;
        char buf[64];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                 (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
                 (addr >> 8) & 0xFF, addr & 0xFF);
        xorResult = {buf, mappedPort};
      } else if (family == 0x02 && attrLen >= 20) {
        uint8_t addrBytes[16];
        std::memcpy(addrBytes, attrs + offset + 4, 16);
        uint32_t cookie = htonl(stun::MAGIC_COOKIE);
        for (int i = 0; i < 4; i++)
          addrBytes[i] ^= ((uint8_t*)&cookie)[i];
        for (int i = 0; i < 12; i++)
          addrBytes[4 + i] ^= req.transactionId[i];
        char buf[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, addrBytes, buf, sizeof(buf));
        xorResult = {buf, mappedPort};
      }
    } else if (attrType == stun::ATTR_MAPPED_ADDRESS && attrLen >= 8) {
      uint8_t family = attrs[offset + 1];
      uint16_t mappedPort = (uint16_t)(attrs[offset + 2] << 8 | attrs[offset + 3]);

      if (family == 0x01) {
        uint32_t addr;
        std::memcpy(&addr, attrs + offset + 4, 4);
        addr = ntohl(addr);
        char buf[64];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                 (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
                 (addr >> 8) & 0xFF, addr & 0xFF);
        plainResult = {buf, mappedPort};
      }
    }

    offset += (attrLen + 3) & ~3u;
  }

  if (!xorResult.ip.empty()) return xorResult;
  return plainResult;
}

// --- StunQuery (background thread, own socket) ---

void StunQuery::start() {
  if (started_.exchange(true)) return;
  thread_ = std::thread(&StunQuery::run, this);
}

void StunQuery::start(uint16_t localPort) {
  if (started_.exchange(true)) return;
  localPort_ = localPort;
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

StunMappedAddress StunQuery::queryServer(const char* serverAddr, uint16_t port,
                                          uint16_t localPort) {
  enet_initialize();

  ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (sock == ENET_SOCKET_NULL) return {};

  if (localPort != 0) {
    enet_socket_set_option(sock, ENET_SOCKOPT_REUSEADDR, 1);
    ENetAddress localAddr = {};
    localAddr.port = localPort;
    memset(&localAddr.host, 0, sizeof(localAddr.host));
    enet_socket_bind(sock, &localAddr);
  }

  ENetAddress addr = {};
  addr.port = port;
  if (enet_address_set_host(&addr, serverAddr) != 0) {
    enet_socket_destroy(sock);
    return {};
  }

  stun::Header req = stun::buildRequest();

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

// --- StunViaHost (inline, uses ENet host socket) ---

void StunViaHost::start(ENetHost* host) {
  host_ = host;
  ipv4Req_ = stun::buildRequest();
  ipv6Req_ = stun::buildRequest();
  done_ = false;
  gotIPv4_ = false;
  gotIPv6_ = false;
  attempts_ = 0;
  lastSendMs_ = 0;
  result_ = {};
  sendRequests();
}

void StunViaHost::sendRequests() {
  if (!host_) return;
  attempts_++;
  lastSendMs_ = nowMs();

  if (!gotIPv4_) {
    ENetAddress addr = {};
    addr.port = STUN_PORT;
    if (enet_address_set_host(&addr, STUN_SERVER_IPV4) == 0) {
      ENetBuffer buf;
      buf.data = &ipv4Req_;
      buf.dataLength = sizeof(ipv4Req_);
      enet_socket_send(host_->socket, &addr, &buf, 1);
    }
  }

  if (!gotIPv6_) {
    ENetAddress addr = {};
    addr.port = STUN_PORT;
    if (enet_address_set_host(&addr, STUN_SERVER_IPV6) == 0) {
      ENetBuffer buf;
      buf.data = &ipv6Req_;
      buf.dataLength = sizeof(ipv6Req_);
      enet_socket_send(host_->socket, &addr, &buf, 1);
    }
  }
}

void StunViaHost::update() {
  if (done_) return;

  uint64_t now = nowMs();
  if (now - lastSendMs_ >= (uint64_t)kTimeoutMs) {
    if (attempts_ >= kMaxAttempts) {
      done_ = true;
      return;
    }
    sendRequests();
  }
}

bool StunViaHost::feedResponse(const uint8_t* data, size_t len) {
  if (done_) return false;
  if (len < sizeof(stun::Header)) return false;

  // Check magic cookie first (fast rejection)
  auto* hdr = (const stun::Header*)data;
  if (ntohl(hdr->magicCookie) != stun::MAGIC_COOKIE) return false;
  if (ntohs(hdr->type) != stun::BINDING_RESPONSE) return false;

  // Try matching against our IPv4 request
  if (!gotIPv4_ && stun::isResponse(data, len, ipv4Req_)) {
    auto mapped = stun::parseResponse(data, len, ipv4Req_);
    if (!mapped.ip.empty()) {
      result_.ipv4 = mapped.ip;
      result_.ipv4Port = mapped.port;
      gotIPv4_ = true;
      fprintf(stderr, "[stun-via-host] got IPv4: %s:%u\n", mapped.ip.c_str(), mapped.port);
    }
    if (gotIPv4_ && gotIPv6_) done_ = true;
    return true;
  }

  // Try matching against our IPv6 request
  if (!gotIPv6_ && stun::isResponse(data, len, ipv6Req_)) {
    auto mapped = stun::parseResponse(data, len, ipv6Req_);
    if (!mapped.ip.empty()) {
      result_.ipv6 = mapped.ip;
      result_.ipv6Port = mapped.port;
      gotIPv6_ = true;
      fprintf(stderr, "[stun-via-host] got IPv6: %s:%u\n", mapped.ip.c_str(), mapped.port);
    }
    if (gotIPv4_ && gotIPv6_) done_ = true;
    return true;
  }

  return false;
}
