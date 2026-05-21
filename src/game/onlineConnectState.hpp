#pragma once

#include "state.hpp"
#include "net/session.hpp"
#include "net/signaling.hpp"
#include "net/stun.hpp"
#include "net/transport.hpp"

#include <string>
#include <memory>
#include <cstdint>

// Manages the "online" connection flow via signaling server + hole-punching.
//
// Architecture: Single-socket design.
// 1. Create ENet host on game port FIRST
// 2. STUN through ENet socket (via intercept callback)
// 3. Signaling (separate ephemeral socket — doesn't need NAT mapping)
// 4. Hole-punch through ENet socket (via intercept callback)
// 5. On punch success: peer connects via ENet on same socket — NAT mapping preserved
// 6. On failure: relay (token sent via ENet socket, then ENet connects to relay)
struct OnlineConnectState : AppState
{
	OnlineConnectState(NetSession::Role role, std::string roomCode = "");

	void enter() override;
	void handleEvent(SDL_Event& ev) override;
	bool update() override;
	void draw() override;

private:
	void startPunching();
	void onPunchSuccess(const NetTransport::PunchResult& result);
	void onPunchFailed();
	void connectDirect(const std::string& addr, uint16_t port);
	void connectRelay();

	NetSession::Role role_;
	std::string roomCode_;
	uint16_t localPort_ = 19532;

	std::string signalingServer_ = "liero-server.orbmit.org";
	uint16_t signalingPort_ = 19533;

	// ENet host created FIRST for single-socket
	NetTransport transport_;
	StunViaHost stunViaHost_;
	bool stunDone_ = false;

	SignalingClient signaling_;

	std::string statusLine1_;
	std::string statusLine2_;
	bool cancel_ = false;
	bool startedPunch_ = false;
	bool punchRequested_ = false;
	bool reportedPunchFail_ = false;
	uint64_t lastKeepaliveMs_ = 0;
	uint64_t peerJoinedMs_ = 0;
};
