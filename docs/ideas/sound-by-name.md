# Spec: Reference sounds (and other resources) by name, not by index

Tracking issue: [openliero#44](https://github.com/openliero/openliero/issues/44)
(originally [gliptic/liero#42](https://github.com/gliptic/liero/issues/42))

## 1. Background

Sound samples in OpenLiero are stored in `Common::sounds` — a
`std::vector<SfxSample>` populated at TC load time from the
`types.sounds` array in `tc.cfg`:

```toml
sounds = ["shotgun", "shot", "rifle", "bazooka", "blaster", "throw",
          "larpa", "exp3a", "exp3b", "exp2", "exp3", "exp4", "exp5",
          "dirt", "bump", "death1", "death2", "death3", "hurt1",
          "hurt2", "hurt3", "alive", "begin", "dropshel", "reloaded",
          "moveup", "movedown", "select", "boing", "burner"]
```

Each entry causes `Common::load` (src/game/common.cpp:322) to open
`sounds/$name.wav` next to `tc.cfg`. Everywhere else, sounds are
referred to by their **position in this array**:

* `Weapon::launchSound`, `Weapon::loopSound`, `Weapon::exploSound` (int)
* `SObjectType::startSound`, `SObjectType::numSounds` (int range)
* Hardcoded integer literals scattered through the engine and menus
  (`sfx.play(common, 27)`, `game.soundPlayer->play(14)`, etc.)
* Per-weapon `.cfg` files (e.g. `weapons/bazooka.cfg`):
  ```
  launchSound = 3
  loopSound = 0
  exploSound = -1
  ```

### Why this is broken

Some legacy TCs (built with *Snd Tools*) ship a sounds file in which
some entries are **disabled / missing**. Per the original issue, the
`tc_tool` extraction code skips disabled samples, so indices shift,
and as a result the **wrong sound plays** at runtime — e.g. an
explosion sound plays in place of a menu beep. The shipped OpenLiero
TC does not exhibit this, but it is reproducible against several
community TCs.

NObjectTypes, SObjectTypes and Weapons are already cross-referenced
**by name** (`idStr`) via the `objRefToStr` / `objRefFromStr`
helpers in `src/game/common_model.hpp`. Sounds are the last index-based
foreign key in the TC schema.

### What the previous author found (issue #44 comments)

1. They moved hardcoded sound indices that live in C++ (menu beeps,
   death, throw, etc.) **into `tc.cfg`** so the engine no longer
   carries magic constants like `27`. This work was not landed.
2. Some menu sounds bypass `SoundPlayer` and go through the global
   `sfx` directly. They wanted to unify both behind a single
   `SoundPlayer` so the `>= 0` guard could live in one place
   (`SoundPlayer::play` could check then dispatch to a virtual
   `playImpl`, letting the compiler inline the check).
3. They considered turning the sound vector into smart pointers and
   resolving names to pointers; abandoned it as too invasive.
4. They got stuck on the global/scope mismatch: `sfx` is a file-scope
   global, but `SoundPlayer` is owned by `Game`. Menu / front-end
   code runs outside of `Game`, so it can't reach `game.soundPlayer`.

## 2. Goals

1. **Correctness:** wrong sounds must not play when a TC has gaps in
   its sound table. Disabled / missing samples must be tolerated (the
   slot survives as a no-op rather than shifting indices).
2. **Schema clarity:** all sound references in TC config files
   (`tc.cfg`, `weapons/*.cfg`, `sobjects/*.cfg`) are **string names**,
   matching the existing weapon/nobject/sobject cross-reference style.
3. **No magic numbers in code:** the engine refers to its
   built-in/menu sounds by a stable symbolic identifier resolved
   against the TC at load time, never by a literal `27`.
4. **Single play path:** `SoundPlayer::play` is the only call site
   pattern. The `index >= 0` ("no sound") guard lives there once,
   not at every caller. The legacy global `Sfx` is removed; menu
   and in-game code both go through `SoundPlayer`.
5. **Backwards compatibility with current OpenLiero shipped TC:**
   the canonical TC under `data/TC/openliero/` must keep working
   bit-for-bit (determinism tests, replays).

## 3. Non-goals

* Changing the on-disk WAV layout or how samples are loaded from
  `sounds/*.wav`.
* Changing the replay format. Replays record controller inputs, not
  sound plays — sound resolution is a presentation-time concern and
  must not affect simulation determinism.
* Refactoring `SObject` / `NObject` / `Weapon` to hold pointers
  instead of indices. We keep `int` indices **internally**; only
  the **persisted form** and **call sites** change. (Considered and
  rejected by the previous author as too invasive; we agree.)
  Note: unifying `Sfx` and `SoundPlayer` **is** in scope — see
  §4.5 and Step 6 of §6.
* Reading sounds lazily / on-demand. Out of scope.

## 4. Design

### 4.1 Two layers, kept separate

There are two distinct things that today both happen to use raw ints:

| Layer | Today | After |
|---|---|---|
| **Persisted TC references** (in `*.cfg`) | integer indices | string names |
| **Runtime references** (in `Weapon`, `SObjectType`, code) | integer indices into `Common::sounds` | unchanged — still `int`, still indices |

Names are resolved to indices **once**, at TC load time. Runtime
hot paths keep their integer indices.

### 4.2 Resolving names to indices

Add to `Common` (src/game/common.hpp):

```cpp
// Returns the index of the named sound in `sounds`, or -1 if not
// present. -1 is the existing "no sound" sentinel.
int soundIndex(std::string_view name) const;
```

Implementation: linear scan over `sounds` by `name`. The vector is
small (~30 entries) and lookup happens only at TC load, so a hash
map is not justified.

**Crucial:** unlike the broken legacy behavior, **missing sounds do
not shift indices**. `Common::sounds[i]` always corresponds to
position `i` of `types.sounds` in `tc.cfg`. A missing/disabled WAV
leaves `sounds[i].sound == nullptr` and `Sfx::play` becomes a no-op
for that slot (it already handles this safely in `sfx_mixer_add`,
verify in step 0 — see §6).

### 4.3 Engine-side named hooks

The engine itself plays a fixed set of sounds (menu navigation,
reload, begin-round, worm spawn, ninjarope throw, etc.). Today
these are integer literals: 5 (`throw`), 14 (`bump`), 21
(`alive`), 22 (`begin`), 24 (`reloaded`), 25 (`moveup`), 26
(`movedown`), 27 (`select`). All hardcoded uses are listed in §5.

Introduce a new tc.cfg section, mirroring the existing
`LIERO_CDEFS` / `LIERO_SDEFS` / `LIERO_HDEFS` X-macro pattern in
`src/game/constants.hpp`:

```cpp
// Sound hooks the engine plays directly. Sourced from [sounds] in tc.cfg.
#define LIERO_SOUNDDEFS(_) \
    _(MenuMoveUp)     \
    _(MenuMoveDown)   \
    _(MenuSelect)     \
    _(Bump)           \
    _(Begin)          \
    _(Reloaded)       \
    _(Alive)          \
    _(NinjaropeThrow)
```

Generate `enum SOUND_DEF_T { SoundMenuMoveUp, ... MaxSound }` and
store the resolved indices in `Common`:

```cpp
int soundHook[SOUND_DEF_T::MaxSound]; // -1 if not configured
```

The TC config grows a `[sounds]` table:

```toml
[sounds]
MenuMoveUp     = "moveup"
MenuMoveDown   = "movedown"
MenuSelect     = "select"
Bump           = "bump"
Begin          = "begin"
Reloaded       = "reloaded"
Alive          = "alive"
NinjaropeThrow = "throw"
```

Loaded by `loadTcConfig` (src/game/common_model.hpp:488) into
`common.soundHook[i] = common.soundIndex(<configured-name>)`.
Missing entries default to `-1` (no sound, silently ignored at
play time).

Engine call sites become:

```cpp
sfx.play(common, common.soundHook[SoundMenuSelect]);
// or
game.soundPlayer->play(common.soundHook[SoundBump]);
```

### 4.4 String-typed fields in weapon / sobject configs

In `common_model.hpp`, change `launchSound` / `loopSound` /
`exploSound` / `startSound` to round-trip as strings, using the same
`objRefToStr` / `objRefFromStr` pattern already used for `splinterType`
etc. A new pair of helpers, parameterised on `Common::sounds`:

```cpp
inline std::string soundRefToStr(int idx, Common const& c) {
    if (idx < 0 || idx >= (int)c.sounds.size()) return "";
    return c.sounds[idx].name;
}
inline int soundRefFromStr(std::string const& s, Common const& c) {
    if (s.empty()) return -1;
    return c.soundIndex(s); // -1 if not found
}
```

Note the difference from `objRefFromStr`: it returns `0` for an unknown
non-empty string, which is wrong for sounds (would play sound 0
spuriously). Sounds must return `-1` on miss to preserve the "no sound"
contract.

After this change, the `bazooka.cfg` example becomes:

```
launchSound = "bazooka"
loopSound   = ""
exploSound  = ""
```

`numSounds` on `SObjectType` is preserved as an integer count; the
random pick `startSound + rand(numSounds)` semantics in
`src/game/sobject.cpp:22` stay identical. (Alternative considered:
list explicit sound names. Rejected — it changes the schema shape
more than necessary and the consecutive-range pattern is intentional
in the original Liero data.)

### 4.5 Unifying Sfx and SoundPlayer

The previous author wanted every play call to go through one
interface so the `>= 0` check lives in one place. They got stuck
because `sfx` is a file-scope global, but `SoundPlayer` is owned by
`Game`, and menu / front-end code runs outside of `Game`.

**Resolution:** unify them. There is one play path,
`SoundPlayer::play`, with the `>= 0` guard in the base class.
Three concrete implementations:

* `DefaultSoundPlayer` — plays through SDL via the mixer. Holds a
  `Common&`. Owns the audio device (today's `Sfx::init` / `deinit`
  responsibilities migrate here).
* `RecordSoundPlayer` — records into an offline mixer (existing).
* `NullSoundPlayer` — discards (existing).

The global `Sfx sfx` is **removed**. In its place, a single global
pointer `SoundPlayer* g_soundPlayer` is set during front-end init
(when the menu/Gfx layer constructs the default player) and
re-pointed by `Game` when a game is in progress (to whichever
player Game owns: default, record, or null). Menu code calls
`g_soundPlayer->play(...)`; in-game code keeps calling
`game.soundPlayer->play(...)` (they point at the same thing during
gameplay).

The base class becomes:

```cpp
struct SoundPlayer {
    virtual ~SoundPlayer() = default;

    void play(int sound, void* id = 0, int loops = 0) {
        if (sound >= 0) playImpl(sound, id, loops);
    }
    void play(SOUND_DEF_T hook, void* id = 0, int loops = 0) {
        play(common().soundHook[hook], id, loops);
    }

    virtual bool isPlaying(void* id) = 0;
    virtual void stop(void* id) = 0;

protected:
    virtual void playImpl(int sound, void* id, int loops) = 0;
    virtual Common& common() = 0;
};
```

Lifecycle:

* Audio device lives in `DefaultSoundPlayer` (init/deinit moved out
  of `Sfx`). Construction order: `Common` is loaded → front-end
  constructs `DefaultSoundPlayer(common)` → assigns
  `g_soundPlayer`.
* When `Game` is created with a non-default player (record/null),
  `Game`'s ctor saves the previous `g_soundPlayer` and installs
  its own; the dtor restores. This preserves recording/null
  semantics without leaking them into the menu layer.

This change is included in this work (Step 6 of §6); it is the
right time to do it because every menu call site is already being
touched in Step 4 to swap literals for `soundHook[]` lookups, and
in Step 5 to swap fields for names. Doing the unification next
collapses `sfx.play(common, X)` and `game.soundPlayer->play(X)` to
a single shape.

### 4.6 What about NObjects / SObjects?

Issue #44 muses about doing the same for NObjects/SObjects. These
are **already** referenced by name in cfg files (see
`splinterType = "particle__small_damage"`). The only remaining
indexed fields in their configs are sound fields, which §4.4 fixes.
No additional work needed here. Explicitly out of scope.

## 5. File-level inventory of changes

Sources of hardcoded sound indices and references that must be touched.

### 5.1 Hardcoded literal sound indices (engine code)

Replace each with `common.soundHook[Sound<Name>]`:

| File:line | Literal | Maps to | Hook |
|---|---|---|---|
| src/game/worm.cpp:195 | 14 | bump | Bump |
| src/game/worm.cpp:209 | 14 | bump | Bump |
| src/game/worm.cpp:357 | 24 | reloaded | Reloaded |
| src/game/worm.cpp:913 | 21 | alive | Alive |
| src/game/worm.cpp:962 | 24 | reloaded | Reloaded |
| src/game/worm.cpp:1120 | 5 | throw | NinjaropeThrow |
| src/game/game.cpp:562 | 22 | begin | Begin |
| src/game/weapsel.cpp:249,308 | 25 | moveup | MenuMoveUp |
| src/game/weapsel.cpp:272,296 | 26 | movedown | MenuMoveDown |
| src/game/weapsel.cpp:350 | 27 | select | MenuSelect |
| src/game/inputState.cpp:86 | 27 | select | MenuSelect |
| src/game/fileSelectorState.cpp:46 | 27 | select | MenuSelect |
| src/game/weaponMenuState.cpp:66,95 | 26 | movedown | MenuMoveDown |
| src/game/weaponMenuState.cpp:74,101 | 25 | moveup | MenuMoveUp |
| src/game/gfx.cpp:186 | 25/26 | move{up,down} | MenuMoveUp / MenuMoveDown |
| src/game/gfx.cpp:193,239 | 27 | select | MenuSelect |
| src/game/gfx.cpp:1133,1137 | 25/26 | move{up,down} | MenuMoveUp / MenuMoveDown |
| src/game/mainMenuState.cpp (many) | 25/26/27 | move/select | as above |
| src/game/rematchState.cpp:124,132,159,165,170 | 25/26/27 | move/select | as above |
| src/game/controller/networkController.cpp:285,292 | 25/26 | move{up,down} | MenuMoveUp / MenuMoveDown |
| src/game/menu/booleanSwitchBehavior.cpp:11,13,22 | 25/26/27 | move/select | as above |
| src/game/menu/integerBehavior.cpp:41 | 27 | select | MenuSelect |
| src/game/menu/enumBehavior.cpp:14,16,25 | 25/26/27 | move/select | as above |
| src/game/menu/fileSelector.hpp:278,287,294,301 | 25/26 | move{up,down} | MenuMoveUp / MenuMoveDown |

(Exact line numbers will drift; the grep used to produce this list is
`grep -rn 'sfx\.play\|soundPlayer->play' src/game`.)

### 5.2 Random-TC generator

`src/game/mainMenuState.cpp` around lines 516, 521, 526, 593 generates
weapons/sobjects with random sound indices for the "Random TC" mode.
These must pick a random *name* from `common.sounds`, then store the
index — or, simpler, just keep choosing a random index since this
codepath operates on already-loaded `Common`. No persistence change
needed if the random TC is never saved out, but verify.

### 5.3 Headers and types

* `src/game/constants.hpp` — add `LIERO_SOUNDDEFS` X-macro and the
  generated enum.
* `src/game/common.hpp` — add `soundIndex(name)` method and
  `soundHook[MaxSound]` array.
* `src/game/common.cpp` — implement `soundIndex`.
* `src/game/common_model.hpp` — convert weapon/sobject sound fields
  to string round-trip; load/save `[sounds]` table; populate
  `common.soundHook[]`.

### 5.4 SoundPlayer / Sfx unification (§4.5)

* `src/game/mixer/player.hpp` — non-virtual `play` wrapper that
  guards `>= 0` and dispatches to virtual `playImpl`; convenience
  overload taking `SOUND_DEF_T` that resolves via `Common`.
* `src/game/sfx.hpp` / `sfx.cpp` — **delete `Sfx`** and the global
  `sfx` instance. Move SDL audio device init/deinit into
  `DefaultSoundPlayer` (`src/game/mixer/player.cpp`).
* New global `SoundPlayer* g_soundPlayer` declared in
  `mixer/player.hpp`, defined in `mixer/player.cpp`. Push/pop
  semantics on `Game` construction/destruction.
* Every caller currently using `sfx.play(common, ...)` switches to
  `g_soundPlayer->play(...)`.

### 5.5 Data files to migrate (`data/TC/openliero/`)

* `tc.cfg` — add the `[sounds]` table.
* `weapons/*.cfg` — convert `launchSound` / `loopSound` / `exploSound`
  integer literals to string names (or `""`).
* `sobjects/*.cfg` — convert `startSound` integer literals to string
  names (or `""`).

A one-shot migration script (e.g. `tools/migrate_sound_refs.py`)
should read the existing `tc.cfg` sounds array, build the index→name
map, and rewrite the cfg files. Commit the regenerated files
alongside the code change.

### 5.6 tc_tool

* `src/tc_tool/common_exereader.cpp` (`loadSfx` around line 817) —
  unchanged in mechanics, but verify that disabled samples now
  produce a placeholder `SfxSample` (name preserved, `sound ==
  nullptr`) **instead of being skipped**. This is the root-cause fix
  for the "shifting index" bug. The slot must survive so name lookup
  stays stable.

### 5.7 Tests

* `src/tests/test_tc_load.cpp` — extend to assert
  `common.soundIndex("select") >= 0`, `common.soundHook[SoundMenuSelect]`
  matches that, and a missing sound returns `-1`.
* New test: load a synthetic TC where one sound is missing; assert
  that subsequent sounds keep their indices (regression for the
  shifting bug).
* `src/tests/test_determinism.cpp` — should still pass; if it
  fails, the conversion changed simulation behavior, which is a bug
  in this change.

## 6. Step-by-step plan (handover)

Every step below must end in a state where:

* the project builds with the same CMake presets used in CI;
* `ctest` passes for the full test binary;
* the change is small enough to land as a single reviewable PR.

Each step has an explicit **Tests** subsection listing the tests to
add or extend. A step is not "done" until its tests are committed
and green.

### Step 1 — Preserve disabled sound slots in tc_tool ✅ (landed)

**What.** Change `loadSfx` in `src/tc_tool/common_exereader.cpp`
(~line 817) so disabled / empty sample entries become a
placeholder `SfxSample{name, /*length=*/0}` with `sound == nullptr`
rather than being dropped. The vector stays dense in **slot count**
even if some slots have no audio.

**Why first.** This is the root-cause fix. After this step,
indices in legacy TCs no longer shift, even before any of the
naming work lands. Subsequent steps build on a stable index space.

**Tests.**
* New unit test in `src/tests/test_tc_load.cpp` that runs
  `loadSfx` against a fixture `.snd` blob with one disabled
  middle entry and asserts: vector length matches the original
  entry count; the disabled slot has `sound == nullptr` and the
  expected name; the entries **after** the gap have the same
  names as before (regression for the shift bug).
* Sanity-check `sfx_mixer_add` against a `nullptr` sound in a
  small new mixer test (or extend an existing one) to confirm a
  null sample is a silent no-op rather than a crash.

**As landed.** `loadSfx` already pushed back every slot, but the
shared `SfxSample(name, length)` constructor unconditionally called
`sfx_new_sound(length * 2)`, so a disabled (length-0) slot ended up
with a non-null `sound` pointing at a zero-sample buffer — playing
it would have indexed an empty `samples` vector. Fix: the
constructor now leaves `sound == nullptr` when `length == 0`, and
`sfx_mixer_add` early-returns on a null sound so callers can play a
disabled slot as a silent no-op. Stale `loadSfx` declaration in the
header was corrected to match the cpp signature.

The new regression test lives in
`src/tests/test_sfx_loader.cpp` (synthesises a 3-entry `.snd` blob
with a disabled middle slot, asserts vector length, names, and that
the disabled slot is null). To make it linkable, the `tc` static
library was split from `tctool`'s `main()` translation unit.

### Step 2 — Add `Common::soundIndex` ✅ (landed)

**What.** Add `int Common::soundIndex(std::string_view name) const`
in `common.hpp` / `common.cpp`. Linear scan, returns `-1` on miss.
No call sites change.

**Tests.**
* `src/tests/test_tc_load.cpp`: load the shipped TC, assert
  `soundIndex("select") >= 0`, `soundIndex("does_not_exist") == -1`,
  and that the returned index round-trips via
  `common.sounds[idx].name == "select"`.

**As landed.** `Common::soundIndex(std::string_view)` added in
`src/game/common.{hpp,cpp}` as a linear scan over `sounds` returning
`-1` on miss. `test_tc_load.cpp` extended with the three assertions
above against the shipped TC.

### Step 3 — Add `[sounds]` hooks table ✅ (landed)

**What.**
* Add `LIERO_SOUNDDEFS` macro and `SOUND_DEF_T` enum to
  `constants.hpp`.
* Add `int Common::soundHook[MaxSound]` (initialised to `-1`).
* Extend `loadTcConfig` / `saveTcConfig` in `common_model.hpp` to
  read/write a `[sounds]` TOML table, populating `soundHook[i]`
  via `Common::soundIndex`.
* Add the new `[sounds]` section to `data/TC/openliero/tc.cfg`
  with the existing hook → name mapping (see §4.3).

No engine call sites change yet — the array is populated but
unused.

**Tests.**
* Extend `src/tests/test_tc_load.cpp`: after load, assert
  `soundHook[SoundMenuSelect] == soundIndex("select")` and the
  same for each hook in `LIERO_SOUNDDEFS`.
* Extend `src/tests/test_toml_archive.cpp` (or add a new case):
  save → load round-trip produces an identical `soundHook[]`.
* Negative case: a `tc.cfg` whose `[sounds]` references a
  non-existent sound name resolves that hook to `-1` and produces
  a warning (assert via a captured `Console` sink if available,
  otherwise just assert the `-1`).

**As landed.** `LIERO_SOUNDDEFS` + `SOUND_DEF_T` enum added to
`constants.hpp`; `Common::soundHook[MaxSound]` default-initialised
to `-1`; `loadTcConfig` / `saveTcConfig` round-trip a `[sounds]`
TOML table via `Common::soundIndex`, with unknown names producing
a `Console::writeWarning` and resolving to `-1`. The shipped
`data/TC/openliero/tc.cfg` grew the `[sounds]` section. The hook
set was extended past the spec's initial six to include `Alive`
(worm respawn, sound 21) and `NinjaropeThrow` (jump-on-change
deploy, sound 5) so that Step 4 could replace every remaining
literal `play()` call site (originally folded in via amend).
Tests in `test_tc_load.cpp` cover positive resolution per hook,
the round-trip via the shipped TC, and the negative case
(unknown name → `-1`).

### Step 4 — Replace hardcoded integer literals in engine code ✅ (landed)

**What.** Mechanical replacement of every literal listed in §5.1
with `common.soundHook[Sound<Name>]`. Touch only call sites; no
header or schema change in this step.

**Tests.**
* No new unit tests (this step is mechanical and covered by the
  hook resolution tests in Step 3).
* Manual verification matrix, recorded in the PR description:
  menu up/down (`MenuMoveUp` / `MenuMoveDown`), menu select
  (`MenuSelect`), weapon fire/reload, round begin (`Begin`),
  worm bump against terrain (`Bump`), reloaded notification
  (`Reloaded`).
* CI: `test_determinism` must still pass.

**As landed.** Every literal sound index in §5.1, plus the three
`worm.cpp` literals (`913` = `21`/Alive, `962` = `24`/Reloaded,
`1120` = `5`/NinjaropeThrow) the original table missed, was
swapped for `common.soundHook[Sound<Name>]` (or
`common->soundHook[...]` / `gfx->common->soundHook[...]` /
`game.common->soundHook[...]` depending on what `Common` was in
scope). The `gfx.cpp:186` ternary became
`common.soundHook[dir > 0 ? SoundMenuMoveUp : SoundMenuMoveDown]`.
Random-TC generator in `mainMenuState.cpp` was left alone — it
operates on the in-memory `Common` and is never serialised.
After this step
`grep -rn 'soundPlayer->play(\s*[0-9]\+\|sfx\.play([^,]\+,\s*[0-9]\+' src/game`
returns no matches and all 95 tests pass.

### Step 5 — Name-typed sound fields in weapon / sobject configs

**What.**
* Add `soundRefToStr(idx, common)` / `soundRefFromStr(str, common)`
  helpers in `common_model.hpp` (returns `-1` on miss, distinct
  from `objRefFromStr`'s `0`).
* Convert `launchSound`, `loopSound`, `exploSound`, `startSound`
  in `saveWeaponConfig` / `loadWeaponConfig` /
  `saveSObjectConfig` / `loadSObjectConfig` to round-trip as
  strings via the new helpers.
* Write `tools/migrate_sound_refs.py` that reads `types.sounds`
  from `tc.cfg`, builds an index → name map, and rewrites every
  matching field in `data/TC/openliero/{weapons,sobjects}/*.cfg`.
  Run it; commit the regenerated configs in the same commit as
  the code change.

**Tests.**
* Extend `src/tests/test_tc_load.cpp`: after loading the
  migrated TC, assert specific known values, e.g. the bazooka's
  `launchSound == soundIndex("bazooka")` and a sobject's
  `startSound == soundIndex("exp2")`.
* Round-trip test in `test_toml_archive` / `test_cereal_types`:
  load a weapon cfg, save it back, diff the strings.
* Robustness test: hand-craft a weapon cfg whose `exploSound`
  references a name not in `types.sounds`; assert it resolves to
  `-1` and the loaded weapon plays no explosion sound (rather
  than the wrong one).
* `test_determinism` must still pass — the simulation must
  observe the same integer indices it did before this step.

### Step 6 — Unify Sfx and SoundPlayer

**What.** Implement §4.5:

1. Move SDL audio device ownership from `Sfx::init` / `Sfx::deinit`
   into `DefaultSoundPlayer` (`src/game/mixer/player.cpp`).
2. Add the non-virtual `SoundPlayer::play(int, ...)` wrapper that
   guards `>= 0` and dispatches to `playImpl`. Add the overload
   that takes `SOUND_DEF_T`.
3. Introduce `extern SoundPlayer* g_soundPlayer;` plus push/pop
   in `Game`'s ctor/dtor.
4. Convert every `sfx.play(common, X)` call site to
   `g_soundPlayer->play(X)` (or, where the hook overload reads
   nicer, `g_soundPlayer->play(SoundMenuSelect)`).
5. Delete the `Sfx` struct and the global `sfx` instance.
6. Remove the now-redundant `if(w.exploSound >= 0)` /
   `if(startSound >= 0)` guards at call sites — the wrapper owns
   the guard.

**Tests.**
* New `src/tests/test_sound_player.cpp` with a `RecordingPlayer`
  test double (or reuse `NullSoundPlayer` instrumented to count
  calls). Assert:
  * `play(-1)` is a no-op (does not reach `playImpl`).
  * `play(SoundMenuSelect)` resolves via `Common::soundHook` and
    calls `playImpl` with the right int.
  * Pushing a new `g_soundPlayer` on `Game` construction and
    restoring it on destruction works (build a `Game` in a
    scope, observe pointer, exit scope, observe pointer
    restored).
* Determinism: `test_determinism` and the replay tests must
  still pass; if they fail, the Game-owned player push/pop is
  wrong.

### Step 7 — Final regression test for the issue #44 scenario

**What.** Construct a TC fixture (in-tree, under
`src/tests/fixtures/` or similar) where one entry in
`types.sounds` corresponds to a missing WAV file on disk. Drive
the full load pipeline.

**Tests.**
* Assert that the missing slot survives in `Common::sounds` with
  `sound == nullptr` and the configured name.
* Assert that a sobject whose `startSound` points to a later
  sound still resolves to the **correct** sample, not a shifted
  one (this is the user-visible bug from issue #44).
* Assert that playing the missing slot (`g_soundPlayer->play(idx)`)
  is a silent no-op, not a crash.

This step closes the issue.

## 7. Acceptance criteria

* All existing tests pass; the new tests listed in §6 are
  committed and green.
* `grep -rn 'sfx\.play\|extern Sfx sfx' src/` returns nothing —
  the `Sfx` global is gone.
* `grep -rn 'soundPlayer->play(\s*[0-9]\+' src/game` and
  `grep -rn 'sfx\.play([^,]\+,\s*[0-9]\+' src/game` return no
  literal sound indices.
* `weapons/*.cfg` and `sobjects/*.cfg` under
  `data/TC/openliero/` contain **no integer sound fields** — only
  string names or `""`.
* The shipped `data/TC/openliero/` TC plays identically to before
  the change (manual A/B on the matrix from Step 4).
* A TC with a missing sound entry loads cleanly, no spurious
  sounds play, and indices of unrelated sounds are unchanged
  (asserted by the Step 7 regression test).
* `test_determinism` passes on master and the final branch with
  identical hashes for a fixed-seed replay.

## 8. Open questions for the implementer

1. Should `soundHook` entries that fail to resolve at TC load time
   be a hard error or a silent `-1`? **Recommendation:** silent
   `-1` with a `Console::writeWarning`, matching how missing TC
   resources are handled elsewhere.
2. Is there value in interning sound names (e.g. as `int` ids in a
   global string pool) for the menu hot path? **Recommendation:**
   no; the resolution happens at TC load, not per play.
3. Random-TC generator (§5.2) — does it ever serialize? If yes, it
   must emit names not indices. If no (in-memory only), no schema
   change is needed there. **Action:** confirm at implementation
   time by reading `mainMenuState.cpp` around the random-TC code.
4. tc_tool's `loadSfx` currently drops disabled slots. Step 0
   above recommends preserving them. Confirm no downstream code
   assumes the vector is dense.
