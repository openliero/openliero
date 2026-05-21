# Plan: Replace Custom NAT Traversal with libjuice + coturn

## Problem

The current online P2P implementation uses a custom STUN/hole-punch/relay stack that fails in many real-world NAT scenarios:

1. **No full ICE** — Only does blind hole-punching to STUN-observed addresses. Symmetric NATs remap ports per-destination, making the STUN-discovered port useless for the game peer.
2. **No connectivity checks** — Probes are fire-and-forget with a nonce match. There's no RFC-compliant candidate pair validation or priority-based fallback.
3. **No relay candidates in ICE** — The relay is a separate fallback path triggered only after both peers report failure, adding delay.
4. **Custom relay protocol** — The Go relay uses a bespoke token-auth scheme that's fragile and not reusable.

These limitations mean hole-punching works only on easy NATs (full-cone, address-restricted). Symmetric NATs (common on mobile carriers, corporate networks, and some ISPs) always fall through to relay.

## Solution

Replace the custom NAT traversal layer with **libjuice** (a lightweight RFC 8445 ICE library) and **coturn** (standard TURN relay server). This gives us:

- Full ICE with host + server-reflexive + relay candidates
- Proper connectivity checks with candidate pair prioritization
- TURN relay as a first-class candidate (no separate fallback path)
- Battle-tested code handling all NAT types
- ~100 lines of glue code replacing ~1200 lines of custom C++ and Go

## What We Keep

- **ENet** — Still used for reliable/unreliable game transport on top of the established UDP path
- **NetTransport** — Keeps all game packet types, just loses punch/relay code
- **NetSession / NetworkController** — Unchanged game logic
- **Go signaling server** — Simplified to relay ICE credentials + candidates (room codes stay)
- **OnlineConnectState** — Same UI flow, just drives libjuice instead of custom punch

## Architecture (After)

```
┌──────────────────────────────────────────────────────┐
│                   OnlineConnectState                  │
│                                                      │
│  1. Create libjuice agent (with STUN+TURN config)    │
│  2. Signaling: create/join room                      │
│  3. Exchange ICE ufrag/pwd + candidates via signal   │
│  4. libjuice performs ICE (checks all candidate      │
│     pairs, including TURN relay)                     │
│  5. On success → have a connected UDP socket pair    │
│  6. Create ENet host on that socket                  │
│  7. Transition to NetConnectState (same as today)    │
└──────────────────────────────────────────────────────┘
```

## Dependencies

| Dependency | Source | License | Purpose |
|---|---|---|---|
| libjuice | vcpkg (`libjuice`) | BSD-2-Clause | ICE agent (STUN + TURN + connectivity checks) |
| coturn | Docker / apt | Custom permissive | TURN relay server (replaces Go relay) |

libjuice is ~3000 lines of pure C with zero external dependencies. It supports:
- RFC 8445 (ICE)
- RFC 5389 (STUN)
- RFC 5766/8656 (TURN)
- UDP only (perfect for our use case)

## Implementation Phases

### Phase 1: Add libjuice and Create IceAgent Wrapper

**Files to create:**
- `src/game/net/iceAgent.hpp` — C++ wrapper around libjuice
- `src/game/net/iceAgent.cpp` — Implementation

**IceAgent API:**

```cpp
struct IceAgent {
  struct Config {
    std::string stunServer = "stun.l.google.com";
    uint16_t stunPort = 19302;
    std::string turnServer;   // e.g., "turn.liero-server.orbmit.org"
    uint16_t turnPort = 3478;
    std::string turnUser;
    std::string turnPassword;
    uint16_t localPort = 0;   // 0 = ephemeral, or bind to game port
  };

  void start(const Config& config);
  void stop();

  // Get local ICE credentials (send to peer via signaling)
  std::string localUfrag() const;
  std::string localPwd() const;

  // Set remote ICE credentials (received from peer via signaling)
  void setRemoteCredentials(const std::string& ufrag, const std::string& pwd);

  // Add a remote candidate (received from peer via signaling)
  void addRemoteCandidate(const std::string& candidate);

  // Signal that all remote candidates have been provided
  void setRemoteGatheringDone();

  // State
  enum State { New, Gathering, Connecting, Connected, Failed, Disconnected };
  State state() const;

  // Once connected: the selected local address/port and remote address/port
  struct SelectedPair {
    std::string localAddr;
    uint16_t localPort;
    std::string remoteAddr;
    uint16_t remotePort;
  };
  SelectedPair selectedPair() const;

  // Get the socket fd (for handing to ENet after ICE completes)
  int socket() const;

  // Callbacks
  std::function<void(State)> onStateChange;
  std::function<void(const std::string& candidate)> onLocalCandidate;
  std::function<void()> onGatheringDone;
};
```

**Build system:**
- Add `"libjuice"` to `vcpkg.json`
- Add `find_package(LibJuice)` and link in `CMakeLists.txt`

