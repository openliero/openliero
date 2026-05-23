# Plan: Remove the vendored `gvl` library

## Status: NOT STARTED

Branch: _(to be created — suggested name: `remove-gvl`)_

Track progress by checking off boxes in each phase. Each phase is independently
mergeable and leaves the tree green (builds + all tests pass on Linux, macOS,
Windows, and Emscripten).

---

## Background

`src/gvl/` is a vendored copy of [gliptic/gvl](https://github.com/gliptic),
~6,300 lines of header-heavy C++ written in 2010–2012 by the original Liero
author. It pre-dates C++11 in spirit and provides:

- intrusive refcounted smart pointers (`resman/`)
- a `source` / `sink` / `bucket_pipe` I/O abstraction (`io2/`)
- a binary archive layer (`serialization/`)
- a TOML adapter on top of `tomlplusplus` (`serialization/toml_adapter.hpp`)
- a deflate filter on top of `miniz` (`io2/deflate_filter.hpp`)
- a multiply-with-carry RNG (`math/cmwc.hpp`)
- a bespoke 256-bit hash, "gash" (`crypt/gash.hpp`)
- 2D vec / rect math
- bit utilities, assertion macros, MSVC/GCC ifdef plumbing
- intrusive doubly-linked list and pairing heap

The project is **C++23** (`tools/cmake/ConfigurePresetTemplates.json`) and
already depends on `tomlplusplus` and `miniz` via vcpkg. Most of gvl is now
either redundant with the standard library (`<bit>`, `<memory>`, `<random>`,
`std::priority_queue`) or a thin wrapper around a dependency we already pull
in directly.

## Goal

**Delete `src/gvl/` entirely.** Replace every line of it with C++23 stdlib,
existing vcpkg deps, or — only where unavoidable — a few dozen lines of
ours. Maximise stdlib usage, minimise custom code.

## Allowed breakages (user-confirmed)

- **Old replays will not load.** The replay format gets a new magic and
  version; loading anything older returns a clear "unsupported replay
  version — recorded with pre-gvl-removal openliero" error.
- **Settings file format may change.** TOML still, but key names and
  shape are free to drift where it tidies up the migration.
- **Desync hash digests change.** The hash function changes (gash → xxhash);
  values stored in old replays/saves are not comparable.
- **Source-incompatible refactors** (smart-pointer types, namespaces) —
  free hand.

## Non-goals

- Reworking the AI, networking, or rendering. We touch them only where
  they `#include <gvl/...>`.
- Pretty migration path for old replays. They're gone.

---

## Cross-platform considerations

Every replacement is one of:

- C++23 stdlib (`<bit>`, `<memory>`, `<random>`, `<queue>`, `<cstdint>`,
  `<filesystem>`, `<cassert>`, `<format>`, `std::byteswap`),
- already-pulled-in vcpkg deps (`tomlplusplus`, `miniz`),
- one tiny new vcpkg dep (`xxhash`, header-only, in vcpkg, cross-platform).

Required compilers: MSVC 19.34+ (VS 2022 17.4), GCC 12+, clang 15+,
emcc 3.1.45+ — all already used by the existing CI presets.

No platform-conditional code is added; the only ifdef blocks removed
(`GVL_MSVCPP` / `GVL_X86_64` intrinsics in `support/bits.hpp`,
`GVL_WINDOWS` in `system/windows.hpp`) collapse to portable `<bit>` usage.

---

## New third-party dependency

**`xxhash`** (vcpkg name: `xxhash`, BSD-2-Clause, ~1.5 KLOC, header-only
mode available). Used for the desync / settings-change hash. Could also
inline a 50-line implementation if you'd rather not add a dep — the plan
picks vcpkg for less custom code, in line with the stated goal.

> If you'd prefer no new dep, swap "use xxhash" for "inline `xxh3_64bits`
> from the reference implementation, ~100 lines, public domain". Net code
> count is the same either way.

No other new deps. `glm` was considered for vec/rect; rejected as overkill
for what is literally two int fields.

---

## Inventory and replacement table

| gvl path | LOC | Replacement | Lines we keep |
|---|---:|---|---:|
| `math/cmwc.hpp` `math/random.hpp` | 230 | `std::mt19937` from `<random>` directly at each call site | 0 |
| `crypt/gash.hpp` | 209 | `xxhash` via vcpkg (`XXH3_64bits`); replay desync digest becomes a `uint64_t` | 0 |
| `math/vec.hpp` `math/rect.hpp` | 548 | tiny `src/game/gfx/rect.hpp`: `struct IVec { int x, y; }`, `struct IRect { int x1, y1, x2, y2; }`, free-function intersect/contains/union | ~60 |
| `io2/*` `containers/bucket.hpp` | 1190 | direct `FILE*` + `std::vector<uint8_t>`; miniz called directly. Single `src/game/io/binary_io.hpp` with `read<T>(FILE*)`, `write<T>(FILE*, T)`, `read_span`, `write_span`, plus `inflate_buffer` / `deflate_buffer` calling miniz | ~150 |
| `resman/shared.hpp` `resman/shared_ptr.hpp` | 479 | `std::shared_ptr` + `std::enable_shared_from_this` | 0 |
| `containers/pairing_heap.hpp` | 503 | `std::priority_queue` with lazy invalidation in A*/Dijkstra | 0 |
| `containers/list.hpp` | 207 | Internal to resman + stream; deleted with them | 0 |
| `serialization/toml_adapter.hpp` | 375 | **Delete entirely.** Rewrite the ~6 call sites in `settings.cpp`, `worm.cpp`, `common.cpp`, `common_model.hpp`, `tc_tool/common_writer.cpp` to use `tomlplusplus` directly. | 0 |
| `serialization/archive.hpp` `coding.hpp` `context.hpp` `except.hpp` | 863 | **Delete entirely.** Rewrite replay reader/writer (`replay.cpp`) to read/write fields directly via `binary_io.hpp`. The archive abstraction never earned its keep — replay is the only consumer. | 0 |
| `support/bits.hpp` | 274 | `<bit>` (`std::countl_zero`, `std::bit_width`, `std::byteswap`, `std::popcount`) | 0 |
| `support/debug.hpp` | 36 | `<cassert>`. Replace `passert(c, m)` with `assert((c) && (m))`, `sassert(c)` with `assert(c)`, by sed. | 0 |
| `support/platform.hpp` `system/windows.hpp` | 141 | `= delete` for noncopyable, ifdefs deleted | 0 |
| `support/functional.hpp` `support/type_info.hpp` `meta/as_unsigned.hpp` | 162 | `<functional>`, `<typeinfo>`, `std::make_unsigned_t` | 0 |

**Net code change: ~6,300 lines deleted, ~210 lines added.**

---

## Phasing

Each phase is a single PR. Each ends with the full test suite green
(`OPENLIERO_BUILD_TESTS=ON`) and the game running through one full match
visually. Phases land in dependency order — leafy stuff first so we never
have two parallel pointer schemes mid-flight.

### Phase 0 — Test scaffolding

We are *not* preserving behaviour, so there is no "golden bytes" parity
test. Instead we lean on behavioural tests that prove the game still
plays and stays in sync after each phase.

- [ ] Create branch `remove-gvl`.
- [ ] Capture a **post-migration golden replay** at the end of Phase 7
      (not now — its content depends on the new RNG).
- [ ] Verify the existing `test_determinism` is robust: two `Game`
      instances seeded with the same value produce the same state hash
      after N frames. This will be our cross-phase contract.
- [ ] Add `src/tests/test_io.cpp` skeleton (used by Phase 6).
- [ ] Add `src/tests/test_pathfinding.cpp` skeleton (used by Phase 4).
- [ ] Add `src/tests/test_archive.cpp` skeleton — *no longer needed*
      since the archive layer is being deleted, not preserved. (Skip.)
- [ ] Confirm all six presets (linux-x64, linux-arm64, macos-x64,
      macos-arm64, windows-x64, emscripten) still build via CI.

### Phase 1 — Trivia: `support/*`, `meta/*`, `system/*`

Lowest-risk leaves. No behavioural change.

- [ ] Sed `gvl::log2`, `gvl::top_bit`, `gvl::trailing_zeroes`,
      `gvl::bswap`, `gvl::popcount` → `std::bit_width - 1`,
      `std::countl_zero`, `std::countr_zero`, `std::byteswap`,
      `std::popcount`. Audit each by hand (signed vs unsigned, zero-input
      semantics differ in `bit_width` vs `log2`).
- [ ] Replace `gvl::noncopyable` base with explicit `= delete` at each
      use site.
- [ ] Sed `passert(c, m)` → `assert((c) && (m))`, `sassert(c)` →
      `assert(c)`. Remove the `gvl::assert_failure` exception type — it's
      caught nowhere.
- [ ] Sed `gvl::as_unsigned_t` → `std::make_unsigned_t`.
- [ ] Delete the corresponding headers and `.cpp` files; remove from
      `src/gvl/CMakeLists.txt`.

**Tests:** existing suite must remain green. No new tests needed —
`<bit>`/`<cassert>` are stdlib.

### Phase 2 — Math: `math/*`

- [ ] Add `src/game/gfx/rect.hpp` with `IVec`, `IRect`, free functions:
      `bool contains(IRect, IVec)`, `bool intersects(IRect, IRect)`,
      `IRect intersect(IRect, IRect)`, `IRect unite(IRect, IRect)`,
      `IVec size(IRect)`. Keep field names `x1`, `y1`, `x2`, `y2` to
      minimise call-site churn.
- [ ] Update `src/game/gfx.hpp` aliases so the rest of the game sees the
      new type unchanged where possible.
- [ ] Replace `Rand : gvl::mwc` with `using Rand = std::mt19937;` in
      `src/game/rand.hpp`. Audit every call site:
  - `rand()` returning `uint32_t` → `static_cast<uint32_t>(rand())`
  - `rand(max)` → `std::uniform_int_distribution<uint32_t>{0, max-1}(rand)`
        wrapped in a small `random_int(rand, max)` helper for ergonomics.
  - `rand.seed(s)` works as-is on `std::mt19937`.
  - `get_double()` → `std::uniform_real_distribution<double>{0.0, 1.0}(rand)`.
- [ ] Delete `src/gvl/include/gvl/math/`.

**Tests added:**
- [ ] `test_rect` — intersection / contains / union sweep, 30 lines.

**Verification:**
- [ ] `test_determinism` still green (two games with the same `mt19937`
      seed must still agree — this proves the migration didn't drift
      between the two `Game` instances).

### Phase 3 — `crypt/gash.hpp`

- [ ] Add `xxhash` to `vcpkg.json`.
- [ ] Add `find_package(xxHash CONFIG REQUIRED)` and link `xxHash::xxhash`
      to the `game` target in `CMakeLists.txt`.
- [ ] Replace `gvl::gash::value_type` (a 4×u64 struct) with `uint64_t`
      everywhere. Members like `Settings::hash`, `WormSettings::hash`,
      `Replay::lastSettingsHash` become `uint64_t`.
- [ ] Replace `gvl::hash_accumulator<gvl::gash> ha; ar(ha); ha.finalize();`
      with `XXH3_state_t* s = XXH3_createState(); XXH3_64bits_reset(s); ...
      uint64_t h = XXH3_64bits_digest(s);`. Wrap into a small
      `src/game/hash.hpp` helper that takes a `std::span<const uint8_t>`
      or a callback-fill pattern.
- [ ] Delete `src/gvl/include/gvl/crypt/`.

**Tests:** existing `test_settings` still green.

### Phase 4 — `containers/pairing_heap.hpp`

- [ ] Rewrite `src/game/ai/astar.hpp` and `src/game/ai/dijkstra.hpp` open-list
      handling on top of `std::priority_queue`. Use lazy invalidation: when
      a node is re-inserted with a lower cost, the stale entry stays in the
      heap and is skipped at pop time (standard A* technique).
- [ ] Delete `src/gvl/include/gvl/containers/pairing_heap.hpp`.

**Tests added:**
- [ ] `test_pathfinding` — fixed small map, start/goal, asserted path
      length; demonstrates A* still terminates and returns an optimal path.

### Phase 5 — Serialization: delete the archive layer

This is a behavioural break, not a refactor.

- [ ] In `src/game/replay.cpp`, replace every `archive(ar, x)` call with
      direct `read_T` / `write_T` calls from the (forthcoming, Phase 6)
      `binary_io.hpp`. The replay byte layout is being redesigned — choose
      a clean LE-everywhere encoding, version byte = 1 of the new format,
      magic bytes = `OLR1`.
- [ ] In `src/game/settings.cpp`, `worm.cpp`, `common.cpp`,
      `common_model.hpp`, `tc_tool/common_writer.cpp`: replace the
      `gvl::toml::reader<...>` / `gvl::toml::writer<...>` calls with
      direct `tomlplusplus` table construction and parsing. Use
      `toml::table`, `toml::array`, the operator-`[]` overloads,
      `toml::parse`, `t << toml::default_formatter{}`.
- [ ] On load, refuse non-`OLR1` replays with a clear error. On load of
      old TOML settings, if a key has been renamed, take the new key only
      and skip silently if the old one was present — or refuse and tell
      the user to regenerate. Pick "regenerate"; we explicitly don't care
      about old configs.
- [ ] Delete `src/gvl/include/gvl/serialization/`.

**Tests added:**
- [ ] `test_replay_roundtrip` — record a short scripted match, save,
      reload, step deterministically forward N frames, hash state, must
      equal the live-recorded state hash.
- [ ] `test_settings_roundtrip` — construct `Settings`, serialise to
      TOML, parse back, every field equal.
- [ ] Update existing `test_settings` for new TOML layout.

### Phase 6 — I/O: `io2/*`, `containers/*`

The big one. ~11 files touched.

- [ ] Add `src/game/io/binary_io.hpp`:
  - `template<class T> T read_le(FILE*);`
  - `template<class T> void write_le(FILE*, T);`
  - `void read_exact(FILE*, std::span<uint8_t>);`
  - `void write_exact(FILE*, std::span<const uint8_t>);`
  - `std::vector<uint8_t> read_all(FILE*);`
  - Overloads for `std::vector<uint8_t>` as in-memory sink/source for the
    network code (`net/memoryFs.cpp`).
- [ ] Add `src/game/io/miniz_codec.hpp`:
  - `std::vector<uint8_t> deflate_buffer(std::span<const uint8_t>);`
  - `std::vector<uint8_t> inflate_buffer(std::span<const uint8_t>);`
  - Direct calls to `mz_deflate` / `mz_inflate`. ~40 lines.
- [ ] Rewrite `src/game/reader.hpp`, `filesystem.{hpp,cpp}`, `replay.cpp`,
      `common.{hpp,cpp}`, `level.cpp`, `net/memoryFs.cpp`,
      `tc_tool/common_exereader.cpp`, `tc_tool/common_writer.cpp`,
      `video_tool/replay_to_video.cpp`, `video_tool/tools_main.cpp` against
      the new types. `gvl::source` becomes `FILE*` for files, span/vector
      for memory. `gvl::octet_reader` is replaced by direct
      `read_le<uint8_t>(FILE*)` calls (no need for a wrapper type).
- [ ] Delete `src/gvl/include/gvl/io2/` and `src/gvl/include/gvl/containers/`.

**Tests added:**
- [ ] `test_io` —
  - LE round-trip for `uint8/16/32/64`, signed and unsigned.
  - `read_exact` from a closed stream throws.
  - `deflate_buffer` ∘ `inflate_buffer` is identity on a 64 KiB random
    blob and on an empty blob.
- [ ] `test_tc_load` (extend existing) — every TC under `data/TC/`
      loads cleanly via the new code.

### Phase 7 — `resman/*`

- [ ] For each class currently inheriting `gvl::shared` (`FsNodeImp`,
      `Settings`, `Worm`, `WormSettings`, `WormAI`, `StatsRecorder`,
      `FsNodeZipArchive`, the now-deleted `bucket_pipe` family):
  - [ ] Drop the base class.
  - [ ] Switch ownership to `std::shared_ptr<T>` at every storage site
        (already half-done — `Common`, `Game`, `SoundPlayer` are
        `std::shared_ptr` already; the inconsistency was a code smell).
  - [ ] Where `this` was previously captured into a `gvl::shared_ptr<T>`,
        add `std::enable_shared_from_this<T>` and call `shared_from_this()`.
- [ ] Sed `gvl::shared_ptr<T>` → `std::shared_ptr<T>`.
- [ ] Sed `gvl::weak_ptr<T>` → `std::weak_ptr<T>` (audit usage — grep
      suggests minimal).
- [ ] Delete `src/gvl/include/gvl/resman/`.
- [ ] Capture the **post-migration golden replay** now (from Phase 0's
      deferred step) and commit under `data/test_replays/golden_v1.lrp`.
      Wire it into a `test_replay_golden` test that loads it and plays it
      to completion at the recorded state hash.

**Tests added:**
- [ ] `test_replay_golden` (described above).
- [ ] Sanitizer build: ASan + LSan + UBSan on the Linux preset, running
      `test_determinism` and a 30-second AI-vs-AI loop — proves no
      reference cycles were introduced.

### Phase 8 — Demolition

- [ ] `git rm -r src/gvl/`.
- [ ] Remove `add_subdirectory(src/gvl)` from `CMakeLists.txt`.
- [ ] Remove `gvl` from any `target_link_libraries`.
- [ ] Remove `src/gvl` from `include_directories` (or replace with
      whatever still needs to be on the include path; ideally nothing —
      everything is `src/game/...` now).
- [ ] Update `README.md` and `CLAUDE.md` if they mention gvl.
- [ ] `grep -rni gvl src/` must return zero hits.

**Exit criteria:**
- [ ] All six presets build clean.
- [ ] All tests green.
- [ ] `test_replay_golden` plays the post-migration golden replay end to
      end.

---

## Testing strategy

Three layers, applied at every phase boundary:

1. **Unit tests** — one new test target per replaced module:
   - `test_rect`, `test_pathfinding`, `test_io`, `test_replay_roundtrip`,
     `test_settings_roundtrip`, `test_replay_golden`.
2. **`test_determinism`** (existing) — two `Game` instances with the same
   `std::mt19937` seed must produce the same state hash after N frames.
   This is the single strongest end-to-end check that RNG, hash, I/O, and
   `shared_ptr` migrations did not perturb game logic *internally*. (It
   doesn't catch regressions vs. the pre-migration game — we accept that.)
3. **Sanitizers** at Phase 7 — ASan + LSan + UBSan on Linux running
   `test_determinism` and a 30-second AI-vs-AI loop.
4. **CI matrix** — all six presets build and run tests on every PR.
5. **Manual smoke** — one human-played match per OS at Phase 6 (I/O) and
   Phase 7 (resman) boundaries.

## Risk register

| Risk | Mitigation |
|---|---|
| `std::shared_ptr` cycle leaks `Worm`/`Settings`/`Game` | ASan + LSan + 30-s soak in Phase 7. Audit `enable_shared_from_this` use. |
| `std::priority_queue` lazy-delete reorders A* paths in ways that subtly change AI behaviour | Acceptable — replay format is breaking anyway. `test_pathfinding` asserts optimality, not exact tie-break. |
| `std::mt19937` is much larger state than `mwc` (~2.5 KB vs 8 bytes) — every `Worm` carries an RNG | Audit: in current code only `Game::rand` is per-game. Confirm we don't multiply RNGs and bloat. If we do, switch to `std::minstd_rand` (8 bytes) or roll a 16-byte xoshiro128++ inline. |
| Direct `tomlplusplus` usage at 5+ call sites duplicates code the adapter used to abstract | Add a 30-line `src/game/serialization/toml_helpers.hpp` with `get_or<T>(table, key, default)` and `set<T>(table, key, value)`. Still a net loss vs. the 375-line adapter. |
| xxhash vcpkg port unavailable for a triplet | xxhash supports all six triplets; if a port issue arises, inline `XXH3_64bits` from the public-domain reference (~100 lines). |
| Replay format break upsets users mid-tournament | Communicate clearly in the release notes; bump the major version. User has explicitly accepted this. |

## Progress log

Update this section as work happens. Date format: `YYYY-MM-DD`.

- _(empty)_

---

## What actually happened (2026-05-23)

### Done in this session

**Phase 1, 2, 3, 4** completed as planned:
- ✅ `support/{bits,debug,platform,type_info,functional}.hpp`, `meta/`, `system/`
  — only `noncopyable` had external uses (3 sites), replaced with `= delete`.
  The rest is dead-code-ish inside the moved-but-not-yet-simplified gvl tree.
- ✅ `math/cmwc.hpp`, `math/random.hpp` → `std::mt19937` in `src/game/rand.hpp`.
  RNG state grew from 8 bytes to ~6 KB serialized text; the network sync map
  packet wire format and replay archive now carry the bigger state. Replay
  binary format is broken — old replays will fail to load.
- ✅ `math/vec.hpp`, `math/rect.hpp` → `src/game/math/rect.hpp`. Trimmed
  unused methods (joins, rotations, etc.).
- ✅ `crypt/gash.hpp` → `xxhash` via vcpkg. `gvl::gash::value_type` (256-bit)
  → `uint64_t`. Settings/Worm/Replay hash fields shrank accordingly.
- ✅ `containers/pairing_heap.hpp` → `std::priority_queue` with lazy
  invalidation in `ai/dijkstra.hpp`. `ai/astar.hpp` was dead code, deleted.

**Phase 8 partially**: the separate `src/gvl/` library / `add_subdirectory` /
`libgvl.a` is gone. Files were relocated to `src/game/gvl/`, the .cpp
sources are compiled into the `game` target directly.

### Not done, and a course correction

**Phase 5 / Phase 6 / Phase 7 plan was unrealistic.** Closer reading of
`replay.cpp`, `settings.cpp`, `worm.cpp`, `common.cpp`, `common_model.hpp`
turned up ~52 `archive` call sites and a binary-archive layer that genuinely
earns its keep — the `ar.i32(name, x)` unified API works for both reader and
writer through templates. Replacing it with direct `tomlplusplus` table
manipulation or raw `FILE*` reads doubles each call site (separate read and
write paths). That is *more* custom code, not less.

Likewise the `io2/` stream layer is ~1200 lines of legitimate streaming I/O
(buffered, deflate-piped, multi-source). Replacing with FILE* / span helpers
requires inlining the streaming semantics at every call site (`reader.get()`
per byte, deflate piping, etc.) — large, mechanical, and risky.

So the *library* is removed, the *namespace* and the bulk of its
implementation remain inside our tree. Code count after this session:
- Net change: roughly 1000 lines of custom code removed (xxhash, mt19937,
  std::priority_queue, simpler vec/rect, dead astar.hpp).
- ~5000 lines of moved-but-not-yet-rewritten gvl code remains in
  `src/game/gvl/`, ready for incremental simplification.

### Suggested future work

1. **Replace `gvl::shared` → `std::shared_ptr`** (Phase 7). Tractable, large
   refactor — ~15 classes inherit `gvl::shared`. Sanitizer pass needed.
2. **Replace `octet_reader/writer` + `file_bucket_pipe` + `deflate_source`**
   in `replay.cpp` with a hand-rolled buffered reader on top of `FILE*` +
   `miniz` deflate streaming. ~150 lines added, ~1200 deleted.
3. **Keep `serialization/archive.hpp` and `toml_adapter.hpp`** as-is, or
   simplify only the templating (drop the unused `Context` parameter).
4. **Inline `support/bits.hpp`** at its few call sites using `<bit>` — it's
   only included transitively now and only used inside gvl internals; it
   may well become unreachable after step 2.
