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
- `std::vector<Material> materials` — ~176 KB (assuming 1-byte Material; verify)

Plus the fixed-size object pools (nobjects, sobjects, bonuses, worms) — all known small.

Total snapshot ≈ **~400 KB**. Two `memcpy`s of 176 KB take ~50 µs on commodity hardware. With `MaxRollback = 7` snapshots resident, working set is ~2.8 MB — fits in L2. **Snapshot cost is a non-issue.**

## Plan

Each step is independently buildable and verifiable. Do not skip the verification step.

### Step 0 — Audit & instrument (no code change)

Confirm every non-deterministic side effect inside `processFrame()` and its callees. Produce a short audit document listing each external call from the sim path, tagged `(sim-effect | render-only | sound)`. Known categories to verify:

- `game.soundPlayer->play/stop/isPlaying` — 30+ sites, expected `sound`
- Particle spawning — expected `render-only`; **verify it does not feed back into sim**
- Any logging, printf, or file I/O from sim path

**Exit criteria:** confidence that the only sim-affecting state lives inside the `Game` object graph.

### Step 1 — Cereal-based snapshot (correctness baseline)

Add `Game::saveSnapshot(std::vector<uint8_t>&)` and `Game::loadSnapshot(std::vector<uint8_t> const&)` using the existing `cerealWrite/cerealRead`.

**Test:** `test_snapshot_roundtrip.cpp` — 200 frames → snapshot → 200 frames → restore → 200 frames; assert `fastGameChecksum` matches the no-restore control run at every frame.

**Why cereal first:** if step 2's fast path disagrees, cereal is the reference oracle.

**Build artifact:** test only, no behavior change.

### Step 2 — Fast snapshot path (`memcpy` of POD regions)

```cpp
struct GameSnapshot {
    Rand rand;
    std::array<Worm, MaxWorms> worms;
    ExactObjectList<NObject> nobjects;
    ExactObjectList<SObject> sobjects;
    ExactObjectList<Bonus>   bonuses;
    std::vector<uint8_t>  levelData;
    std::vector<Material> levelMaterials;
    // + scalar fields: cycles, screen shake state, etc.
};
```

All `ExactObjectList<T, N>` are fixed-size pools → straight `memcpy`. Level vectors: two `memcpy`s, ~350 KB total.

**Target:** ≤500 µs save + ≤500 µs restore. (At 7 rollback frames × 70 fps × 1 ms = 0.5% CPU budget — negligible.)

**Verify:** assert byte-identical state hash between cereal-restore and memcpy-restore over a fuzz run.

**Build artifact:** new path behind a `useFastSnapshot` flag; both coexist temporarily.

### Step 3 — Pool snapshots & input ring buffer

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

### Step 4 — `RollbackController` in lockstep mode

New controller alongside `NetworkController`. Initially behaves *identically* to lockstep: only advances when remote input is present. Saves a snapshot after every confirmed frame.

**Verify:** existing session/determinism tests pass when `RollbackController` is substituted.

**Build artifact:** menu still uses `NetworkController`; rollback controller selectable via a hidden debug flag.

### Step 5 — `Game::resimulating` flag + sound suppression

Add `bool Game::resimulating = false`. Wrap `soundPlayer->play/stop`:

```cpp
void SoundPlayer::play(...) {
    if (game.resimulating) return;
    // ...
}
```

**Test:** assert sound `play` count over an `N → snapshot → restore → N` round-trip equals a single `N`-frame run (no double sounds).

**Build artifact:** behavior-neutral in non-rollback play.

### Step 6 — Prediction (no rollback yet)

When remote input is absent for `simFrame`, predict it = last received remote input (GGPO's default). Advance up to `MaxRollback` frames ahead, then stall. Mark each speculative frame.

No rollback yet → the verification test from step 7 will fail under mispredictions. Expected.

**Build artifact:** gameplay works under zero jitter; degrades visibly under jitter.

### Step 7 — Rollback on misprediction

When real input arrives for frame F and differs from prediction:

1. `loadSnapshot(F-1)`
2. Re-apply confirmed inputs through to current local frame, snapshotting as we go
3. `resimulating = true` throughout the resim window

**Write the verification harness before the implementation** (this is effectively the spec):

- Two `Game` instances + `RollbackController`s connected via in-process transport with configurable jitter buffer (0–8 frame random delay each direction)
- Run 5000 frames with random inputs, two seeds
- Final state hashes must match a zero-jitter control run
- Test with `MaxRollback = 1, 4, 8`

**Build artifact:** rollback works end-to-end at the algorithm level.

### Step 8 — Frame-advantage / time sync

Each peer includes its current local frame number in input packets (1 extra byte). If local is ≥2 frames ahead of remote, stall 1 frame. Mirrors GGPO's time-sync.

**Verify:** under simulated 30/60/100 ms RTT, neither peer is >2 frames ahead of the other for sustained windows.

**Build artifact:** smoother latency distribution.

### Step 9 — Render & audio polish

- **Render:** only after `advance(currentFrame)` returns; never mid-resim
- **Audio (simple version):** suppress during resim, accept that some transient sounds in mispredicted frames will be lost. Ship this first.
- **Audio (correct version, optional):** capture `(frame, soundId, args)` during predicted frames, drop them on rollback, emit on confirmation. Defer unless missed sounds are noticeable in playtest.

**Verify:** manual playtest with `tc qdisc netem` adding 80 ms ± 20 ms jitter on localhost.

### Step 10 — Desync detection under rollback

Current desync detection (multiplayer.md → "Runtime desync detection") sends a checksum every frame using *current* state. Under prediction this is wrong — predicted frames will trivially disagree.

Change to: send `(confirmedFrame, checksum)` where `checksum` is cached from the snapshot taken at confirmation. Update `NetSession::onChecksum` to compare accordingly.

**Verify:** intentionally introduce nondeterminism in a test build; confirm detection fires within a few seconds.

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
| `Material` is larger than 1 byte → snapshot >400 KB | Low | Low | Verify in step 2; still well under budget even at 4× |
| Particle system feeds back into sim | Low | High | Step 0 explicitly checks this |

## Out of Scope (Defer)

- AI in network games — still excluded per Phase 1 decision
- Replay recording of network games — separate work; data already available
- More than 2 players — protocol could allow but not now
- Spectator mode — separate work

## Learnings

*(Append as each step lands. Format: `### Step N — [title] (YYYY-MM-DD)` then notes.)*
