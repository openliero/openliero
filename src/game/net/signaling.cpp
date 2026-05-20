#include "signaling.hpp"

#include <enet.h>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

// Protocol constants (must match server/protocol.go)
namespace proto {
  // Client → Server
  constexpr uint8_t CreateRoom  = 0x01;
  constexpr uint8_t JoinRoom    = 0x02;
  constexpr uint8_t ReportAddr  = 0x03;
  constexpr uint8_t PunchOK     = 0x04;
  constexpr uint8_t PunchFail   = 0x05;
  constexpr uint8_t Keepalive   = 0x06;

  // Server → Client
  constexpr uint8_t RoomCreated = 0x81;
  constexpr uint8_t PeerJoined  = 0x82;
  constexpr uint8_t PeerAddr    = 0x83;
  constexpr uint8_t StartPunch  = 0x84;
  constexpr uint8_t UseRelay    = 0x85;
  constexpr uint8_t RoomExpired = 0x86;
  constexpr uint8_t Error       = 0x8F;

  constexpr int RoomCodeLen = 6;

  constexpr uint8_t AddrIPv4 = 4;
  constexpr uint8_t AddrIPv6 = 6;

  static const char* msgName(uint8_t type) {
    switch (type) {
      case CreateRoom: return "CreateRoom";
      case JoinRoom: return "JoinRoom";
      case ReportAddr: return "ReportAddr";
      case PunchOK: return "PunchOK";
      case PunchFail: return "PunchFail";
      case Keepalive: return "Keepalive";
      case RoomCreated: return "RoomCreated";
      case PeerJoined: return "PeerJoined";
      case PeerAddr: return "PeerAddr";
      case StartPunch: return "StartPunch";
      case UseRelay: return "UseRelay";
      case RoomExpired: return "RoomExpired";
      case Error: return "Error";
      default: return "Unknown";
    }
  }
}

SignalingClient::SignalingClient()
    : sock_(ENET_SOCKET_NULL), state_(Idle), serverPort_(0), relayPort_(0) {}

SignalingClient::~SignalingClient() {
  disconnect();
}

bool SignalingClient::connect(const std::string& serverAddr, uint16_t serverPort) {
  fprintf(stderr, "[signaling] connecting to %s:%u\n", serverAddr.c_str(), serverPort);

  // Ensure ENet is initialized (needed for WSAStartup on Windows)
  enet_initialize();

  ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (sock == ENET_SOCKET_NULL) {
    fprintf(stderr, "[signaling] ERROR: enet_socket_create failed\n");
    return false;
  }
  fprintf(stderr, "[signaling] socket created (fd=%d)\n", (int)sock);

  // Enable dual-stack (IPv4 via mapped addresses on IPv6 socket)
  // Required on Windows where IPV6_V6ONLY defaults to 1
  int v6result = enet_socket_set_option(sock, ENET_SOCKOPT_IPV6_V6ONLY, 0);
  fprintf(stderr, "[signaling] IPV6_V6ONLY=0 result: %d\n", v6result);

  // Bind to any local port — required on Windows for receiving replies
  ENetAddress anyAddr = {};
  anyAddr.port = 0;
  memset(&anyAddr.host, 0, sizeof(anyAddr.host));
  int bindResult = enet_socket_bind(sock, &anyAddr);
  fprintf(stderr, "[signaling] bind(port=0) result: %d\n", bindResult);

  // Resolve server address once
  ENetAddress resolved = {};
  resolved.port = serverPort;
  if (enet_address_set_host(&resolved, serverAddr.c_str()) != 0) {
    fprintf(stderr, "[signaling] ERROR: failed to resolve '%s'\n", serverAddr.c_str());
    enet_socket_destroy(sock);
    return false;
  }

  char resolvedIP[INET6_ADDRSTRLEN] = {};
  enet_address_get_host_ip(&resolved, resolvedIP, sizeof(resolvedIP));
  fprintf(stderr, "[signaling] resolved server to %s:%u\n", resolvedIP, serverPort);

  sock_ = sock;
  serverAddr_ = serverAddr;
  serverPort_ = serverPort;
  resolvedAddr_ = resolved;

  fprintf(stderr, "[signaling] connection setup complete\n");
  return true;
}

void SignalingClient::disconnect() {
  if (sock_ != ENET_SOCKET_NULL) {
    fprintf(stderr, "[signaling] disconnecting (fd=%d)\n", (int)sock_);
    enet_socket_destroy(sock_);
    sock_ = ENET_SOCKET_NULL;
  }
  state_ = Idle;
  roomCode_.clear();
  peerCandidates_.clear();
  relayPort_ = 0;
}

