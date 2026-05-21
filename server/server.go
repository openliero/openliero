package main

import (
	"crypto/rand"
	"crypto/subtle"
	"encoding/binary"
	"log"
	"net"
	"sync"
	"time"
)

const (
	roomTTL       = 60 * time.Second
	cleanupPeriod = 10 * time.Second
	maxRooms      = 1000
)

// PeerAddr holds one discovered address for a peer.
type PeerAddr struct {
	Type byte   // AddrIPv4 or AddrIPv6
	IP   net.IP // 4 or 16 bytes
	Port uint16
}

// Peer represents one connected game client.
type Peer struct {
	Addr      *net.UDPAddr // UDP source address for sending replies
	Addresses []PeerAddr   // STUN-discovered external addresses
	PunchOK   bool
	PunchFail bool
}

// Room holds two peers trying to connect.
type Room struct {
	Code       [RoomCodeLen]byte
	Host       *Peer
	Client     *Peer
	CreatedAt  time.Time
	LastSeen   time.Time
	RelayPort  int    // 0 = not allocated
	RelayToken []byte // 8-byte token for relay authentication
}

type Server struct {
	conn          *net.UDPConn
	mu            sync.Mutex
	rooms         map[string]*Room // code string → room
	relayPortBase int
	relayPortMax  int
	relayPorts    map[int]bool // allocated relay ports
}

func NewServer(conn *net.UDPConn, relayPortBase, relayPortCount int) *Server {
	return &Server{
		conn:          conn,
		rooms:         make(map[string]*Room),
		relayPortBase: relayPortBase,
		relayPortMax:  relayPortBase + relayPortCount,
		relayPorts:    make(map[int]bool),
	}
}

func (s *Server) Run() {
	go s.cleanupLoop()

	buf := make([]byte, 512)
	for {
		n, addr, err := s.conn.ReadFromUDP(buf)
		if err != nil {
			log.Printf("Read error: %v", err)
			return
		}
		if n < 1 {
			continue
		}
		s.handlePacket(buf[:n], addr)
	}
}

func (s *Server) handlePacket(data []byte, from *net.UDPAddr) {
	msgType := data[0]
	log.Printf("Recv msg=0x%02x (%d bytes) from %s", msgType, len(data), from)

	switch msgType {
	case MsgCreateRoom:
		s.handleCreateRoom(from)
	case MsgJoinRoom:
		if len(data) < 1+RoomCodeLen {
			s.sendError(from, ErrInvalidMsg, "too short")
			return
		}
		code := string(data[1 : 1+RoomCodeLen])
		s.handleJoinRoom(code, from)
	case MsgReportAddr:
		if len(data) < 1+RoomCodeLen+1+2+4 {
			s.sendError(from, ErrInvalidMsg, "too short")
			return
		}
		code := string(data[1 : 1+RoomCodeLen])
		addrType := data[1+RoomCodeLen]
		port := binary.BigEndian.Uint16(data[1+RoomCodeLen+1:])
		var ipLen int
		if addrType == AddrIPv4 {
			ipLen = 4
		} else if addrType == AddrIPv6 {
			ipLen = 16
		} else {
			s.sendError(from, ErrInvalidMsg, "bad addr type")
			return
		}
		if len(data) < 1+RoomCodeLen+1+2+ipLen {
			s.sendError(from, ErrInvalidMsg, "too short for IP")
			return
		}
		ip := make(net.IP, ipLen)
		copy(ip, data[1+RoomCodeLen+3:1+RoomCodeLen+3+ipLen])
		s.handleReportAddr(code, from, PeerAddr{Type: addrType, IP: ip, Port: port})
	case MsgPunchOK:
		if len(data) < 1+RoomCodeLen {
			return
		}
		code := string(data[1 : 1+RoomCodeLen])
		s.handlePunchResult(code, from, true)
	case MsgPunchFail:
		if len(data) < 1+RoomCodeLen {
			return
		}
		code := string(data[1 : 1+RoomCodeLen])
		s.handlePunchResult(code, from, false)
	case MsgKeepalive:
		if len(data) < 1+RoomCodeLen {
			return
		}
		code := string(data[1 : 1+RoomCodeLen])
		s.mu.Lock()
		if room, ok := s.rooms[code]; ok {
			room.LastSeen = time.Now()
		}
		s.mu.Unlock()
	default:
		s.sendError(from, ErrInvalidMsg, "unknown type")
	}
}

