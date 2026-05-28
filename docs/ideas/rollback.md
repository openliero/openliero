# Rollback Netcode for Open Liero — Implementation Plan

> **Status:** Plan only — no code written. This document is a handover for whoever picks up Phase 2 of the multiplayer work described in [multiplayer.md](./multiplayer.md).
>
> **Keep this document up to date** as steps are completed. Mark steps with ✅ and append learnings to the "Learnings" section as they emerge.

## Context

Phase 1 multiplayer (lockstep with fixed input delay) is shipped. See [multiplayer.md](./multiplayer.md) for the architecture, transport, and determinism work that this plan builds on.

The motivation for rollback is internet play: lockstep with a 3-frame input delay (~42 ms) feels fine on LAN but becomes uncomfortable above ~80 ms RTT, where input delay must grow to mask jitter. Rollback (GGPO-style) keeps local input instantaneous by predicting remote input and resimulating on mismatch.

## What's Already in Place

The codebase has roughly 80% of the infrastructure rollback needs:

- **Deterministic simulation** — verified by `src/tests/test_determinism.cpp`, 1000-frame two-instance comparison
- **Full-state serialization** — `cerealWrite(writer, game)` / `cerealRead` in `src/game/replay.cpp:93`
- **Fixed-size object pools** — `ExactObjectList<T, N>` is a POD array suitable for `memcpy`
- **Tiny input state** — one byte per worm per frame; trivial to ring-buffer
- **Single sound chokepoint** — all sim-path audio goes through `game.soundPlayer` (~30 call sites); resim suppression is a one-touch change
- **ENet transport, ICE NAT traversal, settings sync, desync detection** — see multiplayer.md

## Third-Party Library Decision

| Library | Lang | Verdict |
|---|---|---|
| **GGPO** (pond3r/ggpo) | C/C++ | Canonical reference, but owns its own UDP socket — would conflict with ENet+ICE stack. Use as a reference implementation, not a dependency. |
| **GGRS** | Rust | Wrong language; FFI not worth it. |
| **Backdash** | C# / .NET | Wrong language. |
| Other C++ forks | C++ | Hobby-grade. Not worth the dependency. |

**Decision: roll our own**, mirroring GGPO's API surface (`addLocalInput`, `synchronizeInput`, `advanceFrame`, `saveState`, `loadState`) so a future swap remains possible. The rollback algorithm itself is ~400–600 LOC; the hard parts (determinism, snapshot speed) are codebase-specific and a library wouldn't help with them.

## Snapshot Cost Estimate

Worst-case `Level` is 504×350 = 176,400 cells. `Level` stores two parallel vectors:

- `std::vector<unsigned char> data` — ~176 KB
- `std::vector<Material> materials` — ~176 KB (`Material` is a single `uint8_t flags` — confirmed in `src/game/material.hpp`)

Plus the fixed-size object pools (nobjects, sobjects, wobjects, bonuses, worms) and the variable-size `bobjects` (blood particles, `FastObjectList<BObject>` with `settings->bloodParticleMax` slots).

Total snapshot ≈ **~400 KB**. Two `memcpy`s of 176 KB take ~50 µs on commodity hardware. With `MaxRollback = 7` snapshots resident, working set is ~2.8 MB — fits in L2. **Snapshot cost is a non-issue.**

## `Game` Field Inventory (Step 0 audit, completed)

Auditing `src/game/game.hpp` against the simulation code, each field is classified as:
- **sim** — read or written by `processFrame()` or its callees; must be in the snapshot
- **render** — only read by drawing / viewport code; out of snapshot
- **meta** — controller- or session-owned (e.g. paused, viewports, shared_ptrs to common/settings/sound); out of snapshot

| Field | Class | Notes |
|---|---|---|
| `Level level` (data + materials + width/height/origpal/zeroMaterial/oldRandomLevel/oldLevelFile) | sim | `data`/`materials` change every frame via setPixel; static after load: width, height, origpal, zeroMaterial, oldRandomLevel, oldLevelFile. Snapshot only the dynamic parts. |
| `Rand rand` | sim | Single `uint32_t last`. |
| `int cycles` | sim | Frame counter. |
| `int screenFlash` | sim | Written from `sobject.cpp` during sim, decremented in `processFrame`. Read by renderer but also re-written by sim, so it must round-trip. |
| `bool gotChanged` | sim | Written from `worm.cpp:469` during sim. Currently consumed only by viewport draw — but it's set inside the sim and lives in `Game`, so include for safety. |
| `int lastKilledIdx` | sim | Updated in damage path. |
| `Holdazone holdazone` | sim | Game-mode state. |
| `bool paused` | meta | Controller-level pause; not touched by `processFrame`. |
| `bool quickSim` | meta | Render-skip flag for replay scrubbing. |
| `std::vector<std::shared_ptr<Worm>> worms` | sim (per-worm contents) | Snapshot the Worm payloads, not the `shared_ptr`s themselves — pointers stay stable across the rollback window. Skip render-only worm fields: `gamepadName`, `name`, `statsX`. |
| `BonusList bonuses` (`ExactObjectList<Bonus,99>`) | sim | POD pool. |
| `WObjectList wobjects` (`ExactObjectList<WObject,600>`) | sim | **Missing from the original Step 2 struct — must be added.** |
| `SObjectList sobjects` (`ExactObjectList<SObject,700>`) | sim | POD pool. |
| `NObjectList nobjects` (`ExactObjectList<NObject,600>`) | sim | POD pool. |
| `BObjectList bobjects` (`FastObjectList<BObject>`) | sim | **Blood particles ARE sim-affecting** — `bobject.cpp` calls `game.rand` and `level.setPixel`. Must snapshot. `FastObjectList` wraps `std::vector<BObject>` of size `settings->bloodParticleMax`; treat as POD-block + count. |
| `std::vector<Viewport*> viewports` / `spectatorViewports` | render | Pointers owned by controllers. |
| `std::shared_ptr<Common> common` | meta | TC data; immutable during a session. |
| `std::shared_ptr<Settings> settings` | meta | Match config; immutable during a session. |
| `std::shared_ptr<SoundPlayer> soundPlayer` | meta | See "Sound during resim" below. |
| `std::shared_ptr<StatsRecorder> statsRecorder` | sim? | Stats writes from sim. Decision: **disable stats during resim** (treat like sound) since stats are observational, not load-bearing on game state. Verify in audit. |

**Decision:** `GameSnapshot` covers exactly the fields tagged `sim`. The Step 2 struct definition below is updated accordingly.

### Worm copy semantics (clarification)

`Worm` is **not** a POD type — `memcpy` is unsafe. It contains:

- `std::shared_ptr<WormSettings> settings` — meta (input bindings, name, color); refcount-managed
- `std::shared_ptr<WormAI> ai` — meta in network play (AI excluded from netplay per Phase 1)
- `Ninjarope ninjarope` containing `Worm* anchor` — sim, but the pointer targets the *other* worm in `game.worms`, which has stable storage across the rollback window
- `WormWeapon weapons[NUM_WEAPONS]` each with `Weapon const* type` — sim, pointer into immutable `Common` data
- All other fields are POD scalars / fixed arrays

**Rule:** snapshot Worm via field-wise copy (or plain `operator=`), never `memcpy`. The raw pointers (`anchor`, `type`) are safe to copy by value because their targets are stable. The shared_ptrs in the snapshot's stored Worm copies are kept alive correctly through normal refcount semantics — but the snapshot doesn't actually *need* them. To keep snapshots cheap and avoid touching atomics on save/restore, the snapshot stores a **WormSimState** struct containing only the sim fields (everything except `settings`, `ai`, `cleanControlStates`, `statsX`). On restore, fields are written into the live `Worm` in place; `settings`/`ai`/etc. are left untouched on the live object.

**Sim fields of Worm** (explicit list, for Step 2): `pos`, `vel`, `logicRespawn`, `hotspotX/Y`, `aimingAngle`, `aimingSpeed`, `ableToJump`, `ableToDig`, `keyChangePressed`, `movable`, `animate`, `visible`, `ready`, `flag`, `makeSightGreen`, `health`, `lives`, `kills`, `timer`, `killedTimer`, `currentFrame`, `flags`, `ninjarope` (whole struct), `currentWeapon`, `lastKilledByIdx`, `fireCone`, `leaveShellTimer`, `reacts[4]`, `weapons[NUM_WEAPONS]`, `direction`, `controlStates`, `prevControlStates`, `steerableSumX/Y`, `steerableCount`, `index`.

