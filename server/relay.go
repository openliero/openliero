package main

import (
	"crypto/rand"
	"crypto/subtle"
	"log"
	"net"
	"sync"
	"time"
)

const (
	relayTimeout     = 10 * time.Minute
	relayAuthTimeout = 10 * time.Second
	relayTokenLen    = 8
	relayAckByte     = 0x01
)

// Relay forwards UDP packets between two authenticated peers.
// It is started synchronously — the caller blocks until the relay is listening.
type Relay struct {
	Code  string
	Port  int
	Token []byte

	conn   *net.UDPConn
	mu     sync.Mutex
	peerA  *net.UDPAddr
	peerB  *net.UDPAddr
	done   chan struct{}
	closed bool
}

// NewRelay allocates a relay token but does not start listening.
func NewRelay(code string, port int) *Relay {
	token := make([]byte, relayTokenLen)
	rand.Read(token)
	return &Relay{
		Code:  code,
		Port:  port,
		Token: token,
		done:  make(chan struct{}),
	}
}

// Start binds to the relay port and starts forwarding.
// It returns nil once the relay is listening (synchronous startup).
// The relay runs in a background goroutine until it times out or is stopped.
func (r *Relay) Start() error {
	addr := &net.UDPAddr{Port: r.Port}
	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		return err
	}
	r.conn = conn

	// Start the forwarding loop in the background
	go r.run()
	return nil
}

// Stop shuts down the relay immediately.
func (r *Relay) Stop() {
	r.mu.Lock()
	if r.closed {
		r.mu.Unlock()
		return
	}
	r.closed = true
	r.mu.Unlock()

	r.conn.Close()
	<-r.done
}

// Done returns a channel that is closed when the relay stops.
func (r *Relay) Done() <-chan struct{} {
	return r.done
}

func (r *Relay) run() {
	defer func() {
		r.conn.Close()
		close(r.done)
		log.Printf("Room %s: relay on port %d stopped", r.Code, r.Port)
	}()

	// Set overall deadline
	r.conn.SetDeadline(time.Now().Add(relayTimeout))

	buf := make([]byte, 2048)
	authDeadline := time.Now().Add(relayAuthTimeout)

	for {
		n, from, err := r.conn.ReadFromUDP(buf)
		if err != nil {
			return
		}

		r.mu.Lock()
		peerA := r.peerA
		peerB := r.peerB
		r.mu.Unlock()

		// Authentication phase: peers present token (as prefix or standalone)
		if peerA == nil || (peerB == nil && !udpAddrEqual(from, peerA)) {
			payload, ok := r.tryAuthenticate(buf[:n], from, peerA, peerB)
			if !ok {
				// Check auth timeout
				if time.Now().After(authDeadline) {
					log.Printf("Room %s: relay auth timeout — shutting down", r.Code)
					return
				}
				continue
			}
			// If there's payload after the token, forward it
			if len(payload) > 0 {
				dest := r.getForwardDest(from)
				if dest != nil {
					r.conn.WriteToUDP(payload, dest)
				}
			}
			continue
		}

		// Both peers authenticated — forward
		dest := r.getForwardDest(from)
		if dest == nil {
			continue
		}
		if n > 0 {
			r.conn.WriteToUDP(buf[:n], dest)
		}
	}
}

// tryAuthenticate checks if a packet contains a valid auth token.
// Returns the payload after the token and true if authenticated.
func (r *Relay) tryAuthenticate(data []byte, from *net.UDPAddr,
	peerA, peerB *net.UDPAddr) (payload []byte, ok bool) {

	if len(data) < relayTokenLen {
		return nil, false
	}

	// Check token — it can be a prefix of any packet
	if subtle.ConstantTimeCompare(data[:relayTokenLen], r.Token) != 1 {
		log.Printf("Room %s: relay rejecting invalid token from %s", r.Code, from)
		return nil, false
	}

	payload = data[relayTokenLen:]

	r.mu.Lock()
	defer r.mu.Unlock()

	if r.peerA == nil {
		r.peerA = from
		log.Printf("Room %s: relay peer A authenticated from %s", r.Code, from)
	} else if r.peerB == nil && !udpAddrEqual(from, r.peerA) {
		r.peerB = from
		log.Printf("Room %s: relay peer B authenticated from %s", r.Code, from)
	}

	// Send ACK back to the peer
	r.conn.WriteToUDP([]byte{relayAckByte}, from)

	return payload, true
}

func (r *Relay) getForwardDest(from *net.UDPAddr) *net.UDPAddr {
	r.mu.Lock()
	defer r.mu.Unlock()

	if udpAddrEqual(from, r.peerA) {
		return r.peerB
	}
	if udpAddrEqual(from, r.peerB) {
		return r.peerA
	}
	return nil
}

func udpAddrEqual(a, b *net.UDPAddr) bool {
	if a == nil || b == nil {
		return false
	}
	aIP := a.IP.To4()
	if aIP == nil {
		aIP = a.IP.To16()
	}
	bIP := b.IP.To4()
	if bIP == nil {
		bIP = b.IP.To16()
	}
	return aIP.Equal(bIP) && a.Port == b.Port
}
