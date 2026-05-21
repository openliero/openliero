#pragma once

#include <cstdint>

struct IceAgent;
struct _ENetHost;

// Loopback UDP bridge between ENet and IceAgent.
//
// Creates two connected localhost UDP sockets:
//   - enetSocket_: given to ENet as host->socket
//   - bridgeSocket_: our side — reads ENet outbound → juice_send(),
//     and IceAgent's onRecv writes inbound → enetSocket_
//
// This lets ENet operate unmodified while libjuice handles the real network.
struct IceBridge {
  IceBridge() = default;
  ~IceBridge();

  IceBridge(const IceBridge&) = delete;
  IceBridge& operator=(const IceBridge&) = delete;

  // Create the bridge socket pair and wire up the IceAgent's onRecv callback.
  // Returns the ENet-side socket fd (to replace host->socket), or -1 on error.
  int create(IceAgent& agent);

  // Poll: read outgoing ENet data from bridge socket → juice_send().
  // Call once per frame from the main loop.
  void poll();

  // Get the ENet-side socket fd (for replacing host->socket after enet_host_create).
  int enetSocket() const { return enetSocket_; }

  // Get the port that ENet should "connect" to (the bridge's listening port).
  uint16_t bridgePort() const { return bridgePort_; }

  // Destroy both sockets.
  void destroy();

private:
  int enetSocket_ = -1;
  int bridgeSocket_ = -1;
  uint16_t bridgePort_ = 0;
  IceAgent* agent_ = nullptr;
};