func (s *Server) handleCreateRoom(from *net.UDPAddr) {
	s.mu.Lock()

	if len(s.rooms) >= maxRooms {
		s.mu.Unlock()
		s.sendError(from, ErrRoomFull, "server full")
		return
	}

	code := s.generateRoomCode()
	if code == "" {
		s.mu.Unlock()
		s.sendError(from, ErrRoomFull, "server full")
		return
	}
	now := time.Now()
	room := &Room{
		Host:      &Peer{Addr: from},
		CreatedAt: now,
		LastSeen:  now,
	}
	copy(room.Code[:], code)
	s.rooms[code] = room

	s.mu.Unlock()

	log.Printf("Room %s created by %s", code, from)

	// Send RoomCreated
	resp := make([]byte, 1+RoomCodeLen)
	resp[0] = MsgRoomCreated
	copy(resp[1:], code)
	s.conn.WriteToUDP(resp, from)
}

func (s *Server) handleJoinRoom(code string, from *net.UDPAddr) {
	s.mu.Lock()

	room, ok := s.rooms[code]
	if !ok {
		s.mu.Unlock()
		s.sendError(from, ErrRoomNotFound, "no such room")
		return
	}
	// Allow rejoin: if a client is already registered but hole-punch hasn't
	// succeeded, allow a new client to replace it. This handles the case where
	// the client reconnects from a new source port (new socket).
	if room.Client != nil && room.Client.PunchOK {
		s.mu.Unlock()
		s.sendError(from, ErrRoomFull, "room full")
		return
	}

	room.Client = &Peer{Addr: from}
	room.LastSeen = time.Now()
	hostAddr := room.Host.Addr
	hostAddresses := append([]PeerAddr{}, room.Host.Addresses...)
	shouldStartPunch := len(hostAddresses) > 0

	s.mu.Unlock()

	log.Printf("Room %s: client joined from %s", code, from)

	// Acknowledge to the joining client
	ack := make([]byte, 1+RoomCodeLen)
	ack[0] = MsgPeerJoined
	copy(ack[1:], code)
	s.conn.WriteToUDP(ack, from)

	// Notify host that peer joined
	msg := make([]byte, 1+RoomCodeLen)
	msg[0] = MsgPeerJoined
	copy(msg[1:], code)
	s.conn.WriteToUDP(msg, hostAddr)

	// Send any addresses the host already reported to the new client
	for _, a := range hostAddresses {
		s.sendPeerAddr(from, code, a)
	}

	// If host has addresses, tell both to start punching
	if shouldStartPunch {
		s.sendStartPunchTo(hostAddr, from, code)
	}
}

func (s *Server) handleReportAddr(code string, from *net.UDPAddr, addr PeerAddr) {
	s.mu.Lock()

	room, ok := s.rooms[code]
	if !ok {
		s.mu.Unlock()
		log.Printf("Room %s: ReportAddr from %s but room not found", code, from)
		s.sendError(from, ErrRoomNotFound, "no such room")
		return
	}
	room.LastSeen = time.Now()

	peer := s.findPeer(room, from)
	if peer == nil {
		log.Printf("Room %s: ReportAddr from %s but not a known peer (host=%s, client=%v)",
			code, from, room.Host.Addr, room.Client)
		if room.Client != nil {
			log.Printf("  client addr: %s", room.Client.Addr)
		}
		s.mu.Unlock()
		s.sendError(from, ErrInvalidMsg, "not in room")
		return
	}

	peer.Addresses = append(peer.Addresses, addr)

	// Determine what to send after releasing the lock
	other := s.otherPeer(room, peer)
	var otherAddr *net.UDPAddr
	var shouldStartPunch bool
	var hostAddr, clientAddr *net.UDPAddr
	if other != nil {
		otherAddr = other.Addr
		if len(room.Host.Addresses) > 0 && room.Client != nil && len(room.Client.Addresses) > 0 {
			shouldStartPunch = true
			hostAddr = room.Host.Addr
			clientAddr = room.Client.Addr
		}
	}

	s.mu.Unlock()

	if otherAddr != nil {
		s.sendPeerAddr(otherAddr, code, addr)
	}
	if shouldStartPunch {
		s.sendStartPunchTo(hostAddr, clientAddr, code)
	}
}