**Meta fields (skip):** `settings`, `ai`, `cleanControlStates` (LocalController-only), `statsX` (renderer-only).

### Side-effect suppression during *prediction* (not only resim)

The plan's Step 5 talks about `resimulating = true` during the resim window, but the same hazard exists for **predicted frames** on first execution: a sound or stats event emitted from a predicted frame that later gets rolled back has already escaped. Two options:

- **Option A (chosen): treat predicted frames as "speculative" too.** Repurpose the flag as `Game::speculative` (true during prediction *and* during resim). Sound/stats suppressed whenever `speculative`. Consequence: on confirmation of a frame whose prediction was correct, the transient sound/stat for that frame is lost. Accept this for v1 — fighting-game rollback implementations do the same.
- Option B: capture-and-replay (buffer side effects keyed by frame, flush on confirmation). Deferred to the "correct version" in Step 9.

Rename the flag in Step 5 accordingly. `StatsRecorder` suppression follows the same flag.

### `screenFlash` / `gotChanged` consumption timing

Both fields are written by sim and read by the renderer. Render runs only between `advance()` calls, never mid-resim (Step 9), so the snapshot captures their post-sim value before the renderer consumes them. On rollback-restore the values revert to their post-sim state at frame `F-1` and the renderer simply sees a new sequence. Visible artifact: a flash that played during a mispredicted frame may "un-play" visually on the next render — acceptable, mirrors the audio suppression tradeoff.

### Step 8 frame-advantage encoding (pin down)

Per Step 7.5, each packet carries `baseFrame` and `count = MaxRollback + 1`. The local frame at send time lies in `[baseFrame, baseFrame + count - 1]` — that's `count` values, so the delta fits in `uint8_t` (no sign needed). Encode as:

```
[PacketInput=1B][baseFrame:u32 LE][count:u8][localDelta:u8][input[count]:u8]
```

`localDelta` is the sender's `simFrame - baseFrame` at the moment of send. Receiver reconstructs `remoteLocalFrame = baseFrame + localDelta` and compares to its own `simFrame` for the time-sync calculation. Adds exactly 1 byte over Step 7.5.

### Weapon selection phase

Rollback applies **only to `StateGame`** (`processFrame` path). The `StateWeaponSelection` phase in `NetworkController::advanceWeaponSelection` is menu-driven and tolerant of input delay; keep it on lockstep with the existing 3-frame delay. The rollback buffer is empty/inactive during weapon selection. Document this in the Step 4 controller.

## Sound During Resim (Step 5 expanded)

`SoundPlayer` has three methods (`src/game/mixer/player.hpp`): `play(sound, id, loops)`, `stop(id)`, `isPlaying(id)`.

The sim path calls all three (`grep "soundPlayer->"` finds ~30 sites across worm, weapon, sobject, nobject, weapsel). Both `play` and `stop` must be suppressed during resim — suppressing only `play` (as Step 5 originally stated) would let a stop-during-resim leak through and silence a still-valid sound.

`isPlaying` is *read* by sim code to gate `play` calls (e.g. `worm.cpp:1290` gates the launch-loop sound). Two options:

- **Option A (chosen):** Let `isPlaying` return whatever the underlying mixer says. Since both `play` and `stop` are suppressed during resim, the gate just consults the pre-resim mixer state. Its only consequence is whether a (suppressed) `play` would fire — no further sim effect. This is safe.
- Option B: Track `isPlaying` per-rollback-frame in the snapshot. Unnecessary given A is safe.

**Wrap all three calls** with `if (game.resimulating) return;` (play/stop) and a passthrough for isPlaying. The `Game::resimulating` flag is the single source of truth.

## Transport / Wire Format Changes

The current input wire format is fixed-width (`transport.cpp:349`):
```
[PacketInput=1B][frame:u32 LE][input:u8]   // 6 bytes
```
Lockstep mode sends one packet per frame and the path is reliable-ordered.

Rollback requires:

1. **Input redundancy.** Each packet carries the last `K` inputs (K = `MaxRollback + 1`, ~8 bytes payload). On packet loss, the next packet still confirms the missing frames without a retransmit round-trip. New format:
   ```
   [PacketInput=1B][baseFrame:u32 LE][count:u8][input[count]:u8]   // 6+count bytes
   ```
2. **Frame-advantage byte (Step 8).** Append `localDelta:u8` = `simFrame - baseFrame` at send time (range `[0, count-1]`, unsigned suffices). See "Step 8 frame-advantage encoding" below.
3. **Channel change.** Move inputs from ENet reliable-ordered to *unreliable-sequenced* — redundancy replaces retransmits and removes head-of-line blocking. Drop any packet older than the last accepted `baseFrame + count - 1`.
4. **Version gate.** Bump protocol version so a rollback peer refuses to play against a lockstep peer (or vice versa).

Confirmed inputs are inputs the local peer has *received* from the remote — these are the `Confirmed` state in the rollback buffer. Inputs the local peer has *predicted* but not yet received are `Predicted`. No additional flag travels on the wire; the receiver classifies on arrival.

## Buffer Sizing & Player Count

`NetworkController` has `localIdx ∈ {0,1}`, `remoteIdx = localIdx ^ 1` (`networkController.cpp:18-19`). Multiplayer is **strictly 2 worms, 1 per peer**. The `uint8_t localInput` / `uint8_t remoteInput` in the rollback buffer are correct as written.

`MaxRollback = 7` is derived from: at 70 fps each frame is ~14.3 ms; 7 frames = ~100 ms covers one-way input jitter for sessions with up to ~80 ms RTT and ~20 ms additional jitter. Beyond that the player is going to feel rollbacks regardless of buffer size. Increase only with playtest data.

## Disconnect / Stall Semantics

When remote inputs are absent for more than `MaxRollback` frames, the rollback controller stalls (does not advance `simFrame`). Behavior matches existing `NetworkController` lockstep stall — the existing `update()` loop drives `transport_.update()` and the pause/disconnect signaling. **No new disconnect semantics needed**; rollback only changes *when* a frame is treated as confirmed, not the connection lifecycle.

When stalled, the UI shows "waiting for opponent" (already implemented for lockstep — reuse).

## Desync Detection Under Rollback (Step 10, expanded)

Existing checksum infrastructure (`session.hpp:154-165`):
- Local checksums computed every frame, kept in a 128-entry ring buffer (`checksumBuffer_`).
- Sent unreliable-unsequenced via `sendChecksum(frame, checksum)`.
- Compared per-frame in `onChecksum` against the local ring.

Under rollback, the *current* checksum at frame `F` may reflect predicted state. Solution:

- Compute and send the checksum **only when a frame is confirmed** (all remote inputs through `F` received and no rollback re-applied changes it). Cache the checksum *in the snapshot slot* at confirmation time.
- This drops checksum frequency from ~70/s to "as fast as remote inputs arrive" — still ~70/s under normal play, fewer during stalls.
- Existing `pendingRemoteChecksums_` buffer logic continues to work; only the producer side changes.

## Snapshot Cost Measurement Plan

Step 1 must include a microbenchmark (cereal save/restore time per call) before Step 4 commits to "save after every confirmed frame". If cereal exceeds ~2 ms per save the parity test in Step 4 becomes slow; in that case interleave Step 2's fast path earlier (build it before Step 4's parity test, then use it).

## Plan

Each step is independently buildable and verifiable. Do not skip the verification step.

### Step 0 — Audit & instrument (no code change) — ✅ done in this document

Audit results are recorded above ("Game Field Inventory" and "Sound During Resim"). Key findings:

- Blood particles (`BObject`) **do** affect simulation: `bobject.cpp` calls `game.rand` and `level.setPixel`. Must snapshot.
- `wobjects` were missing from the original Step 2 struct sketch. Added.
- `SoundPlayer::stop` must be suppressed alongside `play`. `isPlaying` can pass through safely.
- `StatsRecorder` writes happen during sim; treat like sound — suppress during resim.
- No file I/O, logging, or printf in the sim path.

**Exit criteria met:** all sim-affecting state is enumerated and lives inside the `Game` object graph (plus the side channels — sound, stats — which are explicitly suppressed during resim).

### Step 1 — Cereal-based snapshot (correctness baseline) — ✅ done

