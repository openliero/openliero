#include "iceBridge.hpp"
#include "iceAgent.hpp"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define BRIDGE_CLOSE closesocket
#define BRIDGE_WOULD_BLOCK (WSAGetLastError() == WSAEWOULDBLOCK)
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define BRIDGE_CLOSE close
#define BRIDGE_WOULD_BLOCK (errno == EAGAIN || errno == EWOULDBLOCK)
#endif

static constexpr int BRIDGE_BUFSIZE = 256 * 1024;

static bool setNonBlocking(int fd) {
#ifdef _WIN32
  u_long mode = 1;
  return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static bool setBufferSizes(int fd) {
  int sz = BRIDGE_BUFSIZE;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&sz), sizeof(sz));
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sz), sizeof(sz));
  return true;
}

IceBridge::~IceBridge() { destroy(); }

int IceBridge::create(IceAgent& agent) {
  agent_ = &agent;

  // Create two UDP sockets on localhost
  enetSocket_ = socket(AF_INET, SOCK_DGRAM, 0);
  bridgeSocket_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (enetSocket_ < 0 || bridgeSocket_ < 0) {
    destroy();
    return -1;
  }

  // Bind both to localhost with ephemeral ports
  sockaddr_in addrA{};
  addrA.sin_family = AF_INET;
  addrA.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addrA.sin_port = 0;

  sockaddr_in addrB{};
  addrB.sin_family = AF_INET;
  addrB.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addrB.sin_port = 0;

  if (bind(enetSocket_, reinterpret_cast<sockaddr*>(&addrA), sizeof(addrA)) < 0 ||
      bind(bridgeSocket_, reinterpret_cast<sockaddr*>(&addrB), sizeof(addrB)) < 0) {
    destroy();
    return -1;
  }

  // Get the assigned ports
  socklen_t len = sizeof(addrA);
  getsockname(enetSocket_, reinterpret_cast<sockaddr*>(&addrA), &len);
  len = sizeof(addrB);
  getsockname(bridgeSocket_, reinterpret_cast<sockaddr*>(&addrB), &len);
  bridgePort_ = ntohs(addrB.sin_port);

  // Connect each socket to the other's address (so send/recv work without specifying addr)
  if (connect(enetSocket_, reinterpret_cast<sockaddr*>(&addrB), sizeof(addrB)) < 0 ||
      connect(bridgeSocket_, reinterpret_cast<sockaddr*>(&addrA), sizeof(addrA)) < 0) {
    destroy();
    return -1;
  }

  // Configure sockets
  setNonBlocking(enetSocket_);
  setNonBlocking(bridgeSocket_);
  setBufferSizes(enetSocket_);
  setBufferSizes(bridgeSocket_);

  // Wire IceAgent's onRecv to write to enetSocket_ via bridgeSocket_
  // (bridgeSocket_ is connected to enetSocket_, so send() delivers to ENet)
  agent_->onRecv = [this](const uint8_t* data, size_t len) {
    // Safe from any thread — separate socket objects, no shared state
    ::send(bridgeSocket_, reinterpret_cast<const char*>(data), len, 0);
  };

  return enetSocket_;
}

void IceBridge::poll() {
  if (bridgeSocket_ < 0 || !agent_) return;

  // Read outgoing datagrams from ENet (via bridge socket) and forward to IceAgent
  uint8_t buf[2048];
  for (;;) {
    auto n = ::recv(bridgeSocket_, reinterpret_cast<char*>(buf), sizeof(buf), 0);
    if (n <= 0) {
      if (n < 0 && BRIDGE_WOULD_BLOCK) break;
      break;
    }
    agent_->send(buf, static_cast<size_t>(n));
  }
}

void IceBridge::destroy() {
  if (agent_) {
    agent_->onRecv = nullptr;
    agent_ = nullptr;
  }
  if (enetSocket_ >= 0) {
    BRIDGE_CLOSE(enetSocket_);
    enetSocket_ = -1;
  }
  if (bridgeSocket_ >= 0) {
    BRIDGE_CLOSE(bridgeSocket_);
    bridgeSocket_ = -1;
  }
  bridgePort_ = 0;
}
