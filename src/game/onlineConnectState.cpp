#include "onlineConnectState.hpp"

#include "gfx.hpp"
#include "text.hpp"
#include "keys.hpp"
#include "common.hpp"
#include "netConnectState.hpp"
#include "net/netutil.hpp"

#include <memory>
#include <string>
#include <cstdio>
#include <random>

using netutil::nowMs;

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

	// Step 1: Create ENet host on game port FIRST (single-socket architecture)
	if (!transport_.host(localPort_)) {
		fprintf(stderr, "[online] ERROR: failed to create ENet host on port %u\n", localPort_);
		statusLine2_ = "FAILED TO BIND GAME PORT";
		cancel_ = true;
		return;
	}
	fprintf(stderr, "[online] ENet host created on port %u\n", transport_.listeningPort());

	// Wire the intercept callback for STUN responses
	transport_.onInterceptedPacket = [this](const uint8_t* data, size_t len) -> bool {
		return stunViaHost_.feedResponse(data, len);
	};

	// Wire punch callbacks
	transport_.onPunchSuccess = [this](const NetTransport::PunchResult& r) {
		onPunchSuccess(r);
	};
	transport_.onPunchTimeout = [this]() {
		onPunchFailed();
	};

	// Step 2: Start STUN through the ENet socket
	stunViaHost_.start(transport_.enetHost());

	// Wire signaling callbacks
	signaling_.onRoomCreated = [this](const std::string& code) {
		fprintf(stderr, "[online] onRoomCreated: code=%s\n", code.c_str());
		roomCode_ = code;
		statusLine1_ = "ROOM CODE: " + code;
		statusLine2_ = "SHARE THIS CODE WITH YOUR PEER";

		auto res = stunViaHost_.result();
		if (!res.ipv4.empty())
			signaling_.reportAddress(4, res.ipv4, res.ipv4Port);
		if (!res.ipv6.empty())
			signaling_.reportAddress(6, res.ipv6, res.ipv6Port);
	};

	signaling_.onPeerJoined = [this]() {
		fprintf(stderr, "[online] onPeerJoined\n");
		statusLine2_ = "PEER JOINED! CONNECTING...";
		peerJoinedMs_ = nowMs();

		auto res = stunViaHost_.result();
		if (res.ipv4.empty() && res.ipv6.empty()) {
			fprintf(stderr, "[online] no STUN addresses — requesting relay\n");
			reportedPunchFail_ = true;
			signaling_.reportPunchFail();
		}
	};

	signaling_.onJoinAcked = [this]() {
		fprintf(stderr, "[online] join acknowledged\n");
		statusLine2_ = "JOINED! WAITING FOR HOST ADDRESSES...";
		peerJoinedMs_ = nowMs();

		auto res = stunViaHost_.result();
		if (!res.ipv4.empty())
			signaling_.reportAddress(4, res.ipv4, res.ipv4Port);
		if (!res.ipv6.empty())
			signaling_.reportAddress(6, res.ipv6, res.ipv6Port);

		if (res.ipv4.empty() && res.ipv6.empty()) {
			fprintf(stderr, "[online] no STUN addresses — requesting relay\n");
			reportedPunchFail_ = true;
			signaling_.reportPunchFail();
		}
	};

	signaling_.onStartPunch = [this]() {
		fprintf(stderr, "[online] onStartPunch — %zu candidates\n",
		        signaling_.peerCandidates().size());
		punchRequested_ = true;
		if (!signaling_.peerCandidates().empty())
			startPunching();
	};

	signaling_.onPeerAddr = [this](const PeerCandidate&) {
		if (punchRequested_ && !startedPunch_)
			startPunching();
	};

	signaling_.onUseRelay = [this](uint16_t relayPort) {
		fprintf(stderr, "[online] onUseRelay: port=%u\n", relayPort);
		statusLine2_ = "USING RELAY (HIGHER LATENCY)";
		transport_.stopPunch();
		connectRelay();
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
			transport_.disconnect();
			return false;
		}
		return true;
	}

	if (gfx->testSDLKeyOnce(SDL_SCANCODE_ESCAPE))
	{
		signaling_.disconnect();
		transport_.disconnect();
		gfx->clearKeys();
		return false;
	}

	// Update STUN-via-host (retries)
	stunViaHost_.update();

	// Poll transport (processes intercept for STUN + punch probes)
	transport_.poll();

	// Check if STUN completed — start signaling
	if (!stunDone_ && stunViaHost_.done())
	{
		stunDone_ = true;
		auto res = stunViaHost_.result();
		fprintf(stderr, "[online] STUN done: ipv4='%s':%u ipv6='%s':%u\n",
		        res.ipv4.c_str(), res.ipv4Port, res.ipv6.c_str(), res.ipv6Port);

		// Start signaling
		if (role_ == NetSession::Host)
		{
			if (!signaling_.createRoom(signalingServer_, signalingPort_))
			{
				statusLine2_ = "FAILED TO REACH SIGNALING SERVER";
				cancel_ = true;
				return true;
			}
		}
		else
		{
			if (!signaling_.joinRoom(signalingServer_, signalingPort_, roomCode_))
			{
				statusLine2_ = "FAILED TO REACH SIGNALING SERVER";
				cancel_ = true;
				return true;
			}
		}

		statusLine2_ = (role_ == NetSession::Host) ? "WAITING FOR PEER..." : "JOINING ROOM...";
	}

	// Poll signaling
	signaling_.poll();

	// Keepalive
	if (signaling_.state() != SignalingClient::Idle &&
	    signaling_.state() != SignalingClient::Failed)
	{
		uint64_t now = nowMs();
		if (now - lastKeepaliveMs_ > 5000) {
			signaling_.sendKeepalive();
			lastKeepaliveMs_ = now;
		}
	}

	// Timeout: if peer joined but no punch started, request relay
	if (peerJoinedMs_ != 0 && !startedPunch_ && !reportedPunchFail_)
	{
		if (nowMs() - peerJoinedMs_ > 10000) {
			fprintf(stderr, "[online] punch never started after 10s — requesting relay\n");
			reportedPunchFail_ = true;
			signaling_.reportPunchFail();
		}
	}

	// Check if punch succeeded (handled via callback, transitions happen there)

	return true;
}

