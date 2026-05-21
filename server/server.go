package main

import (
	"crypto/rand"
	"encoding/binary"
	"log"
	"net"
	"sync"
	"time"
)

const (
	roomTTL       = 5 * time.Minute
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
	Code      [RoomCodeLen]byte
	Host      *Peer
	Client    *Peer
	CreatedAt time.Time
	LastSeen  time.Time
	Relay     *Relay // nil until relay is allocated
}

type Server struct {
	conn          *net.UDPConn
	mu            sync.Mutex
	rooms         map[string]*Room // code string → room
	relayPortBase int
	relayPortMax  int
	relayPorts    map[int]*Relay // port → active relay
}

func NewServer(conn *net.UDPConn, relayPortBase, relayPortCount int) *Server {
	return &Server{
		conn:          conn,
		rooms:         make(map[string]*Room),
		relayPortBase: relayPortBase,
		relayPortMax:  relayPortBase + relayPortCount,
		relayPorts:    make(map[int]*Relay),
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
	if room.Client != nil && room.Client.PunchOK {
		s.mu.Unlock()
		s.sendError(from, ErrRoomFull, "room full")
		return
	}

	room.Client = &Peer{Addr: from}
	room.LastSeen = time.Now()
	hostAddr := room.Host.Addr
	hostAddresses := append([]PeerAddr{}, room.Host.Addresses...)

	s.mu.Unlock()

	log.Printf("Room %s: client joined from %s", code, from)

	// Acknowledge to joining client
	ack := make([]byte, 1+RoomCodeLen)
	ack[0] = MsgPeerJoined
	copy(ack[1:], code)
	s.conn.WriteToUDP(ack, from)

	// Notify host
	msg := make([]byte, 1+RoomCodeLen)
	msg[0] = MsgPeerJoined
	copy(msg[1:], code)
	s.conn.WriteToUDP(msg, hostAddr)

	// Send host's addresses to new client
	for _, a := range hostAddresses {
		s.sendPeerAddr(from, code, a)
	}

	// Don't send StartPunch here — wait until client reports its own addresses.
	// handleReportAddr will trigger StartPunch once both sides have addresses.
}

func (s *Server) handleReportAddr(code string, from *net.UDPAddr, addr PeerAddr) {
	s.mu.Lock()

	room, ok := s.rooms[code]
	if !ok {
		s.mu.Unlock()
		s.sendError(from, ErrRoomNotFound, "no such room")
		return
	}
	room.LastSeen = time.Now()

	peer := s.findPeer(room, from)
	if peer == nil {
		s.mu.Unlock()
		s.sendError(from, ErrInvalidMsg, "not in room")
		return
	}

	peer.Addresses = append(peer.Addresses, addr)

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
		return
	}

	peer := s.findPeer(room, from)
	if peer == nil {
		s.mu.Unlock()
		return
	}

	if ok {
		peer.PunchOK = true
		log.Printf("Room %s: punch OK from %s", code, from)
		s.mu.Unlock()
		return
	}

	peer.PunchFail = true
	log.Printf("Room %s: punch FAILED from %s", code, from)
	shouldRelay := room.Host.PunchFail && room.Client != nil && room.Client.PunchFail
	if !shouldRelay || room.Relay != nil {
		s.mu.Unlock()
		return
	}

	// Allocate relay port under lock
	port := s.allocateRelayPort()
	if port == 0 {
		s.mu.Unlock()
		log.Printf("Room %s: no relay ports available", code)
		return
	}

	hostAddr := room.Host.Addr
	clientAddr := room.Client.Addr

	// Release lock while starting relay (ListenUDP may block briefly)
	s.mu.Unlock()

	// Start relay SYNCHRONOUSLY — it's listening before we notify clients
	relay := NewRelay(code, port)
	if err := relay.Start(); err != nil {
		log.Printf("Room %s: failed to start relay on port %d: %v", code, port, err)
		s.mu.Lock()
		delete(s.relayPorts, port)
		s.mu.Unlock()
		return
	}

	// Store relay in room (re-acquire lock)
	s.mu.Lock()
	room, exists = s.rooms[code]
	if !exists || room.Relay != nil {
		delete(s.relayPorts, port)
		s.mu.Unlock()
		relay.Stop()
		return
	}
	room.Relay = relay
	s.relayPorts[port] = relay
	s.mu.Unlock()

	log.Printf("Room %s: relay READY on port %d", code, relay.Port)

	// Clean up relay when it finishes
	go func() {
		<-relay.Done()
		s.mu.Lock()
		delete(s.relayPorts, port)
		s.mu.Unlock()
	}()

	// NOW notify clients — relay is guaranteed listening
	msg := make([]byte, 1+RoomCodeLen+2+relayTokenLen)
	msg[0] = MsgUseRelay
	copy(msg[1:], code)
	binary.BigEndian.PutUint16(msg[1+RoomCodeLen:], uint16(relay.Port))
	copy(msg[1+RoomCodeLen+2:], relay.Token)
	s.conn.WriteToUDP(msg, hostAddr)
	s.conn.WriteToUDP(msg, clientAddr)
}

func (s *Server) allocateRelayPort() int {
	for p := s.relayPortBase; p < s.relayPortMax; p++ {
		if _, used := s.relayPorts[p]; !used {
			s.relayPorts[p] = nil // placeholder
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
	const chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
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
	return ""
}

func (s *Server) cleanupLoop() {
	ticker := time.NewTicker(cleanupPeriod)
	defer ticker.Stop()
	for range ticker.C {
		type expiredRoom struct {
			code       string
			hostAddr   *net.UDPAddr
			clientAddr *net.UDPAddr
			relay      *Relay
		}
		var expired []expiredRoom

		s.mu.Lock()
		now := time.Now()
		for code, room := range s.rooms {
			if now.Sub(room.LastSeen) > roomTTL {
				log.Printf("Room %s expired", code)
				er := expiredRoom{code: code, relay: room.Relay}
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

		for _, er := range expired {
			if er.relay != nil {
				er.relay.Stop()
			}
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
