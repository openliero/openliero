package main

import (
	"encoding/binary"
	"net"
	"testing"
	"time"
)

func startTestServer(t *testing.T, relayPorts int) (*Server, *net.UDPAddr) {
	t.Helper()
	addr := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 0}
	conn, err := net.ListenUDP("udp4", addr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { conn.Close() })

	// Use high ephemeral ports for relay to avoid conflicts
	srv := NewServer(conn, 0, 0)
	if relayPorts > 0 {
		// Find available ports dynamically
		base := findFreePort(t)
		srv.relayPortBase = base
		srv.relayPortMax = base + relayPorts
	}
	go srv.Run()

	return srv, conn.LocalAddr().(*net.UDPAddr)
}

func findFreePort(t *testing.T) int {
	t.Helper()
	l, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	port := l.LocalAddr().(*net.UDPAddr).Port
	l.Close()
	return port
}

func dial(t *testing.T, srvAddr *net.UDPAddr) *net.UDPConn {
	t.Helper()
	conn, err := net.DialUDP("udp4", nil, srvAddr)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { conn.Close() })
	return conn
}

func readMsg(t *testing.T, conn *net.UDPConn) []byte {
	t.Helper()
	conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	buf := make([]byte, 512)
	n, err := conn.Read(buf)
	if err != nil {
		t.Fatal("read timeout:", err)
	}
	return buf[:n]
}

func readMsgFrom(t *testing.T, conn *net.UDPConn) ([]byte, *net.UDPAddr) {
	t.Helper()
	conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	buf := make([]byte, 512)
	n, addr, err := conn.ReadFromUDP(buf)
	if err != nil {
		t.Fatal("read timeout:", err)
	}
	return buf[:n], addr
}

// --- Basic signaling tests ---

func TestCreateAndJoinRoom(t *testing.T) {
	_, srvAddr := startTestServer(t, 0)

	host := dial(t, srvAddr)
	client := dial(t, srvAddr)

	// Host creates room
	host.Write([]byte{MsgCreateRoom, 0, 0, 0, 0, 0, 0})
	resp := readMsg(t, host)
	if resp[0] != MsgRoomCreated {
		t.Fatalf("expected RoomCreated (0x81), got 0x%02x", resp[0])
	}
	if len(resp) < 1+RoomCodeLen {
		t.Fatal("response too short")
	}
	code := resp[1 : 1+RoomCodeLen]

	// Client joins room
	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], code)
	client.Write(joinMsg)

	// Client gets PeerJoined
	clientResp := readMsg(t, client)
	if clientResp[0] != MsgPeerJoined {
		t.Fatalf("client expected PeerJoined (0x82), got 0x%02x", clientResp[0])
	}

	// Host gets PeerJoined
	hostMsg := readMsg(t, host)
	if hostMsg[0] != MsgPeerJoined {
		t.Fatalf("host expected PeerJoined (0x82), got 0x%02x", hostMsg[0])
	}
}

func TestJoinNonexistentRoom(t *testing.T) {
	_, srvAddr := startTestServer(t, 0)
	client := dial(t, srvAddr)

	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], "XXXXXX")
	client.Write(joinMsg)

	resp := readMsg(t, client)
	if resp[0] != MsgError {
		t.Fatalf("expected Error (0x8F), got 0x%02x", resp[0])
	}
	if resp[1] != ErrRoomNotFound {
		t.Fatalf("expected ErrRoomNotFound, got 0x%02x", resp[1])
	}
}