void OnlineConnectState::startPunching()
{
	if (startedPunch_) return;
	startedPunch_ = true;

	auto& candidates = signaling_.peerCandidates();
	fprintf(stderr, "[online] startPunching with %zu candidates\n", candidates.size());

	if (candidates.empty()) {
		onPunchFailed();
		return;
	}

	statusLine2_ = "HOLE-PUNCHING...";

	// Convert PeerCandidate to PunchCandidate
	std::vector<NetTransport::PunchCandidate> punchCandidates;
	for (auto& c : candidates)
		punchCandidates.push_back({c.type, c.ip, c.port});

	std::random_device rd;
	uint32_t localNonce = ((uint32_t)rd() & 0xFFFF0000) | ((uint32_t)rd() & 0xFFFF);
	if (localNonce == 0) localNonce = 1;

	transport_.startPunch(punchCandidates, localNonce, 0);
}

void OnlineConnectState::onPunchSuccess(const NetTransport::PunchResult& result)
{
	fprintf(stderr, "[online] punch SUCCESS: peer=%s:%u\n", result.peerIP.c_str(), result.peerPort);
	signaling_.reportPunchOK();
	signaling_.disconnect();

	statusLine2_ = "CONNECTED! STARTING GAME...";

	// Transition to game using the same ENet host (socket preserved!)
	// For host: we're already listening. Peer will connect to us.
	// For client: connect to the punched address.
	connectDirect(result.peerIP, result.peerPort);
}

void OnlineConnectState::onPunchFailed()
{
	fprintf(stderr, "[online] punch FAILED — requesting relay\n");
	if (!reportedPunchFail_) {
		reportedPunchFail_ = true;
		signaling_.reportPunchFail();
	}
	statusLine2_ = "HOLE-PUNCH FAILED, WAITING FOR RELAY...";
}

void OnlineConnectState::connectDirect(const std::string& addr, uint16_t port)
{
	fprintf(stderr, "[online] connectDirect: addr=%s port=%u role=%s\n",
	        addr.c_str(), port, role_ == NetSession::Host ? "Host" : "Client");

	// Clear punch/STUN callbacks before transferring transport
	transport_.onInterceptedPacket = nullptr;
	transport_.onPunchSuccess = nullptr;
	transport_.onPunchTimeout = nullptr;

	// Transfer the transport to NetConnectState — preserves the NAT-mapped socket!
	if (role_ == NetSession::Host) {
		gfx->stateStack.scheduleReplaceTop(
			std::make_unique<NetConnectState>(NetSession::Host, std::move(transport_)));
	} else {
		gfx->stateStack.scheduleReplaceTop(
			std::make_unique<NetConnectState>(NetSession::Client, std::move(transport_), addr, port));
	}
}

void OnlineConnectState::connectRelay()
{
	fprintf(stderr, "[online] connectRelay: port=%u\n", signaling_.relayPort());

	auto token = signaling_.relayToken();
	uint16_t relayPort = signaling_.relayPort();

	// For relay, we disconnect and let NetConnectState create a new transport
	// bound to the same local port. The relay doesn't care about NAT mappings
	// since it's a server with a public IP.
	uint16_t boundPort = transport_.listeningPort();
	transport_.disconnect();

	gfx->stateStack.scheduleReplaceTop(
		std::make_unique<NetConnectState>(role_, signalingServer_, relayPort,
		                                  boundPort, std::move(token)));
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
