#include "onlineConnectState.hpp"

#include "gfx.hpp"
#include "text.hpp"
#include "keys.hpp"
#include "common.hpp"
#include "netConnectState.hpp"

#include <memory>
#include <string>
#include <chrono>
#include <cstdio>
#include <random>

OnlineConnectState::OnlineConnectState(NetSession::Role role, std::string roomCode)
: role_(role)
, roomCode_(std::move(roomCode))
{
}

void OnlineConnectState::enter()
{
	localPort_ = gfx->onlinePort;
	fprintf(stderr, "[online] enter: role=%s localPort=%u roomCode='%s'\n",
	        role_ == NetSession::Host ? "Host" : "Client", localPort_, roomCode_.c_str());

	statusLine1_ = (role_ == NetSession::Host)
		? "CREATING ONLINE ROOM..."
		: "JOINING ROOM " + roomCode_ + "...";
	statusLine2_ = "DISCOVERING EXTERNAL ADDRESS...";

	// Start STUN query from the game port
	stunQuery_ = std::make_unique<StunQuery>();
	stunQuery_->start(localPort_);
	fprintf(stderr, "[online] STUN query started from port %u\n", localPort_);

	// Wire up signaling callbacks
	signaling_.onRoomCreated = [this](const std::string& code) {
		fprintf(stderr, "[online] onRoomCreated: code=%s\n", code.c_str());
		roomCode_ = code;
		statusLine1_ = "ROOM CODE: " + code;
		statusLine2_ = "SHARE THIS CODE WITH YOUR PEER";

		// Now that we have a room code, report our STUN addresses
		fprintf(stderr, "[online] reporting STUN results: ipv4='%s':%u ipv6='%s':%u\n",
		        stunResult_.ipv4.c_str(), stunResult_.ipv4Port,
		        stunResult_.ipv6.c_str(), stunResult_.ipv6Port);
		if (!stunResult_.ipv4.empty())
			signaling_.reportAddress(4, stunResult_.ipv4, stunResult_.ipv4Port);
		if (!stunResult_.ipv6.empty())
			signaling_.reportAddress(6, stunResult_.ipv6, stunResult_.ipv6Port);
	};

	signaling_.onPeerJoined = [this]() {
		fprintf(stderr, "[online] onPeerJoined\n");
		statusLine2_ = "PEER JOINED! CONNECTING...";
	};

	signaling_.onJoinAcked = [this]() {
		fprintf(stderr, "[online] join acknowledged, reporting addresses\n");
		statusLine2_ = "JOINED! WAITING FOR HOST ADDRESSES...";
		if (!stunResult_.ipv4.empty())
			signaling_.reportAddress(4, stunResult_.ipv4, stunResult_.ipv4Port);
		if (!stunResult_.ipv6.empty())
			signaling_.reportAddress(6, stunResult_.ipv6, stunResult_.ipv6Port);
	};

	signaling_.onStartPunch = [this]() {
		fprintf(stderr, "[online] onStartPunch — %zu peer candidates\n",
		        signaling_.peerCandidates().size());
		punchRequested_ = true;
		if (!signaling_.peerCandidates().empty())
			startPunching();
	};

	signaling_.onPeerAddr = [this](const PeerCandidate&) {
		// If punch was requested but we had no candidates yet, start now
		if (punchRequested_ && !startedPunch_)
			startPunching();
	};

	signaling_.onUseRelay = [this](uint16_t relayPort) {
		fprintf(stderr, "[online] onUseRelay: port=%u\n", relayPort);
		statusLine2_ = "USING RELAY (HIGHER LATENCY)";
		// Release hole-punch socket before transitioning
		if (holePunch_) { holePunch_->stop(); holePunch_.reset(); }
		connectDirect(signalingServer_, relayPort);
	};

	signaling_.onError = [this](const std::string& msg) {
		fprintf(stderr, "[online] onError: %s\n", msg.c_str());
		statusLine2_ = "ERROR: " + msg;
		cancel_ = true;
	};

	signaling_.onRoomExpired = [this]() {
		fprintf(stderr, "[online] onRoomExpired\n");
		statusLine2_ = "ROOM EXPIRED";
		cancel_ = true;
	};
}

void OnlineConnectState::handleEvent(SDL_Event& ev)
{
	gfx->processEvent(ev);
}