func TestAddressExchange(t *testing.T) {
	_, srvAddr := startTestServer(t, 0)

	host := dial(t, srvAddr)
	client := dial(t, srvAddr)

	// Create room
	host.Write([]byte{MsgCreateRoom, 0, 0, 0, 0, 0, 0})
	resp := readMsg(t, host)
	code := resp[1 : 1+RoomCodeLen]

	// Host reports its address
	addrMsg := make([]byte, 1+RoomCodeLen+1+2+4)
	addrMsg[0] = MsgReportAddr
	copy(addrMsg[1:], code)
	addrMsg[1+RoomCodeLen] = AddrIPv4
	binary.BigEndian.PutUint16(addrMsg[1+RoomCodeLen+1:], 19532)
	copy(addrMsg[1+RoomCodeLen+3:], net.IPv4(1, 2, 3, 4).To4())
	host.Write(addrMsg)

	// Client joins
	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], code)
	client.Write(joinMsg)

	// Client receives: PeerJoined + PeerAddr + StartPunch (host has 1 addr, but
	// StartPunch requires both peers to have addresses, so only PeerAddr here)
	clientMsg := readMsg(t, client)
	// First message should be PeerJoined (ack)
	if clientMsg[0] == MsgPeerJoined {
		// Next should be PeerAddr
		clientMsg = readMsg(t, client)
	}
	if clientMsg[0] != MsgPeerAddr {
		t.Fatalf("expected PeerAddr (0x83), got 0x%02x", clientMsg[0])
	}
	port := binary.BigEndian.Uint16(clientMsg[1+RoomCodeLen+1:])
	if port != 19532 {
		t.Fatalf("expected port 19532, got %d", port)
	}
}

func TestStartPunchWhenBothHaveAddresses(t *testing.T) {
	_, srvAddr := startTestServer(t, 0)

	host := dial(t, srvAddr)
	client := dial(t, srvAddr)

	// Create and join
	host.Write([]byte{MsgCreateRoom, 0, 0, 0, 0, 0, 0})
	resp := readMsg(t, host)
	code := resp[1 : 1+RoomCodeLen]

	// Host reports address
	addrMsg := make([]byte, 1+RoomCodeLen+1+2+4)
	addrMsg[0] = MsgReportAddr
	copy(addrMsg[1:], code)
	addrMsg[1+RoomCodeLen] = AddrIPv4
	binary.BigEndian.PutUint16(addrMsg[1+RoomCodeLen+1:], 19532)
	copy(addrMsg[1+RoomCodeLen+3:], net.IPv4(1, 2, 3, 4).To4())
	host.Write(addrMsg)

	// Client joins
	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], code)
	client.Write(joinMsg)

	// Drain client messages until we find StartPunch or run out
	// (host has address, join triggers StartPunch to both if host has addrs)
	// Actually: StartPunch is sent when host has addresses at join time
	foundStartPunch := false
	for i := 0; i < 5; i++ {
		msg := readMsg(t, client)
		if msg[0] == MsgStartPunch {
			foundStartPunch = true
			break
		}
	}
	if !foundStartPunch {
		t.Fatal("client never received StartPunch")
	}

	// Host should also get StartPunch
	foundStartPunch = false
	for i := 0; i < 5; i++ {
		msg := readMsg(t, host)
		if msg[0] == MsgStartPunch {
			foundStartPunch = true
			break
		}
	}
	if !foundStartPunch {
		t.Fatal("host never received StartPunch")
	}
}

// --- Relay tests ---

func TestRelayActivatesOnBothPunchFail(t *testing.T) {
	_, srvAddr := startTestServer(t, 5)

	host := dial(t, srvAddr)
	client := dial(t, srvAddr)

	// Create room
	host.Write([]byte{MsgCreateRoom, 0, 0, 0, 0, 0, 0})
	resp := readMsg(t, host)
	code := resp[1 : 1+RoomCodeLen]

	// Client joins
	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], code)
	client.Write(joinMsg)

	// Drain PeerJoined messages
	readMsg(t, client)
	readMsg(t, host)

	// Both report punch fail
	failMsg := make([]byte, 1+RoomCodeLen)
	failMsg[0] = MsgPunchFail
	copy(failMsg[1:], code)
	host.Write(failMsg)
	client.Write(failMsg)

	// Both should receive UseRelay
	hostRelay := readMsg(t, host)
	if hostRelay[0] != MsgUseRelay {
		t.Fatalf("host expected UseRelay (0x85), got 0x%02x", hostRelay[0])
	}
	if len(hostRelay) < 1+RoomCodeLen+2+8 {
		t.Fatalf("UseRelay too short: %d bytes", len(hostRelay))
	}

	clientRelay := readMsg(t, client)
	if clientRelay[0] != MsgUseRelay {
		t.Fatalf("client expected UseRelay (0x85), got 0x%02x", clientRelay[0])
	}

	// Verify both got same relay port and token
	hostPort := binary.BigEndian.Uint16(hostRelay[1+RoomCodeLen:])
	clientPort := binary.BigEndian.Uint16(clientRelay[1+RoomCodeLen:])
	if hostPort != clientPort {
		t.Fatalf("relay ports differ: host=%d client=%d", hostPort, clientPort)
	}
	if hostPort == 0 {
		t.Fatal("relay port is 0")
	}
}

