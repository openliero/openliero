#include "signaling.hpp"

#include <enet.h>
#include <cstring>

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
}

SignalingClient::SignalingClient()
    : sock_(-1), state_(Idle), serverPort_(0), relayPort_(0) {}

SignalingClient::~SignalingClient() {
  disconnect();
}

bool SignalingClient::connect(const std::string& serverAddr, uint16_t serverPort) {
  // Ensure ENet is initialized (needed for WSAStartup on Windows)
  enet_initialize();

  ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (sock == ENET_SOCKET_NULL) return false;

  // Set non-blocking
  enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);

  // Resolve server address once
  ENetAddress resolved = {};
  resolved.port = serverPort;
  if (enet_address_set_host(&resolved, serverAddr.c_str()) != 0) {
    enet_socket_destroy(sock);
    return false;
  }

  sock_ = (int)sock;
  serverAddr_ = serverAddr;
  serverPort_ = serverPort;
  static_assert(sizeof(resolvedAddrStorage_) >= sizeof(ENetAddress));
  std::memcpy(resolvedAddrStorage_, &resolved, sizeof(ENetAddress));
  return true;
}

void SignalingClient::disconnect() {
  if (sock_ >= 0) {
    enet_socket_destroy((ENetSocket)sock_);
    sock_ = -1;
  }
  state_ = Idle;
  roomCode_.clear();
  peerCandidates_.clear();
  relayPort_ = 0;
}

void SignalingClient::send(const void* data, size_t len) {
  if (sock_ < 0) return;

  ENetBuffer buf;
  buf.data = const_cast<void*>(data);
  buf.dataLength = len;
  auto* addr = reinterpret_cast<ENetAddress*>(resolvedAddrStorage_);
  enet_socket_send((ENetSocket)sock_, addr, &buf, 1);
}

bool SignalingClient::createRoom(const std::string& serverAddr, uint16_t serverPort) {
  if (!connect(serverAddr, serverPort)) return false;

  uint8_t msg[1 + proto::RoomCodeLen] = {};
  msg[0] = proto::CreateRoom;
  send(msg, sizeof(msg));
  state_ = Creating;
  return true;
}

bool SignalingClient::joinRoom(const std::string& serverAddr, uint16_t serverPort,
                               const std::string& roomCode) {
  if (!connect(serverAddr, serverPort)) return false;
  if (roomCode.size() != proto::RoomCodeLen) return false;

  uint8_t msg[1 + proto::RoomCodeLen];
  msg[0] = proto::JoinRoom;
  std::memcpy(msg + 1, roomCode.data(), proto::RoomCodeLen);
  send(msg, sizeof(msg));
  roomCode_ = roomCode;
  state_ = Joining;
  return true;
}

void SignalingClient::reportAddress(uint8_t addrType, const std::string& ip, uint16_t port) {
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
  uint8_t msg[1 + proto::RoomCodeLen];
  msg[0] = proto::PunchOK;
  std::memcpy(msg + 1, roomCode_.data(), proto::RoomCodeLen);
  send(msg, sizeof(msg));
  state_ = Done;
}

void SignalingClient::reportPunchFail() {
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
  if (sock_ < 0) return;

  uint8_t recvData[512];
  ENetBuffer recvBuf;
  recvBuf.data = recvData;
  recvBuf.dataLength = sizeof(recvData);

  ENetAddress fromAddr = {};
  int recvLen = enet_socket_receive((ENetSocket)sock_, &fromAddr, &recvBuf, 1);
  if (recvLen <= 0) return;

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
      if (onRoomCreated) onRoomCreated(roomCode_);
      break;
    }
    case proto::PeerJoined: {
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

      PeerCandidate cand{addrType, ipStr, port};
      peerCandidates_.push_back(cand);
      if (onPeerAddr) onPeerAddr(cand);
      break;
    }
    case proto::StartPunch: {
      state_ = Punching;
      if (onStartPunch) onStartPunch();
      break;
    }
    case proto::UseRelay: {
      if (len < 1 + proto::RoomCodeLen + 2) break;
      relayPort_ = (uint16_t)(data[1 + proto::RoomCodeLen] << 8 |
                               data[1 + proto::RoomCodeLen + 1]);
      state_ = Relaying;
      if (onUseRelay) onUseRelay(relayPort_);
      break;
    }
    case proto::RoomExpired: {
      state_ = Failed;
      if (onRoomExpired) onRoomExpired();
      break;
    }
    case proto::Error: {
      std::string msg;
      if (len > 2)
        msg = std::string((const char*)data + 2, len - 2);
      state_ = Failed;
      if (onError) onError(msg);
      break;
    }
  }
}