bool OnlineConnectState::update()
{
	if (cancel_)
	{
		if (gfx->testSDLKeyOnce(SDL_SCANCODE_ESCAPE) || gfx->testSDLKeyOnce(SDL_SCANCODE_RETURN))
		{
			signaling_.disconnect();
			if (holePunch_) holePunch_->stop();
			return false;
		}
		return true;
	}

	if (gfx->testSDLKeyOnce(SDL_SCANCODE_ESCAPE))
	{
		signaling_.disconnect();
		if (holePunch_) holePunch_->stop();
		gfx->clearKeys();
		return false;
	}

	// Phase 1: Wait for STUN
	if (!stunDone_ && stunQuery_ && stunQuery_->done())
	{
		stunDone_ = true;
		stunResult_ = stunQuery_->result();
		fprintf(stderr, "[online] STUN done: ipv4='%s':%u ipv6='%s':%u\n",
		        stunResult_.ipv4.c_str(), stunResult_.ipv4Port,
		        stunResult_.ipv6.c_str(), stunResult_.ipv6Port);

		// Start signaling
		fprintf(stderr, "[online] connecting to signaling server %s:%u\n",
		        signalingServer_.c_str(), signalingPort_);
		if (role_ == NetSession::Host)
		{
			if (!signaling_.createRoom(signalingServer_, signalingPort_))
			{
				fprintf(stderr, "[online] ERROR: createRoom failed\n");
				statusLine2_ = "FAILED TO REACH SIGNALING SERVER";
				cancel_ = true;
				return true;
			}
		}
		else
		{
			if (!signaling_.joinRoom(signalingServer_, signalingPort_, roomCode_))
			{
				fprintf(stderr, "[online] ERROR: joinRoom failed\n");
				statusLine2_ = "FAILED TO REACH SIGNALING SERVER";
				cancel_ = true;
				return true;
			}
		}

		statusLine2_ = (role_ == NetSession::Host)
			? "WAITING FOR PEER..."
			: "JOINING ROOM...";
	}

	// Poll signaling
	signaling_.poll();

	// Send keepalive every 5 seconds to prevent room expiry
	if (signaling_.state() != SignalingClient::Idle &&
	    signaling_.state() != SignalingClient::Failed)
	{
		auto now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		if (now - lastKeepaliveMs_ > 5000) {
			signaling_.sendKeepalive();
			lastKeepaliveMs_ = now;
		}
	}

	// Poll hole-punch if active
	if (holePunch_)
		holePunch_->poll();

	return true;
}

void OnlineConnectState::startPunching()
{
	if (startedPunch_) return;
	startedPunch_ = true;

	auto& candidates = signaling_.peerCandidates();
	fprintf(stderr, "[online] startPunching with %zu candidates from port %u\n",
	        candidates.size(), localPort_);

	statusLine2_ = "HOLE-PUNCHING...";

	holePunch_ = std::make_unique<HolePunch>();
	holePunch_->onSuccess = [this](const HolePunch::Result& r) {
		onPunchSuccess(r);
	};
	holePunch_->onTimeout = [this]() {
		onPunchFailed();
	};

	// Generate a random nonce to identify our probes (prevents accepting
	// our own reflected packets as success). peerNonce=0 means accept any
	// non-self nonce (full nonce exchange via signaling is a future improvement).
	std::random_device rd;
	uint32_t localNonce = ((uint32_t)rd() & 0xFFFF0000) | ((uint32_t)rd() & 0xFFFF);
	if (localNonce == 0) localNonce = 1; // ensure non-zero

	holePunch_->start(localPort_, candidates, localNonce, 0);
}

void OnlineConnectState::onPunchSuccess(const HolePunch::Result& result)
{
	fprintf(stderr, "[online] onPunchSuccess: peer=%s:%u\n", result.peerIP.c_str(), result.peerPort);
	signaling_.reportPunchOK();
	signaling_.disconnect();

	// Release the hole-punch socket before ENet binds to the same port
	holePunch_->stop();
	holePunch_.reset();

	statusLine2_ = "CONNECTED! STARTING GAME...";

	// Transition to the normal direct-connect flow with the punched address
	connectDirect(result.peerIP, result.peerPort);
}

void OnlineConnectState::onPunchFailed()
{
	fprintf(stderr, "[online] onPunchFailed — requesting relay\n");
	signaling_.reportPunchFail();
	statusLine2_ = "HOLE-PUNCH FAILED, WAITING FOR RELAY...";
	// Server will send UseRelay if both peers report failure
}

void OnlineConnectState::connectDirect(const std::string& addr, uint16_t port)
{
	fprintf(stderr, "[online] connectDirect: addr=%s port=%u role=%s relay=%d\n",
	        addr.c_str(), port, role_ == NetSession::Host ? "Host" : "Client",
	        signaling_.state() == SignalingClient::Relaying ? 1 : 0);

	bool useRelay = (signaling_.state() == SignalingClient::Relaying);

	if (useRelay)
	{
		// In relay mode, both peers connect as Client to the relay server.
		// The relay forwards traffic between the two participants.
		gfx->stateStack.scheduleReplaceTop(
			std::make_unique<NetConnectState>(NetSession::Client, addr, port));
	}
	else if (role_ == NetSession::Host)
	{
		// Host still listens — peer connects to us via punched address
		gfx->stateStack.scheduleReplaceTop(
			std::make_unique<NetConnectState>(NetSession::Host, "", localPort_));
	}
	else
	{
		gfx->stateStack.scheduleReplaceTop(
			std::make_unique<NetConnectState>(NetSession::Client, addr, port));
	}
}

void OnlineConnectState::draw()
{
	Common& common = *gfx->common;
	Font& font = common.font;

	gfx->playRenderer.pal = common.exepal;
	fill(gfx->playRenderer.bmp, 0);

	int cx = 160;
	int cy = 60;

	int w1 = font.getDims(statusLine1_);
	font.drawText(gfx->playRenderer.bmp, statusLine1_, cx - w1 / 2, cy, 50);

	int w2 = font.getDims(statusLine2_);
	font.drawText(gfx->playRenderer.bmp, statusLine2_, cx - w2 / 2, cy + 14, 7);

	// Show room code prominently for host
	if (role_ == NetSession::Host && !roomCode_.empty()
	    && signaling_.state() == SignalingClient::Hosting)
	{
		std::string codeStr = "CODE: " + roomCode_;
		int wc = font.getDims(codeStr);
		font.drawText(gfx->playRenderer.bmp, codeStr, cx - wc / 2, cy + 30, 45);
	}

	std::string esc = "PRESS ESC TO CANCEL";
	int we = font.getDims(esc);
	font.drawText(gfx->playRenderer.bmp, esc, cx - we / 2, 170, 7);
}