void SignalingClient::send(const void* data, size_t len) {
  if (sock_ == ENET_SOCKET_NULL) return;

  const uint8_t* bytes = (const uint8_t*)data;
  fprintf(stderr, "[signaling] SEND %s (%zu bytes)\n",
          len > 0 ? proto::msgName(bytes[0]) : "empty", len);

  ENetBuffer buf;
  buf.data = const_cast<void*>(data);
  buf.dataLength = len;
  int sent = enet_socket_send(sock_, &resolvedAddr_, &buf, 1);
  if (sent < 0) {
    fprintf(stderr, "[signaling] ERROR: send failed (returned %d)\n", sent);
  } else {
    fprintf(stderr, "[signaling] sent %d bytes\n", sent);
  }
}

bool SignalingClient::createRoom(const std::string& serverAddr, uint16_t serverPort) {
  fprintf(stderr, "[signaling] createRoom on %s:%u\n", serverAddr.c_str(), serverPort);
  if (!connect(serverAddr, serverPort)) return false;

  uint8_t msg[1 + proto::RoomCodeLen] = {};
  msg[0] = proto::CreateRoom;
  send(msg, sizeof(msg));
  state_ = Creating;
  return true;
}

bool SignalingClient::joinRoom(const std::string& serverAddr, uint16_t serverPort,
                               const std::string& roomCode) {
  fprintf(stderr, "[signaling] joinRoom '%s' on %s:%u\n",
          roomCode.c_str(), serverAddr.c_str(), serverPort);
  if (!connect(serverAddr, serverPort)) return false;
  if (roomCode.size() != proto::RoomCodeLen) {
    fprintf(stderr, "[signaling] ERROR: room code '%s' is %zu chars (need %d)\n",
            roomCode.c_str(), roomCode.size(), proto::RoomCodeLen);
    return false;
  }

  uint8_t msg[1 + proto::RoomCodeLen];
  msg[0] = proto::JoinRoom;
  std::memcpy(msg + 1, roomCode.data(), proto::RoomCodeLen);
  send(msg, sizeof(msg));
  roomCode_ = roomCode;
  state_ = Joining;
  return true;
}

void SignalingClient::reportAddress(uint8_t addrType, const std::string& ip, uint16_t port) {
  fprintf(stderr, "[signaling] reportAddress: type=%s ip=%s port=%u room=%s\n",
          addrType == proto::AddrIPv4 ? "IPv4" : "IPv6",
          ip.c_str(), port, roomCode_.c_str());

  int ipLen = (addrType == proto::AddrIPv4) ? 4 : 16;
  std::vector<uint8_t> msg(1 + proto::RoomCodeLen + 1 + 2 + ipLen);
  msg[0] = proto::ReportAddr;
  std::memcpy(msg.data() + 1, roomCode_.data(), proto::RoomCodeLen);
  msg[1 + proto::RoomCodeLen] = addrType;
  msg[1 + proto::RoomCodeLen + 1] = (uint8_t)(port >> 8);
  msg[1 + proto::RoomCodeLen + 2] = (uint8_t)(port & 0xFF);

  uint8_t ipBytes[16] = {};
  if (addrType == proto::AddrIPv4) {
    inet_pton(AF_INET, ip.c_str(), ipBytes);
  } else {
    inet_pton(AF_INET6, ip.c_str(), ipBytes);
  }
  std::memcpy(msg.data() + 1 + proto::RoomCodeLen + 3, ipBytes, ipLen);
  send(msg.data(), msg.size());
}

void SignalingClient::reportPunchOK() {
  fprintf(stderr, "[signaling] reportPunchOK room=%s\n", roomCode_.c_str());
  uint8_t msg[1 + proto::RoomCodeLen];
  msg[0] = proto::PunchOK;
  std::memcpy(msg + 1, roomCode_.data(), proto::RoomCodeLen);
  send(msg, sizeof(msg));
  state_ = Done;
}

void SignalingClient::reportPunchFail() {
  fprintf(stderr, "[signaling] reportPunchFail room=%s\n", roomCode_.c_str());
  uint8_t msg[1 + proto::RoomCodeLen];
  msg[0] = proto::PunchFail;
  std::memcpy(msg + 1, roomCode_.data(), proto::RoomCodeLen);
  send(msg, sizeof(msg));
}

void SignalingClient::sendKeepalive() {
  uint8_t msg[1 + proto::RoomCodeLen];
  msg[0] = proto::Keepalive;
  std::memcpy(msg + 1, roomCode_.data(), proto::RoomCodeLen);
  send(msg, sizeof(msg));
}

