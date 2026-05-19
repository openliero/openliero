#include "holepunch.hpp"

#include <enet.h>
#include <cstring>
#include <cstdio>
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
  fprintf(stderr, "[holepunch] starting with localPort=%u, %zu candidates\n",
          localPort, candidates.size());
  for (size_t i = 0; i < candidates.size(); i++) {
    fprintf(stderr, "[holepunch]   candidate %zu: %s %s:%u\n",
            i, candidates[i].type == 4 ? "IPv4" : "IPv6",
            candidates[i].ip.c_str(), candidates[i].port);
  }

  if (candidates.empty()) {
    fprintf(stderr, "[holepunch] ERROR: no candidates\n");
    return false;
  }

  enet_initialize();

  ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (sock == ENET_SOCKET_NULL) {
    fprintf(stderr, "[holepunch] ERROR: enet_socket_create failed\n");
    return false;
  }
  fprintf(stderr, "[holepunch] socket created (fd=%d)\n", (int)sock);

  // Enable dual-stack
  int v6result = enet_socket_set_option(sock, ENET_SOCKOPT_IPV6_V6ONLY, 0);
  fprintf(stderr, "[holepunch] IPV6_V6ONLY=0 result: %d\n", v6result);

  // Bind to the same port as the game's ENet host
  ENetAddress localAddr = {};
  localAddr.port = localPort;
  memset(&localAddr.host, 0, sizeof(localAddr.host));

  // Allow address reuse so we can share the port with ENet
  enet_socket_set_option(sock, ENET_SOCKOPT_REUSEADDR, 1);

  int bindResult = enet_socket_bind(sock, &localAddr);
  if (bindResult != 0) {
    fprintf(stderr, "[holepunch] WARNING: bind to port %u failed (result=%d), using ephemeral\n",
            localPort, bindResult);
  } else {
    fprintf(stderr, "[holepunch] bound to port %u\n", localPort);
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

    char resolvedIP[INET6_ADDRSTRLEN] = {};
    enet_address_get_host_ip(&addr, resolvedIP, sizeof(resolvedIP));

    int sent = enet_socket_send((ENetSocket)sock_, &addr, &buf, 1);
    fprintf(stderr, "[holepunch] probe sent to %s:%u (resolved=%s) result=%d\n",
            cand.ip.c_str(), cand.port, resolvedIP, sent);
  }

  lastProbeMs_ = nowMs();
}

void HolePunch::poll() {
  if (state_ != Punching || sock_ < 0) return;

  uint64_t now = nowMs();

  // Check timeout
  if (now - startTimeMs_ > (uint64_t)timeoutMs_) {
    fprintf(stderr, "[holepunch] TIMEOUT after %u ms\n", timeoutMs_);
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

  // Log any received data
  char fromIP[INET6_ADDRSTRLEN] = {};
  enet_address_get_host_ip(&fromAddr, fromIP, sizeof(fromIP));
  fprintf(stderr, "[holepunch] received %d bytes from %s:%u\n", recvLen, fromIP, fromAddr.port);

  // Verify magic
  if (std::memcmp(recvData, PROBE_MAGIC, 4) != 0) {
    fprintf(stderr, "[holepunch] received data does NOT match probe magic (first 4 bytes: %02x %02x %02x %02x)\n",
            recvData[0], recvData[1], recvData[2], recvData[3]);
    return;
  }

  // Got a valid probe from peer — hole is punched!
  fprintf(stderr, "[holepunch] SUCCESS! Valid probe from %s:%u\n", fromIP, fromAddr.port);

  result_.peerIP = fromIP;
  result_.peerPort = fromAddr.port;
  state_ = Succeeded;

  if (onSuccess) onSuccess(result_);
}

void HolePunch::stop() {
  if (sock_ >= 0) {
    fprintf(stderr, "[holepunch] stopping (fd=%d)\n", sock_);
    enet_socket_destroy((ENetSocket)sock_);
    sock_ = -1;
  }
  if (state_ == Punching)
    state_ = Failed;
}