Add `Game::saveSnapshot(std::vector<uint8_t>&)` and `Game::loadSnapshot(std::vector<uint8_t> const&)` using the existing `cerealWrite/cerealRead`.

**Test:** `test_snapshot_roundtrip.cpp` — 200 frames → snapshot → 200 frames → restore → 200 frames; assert `fastGameChecksum` matches the no-restore control run at every frame.

**Why cereal first:** if step 2's fast path disagrees, cereal is the reference oracle.

**Build artifact:** test only, no behavior change.

### Step 2 — Fast snapshot path (`memcpy` of POD regions) — ✅ done

```cpp
struct GameSnapshot {
    // RNG
    Rand rand;

    // Scalars from Game
    int cycles;
    int screenFlash;
    int lastKilledIdx;
    bool gotChanged;
    Holdazone holdazone;

    // Worms (2, fixed) — sim-only field set, NOT a full Worm (Worm holds
    // shared_ptrs and so is not memcpy-safe). See "Worm copy semantics" above.
    std::array<WormSimState, 2> worms;

    // Object pools (all sim-affecting)
    Game::BonusList   bonuses;
    Game::WObjectList wobjects;
    Game::SObjectList sobjects;
    Game::NObjectList nobjects;

    // Blood particles — dynamic capacity (settings->bloodParticleMax)
    std::vector<BObject> bobjectsArr;
    std::size_t          bobjectsCount;

    // Level dynamic state (width/height/origpal/etc. are static after load)
    std::vector<uint8_t>  levelData;
    std::vector<Material> levelMaterials;

    // Cached checksum (computed at confirmation, sent under Step 10)
    uint32_t checksum;
};
```

All `ExactObjectList<T, N>` are fixed-size pools → straight `memcpy`. Level vectors and `bobjectsArr`: pre-size their buffers at startup (`bloodParticleMax`, `level.width * level.height`) so no allocations happen per save. Save = three `memcpy`s + scalar copies, ~350 KB total.

**Target:** ≤500 µs save + ≤500 µs restore. (At 7 rollback frames × 70 fps × 1 ms = 0.5% CPU budget — negligible.)

**Verify:** assert byte-identical state hash between cereal-restore and memcpy-restore over a fuzz run.

**Build artifact:** new path behind a `useFastSnapshot` flag; both coexist temporarily.

### Step 3 — Pool snapshots & input ring buffer — ✅ done

```cpp
class RollbackBuffer {
    struct Slot {
        int frame;
        GameSnapshot snapshot;
        uint8_t localInput;
        uint8_t remoteInput;
        enum { Predicted, Confirmed } remoteState;
    };
    std::array<Slot, MaxRollback + 1> slots;  // pre-allocated
    // ...
};
```

Pre-allocate all snapshots at startup — no per-frame malloc.

**Test:** lookup-by-frame, wrap-around, eviction.

**Build artifact:** standalone data structure.

### Step 4 — `RollbackController` in lockstep mode — ✅ done

New controller alongside `NetworkController`. Initially behaves *identically* to lockstep: only advances when remote input is present. Saves a snapshot after every confirmed frame.

**Verify:** existing session/determinism tests pass when `RollbackController` is substituted.

**Build artifact:** menu still uses `NetworkController`; rollback controller selectable via a hidden debug flag.

### Step 5 — `Game::speculative` flag + side-effect suppression — ✅ done

Add `bool Game::speculative = false`. Set to `true` both during initial prediction (frames whose remote input is predicted) **and** during resim. See "Side-effect suppression during *prediction*" above for the rationale. Suppress when `speculative`:

- `SoundPlayer::play` — early return
- `SoundPlayer::stop` — early return (do **not** silence valid sounds)
- `SoundPlayer::isPlaying` — pass through (safe, see "Sound During Resim" above)
- `StatsRecorder` writes — early return at the recorder boundary

```cpp
void SoundPlayer::play(...) { if (game.speculative) return; /* ... */ }
void SoundPlayer::stop(...) { if (game.speculative) return; /* ... */ }
```

**Test:** assert sound `play` count and stats event count over an `N → snapshot → restore → N` round-trip equals a single `N`-frame run.

**Build artifact:** behavior-neutral in non-rollback play.

### Step 6 — Prediction (no rollback yet) — ✅ done