void SignalingClient::poll() {
  if (sock_ == ENET_SOCKET_NULL) return;

  // Check if data is available (non-blocking via zero timeout wait)
  enet_uint32 waitCondition = ENET_SOCKET_WAIT_RECEIVE;
  int waitResult = enet_socket_wait(sock_, &waitCondition, 0);
  if (waitResult != 0) {
    if (pollErrCount_++ < 3)
      fprintf(stderr, "[signaling] poll: enet_socket_wait returned error %d\n", waitResult);
    return;
  }
  if (!(waitCondition & ENET_SOCKET_WAIT_RECEIVE))
    return;

  uint8_t recvData[512];
  ENetBuffer recvBuf;
  recvBuf.data = recvData;
  recvBuf.dataLength = sizeof(recvData);

  ENetAddress fromAddr = {};
  int recvLen = enet_socket_receive(sock_, &fromAddr, &recvBuf, 1);
  if (recvLen <= 0) {
    fprintf(stderr, "[signaling] poll: receive returned %d (wait said data available)\n", recvLen);
    return;
  }

  char fromIP[INET6_ADDRSTRLEN] = {};
  enet_address_get_host_ip(&fromAddr, fromIP, sizeof(fromIP));
  fprintf(stderr, "[signaling] RECV %s (%d bytes) from %s:%u\n",
          proto::msgName(recvData[0]), recvLen, fromIP, fromAddr.port);

  handleMessage(recvData, (size_t)recvLen);
}

void SignalingClient::handleMessage(const uint8_t* data, size_t len) {
  if (len < 1) return;

  uint8_t type = data[0];

  switch (type) {
    case proto::RoomCreated: {
      if (len < 1 + proto::RoomCodeLen) break;
      roomCode_ = std::string((const char*)data + 1, proto::RoomCodeLen);
      state_ = Hosting;
      fprintf(stderr, "[signaling] room created: %s\n", roomCode_.c_str());
      if (onRoomCreated) onRoomCreated(roomCode_);
      break;
    }
    case proto::PeerJoined: {
      fprintf(stderr, "[signaling] peer joined our room\n");
      if (onPeerJoined) onPeerJoined();
      break;
    }
    case proto::PeerAddr: {
      if (len < 1 + proto::RoomCodeLen + 1 + 2 + 4) break;
      uint8_t addrType = data[1 + proto::RoomCodeLen];
      uint16_t port = (uint16_t)(data[1 + proto::RoomCodeLen + 1] << 8 |
                                  data[1 + proto::RoomCodeLen + 2]);
      int ipLen = (addrType == proto::AddrIPv4) ? 4 : 16;
      if ((int)len < 1 + proto::RoomCodeLen + 3 + ipLen) break;

      char ipStr[INET6_ADDRSTRLEN] = {};
      if (addrType == proto::AddrIPv4) {
        inet_ntop(AF_INET, data + 1 + proto::RoomCodeLen + 3, ipStr, sizeof(ipStr));
      } else {
        inet_ntop(AF_INET6, data + 1 + proto::RoomCodeLen + 3, ipStr, sizeof(ipStr));
      }

      fprintf(stderr, "[signaling] received peer address: %s %s:%u\n",
              addrType == proto::AddrIPv4 ? "IPv4" : "IPv6", ipStr, port);

      PeerCandidate cand{addrType, ipStr, port};
      peerCandidates_.push_back(cand);
      if (onPeerAddr) onPeerAddr(cand);
      break;
    }
    case proto::StartPunch: {
      fprintf(stderr, "[signaling] received StartPunch — beginning hole-punch\n");
      state_ = Punching;
      if (onStartPunch) onStartPunch();
      break;
    }
    case proto::UseRelay: {
      if (len < 1 + proto::RoomCodeLen + 2 + 8) break;
      relayPort_ = (uint16_t)(data[1 + proto::RoomCodeLen] << 8 |
                               data[1 + proto::RoomCodeLen + 1]);
      relayToken_.assign(data + 1 + proto::RoomCodeLen + 2,
                         data + 1 + proto::RoomCodeLen + 2 + 8);
      fprintf(stderr, "[signaling] received UseRelay — relay port %u (token %zu bytes)\n",
              relayPort_, relayToken_.size());
      state_ = Relaying;
      if (onUseRelay) onUseRelay(relayPort_);
      break;
    }
    case proto::RoomExpired: {
      fprintf(stderr, "[signaling] room expired\n");
      state_ = Failed;
      if (onRoomExpired) onRoomExpired();
      break;
    }
    case proto::Error: {
      std::string msg;
      if (len > 2)
        msg = std::string((const char*)data + 2, len - 2);
      fprintf(stderr, "[signaling] ERROR from server: code=0x%02x msg='%s'\n",
              len > 1 ? data[1] : 0, msg.c_str());
      state_ = Failed;
      if (onError) onError(msg);
      break;
    }
    default:
      fprintf(stderr, "[signaling] unknown message type 0x%02x (%d bytes)\n", type, (int)len);
      break;
  }
}
