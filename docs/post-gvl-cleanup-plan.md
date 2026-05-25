# Post-gvl cleanup — replace homegrown serialization with cereal (binary + TOML)

Now that `src/game/gvl/` is gone and the remaining serialization code lives
under `ser::`, we're going to **replace the entire homegrown serialization
layer — both the binary archive and the TOML adapter — with
[cereal](https://uscilab.github.io/cereal/)**. For the binary side we
use cereal's built-in `PortableBinaryArchive`. For the TOML side we
write a custom `cereal::TomlOutputArchive` / `TomlInputArchive` backed
by `toml++`.

The win is **one `serialize()` function per type**, used for replays,
network sync, settings persistence, and `tc.cfg` loading. Versioning
becomes a first-class concern (`CEREAL_CLASS_VERSION`), and the
chronic duplication between the binary and TOML field-list code goes
away.

The branch state at the time of writing: build green on Linux,
66/66 ctest cases pass, `grep -rn gvl src/ CMakeLists.txt` is empty.

**Progress as of this revision** (branch `remove-gvl-and-new-serialization`):

- Phase 0 — ✅ done (commit `13ea321`)
- Phase 1 — ✅ done (commit `600fd6a`)
- Phase 2 — ✅ done
- Phase 3 — ✅ done (commits `3033fed`, `243be75`); see Phase 3 notes for the deferred half
- Phase 4 — ✅ done (commits `9b7c7a7`, `7e48263`, `848a094`, see Phase 4 notes for what was deferred)
- Phase 5 — ✅ done (see Phase 5 notes)
- Phase 6 — ✅ done (commits `bd89396`, `6a805cf`, `b1636a8`, `85e1f43`)
- Phase 7 — ✅ done (commit `297592b`)
- Phase 8 — ✅ done

ctest 93/93. All phases complete.

---

## Library evaluation

### cereal — chosen

Header-only, BSD-3, single include. The archive pattern matches what
our code is shaped for: write `serialize(Archive& ar, T& obj, std::uint32_t version)`
per type, list fields with `ar(CEREAL_NVP(field1), CEREAL_NVP(field2), ...)`.
Multiple built-in backends — `PortableBinaryArchive` (binary,
endian-normalized), `JSONOutputArchive`, `XMLOutputArchive` — and
cereal is explicitly designed to support custom archives, which is
what makes our TOML plan tractable.

**Versioning:** `CEREAL_CLASS_VERSION(Worm, 3)` declares the current
version; `serialize()` receives the version that was written:

```cpp
template<class Archive>
void serialize(Archive& ar, Worm& w, std::uint32_t const version) {
    ar(CEREAL_NVP(w.pos), CEREAL_NVP(w.vel), CEREAL_NVP(w.health));
    if (version >= 2) ar(CEREAL_NVP(w.newField));
}
```

Old replays read v1 and skip new fields; new code writes v3 and
includes them. This is the feature we're paying for.

**Pointer tracking:** `std::shared_ptr<T>` is tracked automatically —
the first write emits the object inline, subsequent writes emit a
reference. Replaces our manual `serialization_context::write/read`
machinery.

**Cost:** template-heavy; compile-time hit on `replay.cpp` and
`worm.cpp` is expected. Active project (commits in 2024), used in
production game engines.

### Rejected alternatives

- **bitsery** — fast binary, but binary-only. Doesn't help with TOML.
- **Boost.Serialization** — mature but heavy; build cost and compile
  cost both poor.
- **Protobuf / FlatBuffers / Cap'n Proto** — schema-driven; would
  rewrite the authoring model. Too disruptive for the gain.
- **Reflection-based (glaze / reflect-cpp / boost::pfr)** — promising
  but constrains the types we can serialize (aggregate-only or with
  member registration). Mixing reflection-based and explicit
  serialize() in one project is awkward.

---

## What the migration looks like at call sites

### Binary archive

Current style (from `replay.cpp:67`):

```cpp
template<typename Archive>
void archive(Archive ar, Worm& worm) {
    ar
    .i32(worm.pos.x)
    .i32(worm.pos.y)
    .b(worm.alive)
    .obj(worm.ninjarope.anchor, WormCreator())
    ;
    ar.check();
}
```

Becomes:

```cpp
template<class Archive>
void serialize(Archive& ar, Worm& worm, std::uint32_t const version) {
    ar(CEREAL_NVP(worm.pos),
       CEREAL_NVP(worm.vel),
       CEREAL_NVP(worm.alive),
       CEREAL_NVP(worm.ninjarope.anchor),  // shared_ptr<Worm>, dedup'd
       CEREAL_NVP(worm.settings));
}
CEREAL_CLASS_VERSION(Worm, 1);
```

The `ar.i32() / ar.b() / ar.ui8()` distinction disappears — cereal
dispatches on the actual C++ type. `bool` → 1 byte; `int32_t` →
4 bytes (portable archive normalizes endian). The `0x12345678`
`ar.check()` sentinel goes away.

### TOML

Current style (from `settings.hpp:159`):

```cpp
template<typename Archive>
void archive_text(Settings& settings, Archive& ar) {
#define S(n) #n, settings.n
    ar.i32(S(maxBonuses));
    ar.i32(S(lives));
    ar.b(S(shadow));
    ar.arr("weapTable", settings.weapTable, [&](uint32_t& v) {
        ar.u32(0, v);
    });
#undef S
}
```

Becomes the **same `serialize()` function** as the binary version —
the cereal call-site looks identical for both backends. The
`CEREAL_NVP` macro expands to a name/value pair that the binary
archive ignores (it just writes positionally) and the TOML archive
uses as the table key.

```cpp
template<class Archive>
void serialize(Archive& ar, Settings& s, std::uint32_t const version) {
    ar(CEREAL_NVP(s.maxBonuses),
       CEREAL_NVP(s.lives),
       CEREAL_NVP(s.shadow),
       CEREAL_NVP(s.weapTable));
}
CEREAL_CLASS_VERSION(Settings, 1);
```

The `Settings`-via-TOML-as-string hack in `replay.cpp` goes away —
cereal serializes Settings directly through whichever archive it's
handed.

---

## The custom TOML archive

Cereal supports custom archives via two CRTP base classes:
`cereal::OutputArchive<DerivedT>` and `cereal::InputArchive<DerivedT>`.
You override the primitives (save/load for ints, strings, bools)
and the lifecycle hooks (`prologue`/`epilogue` for objects, arrays,
and NVPs).

Reference: cereal's `include/cereal/archives/json.hpp` is the
closest analog — JSON is the other named-key text format cereal
supports out of the box. ~600 lines including comments. A TOML
archive backed by `toml++` will be in the same ballpark, with
straightforward 1:1 mappings:

| Concept             | JSON archive          | TOML archive         |
|---------------------|-----------------------|----------------------|
| Named field         | `"key": value`        | `key = value`        |
| Sub-object          | `{ ... }`             | `[section]`          |
| Array of primitives | `[1, 2, 3]`           | `[1, 2, 3]`          |
| Array of objects    | `[{...}, {...}]`      | `[[section]]`        |
| Null                | `null`                | (omit key)           |

The few semantic gaps:
- **TOML has no `null`.** The current adapter handles this with a
  `null()` method (one call site, in a ref resolver). Mirror that:
  serialize null by omitting the key, deserialize by checking
  `table["key"]`'s presence.
- **TOML integers are 64-bit signed.** `uint32_t` round-trips through
  `int64_t`; values above `INT64_MAX` would corrupt, but the codebase
  uses only `int32_t` and `uint32_t`, both safe.
- **Floating point precision.** Not currently a concern — gameplay
  uses fixed-point (`fixedvec`), and settings have no floats.
- **Version tag.** cereal writes a `__version__` field for versioned
  types. In TOML this becomes a `__version__` key in each section.
  For arrays of objects (`[[section]]`), decide up front whether each
  element carries its own `__version__` (cereal's JSON archive does
  this per-object) or whether we hoist it to the outer table — the
  archive's `prologue`/`epilogue` for `SizeTag` is where this is
  decided.
- **Endianness.** The old archive wrote explicit little-endian.
  `PortableBinaryArchive` normalizes endian on read/write, so replays
  stay cross-platform — this is a hard requirement, not a nice-to-have.

The archive class lives in `src/game/serialization/toml_archive.hpp`
and replaces `src/game/serialization/toml_adapter.hpp`. The
`ser::` namespace can host both archives (binary uses cereal's
default) for the migration period; once everything compiles against
cereal, the `ser::` namespace either renames to something more
specific or goes away entirely.

---

## Pointer model — the one design decision worth a meeting

The current binary archive uses raw pointers + a `serialization_context`
that hands out IDs and dedups. Two cases mix together in `replay.cpp`:

1. **Owning, deduplicated pointers** (`worm.ninjarope.anchor`, a
   `Worm*` that may appear in multiple places): **convert to
   `std::shared_ptr<Worm>`** and let cereal handle dedup natively.
   Game ownership of `Worm` becomes shared. `Game::worms` becomes
   `std::vector<std::shared_ptr<Worm>>` (or `unique_ptr` for owners
   + `weak_ptr` for refs if we want sharper ownership).

2. **Index references** (`WormIdxRefCreator`, `WeaponIdxRefCreator`):
   the pointer is really an index into a known array. **Store the
   index directly** — get rid of the indirection. The serialization
   just writes the int.

This refactor is the riskiest part of the migration because it
touches lifetime code outside the serialization layer. It should
land **before** swapping the archive library so each change is
independently verifiable.

---

## Phases

### Phase 0 — round-trip tests against the current code (mandatory prerequisite) — ✅ done (commit `13ea321`)

`src/tests/test_archive.cpp` (6 cases / 30 assertions) and
`src/tests/test_toml_adapter.cpp` (7 cases / 21 assertions) cover the
primitive read/write API, byte-compare fixtures for the binary layout,
the `check()` sentinel, `pascal_str` truncate/pad, `obj()` pointer
dedup, `obj()` null handling, missing-key and wrong-type tolerance on
the TOML reader, nested `obj()`, `arr()` of primitives, `array_obj()`
of objects, and `ref()` null omission. ctest goes from 66 to 79.

Two intentional scope reductions to flag for the next agent:
- **No standalone Worm round-trip test.** `archive(Worm&)` is wired
  tightly to `GameSerializationContext` (resolves `lastKilledByIdx`
  via `game->worms.at(idx)` and `weapons[i].type` via
  `game->common->weapons[idx]`). Building a Game with a loaded Common
  was deemed disproportionate for tests that get deleted in Phase 4.
  `test_determinism` is the de-facto end-to-end Worm guard.
- **Byte-compare fixtures are inline literals, not checked-in files.**
  Easier review; no separate file management; the bytes are read
  alongside the writes.

Original phase notes follow.

Before we rip anything out, lock down the existing behavior with
direct tests. Today the only verification is end-to-end determinism.

Create `src/tests/test_archive.cpp` (binary) and
`src/tests/test_toml_adapter.cpp` (TOML) with:
- Per-primitive round trip (`i32`, `ui8`, `b`, `str` for binary;
  `i32`, `u32`, `b`, `str` for TOML).
- Per-type round trip: serialize a `Worm` / `Settings` with known
  field values, deserialize, compare every field.
- Pointer dedup (binary only, current behavior): serialize a graph
  where worm A's anchor is worm B; verify B isn't written twice.
- Byte-compare against a checked-in fixture for one `Worm` write
  and one `Settings` write. **These two tests guard Phases 1–3 only**
  — they will break (and should be deleted) in Phase 4 when the
  format changes intentionally. The round-trip property tests keep
  working across the migration.

This phase is non-negotiable. A silent regression in the archive
layer corrupts replays in ways that won't surface until someone
tries to play one.

### Phase 1 — add cereal, prove it builds — ✅ done (commit `600fd6a`)

`cereal` added to `vcpkg.json` (vcpkg pulled 1.3.2#1, BSD-3).
`find_package(cereal CONFIG REQUIRED)` added to `CMakeLists.txt`.
`src/tests/test_cereal_smoke.cpp` round-trips `int32_t` and
`std::string` through `PortableBinaryArchive`. The smoke target links
only `cereal::cereal` + Catch2 (cereal is header-only; no need to pull
in the `game` lib). Verified on `linux-x64-debug`. Cross-platform
(Windows, emscripten) verification deferred to CI on push.

Original phase notes follow:

- Add `cereal` to `vcpkg.json`.
- Add `test_cereal_smoke.cpp` that round-trips an `int` and a
  `std::string` through `PortableBinaryArchive`.
- Verify on all build presets (linux-x64 today; eventually Windows
  and emscripten).

### Phase 2 — write the TOML archive — ✅ done

`src/game/serialization/toml_archive.hpp` adds `ser::TomlOutputArchive`
and `ser::TomlInputArchive` (derived from `cereal::OutputArchive` /
`cereal::InputArchive` + `cereal::traits::TextArchive`). The output
archive builds a `toml::table` in memory using a lazy-materialization
stack — frames are deferred until the first child arrives, so a
`makeArray()` triggered by `SizeTag`'s prologue can flip the
just-started node from table to array before it lands in the tree.
Prologue/epilogue overloads cover NVP, SizeTag, generic objects,
arithmetic, and string — mirroring the structure of cereal's bundled
`json.hpp`.

`src/tests/test_toml_archive.cpp` covers primitives, nested objects,
arrays of primitives, arrays of objects, missing-key tolerance, and a
versioned type (round-trip plus an assertion that
`cereal_class_version` appears in the serialized output). ctest goes
from 80 to 86.

Out of scope for Phase 2 (deferred until needed):
- Special `__version__` handling. Cereal writes the version inline as
  a `cereal_class_version` NVP at first encounter of a versioned type;
  in TOML this becomes a regular key in that table. Good enough until
  Phase 5 reveals it isn't.
- Explicit null handling. TOML omits the key; readers preserve the
  default. The plan's `null()`-via-`ref()` mechanism doesn't have a
  cereal-side analog yet — defer until Phase 3 makes the call sites
  concrete.

Original phase notes follow.

- Create `src/game/serialization/toml_archive.hpp` with
  `TomlOutputArchive` and `TomlInputArchive` derived from
  `cereal::OutputArchive<TomlOutputArchive>` and
  `cereal::InputArchive<TomlInputArchive>`.
- Override the cereal extension points: `prologue`/`epilogue` for
  `NameValuePair`, `SizeTag`, generic types; `saveValue`/`loadValue`
  for primitives.
- Cover the corner cases: omit-on-null, version tag as
  `__version__` field, arrays-of-objects via TOML's `[[section]]`
  syntax.
- **Unit-test the archive in isolation** — round-trip simple structs
  with `serialize()` methods through it, then progressively more
  complex structs (nested, arrays, version-gated fields).

Land before any type conversions. The archive is the foundation
everything else builds on.

### Phase 3 — refactor the pointer model — ✅ done (commits `3033fed`, `243be75`)

Split into two commits.

**`3033fed` — index references go direct.** `WormIdxRefCreator` and
`WeaponIdxRefCreator` both tunneled what's really an int through a
pointer-dedup roundtrip. Replaced with `ar.i32(idx)` at all four
sites (`worm.lastKilledByIdx`, `worm.weapons[i].type` via Common
offset, `vp.wormIdx`, `game.lastKilledIdx`); both creator structs
deleted. Wire format changes (~4 bytes per site, different number)
but determinism round-trip still passes.

**`243be75` — `Game::worms` becomes `vector<shared_ptr<Worm>>`.**
This also fixes a pre-existing leak: `Game::clearWorms()` was
calling `.clear()` on a `vector<Worm*>` without deleting. Touched
~14 files (callers using `auto* w` / `Worm* w` → `auto const& w`;
pointer-identity checks gained `.get()`; controllers and tests
switched `new Worm()` → `make_shared<Worm>`). `wormByIdx` keeps
returning a non-owning `Worm*` via `.get()`.

**Deferred half:** `Ninjarope::anchor` stays as raw `Worm*`. The
old archive's `obj()` dedup hands out raw pointers via a
`serialization_context`; if `anchor` were `shared_ptr<Worm>`, two
deserialized shared_ptrs to the same worm would get independent
control blocks and double-free. Building shared_ptr-aware dedup in
the throwaway archive isn't worth it — handled naturally in Phase 4
when cereal's native `shared_ptr` serialization takes over.

Original phase notes follow.

For every `obj()` / `fobj()` call site in `replay.cpp`, decide:
- "Already `shared_ptr`, no change."
- "Convert to `shared_ptr`."
- "Replace with index reference."

Land each conversion as its own commit. The serialization still
uses the old `ser::` archive in this phase; tests must keep
passing after every commit.

**Interop caveat.** The old archive's `obj()` / `fobj()` and
`WormCreator` take raw `Worm*` via `serialization_context`. To keep
each commit green while ownership migrates to `shared_ptr`, we need
*throwaway* shims — either teach the old archive entry points to
accept `shared_ptr` (extracting `.get()` for the existing
context lookups), or hold a raw `Worm*` view alongside the shared
owner at each archive call site. Both go away in Phase 4.

If the shim cost looks excessive once we scope it concretely,
the fallback is to fold Phase 3 into Phase 4 cluster-by-cluster:
convert ownership and swap the `serialize()` for that type in the
same commit, so the old archive never sees `shared_ptr`. Decide
after scoping, not now.

When this phase ends, no more `WormCreator`/`ViewportCreator`
factory objects, no more `serialization_context` per-pointer
register/unregister — just `shared_ptr` and ints.

### Phase 4 — convert types to cereal `serialize()` — ✅ done

Landed as the *coexist* strategy: cereal `serialize()` functions live
alongside the existing `ser::` `archive()` functions; the wire format
and call sites in `replay.cpp` / `settings.cpp` don't change yet
(Phase 6 swaps them). All glue lives in
`src/game/serialization/cereal_types.hpp`, keeping the gameplay headers
free of cereal. Each cluster is its own commit; per-type unit tests
in `src/tests/test_cereal_types.cpp` round-trip through both
`PortableBinaryArchive` and the new TOML archive.

Per-type coverage so far:
- Math leaves: `BasicVec<T, 2>`, `BasicRect<T>` (commit `9b7c7a7`).
- Simple structs: `Worm::ControlState`, `Ninjarope`, `WormWeapon`
  (commit `7e48263`). `Ninjarope::anchor` and `WormWeapon::type` are
  intentionally excluded — they're pointer fields that will be
  resolved as indices at the enclosing Worm/Game scope.
- Mid-level: `Viewport`, `Worm`, `WormSettings`, `Settings`, `Rand`
  (commit `848a094`). `Worm` excludes the same context-dependent
  pointer fields plus its `settings` `shared_ptr` and `ai`. `Settings`
  serializes `wormSettings[3]` natively via cereal's `shared_ptr`
  support. Rand uses a save/load pair around `std::mt19937`'s stream
  format.
- Top-level leaves: `Color`, `Palette`, `Level` (commit `<this>`).
  `Level::materials` is intentionally not serialized — it's
  re-derived from `Level::data` + `Common` on load, matching the
  existing replay behaviour.

**Deferred to Phase 6:** `Game::serialize()`. The current
`archive(Archive, Game&)` resolves three context-dependent fields
that span worms:
1. `worm.ninjarope.anchor` (raw `Worm*`) → index into `game.worms`.
2. `worm.weapons[i].type` (`Weapon const*`) → offset into
   `Common::weapons`.
3. `worm.settings` (`shared_ptr<WormSettings>`) — shared across
   `game.settings.wormSettings[]` and per-worm overrides.

Wiring those through cereal needs a context object (cereal supports
this via `UserDataAdapter` / `get_user_data`). That context lives
naturally next to the call-site swap in `replay.cpp` and is the
work of Phase 6 — there's no value in writing it now and immediately
deleting the old `archive(Game&)` code.

ctest: 86 → 102.

Original phase notes follow.

**Prerequisite audit** (do before writing any `serialize()`):
- Default-constructibility of every type we'll serialize:
  `Worm`, `Viewport`, `WormWeapon`, `Game`, `Level`, `Settings`,
  `NinjaRope`, `Common`'s reachable parts. Anything lacking a
  default constructor needs `cereal::load_and_construct` — list
  them up front so the pattern is decided once.
- The fixed-point math types (`Scalar`, `BasicVec`, `IVec2`,
  `Rect`) are leaves used by almost every gameplay type. Write
  their `serialize()` (or `save`/`load` non-member pair) first,
  before anything depending on them.

One type at a time (or one cluster at a time), replace the
`archive()` / `archive_text()` / `archive_gameplay_text()` free
functions with **one `serialize()` per type** that works for both
binary and TOML.

Order leaves-upward:
- Fixed-point primitives (`Scalar`, `BasicVec`, `IVec2`, `Rect`).
- Simple structs (`Worm::ControlState`, `NinjaRope`).
- Mid-level (`Viewport`, `WormWeapon`, `Worm`).
- Top-level (`Game`, `Level`, `Settings`, `Common`'s reachable parts).

After each type converts, the round-trip property tests from
Phase 0 must pass. The byte-compare tests from Phase 0 break the
moment the first type converts — that's the expected signal.

### Phase 5 — turn on versioning, prove it works end-to-end — ✅ done

`Settings` bumped from v1 → v2 with a new `bonusTimeout` field (default 0).
Fixture approach chosen: **option (a)** — v1 binary (769 bytes) and v1 TOML
fixtures captured at end of Phase 4 and embedded as inline arrays in
`src/tests/test_versioning.cpp`. Four tests cover both paths:
- v2 binary round-trip (new field preserved).
- v1 binary fixture → v2 reader (new field = 0).
- v2 TOML round-trip (new field in output, preserved on read).
- v1 TOML fixture → v2 reader (new field = 0, missing key tolerated).

ctest: 102 → 106.

Original phase notes follow.

Pick a type likely to grow — `Settings` is natural — and bump it
to version 2 with a synthetic new field. Write tests:
- Write a v2 replay (binary), read it back, see the new field.
- Read a v1 replay as v2 code, see the field at its default.
- Same two tests through the TOML path.

**Producing the v1 fixture.** Once code is at v2 you can't naturally
emit v1 output, so pick one mechanism up front and stick to it:
either (a) check in a v1 binary fixture and a v1 TOML fixture
captured at the end of Phase 4 (preferred — it's a real file from
real code), or (b) keep a `force_version` test hook that lets the
test drive the version cereal writes. Option (a) is what real
backward-compat looks like; (b) is easier to maintain. Decide
before Phase 5 starts.

This is the deliverable that justifies the whole exercise. The
tests document the pattern future contributors will copy.

### Phase 6 — delete the old code ✅

Completed in sub-phases:
- 6a+6b: Game::serialize() + replay.cpp rewrite (cereal PortableBinaryArchive)
- 6c: Settings/WormSettings TOML swap (cereal TomlOutputArchive/TomlInputArchive)
- 6d: common_model types ported (NObjectType, SObjectType, Weapon, Common tc.cfg)
- 6e: Deleted archive.hpp, coding.hpp, context.hpp, except.hpp, toml_adapter.hpp,
  test_archive.cpp, test_toml_adapter.cpp

Deleted:
- `src/game/serialization/archive.hpp` (~415 lines)
- `src/game/serialization/coding.hpp` (~207 lines, after confirming
  `io/coding.hpp` covers all remaining uses)
- `src/game/serialization/context.hpp` (~180 lines)
- `src/game/serialization/except.hpp` (~15 lines — fold into
  `io::ArchiveCheckError` or cereal's own exception type)
- `src/game/serialization/toml_adapter.hpp` (~375 lines)
- `WormCreator`, `ViewportCreator`, `WormIdxRefCreator`,
  `WeaponIdxRefCreator` in `replay.cpp`.
- The `S(n)` macro in `settings.hpp`.

Net delete: ~1200 lines of homegrown serialization. Net add: one
custom cereal archive (~600–800 lines including input + output +
its own unit tests — cereal's `json.hpp` is ~600 lines for
reference, and TOML's `[[section]]` handling adds a bit on top),
per-type `serialize()` methods (~30–50 lines per type, ~30 types).

After Phase 6, the `ser::` namespace contains only the new TOML
archive. Renaming or retiring it lands separately in Phase 8.

### Phase 7 — modernize `io::read_uintNN_le` with `std::byteswap`

Independent of the cereal migration but folded into this plan to
avoid leaving a stray cleanup behind. `io/coding.hpp`'s
`read_uintNN_le` / `write_uintNN_le` currently shuffle bytes by
hand; replace the body with `std::byteswap` on big-endian and a
straight copy on little-endian (C++23, already required by the
build).

Trivial change, lands as its own commit at the end of the PR (or
as a follow-up PR if the diff is already large). Verify with the
existing `io` tests — no new tests needed.

### Phase 8 — rename or retire the `ser::` namespace

After Phase 6, `ser::` houses only the TOML archive — the binary
path goes through `cereal::` directly, so the namespace is no
longer doing double duty and the name no longer describes its
contents. Pick one:

- **Rename** to something that reflects what's actually there:
  `tomlcfg::`, `config::`, or fold into an existing namespace if
  one fits. Mechanical rename across the codebase, one commit.
- **Retire** entirely: put `TomlOutputArchive` / `TomlInputArchive`
  in the top-level namespace or under `cereal::` (cereal's own
  custom-archive examples sit in `cereal::`).

Decide based on what reads best at call sites once Phase 6 is
done — premature now. Land as its own commit; no behavior change,
just naming.

---

## Items obsoleted by this plan

The earlier draft of this plan had Tiers 1–4 of cleanups on the
existing archive layer (delete `versioned_archive`, merge
`ser/coding.hpp` into `io/coding.hpp`, modernize
`serialization_context` to use `unique_ptr`, CapCase the type
names). **All moot** — we're deleting the code they were cleaning up.

The one item still worth doing is the original Tier 3.2: replace
the hand-rolled byte-shuffling in `io::read_uintNN_le` with
`std::byteswap`. `io::` survives this migration untouched, so that
cleanup is still relevant — see Phase 7 above, where it's folded
in as a separate commit so it doesn't get lost.

---

## Out of scope (deliberately)

- **Network protocol message-level versioning.** There are two
  versioning concerns in the netplay code, and this plan only
  addresses one:
  1. *Inside* a message — the payload contents. When a `Worm` or
     `GameState` grows a field, old and new peers need to agree
     on how to read it. This is exactly what `CEREAL_CLASS_VERSION`
     solves, and it's covered by this migration.
  2. *Around* a message — the framing and dispatch. The network
     controller reads a tag (byte/enum) off the wire that says
     "this is a `JoinRequest`" vs "this is an `InputUpdate`" and
     dispatches accordingly. Adding new message types, deprecating
     old ones, or changing the tag scheme is hand-written framing
     in `controller/networkController.cpp`. Cereal has nothing to
     say about it — it only sees the payload *after* dispatch has
     chosen the type.

  A reader could plausibly assume "cereal versioning means network
  protocol compatibility is solved." Only half of that is true.
  See "Future work" below.
- **Replacing `toml++`.** The third-party TOML library stays;
  our custom archive sits on top of it.

---

## Future work (after this plan lands)

- **Message-type framing in `networkController.cpp`.** With payload
  versioning solved by cereal, the remaining gap is the wire-level
  message tag. Worth a follow-up PR: audit the current tag scheme,
  decide whether to version it (a protocol version byte at handshake?
  per-message-tag namespacing?) and document how new message types
  get added without breaking old peers. Independent of cereal, but
  this migration is the natural moment to notice the gap.

---

## Risks / things that might surprise you

- **Custom cereal archive is real engineering.** Cereal's
  extension points are documented but the API isn't beginner-friendly.
  Budget time to read `json.hpp` carefully and test the TOML
  archive in isolation before plugging it into the codebase.
- **Compile time on `replay.cpp`.** Cereal is heavily templated;
  the file pulling in every `serialize()` overload gets slower.
  Measure before/after; consider explicit instantiation if it
  becomes painful.
- **Wire-format change is irreversible per replay file.** Once we
  ship the cereal binary format, old `.lrp` files written by the
  homegrown code are unreadable. User has already accepted this
  for the gvl removal; reconfirm before Phase 6.
- **TOML format change.** The on-disk TOML may not be byte-identical
  to what the current adapter produces (whitespace, key ordering,
  inline-vs-block tables). Users editing `settings.toml` by hand
  will see cosmetic changes. The semantic content stays compatible
  *if we keep the same key names* — which `CEREAL_NVP(field)`
  naturally does, since it uses the C++ identifier.
- **Pointer-model refactor (Phase 3) is the highest-risk change
  outside serialization.** Moving `Worm` ownership from raw to
  shared has subtle implications for lifetime in `Game`,
  `Viewport`, and the controllers. Lean on the determinism test
  throughout.
- **`load_and_construct` for non-default-constructible types.**
  Audit before Phase 4. If any of `Worm`/`Viewport`/etc. lack a
  default constructor, cereal needs the `load_and_construct`
  static method form.
