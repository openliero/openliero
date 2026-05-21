#pragma once

#include "state.hpp"
#include "net/session.hpp"
#include "net/signaling.hpp"
#include "net/holepunch.hpp"
#include "net/stun.hpp"

#include <string>
#include <memory>
#include <cstdint>

// Manages the "online" connection flow via signaling server + hole-punching.
// Host flow: STUN → create room → show code → wait for peer → punch → game
// Client flow: STUN → join room → receive addrs → punch → game
struct OnlineConnectState : AppState
{
	OnlineConnectState(NetSession::Role role, std::string roomCode = "");

	void enter() override;
	void handleEvent(SDL_Event& ev) override;
	bool update() override;
	void draw() override;

private:
	void startPunching();
	void onPunchSuccess(const HolePunch::Result& result);
	void onPunchFailed();
	void connectDirect(const std::string& addr, uint16_t port);

	NetSession::Role role_;
	std::string roomCode_;
	uint16_t localPort_ = 19532;

	// Signaling server config (TODO: make configurable)
	std::string signalingServer_ = "liero-server.orbmit.org";
	uint16_t signalingPort_ = 19533;

	std::unique_ptr<StunQuery> stunQuery_;
	StunResult stunResult_;
	bool stunDone_ = false;

	SignalingClient signaling_;
	std::unique_ptr<HolePunch> holePunch_;

	std::string statusLine1_;
	std::string statusLine2_;
	bool cancel_ = false;
	bool startedPunch_ = false;
	bool punchRequested_ = false;
	bool reportedPunchFail_ = false;
	uint64_t lastKeepaliveMs_ = 0;
	uint64_t peerJoinedMs_ = 0;
};