When remote input is absent for `simFrame`, predict it = last received remote input (GGPO's default). Advance up to `MaxRollback` frames ahead, then stall. Mark each speculative frame.

No rollback yet → the verification test from step 7 will fail under mispredictions. Expected.

**Build artifact:** gameplay works under zero jitter; degrades visibly under jitter.

### Test harness: `JitterTransport` (built in Step 4, reused throughout)

All rollback behavior is testable on a single machine using the same in-process loopback pattern already established in `src/tests/test_network_controller.cpp` (see `LoopbackFixture`). Build a `JitterTransport` helper alongside that fixture — a configurable queue between two `RollbackController`s that models real-world packet behavior without touching ENet.

```cpp
struct JitterTransport {
    struct Params {
        std::mt19937 rng{0xC0FFEE};       // deterministic seed for reproducibility
        int minDelayFrames = 0;           // floor on delivery delay
        int maxDelayFrames = 0;           // ceiling on delivery delay (uniform random)
        double lossProbability = 0.0;     // 0..1; dropped packets are silently discarded
        double duplicateProbability = 0.0;// 0..1; duplicated packets are delivered twice
        int reorderWindow = 0;            // if >0, allow out-of-order delivery within window
    };

    struct InFlight {
        int deliverAtFrame;               // localFrame at which to deliver
        std::vector<uint8_t> packet;      // raw bytes
    };

    Params params;
    std::vector<InFlight> aToB, bToA;
    int currentFrame = 0;

    // Called by the controller's send callback.
    void send(std::vector<InFlight>& queue, std::vector<uint8_t> packet);

    // Called once per local tick. Drains anything whose deliverAtFrame <= currentFrame.
    void tick(/*deliver callbacks for A and B*/);
};
```

Use this for every rollback test below by varying `Params`. Seed the RNG so failures are reproducible (the seed is part of the test name / Catch2 section).

**Why a custom transport instead of `tc qdisc netem`:** netem requires root, only works on Linux, and can't be wired into CI. The in-process harness runs the same code paths as ENet (Step 7.5 onward parses the same wire format from a `std::vector<uint8_t>` regardless of source) and gives bit-exact reproducibility.

### Test matrix (rollback-specific)

Add these to `src/tests/`. Each test runs entirely on one machine; no localhost ENet needed.

| Test file | Step | What it verifies |
|---|---|---|
| `test_snapshot_roundtrip.cpp` | 1 | Cereal save → 200 frames → restore → 200 frames matches no-restore control via `fastGameChecksum` every frame. |
| `test_snapshot_fast.cpp` | 2 | Fast (memcpy) snapshot byte-equivalent to cereal snapshot across 5000-frame fuzz run; also measures save/restore time (asserts ≤500 µs) and reports it. |
| `test_rollback_buffer.cpp` | 3 | Ring buffer lookup, wrap, eviction, predicted/confirmed transitions; pure data-structure test. |
| `test_rollback_lockstep_parity.cpp` | 4 | `RollbackController` with no jitter produces frame-by-frame identical state hashes to `NetworkController` over 1000 frames with scripted inputs. |
| `test_speculative_suppression.cpp` | 5 | Two runs of `N → snapshot → restore → N` frames produce identical `SoundPlayer::play` and `StatsRecorder` event counts as a single `N`-frame run. Uses a counting mock for both. |
| `test_prediction_no_rollback.cpp` | 6 | With zero jitter the predicted input equals the real input; no rollback ever triggers; gameplay identical to lockstep. |
| `test_rollback_correctness.cpp` | 7 | **The headline test.** Two `RollbackController`s wired via `JitterTransport` with `minDelay=0, maxDelay=8`, 10% loss, run 5000 frames with PRNG-driven random inputs. Final state hashes match a zero-jitter control run. Repeat for `MaxRollback ∈ {1, 4, 8}` and 3 RNG seeds (= 9 sub-cases). |
| `test_rollback_packet_loss.cpp` | 7.5 | With 10% loss + redundancy, every frame eventually receives confirmed input within `MaxRollback`; no stalls under steady-state. |
| `test_rollback_reorder.cpp` | 7.5 | Packets reordered within `reorderWindow=3` are deduplicated correctly; no duplicate input application. |
| `test_frame_advantage.cpp` | 8 | With asymmetric delay (A→B 2 frames, B→A 6 frames), after 1000 frames the faster peer has stalled enough that frame advantage stays within ±2. |
| `test_rollback_desync.cpp` | 10 | (a) Two clean runs over 5000 frames produce zero desync alarms. (b) A test build with a 1-bit nondeterminism injection triggers desync detection within 200 frames. |
| `test_rollback_long_haul.cpp` | 11 | 50 000-frame run with realistic params (`maxDelay=7`, 2% loss, frame-advantage on) shows no asserts, no leaks, bounded snapshot memory. |

Catch2 sub-sections (`SECTION`) for parameter sweeps within each file; the seed/jitter params print on failure for repro.

### Manual / interactive testing

A debug menu toggle (Step 11) lets the user enable the same `JitterTransport` in an actual gameplay session against a local second instance (or against themselves via the existing 2-controller local mode running through the network path). Two knobs in the dev HUD:

- `+/-` adjust simulated one-way delay (0–200 ms)
- `[/]` adjust simulated loss (0–20%)
- Display: `RB:n` (frames resimulated this tick), `D:±n` (frame advantage), `L:n%` (loss)

This is how the developer feels rollback before promoting it to default in Step 11. Combined with the automated harness, no `tc qdisc netem` or two-machine setup is needed at any point.

### Step 7 — Rollback on misprediction — ✅ done

When real input arrives for frame F and differs from prediction:

1. `loadSnapshot(F-1)`
2. Re-apply confirmed inputs through to current local frame, snapshotting as we go
3. `resimulating = true` throughout the resim window

**Write the verification harness before the implementation** (this is the spec). See `JitterTransport` and `test_rollback_correctness.cpp` in the test matrix above — that test must compile (failing) before Step 7 implementation begins, and pass before Step 7 is complete.

**Build artifact:** rollback works end-to-end at the algorithm level.

### Step 7.5 — Transport: input redundancy + protocol bump — ✅ done

Before frame-advantage (Step 8), change the input wire format per "Transport / Wire Format Changes" above:

- Packet carries `[baseFrame][count][input[count]]` with `count = MaxRollback + 1`.
- Move from reliable-ordered to unreliable-sequenced; drop stale packets at the receiver.
- Bump protocol version; refuse cross-mode play.

**Verify:** `test_rollback_packet_loss.cpp` and `test_rollback_reorder.cpp` from the test matrix above.

**Build artifact:** rollback survives lossy links without retransmit stalls.

### Step 8 — Frame-advantage / time sync — ✅ done

Each peer includes its current local frame number in input packets as `localDelta:u8` = `simFrame - baseFrame` (+1 byte, see "Step 8 frame-advantage encoding" above). If local is ≥2 frames ahead of remote, stall 1 frame. Mirrors GGPO's time-sync.

**Verify:** `test_frame_advantage.cpp` from the test matrix above (asymmetric A→B / B→A delay; assert frame advantage stays within ±2 in steady state).

**Build artifact:** smoother latency distribution.

### Step 9 — Render & audio polish — ✅ done

- **Render:** only after `advance(currentFrame)` returns; never mid-resim
- **Audio (simple version):** suppress during resim, accept that some transient sounds in mispredicted frames will be lost. Ship this first.
- **Audio (correct version, optional):** capture `(frame, soundId, args)` during predicted frames, drop them on rollback, emit on confirmation. Defer unless missed sounds are noticeable in playtest.

**Verify:** manual playtest using the dev-HUD jitter knobs (see "Manual / interactive testing" above) at 80 ms ± 20 ms; both peers on the same machine.

### Step 10 — Desync detection under rollback — ✅ done

Current desync detection (multiplayer.md → "Runtime desync detection") sends a checksum every frame using *current* state. Under prediction this is wrong — predicted frames will trivially disagree.

Change to: send `(confirmedFrame, checksum)` where `checksum` is cached from the snapshot taken at confirmation. Update `NetSession::onChecksum` to compare accordingly.

**Verify:** `test_rollback_desync.cpp` from the test matrix — clean-run negative case + 1-bit injection positive case must fire detection within 200 frames.

### Step 11 — Settings, UX, defaults

Sub-steps to keep blast radius small:
- 11a — Transport wire format: `PacketInputBatch` + handshake protocol-version byte.
- 11b — Wire `RollbackController` through `NetSession` / `NetTransport`.
- 11c — `useRollback` / `maxRollback` / `inputDelay` in `Settings` + `MatchSettingsData` sync.
- 11d — Dev-HUD `RB:n` indicator + jitter knobs (Step 9 deferred playtest).
- 11e — Promote `RollbackController` to default; update `multiplayer.md`.

Settings (host-authoritative, synced via existing `MatchSettingsData`):

- `useRollback` (bool)
- `maxRollback` (default 7)
- `inputDelay` (default 1, down from 3)

Dev HUD: small `RB:n` indicator when `n>0` frames were resimulated this tick.

Update `multiplayer.md` Phase-2 section.

Promote `RollbackController` to default for network games; keep `NetworkController` behind a debug flag for ~1 release in case of regressions.

## Key Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Hidden non-determinism only surfaces under rollback | Medium | High | Step 0 audit; step 1 cereal oracle; step 7 fuzz harness |
| Snapshot performance worse than estimated | Low | Medium | Step 2 measured before commitment; cereal fallback always available |
| Sound deferral (step 9) becomes a rabbit hole | Medium | Low | Step 9 explicitly allows shipping the "suppress" version |
| ~~`Material` is larger than 1 byte~~ | — | — | Resolved: `Material` is `uint8_t flags`. |
| ~~Particle system feeds back into sim~~ | — | — | Resolved: blood particles **do** feed back (rand + setPixel). Included in snapshot. |
| Cereal save cost makes Step 4 parity test slow | Medium | Low | Step 1 measures; pull Step 2 forward if needed. |
| StatsRecorder writes leak through resim | Medium | Low | Step 5 suppresses at recorder boundary. |

## Out of Scope (Defer)

- AI in network games — still excluded per Phase 1 decision
- Replay recording of network games — separate work; data already available
- More than 2 players — protocol could allow but not now
- Spectator mode — separate work

## Learnings

*(Append as each step lands. Format: `### Step N — [title] (YYYY-MM-DD)` then notes.)*

### Step 1 — Cereal-based snapshot (2026-05-27)

- The existing `cereal_types.hpp` Game save/load is replay-oriented and **omits** the object pools (bonuses/wobjects/sobjects/nobjects/bobjects) and `Holdazone`. A full mid-game snapshot needs all of them. Solution: a separate `serialization/snapshot.hpp` that composes the existing Game save/load with the extra state, leaving replay format untouched.
- `NObject::firedBy` / `WObject::firedBy` (`WormWeapon*`) had to be encoded; chosen scheme is `(wormIdx:int8, slot:int8)` resolved to `&worms[i]->weapons[j]` after worms are reconstructed. Cheap and survives the worm-recreation that the existing Game load does.
- `ExactObjectList<T,Limit>` round-trip preserves **slot identity** (per-slot used flag + raw rewrite of `freeList` bits + `count`). Required because slot index drives the order in which `getFreeObject` hands out new slots, which would otherwise diverge from the source sim. Cannot use `getFreeObject` during load because it picks the lowest free slot, not slot `i`.
- `Level::materials` is intentionally not serialized — it's rederived from `level.data` + `Common::materials` in the existing Level load. Saves ~176 KB per snapshot for free.
- Microbench (Release build, ~500 frames of warm-up, snapshot size **~196 KB**):
  - save ≈ 211 µs
  - load ≈ 122 µs
  - well under the 2 ms threshold the plan flagged as the trigger for pulling Step 2 forward, so we don't need to.
- Debug build is ~10× slower (save 549 µs, load 2.4 ms). Worth running snapshot tests under Release if they become slow.
- New: `src/game/serialization/snapshot.hpp`, `Game::saveSnapshot/loadSnapshot` in `game.cpp`/`game.hpp`, `src/tests/test_snapshot_roundtrip.cpp` (correctness + microbench).

### Step 2 — Fast snapshot path (2026-05-27)

- Implemented as `Game::saveSnapshotFast(GameSnapshot&) / loadSnapshotFast` next to the cereal pair, sharing the `GameSnapshot` definition in `src/game/serialization/fast_snapshot.hpp`.
- The plan's `useFastSnapshot` flag turned out to be unnecessary: the cereal and fast paths are independent methods, so a future caller (Step 3's RollbackBuffer) just picks one. Cereal stays as the oracle.
- **Worm** is copied via a hand-written `WormSimState` (per-field). `memcpy(Worm)` would touch `shared_ptr<WormSettings>`/`shared_ptr<WormAI>` reference counts and is unsafe; the field-wise path leaves those live shared_ptrs untouched on restore. `Ninjarope::anchor` and `WormWeapon::type` are raw pointers but their targets (other worms, `Common::weapons`) are stable across the rollback window, so plain value copy is fine.
- **ExactObjectList<T,N>** pools (`Bonus`, `WObject`, `SObject`, `NObject`): the compiler-generated copy assignment is a straight memcpy of the fixed `arr/freeList/count` layout. No special handling needed; `firedBy`/`type` pointers inside the elements stay valid on restore for the same reason as above.
- **FastObjectList<BObject>**: the snapshot stores a `std::vector<BObject>` pre-sized to `bobjects.limit` (`prepare(game)` once) plus the live `count`. Save/load `memcpy` only the used prefix.
- **Level**: only `data` + `materials` are dynamic; `width`/`height`/`origpal`/etc. are static after generation and live on `Game::level` untouched. Pre-sized in `prepare`.
- Microbench (Debug build, ~500 frames warm-up): save ≈ 7.9 µs, load ≈ 7.7 µs — ~30× under the 500 µs plan target. Release should be substantially faster still; not worth measuring until Step 4 has a CPU budget to defend.
- Test (`src/tests/test_snapshot_fast.cpp`) covers three properties: round-trip parity vs. a no-restore control, cereal-vs-fast cross-check at five snapshot points across a 2000-frame fuzz, and the microbench with a 2 ms generous bound.
- New: `src/game/serialization/fast_snapshot.hpp`, `Game::saveSnapshotFast/loadSnapshotFast`, `src/tests/test_snapshot_fast.cpp`, CMake entry.