func (s *Server) handlePunchResult(code string, from *net.UDPAddr, ok bool) {
	s.mu.Lock()

	room, exists := s.rooms[code]
	if !exists {
		s.mu.Unlock()
		log.Printf("Room %s: PunchResult from %s but room not found", code, from)
		return
	}

	peer := s.findPeer(room, from)
	if peer == nil {
		log.Printf("Room %s: PunchResult from %s but not a known peer (host=%s, client=%v)",
			code, from, room.Host.Addr, room.Client)
		if room.Client != nil {
			log.Printf("  client addr: %s", room.Client.Addr)
		}
		s.mu.Unlock()
		return
	}

	var shouldRelay bool
	if ok {
		peer.PunchOK = true
		log.Printf("Room %s: punch OK from %s", code, from)
	} else {
		peer.PunchFail = true
		log.Printf("Room %s: punch FAILED from %s", code, from)
		shouldRelay = room.Host.PunchFail && room.Client != nil && room.Client.PunchFail
	}

	// Collect relay data while still holding the lock
	var relayPort int
	var relayToken []byte
	var hostAddr, clientAddr *net.UDPAddr
	if shouldRelay {
		relayPort = s.allocateRelayPort()
		if relayPort != 0 {
			room.RelayPort = relayPort
			relayToken = make([]byte, 8)
			rand.Read(relayToken)
			room.RelayToken = relayToken
			hostAddr = room.Host.Addr
			if room.Client != nil {
				clientAddr = room.Client.Addr
			}
		}
	}

	s.mu.Unlock()

	// Send relay offer outside the lock
	if shouldRelay && relayPort != 0 {
		log.Printf("Room %s: offering relay on port %d", code, relayPort)
		go s.runRelay(code, relayPort, relayToken)

		msg := make([]byte, 1+RoomCodeLen+2+8)
		msg[0] = MsgUseRelay
		copy(msg[1:], code)
		binary.BigEndian.PutUint16(msg[1+RoomCodeLen:], uint16(relayPort))
		copy(msg[1+RoomCodeLen+2:], relayToken)
		s.conn.WriteToUDP(msg, hostAddr)
		if clientAddr != nil {
			s.conn.WriteToUDP(msg, clientAddr)
		}
	} else if shouldRelay {
		log.Printf("Room %s: no relay ports available", code)
	}
}

func (s *Server) runRelay(code string, port int, token []byte) {
	addr := &net.UDPAddr{Port: port}
	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		log.Printf("Room %s: failed to start relay on port %d: %v", code, port, err)
		s.mu.Lock()
		delete(s.relayPorts, port)
		s.mu.Unlock()
		return
	}
	defer func() {
		conn.Close()
		s.mu.Lock()
		delete(s.relayPorts, port)
		s.mu.Unlock()
		log.Printf("Room %s: relay on port %d stopped", code, port)
	}()

	// Set deadline so relay doesn't live forever
	conn.SetDeadline(time.Now().Add(10 * time.Minute))

	buf := make([]byte, 2048)
	var peerA, peerB *net.UDPAddr
	tokenLen := len(token)

	for {
		n, from, err := conn.ReadFromUDP(buf)
		if err != nil {
			return
		}

		// Authentication: peers must present the token in their first packet.
		// The token is stripped before forwarding.
		if peerA == nil {
			if n < tokenLen || subtle.ConstantTimeCompare(buf[:tokenLen], token) != 1 {
				log.Printf("Room %s: relay rejecting unauthenticated peer from %s", code, from)
				continue
			}
			peerA = from
			// Strip token, forward remaining data if any
			copy(buf, buf[tokenLen:n])
			n -= tokenLen
		} else if peerB == nil && !udpAddrEqual(from, peerA) {
			if n < tokenLen || subtle.ConstantTimeCompare(buf[:tokenLen], token) != 1 {
				log.Printf("Room %s: relay rejecting unauthenticated peer from %s", code, from)
				continue
			}
			peerB = from
			copy(buf, buf[tokenLen:n])
			n -= tokenLen
		}

		// Forward to the other peer
		var dest *net.UDPAddr
		if udpAddrEqual(from, peerA) {
			dest = peerB
		} else if udpAddrEqual(from, peerB) {
			dest = peerA
		} else {
			// Unknown sender after both peers established — ignore
			continue
		}

		if dest != nil && n > 0 {
			conn.WriteToUDP(buf[:n], dest)
		}
	}
}