### Phase 2: Update Signaling Protocol

The signaling server needs to relay ICE data instead of raw addresses. New protocol:

**New messages (replacing ReportAddr/StartPunch/PunchOK/PunchFail):**

```
Client → Server:
  IceCredentials [0x07] + [6: room code] + [1: ufrag_len] + [N: ufrag] + [1: pwd_len] + [N: pwd]
  IceCandidate   [0x08] + [6: room code] + [2: candidate_len BE] + [N: candidate SDP string]
  IceGatherDone  [0x09] + [6: room code]

Server → Client:
  PeerCredentials [0x87] + [6: room code] + [1: ufrag_len] + [N: ufrag] + [1: pwd_len] + [N: pwd]
  PeerCandidate   [0x88] + [6: room code] + [2: candidate_len BE] + [N: candidate SDP string]
  PeerGatherDone  [0x89] + [6: room code]
```

**Removed messages:** `ReportAddr (0x03)`, `PunchOK (0x04)`, `PunchFail (0x05)`, `StartPunch (0x84)`, `UseRelay (0x85)`

**Keep:** `CreateRoom`, `JoinRoom`, `RoomCreated`, `PeerJoined`, `Keepalive`, `RoomExpired`, `Error`

**Server changes:**
- Remove `Relay` struct and relay allocation logic entirely
- Room no longer tracks `Addresses []PeerAddr` or `PunchOK/PunchFail`
- Simply forwards ICE credentials and candidates between peers
- Server becomes ~150 lines (down from ~470)

### Phase 3: Update SignalingClient (C++)

**Modify:** `src/game/net/signaling.hpp` and `signaling.cpp`

- Remove: `reportAddress()`, `reportPunchOK()`, `reportPunchFail()`
- Remove: `onStartPunch`, `onUseRelay` callbacks
- Remove: `PeerCandidate` struct (replaced by raw SDP candidate strings)
- Add: `sendIceCredentials(ufrag, pwd)`
- Add: `sendIceCandidate(sdpCandidate)`
- Add: `sendIceGatherDone()`
- Add: `onPeerCredentials(ufrag, pwd)` callback
- Add: `onPeerCandidate(sdpCandidate)` callback
- Add: `onPeerGatherDone()` callback

### Phase 4: Update OnlineConnectState

**Modify:** `src/game/onlineConnectState.hpp` and `onlineConnectState.cpp`

New flow:

```
enter():
  1. Create IceAgent with STUN + TURN config
  2. Wire onLocalCandidate → buffer candidates until signaling ready
  3. Wire onStateChange → transition on Connected/Failed
  4. Start IceAgent (begins gathering)
  5. Connect to signaling server (create/join room)

onRoomCreated / onJoinAcked:
  → Send local ICE credentials to signaling
  → Send any buffered local candidates

onPeerCredentials:
  → agent.setRemoteCredentials(ufrag, pwd)

onPeerCandidate:
  → agent.addRemoteCandidate(candidate)

onPeerGatherDone:
  → agent.setRemoteGatheringDone()

onLocalCandidate:
  → signaling.sendIceCandidate(candidate)

onGatheringDone:
  → signaling.sendIceGatherDone()

onStateChange(Connected):
  → Get selected pair from agent
  → Create ENet host using the agent's socket (or connect existing)
  → Transition to NetConnectState (same as today's connectDirect)

onStateChange(Failed):
  → Show "CONNECTION FAILED" error
```

**Remove:**
- `StunViaHost stunViaHost_`
- All punch-related state (`startedPunch_`, `punchRequested_`, etc.)
- `startPunching()`, `onPunchSuccess()`, `onPunchFailed()`, `connectRelay()`

### Phase 5: Clean Up NetTransport

**Modify:** `src/game/net/transport.hpp` and `transport.cpp`

- Remove: `startPunch()`, `stopPunch()`, `sendProbes()`, `punchPoll()`
- Remove: `PunchCandidate`, `PunchResult`, `PunchState` and all punch state fields
- Remove: `hostViaRelay()`, `connectViaRelay()`, `sendRelayToken()` and relay state fields
- Remove: `PROBE_MAGIC`, relay constants
- Remove: `onPunchSuccess`, `onPunchTimeout` callbacks
- Remove: `onInterceptedPacket` callback (STUN no longer goes through ENet)
- Keep: `host()`, `connect()`, `connectExisting()`, all game packet types

Alternatively, add a new method:
```cpp
// Create ENet host using an existing socket fd (from libjuice)
bool hostOnSocket(int socketFd);
```

This lets us create the ENet host on the socket that libjuice already established the ICE connection through, preserving the NAT mapping.

### Phase 6: Remove Old STUN Code

**Delete:**
- `src/game/net/stun.hpp`
- `src/game/net/stun.cpp`
- `src/tests/test_stun.cpp`

### Phase 7: Deploy coturn (Server-Side)

Replace the Go relay with a standard coturn deployment:

```yaml
# docker-compose.yml addition for the server
coturn:
  image: coturn/coturn
  network_mode: host
  volumes:
    - ./turnserver.conf:/etc/turnserver.conf
  command: ["-c", "/etc/turnserver.conf"]
```

```ini
# turnserver.conf
listening-port=3478
realm=liero-server.orbmit.org
use-auth-secret
static-auth-secret=<GENERATED_SECRET>
total-quota=100
max-bps=256000
no-multicast-peers
fingerprint
```

The signaling server can generate time-limited TURN credentials using the shared secret (standard TURN REST API pattern):

```go
func generateTurnCredentials(secret string) (user, pass string) {
    expiry := time.Now().Add(24 * time.Hour).Unix()
    user = fmt.Sprintf("%d", expiry)
    mac := hmac.New(sha1.New, []byte(secret))
    mac.Write([]byte(user))
    pass = base64.StdEncoding.EncodeToString(mac.Sum(nil))
    return
}
```

The signaling server sends TURN credentials to clients in the `RoomCreated`/`PeerJoined` response so they can configure their ICE agent.

### Phase 8: Update Signaling Server (Go)

**Simplify:** `server/server.go`, `server/protocol.go`

- Remove: `relay.go` entirely
- Remove: `Relay` field from `Room`, relay port allocation, relay goroutines
- Remove: `PeerAddr`, `Addresses`, `PunchOK/PunchFail` from `Peer`/`Room`
- Add: Handle `IceCredentials`, `IceCandidate`, `IceGatherDone` messages (just forward to other peer)
- Add: Include TURN credentials in `RoomCreated`/`PeerJoined` responses

The server becomes a pure message relay (~150 lines).

## Migration Strategy

1. **Feature-flag approach:** Add a compile-time flag `USE_LIBJUICE` so both paths can coexist during development
2. **LAN mode unaffected:** Direct IP connection (`HOST LAN`/`JOIN LAN`) doesn't use ICE at all — unchanged
3. **Test with symmetric NAT:** Use `iptables` to simulate symmetric NAT and verify TURN fallback works
4. **Backwards-incompatible:** The new signaling protocol is not compatible with the old one. Both client and server must be updated together. This is fine for an alpha.

## Risk Assessment

| Risk | Mitigation |
|---|---|
| libjuice socket incompatible with ENet | ENet supports taking over existing sockets. libjuice can yield its fd after ICE completes. Fallback: create new ENet socket to the known peer address. |
| TURN adds latency | Expected ~20-50ms extra RTT vs direct. Acceptable for lockstep with 3-frame delay. Better than no connection at all. |
| coturn operational complexity | Docker one-liner. Can also use free public TURN servers (e.g., Metered.ca free tier) for testing. |
| libjuice doesn't support IPv6 TURN | libjuice supports IPv6 for host/srflx candidates. TURN over IPv4 is sufficient as fallback. |

## Files Changed Summary

| File | Action |
|---|---|
| `vcpkg.json` | Add `libjuice` dependency |
| `CMakeLists.txt` | Add libjuice find/link |
| `src/game/net/iceAgent.hpp` | **Create** — libjuice wrapper |
| `src/game/net/iceAgent.cpp` | **Create** — implementation |
| `src/game/net/signaling.hpp` | Modify — new ICE message types |
| `src/game/net/signaling.cpp` | Modify — new ICE message types |
| `src/game/net/transport.hpp` | Modify — remove punch/relay, add `hostOnSocket()` |
| `src/game/net/transport.cpp` | Modify — remove punch/relay code |
| `src/game/net/stun.hpp` | **Delete** |
| `src/game/net/stun.cpp` | **Delete** |
| `src/game/onlineConnectState.hpp` | Modify — use IceAgent |
| `src/game/onlineConnectState.cpp` | Modify — new ICE-based flow |
| `src/tests/test_stun.cpp` | **Delete** |
| `server/relay.go` | **Delete** |
| `server/server.go` | Simplify — remove relay, add ICE forwarding |
| `server/protocol.go` | Update message types |
| `server/server_test.go` | Update tests |

## Estimated Effort

- Phase 1 (IceAgent wrapper): ~2 hours
- Phase 2-3 (Signaling protocol): ~2 hours
- Phase 4 (OnlineConnectState): ~2 hours
- Phase 5-6 (Transport cleanup): ~1 hour
- Phase 7-8 (Server): ~2 hours
- Testing & debugging: ~3 hours

**Total: ~12 hours** (vs maintaining/debugging the current custom stack indefinitely)

## Success Criteria

1. Two peers behind different NAT types can connect and play
2. Symmetric NAT → TURN relay used automatically (no manual fallback)
3. Easy NAT (full-cone) → direct P2P connection established
4. LAN play continues to work unchanged
5. Connection time ≤ 5 seconds on easy NATs, ≤ 10 seconds via TURN
6. No regression in gameplay determinism or desync detection