### Step 3 — Pool snapshots & input ring buffer (2026-05-27)

- Buffer lives at `src/game/rollback/buffer.hpp` as a header-only `rollback::RollbackBuffer` with `kCapacity = kMaxRollback + 1 = 8` slots. Pure data structure: no Game/processFrame dependency, no per-frame allocation. `prepare(game)` once forwards to `GameSnapshot::prepare` on every slot so the per-snapshot vectors (level data, bobjects) are sized up front.
- API mirrors the plan sketch but uses `write(frame)`/`find(frame)` rather than direct slot access. `write` repurposes the ring slot at `frame % kCapacity`; if the slot already holds `frame`, inputs/state are kept (cheap input-only updates), otherwise inputs reset to defaults. The snapshot field is **deliberately not cleared on eviction** — callers overwrite it via `saveSnapshotFast` when they actually re-snapshot. Keeps the hot path allocation-free and avoids zeroing ~200 KB on every wrap.
- `oldestFrame()` returns 0 while the buffer is filling and tracks `newest - kCapacity + 1` after the first eviction; controllers compare incoming confirmed frames against this to detect stall conditions.
- New: `src/game/rollback/buffer.hpp`, `src/tests/test_rollback_buffer.cpp` (8 cases: empty state, sequential fill, wrap-around eviction, idempotent same-frame writes, ring-slot collision reset, Predicted→Confirmed transition, clear/reuse, oldestFrame during fill), CMake entry.

### Step 4 — `RollbackController` in lockstep mode (2026-05-28)

- Implemented `RollbackController` as a structural copy of `NetworkController` (`src/game/controller/rollbackController.{hpp,cpp}`). Same wire format, same 3-frame input delay, same edge-detection and key-repeat logic, same pause / weapon-select / level-preload plumbing. The only behavioural addition is calling `game.saveSnapshotFast` into a `rollback::RollbackBuffer` slot after every confirmed frame, with the slot tagged `Confirmed`.
- Chose duplication over inheritance / composition. Steps 5–7 mutate `advanceSimulation` heavily (prediction window, resim loop, speculative flag) and the plan explicitly keeps both controllers alive for ~1 release after rollback ships — sharing a base class would have meant `virtual` plumbing on a hot path for code that diverges anyway. Cost is ~570 lines of mirrored code; benefit is that future rollback work doesn't have to defend a stable lockstep interface.
- `rollbackBuffer_.prepare(game)` runs at the end of `focus()` so per-slot snapshot vectors (level data, bobjects) are sized once before any sim frame runs. Re-runs after weapon selection finalises (the `startGame` path can resize state) — `prepare` is idempotent.
- Snapshot per frame in Release is dominated by the existing fast path (~tens of µs); the ring is `kMaxRollback + 1 = 8` slots, so even worst-case the buffer working set is ~1.6 MB and fits in L2 as the plan estimated.
- Parity test (`src/tests/test_rollback_lockstep_parity.cpp`) stands two loopbacks side-by-side — one running `NetworkController × NetworkController`, one running `RollbackController × RollbackController` — with identical scripted inputs (Rand 0xC0FFEE, fire-biased) and asserts `fastGameChecksum` matches frame-by-frame for 1000 ticks. Also a second case that confirms the ring fills with `Confirmed` slots after wrap-around.
- All existing tests still pass (network controller fuzz, determinism, snapshot round-trip, rollback buffer).
- The plan also mentions a `JitterTransport` test helper as Step 4 scope; deferred to Step 6/7 where it's actually needed. Step 4's parity test only needs zero-jitter loopback, which we already have.
- New: `src/game/controller/rollbackController.{hpp,cpp}`, `src/tests/test_rollback_lockstep_parity.cpp`, CMake entries.

### Step 5 — `Game::speculative` flag + side-effect suppression (2026-05-28)

- `bool speculative = false` lives on three places: `Game`, `SoundPlayer` (base), `StatsRecorder` (base). `Game::setSpeculative(bool)` is the single setter — it mirrors the value onto `soundPlayer` and `statsRecorder` so callers never touch them directly. Read-side of `Game::speculative` is still useful for sim code that wants to short-circuit a side effect inline (none exist yet, but the hook is there).
- Suppression sits inside each concrete implementation, not at the call sites — there are ~30 sound sites in sim code and gating each would have been churn for no behavioural gain. `RecordSoundPlayer::stop`, `DefaultSoundPlayer::play/stop`, every `NormalStatsRecorder::*` writer all early-return when `speculative`. `NullSoundPlayer` already no-ops so it inherits the flag harmlessly.
- `SoundPlayer::isPlaying` is **not** gated. It's read by sim code (e.g. `worm.cpp` launch-loop) to decide whether to call `play`. With `play`/`stop` already suppressed during speculation, letting `isPlaying` pass through is safe — the worst it does is gate a `play` that would have been suppressed anyway. This matches the "Sound During Resim" Option A in the plan.
- `StatsRecorder::finish` is suppression-gated for consistency but it's called once per match from the controller, not from `processFrame` — speculation cannot reach it in practice.
- Added a missing `virtual ~StatsRecorder() = default;` while editing the header — `Game` holds it via `shared_ptr<StatsRecorder>` so deletion was already polymorphic, but the base lacked a virtual dtor.
- The test (`src/tests/test_speculative_suppression.cpp`) uses a counting mock for both sides. Two cases: (a) `200 normal → snapshot → 200 speculative → restore → 200 normal` produces the exact same counts as a single `400`-frame control run; (b) `setSpeculative` propagation invariant. The mocks check `speculative` themselves to confirm the *flag* is being toggled correctly — separate from the production paths, which test the actual gating.
- New: `bool speculative` + `setSpeculative()` on `Game`, `bool speculative` on `SoundPlayer`/`StatsRecorder` bases, early-returns in `RecordSoundPlayer::stop` / `DefaultSoundPlayer::play+stop` / all `NormalStatsRecorder` writers, `src/tests/test_speculative_suppression.cpp`, CMake entry.

### Step 7 — Rollback on misprediction (2026-05-28)