func TestRelayForwardsBidirectionally(t *testing.T) {
	_, srvAddr := startTestServer(t, 5)

	host := dial(t, srvAddr)
	client := dial(t, srvAddr)

	// Create room + join + punch fail to get relay
	host.Write([]byte{MsgCreateRoom, 0, 0, 0, 0, 0, 0})
	resp := readMsg(t, host)
	code := resp[1 : 1+RoomCodeLen]

	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], code)
	client.Write(joinMsg)
	readMsg(t, client) // PeerJoined
	readMsg(t, host)   // PeerJoined

	failMsg := make([]byte, 1+RoomCodeLen)
	failMsg[0] = MsgPunchFail
	copy(failMsg[1:], code)
	host.Write(failMsg)
	client.Write(failMsg)

	hostRelay := readMsg(t, host)
	readMsg(t, client) // UseRelay

	relayPort := binary.BigEndian.Uint16(hostRelay[1+RoomCodeLen:])
	token := hostRelay[1+RoomCodeLen+2 : 1+RoomCodeLen+2+8]

	// Connect to relay
	relayAddr := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: int(relayPort)}

	hostRelaySock, err := net.DialUDP("udp4", nil, relayAddr)
	if err != nil {
		t.Fatal(err)
	}
	defer hostRelaySock.Close()

	clientRelaySock, err := net.DialUDP("udp4", nil, relayAddr)
	if err != nil {
		t.Fatal(err)
	}
	defer clientRelaySock.Close()

	// Authenticate: send token
	hostRelaySock.Write(token)
	time.Sleep(10 * time.Millisecond)
	clientRelaySock.Write(token)
	time.Sleep(10 * time.Millisecond)

	// Read ACKs
	ack := make([]byte, 16)
	hostRelaySock.SetReadDeadline(time.Now().Add(time.Second))
	n, _ := hostRelaySock.Read(ack)
	if n != 1 || ack[0] != relayAckByte {
		t.Fatalf("host expected relay ACK, got %d bytes: %v", n, ack[:n])
	}
	clientRelaySock.SetReadDeadline(time.Now().Add(time.Second))
	n, _ = clientRelaySock.Read(ack)
	if n != 1 || ack[0] != relayAckByte {
		t.Fatalf("client expected relay ACK, got %d bytes: %v", n, ack[:n])
	}

	// Host sends data through relay
	hostRelaySock.Write([]byte("hello from host"))
	time.Sleep(10 * time.Millisecond)

	// Client should receive it
	buf := make([]byte, 256)
	clientRelaySock.SetReadDeadline(time.Now().Add(time.Second))
	n, err = clientRelaySock.Read(buf)
	if err != nil {
		t.Fatal("client read from relay:", err)
	}
	if string(buf[:n]) != "hello from host" {
		t.Fatalf("client got %q, want %q", buf[:n], "hello from host")
	}

	// Client sends data through relay
	clientRelaySock.Write([]byte("hello from client"))
	time.Sleep(10 * time.Millisecond)

	// Host should receive it
	hostRelaySock.SetReadDeadline(time.Now().Add(time.Second))
	n, err = hostRelaySock.Read(buf)
	if err != nil {
		t.Fatal("host read from relay:", err)
	}
	if string(buf[:n]) != "hello from client" {
		t.Fatalf("host got %q, want %q", buf[:n], "hello from client")
	}
}