func udpAddrEqual(a, b *net.UDPAddr) bool {
	if a == nil || b == nil {
		return false
	}
	// Normalize to 4-byte IPv4 if it's an IPv4-mapped IPv6 address
	aIP := a.IP.To4()
	if aIP == nil {
		aIP = a.IP
	}
	bIP := b.IP.To4()
	if bIP == nil {
		bIP = b.IP
	}
	return aIP.Equal(bIP) && a.Port == b.Port
}

func (s *Server) allocateRelayPort() int {
	for p := s.relayPortBase; p < s.relayPortMax; p++ {
		if !s.relayPorts[p] {
			s.relayPorts[p] = true
			return p
		}
	}
	return 0
}

func (s *Server) sendPeerAddr(to *net.UDPAddr, code string, addr PeerAddr) {
	ipLen := len(addr.IP)
	msg := make([]byte, 1+RoomCodeLen+1+2+ipLen)
	msg[0] = MsgPeerAddr
	copy(msg[1:], code)
	msg[1+RoomCodeLen] = addr.Type
	binary.BigEndian.PutUint16(msg[1+RoomCodeLen+1:], addr.Port)
	copy(msg[1+RoomCodeLen+3:], addr.IP)
	s.conn.WriteToUDP(msg, to)
}

func (s *Server) sendStartPunchTo(host, client *net.UDPAddr, code string) {
	msg := make([]byte, 1+RoomCodeLen)
	msg[0] = MsgStartPunch
	copy(msg[1:], code)
	s.conn.WriteToUDP(msg, host)
	if client != nil {
		s.conn.WriteToUDP(msg, client)
	}
}

func (s *Server) sendError(to *net.UDPAddr, code byte, message string) {
	msg := make([]byte, 2+len(message))
	msg[0] = MsgError
	msg[1] = code
	copy(msg[2:], message)
	s.conn.WriteToUDP(msg, to)
}

func (s *Server) findPeer(room *Room, addr *net.UDPAddr) *Peer {
	if room.Host != nil && udpAddrEqual(room.Host.Addr, addr) {
		return room.Host
	}
	if room.Client != nil && udpAddrEqual(room.Client.Addr, addr) {
		return room.Client
	}
	return nil
}

func (s *Server) otherPeer(room *Room, peer *Peer) *Peer {
	if peer == room.Host {
		return room.Client
	}
	return room.Host
}

func (s *Server) generateRoomCode() string {
	const chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789" // no I/O/0/1 to avoid confusion
	const maxAttempts = 100
	b := make([]byte, RoomCodeLen)
	for attempt := 0; attempt < maxAttempts; attempt++ {
		rand.Read(b)
		for i := range b {
			b[i] = chars[b[i]%byte(len(chars))]
		}
		code := string(b)
		if _, exists := s.rooms[code]; !exists {
			return code
		}
	}
	return "" // caller should check for empty and return ErrRoomFull
}

func (s *Server) cleanupLoop() {
	ticker := time.NewTicker(cleanupPeriod)
	defer ticker.Stop()
	for range ticker.C {
		// Collect expired rooms under lock
		type expiredRoom struct {
			code     string
			hostAddr *net.UDPAddr
			clientAddr *net.UDPAddr
		}
		var expired []expiredRoom

		s.mu.Lock()
		now := time.Now()
		for code, room := range s.rooms {
			if now.Sub(room.LastSeen) > roomTTL {
				log.Printf("Room %s expired", code)
				er := expiredRoom{code: code}
				if room.Host != nil {
					er.hostAddr = room.Host.Addr
				}
				if room.Client != nil {
					er.clientAddr = room.Client.Addr
				}
				expired = append(expired, er)
				delete(s.rooms, code)
			}
		}
		s.mu.Unlock()

		// Send notifications outside the lock
		for _, er := range expired {
			msg := make([]byte, 1+RoomCodeLen)
			msg[0] = MsgRoomExpired
			copy(msg[1:], er.code)
			if er.hostAddr != nil {
				s.conn.WriteToUDP(msg, er.hostAddr)
			}
			if er.clientAddr != nil {
				s.conn.WriteToUDP(msg, er.clientAddr)
			}
		}
	}
}