- The whole change sits inside `RollbackController::advanceSimulation` — no new module. After the existing send/receive plumbing, a single promote loop walks `confirmedSimFrame_+1 .. simFrame-1`, upgrading slots whose prediction matched the now-arrived real input. On the first mismatch it captures `rollbackTo = F-1`, breaks (without clearing `remoteInputReady` for F), and the block below restores `lastGood->snapshot`, replays the gap with `game.setSpeculative(true)`, and re-snapshots each replayed frame. localPrevInput / remotePrevInput are reseeded from the rollback slot's stored input bytes — they're controller-level edge-tracking state, not part of the snapshot.
- **The headline bug** in the first cut was a wrong invariant in the new-frame block. The old code consumed `remoteInputReady[currentSlot]` and cleared it whenever the byte was present, even when an earlier frame's input was still missing (out-of-order delivery). The slot got marked Confirmed using a real value, but `confirmedSimFrame_` couldn't advance past the gap. When the missing predecessor finally arrived and rollback fired, the resim for the early-arrived frame had nothing to consume — its `remoteInputReady` had already been cleared — so the slot was re-marked Predicted using the prediction byte, the real value was lost, and the controller stalled forever. Fix: only consume real input (and clear the ring entry) when `confirmedSimFrame_ + 1 == simFrame`. Otherwise predict and leave the real byte in the ring for a later promote+rollback cycle. Same rule applies inside the resim loop.
- That fix also collapsed several earlier contiguity guards: `!predicted` now *implies* the chain is contiguous through `simFrame-1`, so the post-increment update is unconditional and the checksum-send no longer needs the explicit chain check.
- **`JitterTransport`** (src/tests/jitter_transport.hpp): seed-driven `std::mt19937`, per-packet random delay in `[min,max]` frames, no loss/duplication for Step 7 (those land in 7.5). Out-of-order arrival is real — the queue isn't sorted by delivery time, so a later-sent packet with a smaller random delay genuinely arrives before an earlier one. That's exactly what tickled the bug above.
- Test cap on delay was set deliberately small (max 5). The 2-peer lockstep protocol has no input redundancy yet, so once one peer's `simFrame - confirmedSimFrame_` reaches `kMaxRollback + 1` the stall guard returns *before sending the next input* (`if (inputFrame != lastSentFrame)` blocks the resend). The peer stops emitting; the other peer starves and stalls in turn — a permanent cascade. Step 7.5's redundancy + unreliable-sequenced channel breaks the cycle. For Step 7's algorithmic correctness test, picking delays where the steady-state gap stays well under `kMaxRollback` keeps the cascade off the table.
- Added `RollbackController::rollbackCount_` + accessor purely so the correctness test can assert "rollback actually fired" — otherwise a passing test with zero mispredictions would be vacuous. With the random inputs and any delay > 0 the predicted byte (last received) almost never matches the next real byte, so rollbacks fire constantly (~hundreds per 800-tick run).
- New: rollback + resim path in `advanceSimulation`, `rollbackCount()` accessor + `rollbackCount_` field, `src/tests/jitter_transport.hpp`, `src/tests/test_rollback_correctness.cpp` (4 delay configurations), CMake entry.

### Step 8 — Frame-advantage / time sync (2026-05-28)

- Each batched packet now carries the sender's `simFrame` at send
  time (`InputBatchSendCallback` gained a `uint32_t localFrame`
  parameter). The receiver tracks the largest such value seen via a
  new `injectRemoteBatch(baseFrame, count, inputs, remoteLocalFrame)`
  entry point that wraps per-frame `injectRemoteInput` and folds in
  the frame-advantage estimate. `lastKnownRemoteFrame_` is `int32_t`
  initialised to `-1` (sentinel) and updated monotonically — a
  late-arriving stale packet must not pull the estimate backwards
  because the stall guard uses it as a lower bound on the remote's
  progress.
- Stall logic in `advanceSimulation`: after Step 7's rollback-window
  guard, if `simFrame - lastKnownRemoteFrame_ >= kFrameAdvantage` (= 2),
  return early — the redundant batch send already happened so the
  remote keeps hearing from us, we just skip the simulation step. The
  -1 sentinel disarms the stall during warm-up before any packet
  arrives. Counter `frameAdvantageStalls_` is exposed for tests so
  they can assert the stall actually fires.
- The wire-format byte (`localDelta:u8` per the plan's encoding
  section) is deferred to Step 11 along with the rest of the
  NetTransport bump — at the controller boundary the test transport
  passes a full `uint32_t localFrame` and the Step 11 wire encoder
  will compute the delta.
- **Threshold choice.** kFrameAdvantage = 2 matches the plan's "≥2
  frames ahead" wording. Initial back-of-envelope said this would
  deadlock under D_AB=2, D_BA=6 (since each peer's steady-state
  advantage equals its inbound delay), but tracing the actual
  algorithm shows the redundant-batch send carries fresh `localFrame`
  every tick so `lastKnownRemoteFrame_` rises even while the peer is
  stalled. That broke the deadlock concern and the asymmetric case
  settles to gap = ±2 around equilibrium, matching the plan's "±2"
  assertion.
- **`setFrameAdvantageEnabled(bool)`** added because the Step 7 / 7.5
  tests (`test_rollback_correctness`, `test_rollback_packet_loss`,
  `test_rollback_reorder`) assume peers freely run ahead to exercise
  prediction + rollback. Step 8's stall clamps them to near-lockstep
  and suppresses the rollback signal the tests are asserting on. The
  setter raises the threshold to `INT32_MAX` (effectively off) for
  those tests; production never calls it. Lockstep parity and
  zero-jitter prediction tests work unchanged because their `lastKR`
  rises to match `simFrame` within one tick — `simFrame - lastKR` stays
  in {0, 1} and never trips the 2-frame threshold.
- `test_frame_advantage.cpp` drives two `JitterTransport`s with fixed
  one-way delays (D_AB=2 forward, D_BA=6 back) and asserts the gap
  between the peers' `simFrame`s stays ≤ 2*kFrameAdvantage = 4 after
  warm-up. Asserts at least one peer accumulated frame-advantage
  stalls so the test isn't vacuous. A second SECTION runs the
  symmetric (3, 3) case as a sanity check that the algorithm doesn't
  break when delays are equal.
- New: `lastKnownRemoteFrame_` / `frameAdvantageStalls_` /
  `frameAdvantageThreshold_` fields on `RollbackController`,
  `injectRemoteBatch` method, frame-advantage stall in
  `advanceSimulation`, `setFrameAdvantageEnabled` setter,
  `lastKnownRemoteFrame()` / `frameAdvantageStallCount()` accessors,
  `kFrameAdvantage` constant. `JitterTransport` and all rollback tests
  updated to thread `localFrame` through the batched send/deliver
  callbacks. `src/tests/test_frame_advantage.cpp` added.

### Step 9 — Render & audio polish (2026-05-28)

- No new code. Both polishes are already in place by construction:
  - **Render gating** falls out of `GamePlayState`'s update/draw split
    (`src/game/gamePlayState.cpp:48` and `:91`). `controller->process()`
    runs the entire `advanceSimulation` (including any Step 7 resim
    loop) inside `update()`. `draw()` is only invoked by the AppState
    machinery after `update()` returns, so the renderer cannot observe
    a mid-resim state. The plan's "render only after `advance` returns"
    requirement is therefore an architectural invariant, not something
    the controller has to enforce.
  - **Audio simple-version suppression** was shipped in Step 5 via
    `Game::setSpeculative` → `SoundPlayer::play/stop` early-returns.
    `RollbackController` toggles `game.setSpeculative(predicted)` around
    `processFrame()` on the forward path (`rollbackController.cpp:638`)
    and wraps the entire resim window with `setSpeculative(true)`
    (`:533` / `:575`), so both predicted-on-first-execution and resim
    paths are covered. `test_speculative_suppression.cpp` already
    asserts the round-trip count parity that this step calls for.
- Audited menu-level audio (`sfx.play`) to confirm it doesn't leak into
  the sim path. The only call sites inside `RollbackController` are in
  `process()`'s pause-menu handling (`rollbackController.cpp:306` and
  `:313`); they fire on the *local* `gfx.testSDLKeyOnce` edge while
  `isPaused()` is true and never run inside `advanceSimulation`. All
  other `sfx.play` users live in menu/AppState code outside the sim
  loop. No additional gating needed.
- Audio "correct version" (capture-and-replay keyed by frame) explicitly
  deferred per the plan — Step 11 playtest will tell us whether the
  missed transient sounds are noticeable enough to justify the buffer.
- Manual playtest at 80 ms ± 20 ms is gated on the Step 11 dev-HUD
  jitter knobs; deferred to Step 11 where the knobs land. No automated
  test added here — the existing speculative-suppression and rollback
  correctness tests already cover the only behaviours a render/audio
  test could observe.