func TestRelayRejectsInvalidToken(t *testing.T) {
	_, srvAddr := startTestServer(t, 5)

	host := dial(t, srvAddr)
	client := dial(t, srvAddr)

	// Setup relay
	host.Write([]byte{MsgCreateRoom, 0, 0, 0, 0, 0, 0})
	resp := readMsg(t, host)
	code := resp[1 : 1+RoomCodeLen]

	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], code)
	client.Write(joinMsg)
	readMsg(t, client)
	readMsg(t, host)

	failMsg := make([]byte, 1+RoomCodeLen)
	failMsg[0] = MsgPunchFail
	copy(failMsg[1:], code)
	host.Write(failMsg)
	client.Write(failMsg)

	hostRelay := readMsg(t, host)
	readMsg(t, client)

	relayPort := binary.BigEndian.Uint16(hostRelay[1+RoomCodeLen:])
	relayAddr := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: int(relayPort)}

	// Try to connect with wrong token
	badConn, err := net.DialUDP("udp4", nil, relayAddr)
	if err != nil {
		t.Fatal(err)
	}
	defer badConn.Close()

	badToken := []byte{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
	badConn.Write(badToken)

	// Should NOT get an ACK (no response expected for bad token)
	badConn.SetReadDeadline(time.Now().Add(200 * time.Millisecond))
	buf := make([]byte, 16)
	_, err = badConn.Read(buf)
	if err == nil {
		t.Fatal("expected timeout for bad token, got response")
	}
}

func TestRelayTokenAsPrefix(t *testing.T) {
	// Verifies that a peer can send token + payload in a single packet.
	// The payload is forwarded if the other peer is already connected.
	_, srvAddr := startTestServer(t, 5)

	host := dial(t, srvAddr)
	client := dial(t, srvAddr)

	// Setup relay
	host.Write([]byte{MsgCreateRoom, 0, 0, 0, 0, 0, 0})
	resp := readMsg(t, host)
	code := resp[1 : 1+RoomCodeLen]

	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], code)
	client.Write(joinMsg)
	readMsg(t, client)
	readMsg(t, host)

	failMsg := make([]byte, 1+RoomCodeLen)
	failMsg[0] = MsgPunchFail
	copy(failMsg[1:], code)
	host.Write(failMsg)
	client.Write(failMsg)

	hostRelay := readMsg(t, host)
	readMsg(t, client)

	relayPort := binary.BigEndian.Uint16(hostRelay[1+RoomCodeLen:])
	token := hostRelay[1+RoomCodeLen+2 : 1+RoomCodeLen+2+8]
	relayAddr := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: int(relayPort)}

	hostRelaySock, err := net.DialUDP("udp4", nil, relayAddr)
	if err != nil {
		t.Fatal(err)
	}
	defer hostRelaySock.Close()

	clientRelaySock, err := net.DialUDP("udp4", nil, relayAddr)
	if err != nil {
		t.Fatal(err)
	}
	defer clientRelaySock.Close()

	// Client authenticates first
	clientRelaySock.Write(token)
	time.Sleep(10 * time.Millisecond)

	ack := make([]byte, 16)
	clientRelaySock.SetReadDeadline(time.Now().Add(time.Second))
	n, _ := clientRelaySock.Read(ack)
	if n != 1 || ack[0] != relayAckByte {
		t.Fatalf("expected ACK, got %d bytes", n)
	}

	// Host sends token + "game data" in one packet (peerB already connected)
	combined := append([]byte{}, token...)
	combined = append(combined, []byte("game data")...)
	hostRelaySock.Write(combined)
	time.Sleep(10 * time.Millisecond)

	// Read host ACK
	hostRelaySock.SetReadDeadline(time.Now().Add(time.Second))
	n, _ = hostRelaySock.Read(ack)
	if n != 1 || ack[0] != relayAckByte {
		t.Fatalf("expected ACK, got %d bytes", n)
	}

	// Client should receive the "game data" payload
	buf := make([]byte, 256)
	clientRelaySock.SetReadDeadline(time.Now().Add(time.Second))
	n, err = clientRelaySock.Read(buf)
	if err != nil {
		t.Fatal("client read after prefix auth:", err)
	}
	if string(buf[:n]) != "game data" {
		t.Fatalf("got %q, want %q", buf[:n], "game data")
	}
}

