#include "holepunch.hpp"

#include <enet.h>
#include <cstring>
#include <chrono>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

constexpr uint8_t HolePunch::PROBE_MAGIC[4];

static uint64_t nowMs() {
  return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

HolePunch::HolePunch()
    : sock_(-1), state_(Idle), timeoutMs_(5000), startTimeMs_(0), lastProbeMs_(0) {}

HolePunch::~HolePunch() {
  stop();
}

bool HolePunch::start(uint16_t localPort, const std::vector<PeerCandidate>& candidates) {
  if (candidates.empty()) return false;

  ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (sock == ENET_SOCKET_NULL) return false;

  // Bind to the same port as the game's ENet host
  ENetAddress localAddr = {};
  localAddr.port = localPort;
  memset(&localAddr.host, 0, sizeof(localAddr.host));

  // Allow address reuse so we can share the port with ENet
  enet_socket_set_option(sock, ENET_SOCKOPT_REUSEADDR, 1);

  if (enet_socket_bind(sock, &localAddr) != 0) {
    // Try without binding — NAT may still work if the OS assigns the same port
    // (common on Linux with SO_REUSEADDR)
  }

  enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);

  sock_ = (int)sock;
  candidates_ = candidates;
  state_ = Punching;
  startTimeMs_ = nowMs();
  lastProbeMs_ = 0;

  // Send initial probes immediately
  sendProbes();
  return true;
}

void HolePunch::sendProbes() {
  if (sock_ < 0) return;

  // Probe packet: 4 bytes magic + 2 bytes local port (for verification)
  uint8_t probe[6];
  std::memcpy(probe, PROBE_MAGIC, 4);
  // Leave port bytes as 0 — peer doesn't need them to validate

  ENetBuffer buf;
  buf.data = probe;
  buf.dataLength = sizeof(probe);

  for (auto& cand : candidates_) {
    ENetAddress addr = {};
    addr.port = cand.port;
    enet_address_set_host(&addr, cand.ip.c_str());
    enet_socket_send((ENetSocket)sock_, &addr, &buf, 1);
  }

  lastProbeMs_ = nowMs();
}

void HolePunch::poll() {
  if (state_ != Punching || sock_ < 0) return;

  uint64_t now = nowMs();

  // Check timeout
  if (now - startTimeMs_ > (uint64_t)timeoutMs_) {
    state_ = Failed;
    if (onTimeout) onTimeout();
    return;
  }

  // Send probes every 200ms
  if (now - lastProbeMs_ > 200) {
    sendProbes();
  }

  // Try to receive
  uint8_t recvData[64];
  ENetBuffer recvBuf;
  recvBuf.data = recvData;
  recvBuf.dataLength = sizeof(recvData);

  ENetAddress fromAddr = {};
  int recvLen = enet_socket_receive((ENetSocket)sock_, &fromAddr, &recvBuf, 1);
  if (recvLen < 4) return;

  // Verify magic
  if (std::memcmp(recvData, PROBE_MAGIC, 4) != 0) return;

  // Got a valid probe from peer — hole is punched!
  char ipStr[INET6_ADDRSTRLEN] = {};
  enet_address_get_host_ip(&fromAddr, ipStr, sizeof(ipStr));

  result_.peerIP = ipStr;
  result_.peerPort = fromAddr.port;
  state_ = Succeeded;

  if (onSuccess) onSuccess(result_);
}

void HolePunch::stop() {
  if (sock_ >= 0) {
    enet_socket_destroy((ENetSocket)sock_);
    sock_ = -1;
  }
  if (state_ == Punching)
    state_ = Failed;
}