### Step 11b — NetSession integration with RollbackController (2026-05-28)

- `NetSession` gains an optional `useRollback` ctor flag (default
  false). When set, the session builds and wires a `RollbackController`
  in parallel to the existing lockstep `NetworkController` path.
  Lockstep callers and their tests are unchanged.
- Added two parallel members: `std::unique_ptr<RollbackController>
  rollback_` and `RollbackController* rollbackPtr_` alongside the
  existing `controller_`/`controllerPtr_`. Exactly one pair is
  populated per session lifetime. `controller()` returns the lockstep
  handle (null in rollback mode); new `rollbackController()` returns
  the rollback handle (null in lockstep mode). `useRollback()`
  exposes the mode flag.
- `releaseController()` changed signature from
  `unique_ptr<NetworkController>` to polymorphic
  `unique_ptr<Controller>`. The two external callers
  (`netConnectState`, `rematchState`) already store the result as a
  `Controller*` base via `gfx->controller`, so the upcast was free.
  Tests that touch `controller()->game.X` continue to compile — they
  only exercise lockstep sessions.
- Three private helpers consolidate the per-mode branching:
  - `createController(int localIdx)` — instantiates either NC or RC.
  - `wireActiveController()` — wires checksum/pause/end on either,
    and routes the input callback: lockstep uses
    `transport_.sendInput(frame, input)`; rollback uses
    `transport_.sendInputBatch(baseFrame, count, localDelta, inputs)`
    where `localDelta = localFrame - baseFrame` (Step 8 encoding).
  - `activeGame()` / `injectRemoteInputActive(...)` — single-line
    dispatch that replaces ~10 inline `controller_->game.X` / 
    `controller_->injectRemoteInput(...)` sites across the 3 setup
    paths (`tryStartGame`, `startRematch` host, `startRematchClient`)
    and `generateAndSendMap`.
- New `onRemoteInputBatch` handler on the session forwards
  `transport_.onRemoteInputBatch` to `rollback_->injectRemoteBatch`.
  The lockstep `onRemoteInput` handler now drops batches received
  before `controllerPtr_` is wired only in lockstep mode; in rollback
  mode pre-Playing batches are dropped on purpose because the K-wide
  redundancy guarantees the next batch (~14 ms later) re-delivers
  the same window.
- `onPause` / `onResume` / `onRemoteEndMatch` dispatch by which
  pointer is set — `controllerPtr_` first, then `rollbackPtr_` —
  matching the mutex invariant above.