func TestRelayNoRaceCondition(t *testing.T) {
	// This test verifies the relay is listening before UseRelay is sent.
	// We do this by immediately connecting to the relay after receiving UseRelay.
	_, srvAddr := startTestServer(t, 5)

	host := dial(t, srvAddr)
	client := dial(t, srvAddr)

	host.Write([]byte{MsgCreateRoom, 0, 0, 0, 0, 0, 0})
	resp := readMsg(t, host)
	code := resp[1 : 1+RoomCodeLen]

	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], code)
	client.Write(joinMsg)
	readMsg(t, client)
	readMsg(t, host)

	failMsg := make([]byte, 1+RoomCodeLen)
	failMsg[0] = MsgPunchFail
	copy(failMsg[1:], code)
	host.Write(failMsg)
	client.Write(failMsg)

	hostRelay := readMsg(t, host)
	readMsg(t, client)

	relayPort := binary.BigEndian.Uint16(hostRelay[1+RoomCodeLen:])
	token := hostRelay[1+RoomCodeLen+2 : 1+RoomCodeLen+2+8]
	relayAddr := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: int(relayPort)}

	// IMMEDIATELY connect — no delay. If relay had a race, this would fail.
	relaySock, err := net.DialUDP("udp4", nil, relayAddr)
	if err != nil {
		t.Fatal(err)
	}
	defer relaySock.Close()

	relaySock.Write(token)

	// Should get ACK immediately (relay is already listening)
	ack := make([]byte, 16)
	relaySock.SetReadDeadline(time.Now().Add(time.Second))
	n, err := relaySock.Read(ack)
	if err != nil {
		t.Fatal("immediate connect after UseRelay failed:", err)
	}
	if n != 1 || ack[0] != relayAckByte {
		t.Fatalf("expected ACK byte, got %v", ack[:n])
	}
}

func TestKeepaliveResetsExpiry(t *testing.T) {
	_, srvAddr := startTestServer(t, 0)

	host := dial(t, srvAddr)

	host.Write([]byte{MsgCreateRoom, 0, 0, 0, 0, 0, 0})
	resp := readMsg(t, host)
	code := resp[1 : 1+RoomCodeLen]

	// Send keepalive
	keepalive := make([]byte, 1+RoomCodeLen)
	keepalive[0] = MsgKeepalive
	copy(keepalive[1:], code)
	host.Write(keepalive)

	// Room should still exist — try to join
	client := dial(t, srvAddr)
	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], code)
	client.Write(joinMsg)

	resp = readMsg(t, client)
	if resp[0] != MsgPeerJoined {
		t.Fatalf("expected PeerJoined after keepalive, got 0x%02x", resp[0])
	}
}

func TestOnlyOnePunchFailDoesNotTriggerRelay(t *testing.T) {
	_, srvAddr := startTestServer(t, 5)

	host := dial(t, srvAddr)
	client := dial(t, srvAddr)

	host.Write([]byte{MsgCreateRoom, 0, 0, 0, 0, 0, 0})
	resp := readMsg(t, host)
	code := resp[1 : 1+RoomCodeLen]

	joinMsg := make([]byte, 1+RoomCodeLen)
	joinMsg[0] = MsgJoinRoom
	copy(joinMsg[1:], code)
	client.Write(joinMsg)
	readMsg(t, client)
	readMsg(t, host)

	// Only host reports failure
	failMsg := make([]byte, 1+RoomCodeLen)
	failMsg[0] = MsgPunchFail
	copy(failMsg[1:], code)
	host.Write(failMsg)

	// Should NOT receive UseRelay
	host.SetReadDeadline(time.Now().Add(200 * time.Millisecond))
	buf := make([]byte, 512)
	_, err := host.Read(buf)
	if err == nil {
		t.Fatal("received unexpected message when only one peer failed")
	}
}
