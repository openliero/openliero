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

### Step 5 — `Game::speculative` flag + side-effect suppression

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

### Step 6 — Prediction (no rollback yet)

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

### Step 7 — Rollback on misprediction

When real input arrives for frame F and differs from prediction:

1. `loadSnapshot(F-1)`
2. Re-apply confirmed inputs through to current local frame, snapshotting as we go
3. `resimulating = true` throughout the resim window

**Write the verification harness before the implementation** (this is the spec). See `JitterTransport` and `test_rollback_correctness.cpp` in the test matrix above — that test must compile (failing) before Step 7 implementation begins, and pass before Step 7 is complete.

**Build artifact:** rollback works end-to-end at the algorithm level.

### Step 7.5 — Transport: input redundancy + protocol bump

Before frame-advantage (Step 8), change the input wire format per "Transport / Wire Format Changes" above:

- Packet carries `[baseFrame][count][input[count]]` with `count = MaxRollback + 1`.
- Move from reliable-ordered to unreliable-sequenced; drop stale packets at the receiver.
- Bump protocol version; refuse cross-mode play.

**Verify:** `test_rollback_packet_loss.cpp` and `test_rollback_reorder.cpp` from the test matrix above.

**Build artifact:** rollback survives lossy links without retransmit stalls.

### Step 8 — Frame-advantage / time sync

Each peer includes its current local frame number in input packets as `localDelta:u8` = `simFrame - baseFrame` (+1 byte, see "Step 8 frame-advantage encoding" above). If local is ≥2 frames ahead of remote, stall 1 frame. Mirrors GGPO's time-sync.

**Verify:** `test_frame_advantage.cpp` from the test matrix above (asymmetric A→B / B→A delay; assert frame advantage stays within ±2 in steady state).

**Build artifact:** smoother latency distribution.

### Step 9 — Render & audio polish

- **Render:** only after `advance(currentFrame)` returns; never mid-resim
- **Audio (simple version):** suppress during resim, accept that some transient sounds in mispredicted frames will be lost. Ship this first.
- **Audio (correct version, optional):** capture `(frame, soundId, args)` during predicted frames, drop them on rollback, emit on confirmation. Defer unless missed sounds are noticeable in playtest.

**Verify:** manual playtest using the dev-HUD jitter knobs (see "Manual / interactive testing" above) at 80 ms ± 20 ms; both peers on the same machine.

### Step 10 — Desync detection under rollback

Current desync detection (multiplayer.md → "Runtime desync detection") sends a checksum every frame using *current* state. Under prediction this is wrong — predicted frames will trivially disagree.

Change to: send `(confirmedFrame, checksum)` where `checksum` is cached from the snapshot taken at confirmation. Update `NetSession::onChecksum` to compare accordingly.

**Verify:** `test_rollback_desync.cpp` from the test matrix — clean-run negative case + 1-bit injection positive case must fire detection within 200 frames.

### Step 11 — Settings, UX, defaults

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