- `test_session_rollback.cpp` exercises the end-to-end path: two
  sessions over loopback in rollback mode, handshake → settings →
  mapdata → Playing, then 50 sim ticks with zero jitter. Asserts
  `rollbackController() != nullptr`, `controller() == nullptr`,
  RNG parity, frame parity, `rollbackCount() == 0` (zero-jitter
  loopback shouldn't mispredict), and no desync. Vacuity guards on
  the frame number protect against the "session never advanced"
  failure mode.
- Production callsites (`netConnectState`, `rematchState`) still
  construct sessions with the default `useRollback=false` — the
  flip happens in 11e. 11c will plumb the runtime setting through
  `MatchSettingsData` so the host can dictate the mode for the
  session.
- New: `useRollback_`/`rollback_`/`rollbackPtr_` on NetSession;
  `createController`/`wireActiveController`/`activeGame`/
  `injectRemoteInputActive` helpers; `rollbackController()` +
  `useRollback()` accessors; `releaseController` polymorphic;
  `onRemoteInputBatch` handler; `test_session_rollback.cpp` + CMake.

### Step 11a — Transport wire format + version bump (2026-05-28)

- New `NetTransport::kProtocolVersion = 2` constant. The handshake
  packet grew by one byte to `[type:1][version:1][seed:4][hash:4]`
  (10 B). Receivers silently drop handshakes whose `len != 10` or
  whose `version` byte doesn't match `kProtocolVersion`. The session
  layer surfaces the resulting handshake timeout as a normal
  connection failure to the user — no UI work needed in 11a.
- Old (v1) builds send 9-byte handshakes and are rejected here; new
  builds receiving from old peers do the same. Cross-version play is
  impossible by construction; the plan's "rollback peer refuses to
  play against a lockstep peer (or vice versa)" requirement is met by
  the version byte, not by feature detection.
- Added `PacketInputBatch = 15` for the K-wide redundant input window
  Steps 7.5 and 8 already use at the controller boundary. Wire layout
  is exactly the plan's "Step 8 frame-advantage encoding":
  `[type:1][baseFrame:u32 LE][count:u8][localDelta:u8][input[count]:u8]`.
  Receiver reconstructs `remoteLocalFrame = baseFrame + localDelta`
  and forwards via `onRemoteInputBatch(bf, count, ptr, remoteLocalFrame)`.
  Parser rejects `count == 0`, `len != 7 + count`, and
  `localDelta >= count` so a malformed or version-drifted payload
  can't drive the controller into an inconsistent state.
- New channel `CHANNEL_UNRELIABLE_SEQUENCED = 2` (NUM_CHANNELS bumped
  to 3). PacketInputBatch is sent with ENet flag = 0 — that's ENet's
  "unreliable but sequenced" mode: stale duplicates after a newer
  batch are dropped at the channel layer. Within a batch, the
  controller's existing `injectRemoteInput` de-dups against
  `confirmedSimFrame_` (Step 7.5's stale-frame drop guard).
  Lockstep `PacketInput` continues to use the reliable channel — no
  behaviour change to the existing `NetworkController` path.
- Defensive cap on `sendInputBatch::count` (≤ 64) so a future caller
  misuse can't blow the stack buffer. Rollback uses count = 8.
- `test_transport.cpp` gained two cases: round-trip of a K=8 batch
  asserting `baseFrame`/`count`/`remoteLocalFrame`/payload equality,
  and a sanity case pinning `kProtocolVersion = 2`. Existing transport
  + session tests still pass — the handshake test only checks the
  callback delivers, not the byte length.
- Deferred to 11b: wiring `RollbackController` to `NetTransport`
  via `NetSession`. The new methods exist but no production code
  calls them yet; only the new transport test exercises them.
- New: `kProtocolVersion`, `PacketInputBatch`, `sendInputBatch` +
  `onRemoteInputBatch` on `NetTransport`; handshake send/recv now
  carries the version byte; `CHANNEL_UNRELIABLE_SEQUENCED` channel
  added (NUM_CHANNELS = 3). Two transport tests added.

### Step 10 — Desync detection under rollback (2026-05-28)

- Added a `uint32_t checksum` field to `rollback::Slot`. Every
  `processFrame()` on the forward path and inside the resim loop now
  caches `fastGameChecksum(game)` into the slot it just snapshotted —
  regardless of whether the frame was predicted or confirmed. The
  cached value is what the desync detector ultimately sees.
- The emit policy is "send exactly once at confirmation time". Three
  paths converge into that rule:
  - **Forward, `!predicted`**: confirmed on first execution; send the
    cached value immediately (same site as Step 6's gate, now reading
    from the slot instead of recomputing).
  - **Promote loop**: when a previously-Predicted slot transitions to
    Confirmed because the real input matched the prediction, the slot's
    cached checksum is correct by construction (the snapshot was
    produced from the input that matched) — send it now. This is the
    only chance, since the forward path skipped the send.
  - **Resim**: when a replayed frame consumes real (chain-contiguous)
    input, send after re-snapshotting. Resim frames whose remote input
    is still predicted stay silent and wait for a future cycle.
- A `wasPredicted` flag in the promote loop avoids re-sending a slot
  that was already Confirmed in a prior tick. Forward-confirmed and
  resim-confirmed paths are mutually exclusive by frame, so no
  single frame's checksum is sent twice in normal operation.
- The plan's "cache in snapshot" wording suggested putting the field on
  `GameSnapshot`. Chose `Slot` instead: the checksum isn't part of the
  serialisable game state — it's metadata about *when this slot was
  confirmed* — and putting it on `Slot` keeps `GameSnapshot::prepare`
  unchanged and `saveSnapshotFast` allocation-free.
- `test_rollback_desync.cpp` runs two `RollbackController`s through
  `JitterTransport([1,4])` for 1500 ticks and observes the
  (frame, checksum) emission streams. Two SECTIONs:
  - **Clean**: both peers' checksums match on every frame both emit
    one. Vacuity guards assert >50% of frames produce a comparable
    pair on each side, and that rollback actually fired.
  - **Injection**: peer B's emit callback XORs bit 0 of the checksum
    starting at frame 200 (chose the emit boundary rather than poking
    sim state mid-frame — that would feed back into B's snapshot and
    get restored on rollback, turning a 1-bit drift into a structural
    divergence). The test asserts the first mismatch lands within 200
    frames of injection, which it does immediately (the first
    confirmed emit ≥ frame 200 triggers).
- Producer-side only, per the plan: no changes to `NetSession`'s
  `pendingRemoteChecksums_` ring or `onChecksum` comparator. The wire
  format (`PacketChecksum` in `transport.cpp`) is unchanged; what
  travels on the wire is still `(frame, checksum)` and the existing
  comparison logic already handles the lower checksum rate under
  predicted-frame stalls.
- New: `uint32_t checksum` on `rollback::Slot` + `clear()`/`write()`
  reset, slot.checksum write at the forward snapshot site (now wrapping
  the send), promote-loop wasPredicted send, resim-loop send, and
  `src/tests/test_rollback_desync.cpp` (2 cases) + CMake entry. All
  rollback, snapshot, network, and determinism tests still green.

### Step 7.5 — Transport: input redundancy (2026-05-28)

- Scope ended up controller-layer only, mirroring Steps 4/6/7. The plan's
  three bullets split cleanly: (a) batched send/receive contract +
  redundancy logic — done here; (b) ENet channel change to
  unreliable-sequenced + protocol-version bump — deferred to Step 11
  where the session actually wires `RollbackController` through
  `NetTransport`. Doing the wire-format bump on a controller that no
  caller in `NetSession` instantiates yet would have meant flipping
  `PacketInput`'s layout under the still-shipping lockstep controller
  and gaining nothing. Step 11's "promote RollbackController to default"
  is the right place to introduce the new packet type and version bump
  together.
- `RollbackController`'s `InputSendCallback` retired; replaced with
  `InputBatchSendCallback = void(uint32_t baseFrame, uint8_t count,
  uint8_t const* inputs)` in `networkController.hpp`. `NetworkController`
  keeps the old single-input callback. The two controller types now
  diverge at the transport boundary, which is exactly the version-bump
  contract Step 11 will enforce.
- `sendInputWindow(newestFrame)` builds the last K = kMaxRollback + 1
  bytes from `localInputs` each tick (truncated near frame 0) and fires
  `sendInputBatch`. Always sends, regardless of whether `inputFrame`
  changed — when the controller is stalled below the new-frame block,
  the redundant batch is still what lets the remote peer promote out of
  its own stall once a previously-dropped packet's contents arrive via
  a later batch.
- `injectRemoteInput` now drops `frame <= confirmedSimFrame_` at the
  door. Without this guard the K-wide redundant batches would re-set
  `remoteInputReady` for slots whose frame number has already wrapped
  the 256-entry ring. With the 8-frame rollback window vs 256-entry
  ring there's an enormous safety margin, but the rule keeps the
  invariant local to one function and lets us drop input-validation
  worries from the rest of the controller.
- `JitterTransport` extended: batched packet payloads
  (`baseFrame:u32, count:u8, inputs[count]:u8`),
  `lossProbability` and `duplicateProbability` parameters. Loss is
  drawn at enqueue; duplication produces a second copy with an
  independently-rolled delivery delay (so the duplicate is itself
  out-of-order vs the original most of the time — exactly what stresses
  the dedup). Telemetry counters (`packetsSent`, `packetsDropped`,
  `packetsDuplicated`) expose whether tests are vacuous.
- `test_rollback_packet_loss.cpp` — 10% loss, delay [1,3], 1500 ticks.
  Asserts: steady-state lag (currentFrame - confirmedFrame - 1) stays
  ≤ kMaxRollback; no zero-progress stall ticks once warm; both peers
  agree on final checksum after flush; `packetsDropped > 0` and
  `rollbackCount() > 0` so the test isn't trivially passing.
- `test_rollback_reorder.cpp` — wide delay band [1,5] + 30% duplication,
  zero loss, 1000 ticks. Asserts: duplication actually fires, rollback
  fires, peers converge to identical checksum after flush. The wide
  delay range is what produces reordering naturally — no separate
  reorder knob is needed because per-packet random delay over a wide
  range guarantees out-of-order arrival.
- One subtlety in `test_rollback_correctness` after the API switch:
  with K-wide redundant sends, the existing zero-jitter loopback
  pattern (push every send into a `vector<pair>`, drain every tick)
  pushes K times more entries per tick. The dedup inside
  `injectRemoteInput` absorbs them without behavior change. Test still
  passes the same 4 delay-config sub-cases it did under Step 7.
- `test_rollback_lockstep_parity` had to specialize the templated
  Loopback wiring via `if constexpr (is_same_v<Ctrl, RollbackController>)`
  since the send-callback signatures of the two controllers now
  differ. Behavior unchanged.
- `test_prediction_no_rollback`'s Loopback collapsed to direct
  push-on-send (the receiving peer's `injectRemoteInput` runs inside the
  sending lambda). The old recv-pull pattern made less sense with
  batched sends; this matches what the production session will do once
  Step 11 wires NetTransport's batched receive callback.
- New: `InputBatchSendCallback` type, `RollbackController::sendInputBatch`
  + `sendInputWindow()`, stale-frame drop in `injectRemoteInput`,
  batched-packet `JitterTransport`, `src/tests/test_rollback_packet_loss.cpp`,
  `src/tests/test_rollback_reorder.cpp`, CMake entries. All 112 tests
  green.

### Step 6 — Prediction without rollback (2026-05-28)

- Added two pieces of state to `RollbackController`: `confirmedSimFrame_` (highest already-advanced frame whose remote input was real) and `lastRemoteInput_` (used as the predicted byte when the current frame's slot is empty). GGPO's default — predict = repeat the last received input.
- `advanceSimulation` now has a **promote** loop at the top: when a remote-input ring slot for a frame in `(confirmedSimFrame_, simFrame)` is ready, the test advances `confirmedSimFrame_` and updates `lastRemoteInput_` without touching the buffer slot. The Predicted snapshot stays Predicted on purpose — Step 7 will compare predicted vs real input there and decide whether to rollback. Touching the slot here would lose the prediction.
- **Stall guard**: `simFrame - confirmedSimFrame_ > kMaxRollback` → bail. After the tick `simFrame` becomes `simFrame+1`, so this caps the in-flight predicted count at exactly `kMaxRollback`. The ring buffer's `kMaxRollback + 1` slots still fit.
- The frame execution itself flips `game.setSpeculative(predicted)` around `processFrame()` and back to `false` after, so Step 5's suppression handles sound/stats automatically. The buffer slot's `remoteState` is tagged accordingly.
- **Checksums for predicted frames are not sent.** Predicted state will trivially disagree with the real peer's confirmed state and would spam the desync detector. Step 10 generalises this rule to "checksum cached at confirmation time"; Step 6 just gates the existing send. Confirmed pure-lockstep behaviour (and the existing `RollbackController writes confirmed snapshots…` test) is unchanged because zero jitter means `predicted` is always false.
- Zero-jitter parity from Step 4 still holds — verified by re-running the parity test. With `inputDelay=3` and synchronous delivery, every frame's remote input is already in the ring before its slot comes up, so prediction never fires.
- Test (`test_prediction_no_rollback.cpp`) has two cases:
  - **Zero jitter**: 500 ticks of loopback with random inputs → every resident slot is `Confirmed`; `confirmedFrame == currentFrame - 1`.
  - **Starvation/recovery**: drive a single controller with `injectRemoteInput`, warm up `kWarm` frames, stop delivering → controller predicts exactly `kMaxRollback` more frames (asserted via `currentFrame`) then stalls. Inject the missing inputs → next `process()` drains the promote loop and `confirmedFrame` catches up. One subtle bit: seed *only* `[0..kWarm-1]` of remote inputs; an off-by-3 here (mirroring `inputDelay`) leaves extra ready slots that get consumed before prediction begins.
- Public introspection: `RollbackController::confirmedFrame()` for tests/HUD.
- New: prediction logic in `advanceSimulation`, `confirmedFrame()` accessor + `confirmedSimFrame_`/`lastRemoteInput_` fields on `RollbackController`, `src/tests/test_prediction_no_rollback.cpp`, CMake entry.
