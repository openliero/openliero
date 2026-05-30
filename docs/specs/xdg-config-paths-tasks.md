# Tasks: XDG-style config and data paths

Spec: [xdg-config-paths.md](xdg-config-paths.md)

Tasks are ordered by dependency. Each task should ship as its own commit
(or PR), keep `master` green, and leave the game launchable on Linux from
a build-tree run between tasks.

---

## Task 1 — `paths::` helpers  ✅ done (commit 0c7c2f2)

- [x] Add `namespace paths { FsNode userDataRoot(); FsNode systemDataRoot(); }`
      in `src/game/filesystem.hpp` / `.cpp`.
  - `userDataRoot()` wraps `SDL_GetPrefPath("openliero", "openliero")`,
    calls `create_directories` on the result, returns an `FsNode`.
  - `systemDataRoot()` resolves in order:
    1. `OPENLIERO_DATADIR` env var (runtime override for Flatpak/packagers/tests)
    2. `OPENLIERO_DATADIR` compile-time macro
    3. `SDL_GetBasePath()`
- **Acceptance**: helpers compile and link; unit test (Catch2) covers env-var
  override + fallback.
- **Verify**: `ctest --test-dir build/linux-x64` passes new tests.

---

## Task 2 — CMake compile-time `OPENLIERO_DATADIR`

- [ ] In `CMakeLists.txt`, when not Emscripten, define
      `OPENLIERO_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/openliero"` on the
      `openliero` (and `tctool`) targets via `target_compile_definitions`.
  - Use `GNUInstallDirs` (`include(GNUInstallDirs)`).
  - Leave the existing flat `install(DIRECTORY data/... DESTINATION .)`
    rules unchanged in this task — install-layout reorg is Task 7.
- **Acceptance**: Building with `-DCMAKE_INSTALL_PREFIX=/tmp/foo`
  bakes `/tmp/foo/share/openliero` into the binary.
  Verify with `strings build/linux-x64/openliero | grep '/share/openliero'`.
- **Files**: `CMakeLists.txt`.

---

## Task 3 — `paths::resolve(argc, argv)`

- [ ] Add `struct ResolvedPaths { FsNode configNode; FsNode userConfigNode;
      std::string configRootFlag; uint16_t portFlag; std::string positionalTc; };`
      and `ResolvedPaths paths::resolve(int argc, char* argv[])` in
      `filesystem.hpp/.cpp`.
- [ ] Implements the algorithm from the spec:
      `--config-root` → both nodes point at that path;
      else `portable.txt` next to `SDL_GetBasePath()` → both nodes point
      at `SDL_GetBasePath()`;
      else `userConfigNode = userDataRoot()` and
      `configNode = join(userDataRoot(), systemDataRoot())`.
- [ ] CLI flag parsing identical to today's `gameEntry.cpp` loop so both
      binaries can call it.
- **Acceptance**: Unit test sets up two temp dirs, asserts user shadows
  system on read, writer routes to user, `portable.txt` short-circuits,
  explicit `--config-root` overrides both.
- **Files**: `src/game/filesystem.hpp`, `src/game/filesystem.cpp`, `src/tests/test_paths.cpp`.

---

## Task 4 — Wire `paths::resolve` into gfx + gameEntry

- [ ] Add `FsNode userConfigNode` next to `configNode` in `gfx.hpp`; expose
      `getUserConfigNode()`.
- [ ] In `gameEntry.cpp`, replace the manual CLI loop and
      `gfx.setConfigPath(...)` calls with a single
      `auto r = paths::resolve(argc, argv); gfx.setConfigNodes(r.configNode, r.userConfigNode);`
      block. Keep the Emscripten branch.
- [ ] Settings load/save in `gameEntry.cpp` already write to
      `configNode / "Setups" / "liero.cfg"` — switch the **save** call to
      `userConfigNode`, leave **load** on `configNode`.
- **Acceptance**: Game launches from build tree; one round of play, settings
  round-trip across restart.
- **Files**: `src/game/gfx.hpp`, `src/game/gfx.cpp`, `src/game/gameEntry.cpp`.

---

## Task 5 — Migrate write sites to `userConfigNode`

Audit list — switch every save/write call from `gfx->getConfigNode()` to
`gfx->getUserConfigNode()`:

- [ ] `src/game/mainMenuState.cpp:297` (save Setups)
- [ ] `src/game/mainMenuState.cpp:358` (save Profiles)
- [ ] `src/game/controller/localController.cpp:294` (write replay)
- [ ] `src/game/controller/rollbackController.cpp:674,701` (write replay)
- [ ] Grep `getConfigNode\(\)` and inspect remaining sites.
- **Acceptance**: `chmod -w` on the data directory next to the binary;
  game still saves settings, custom setups, profiles, and replays
  into the user dir.
- **Files**: 2–3 controller / menu state files.

---

## Task 6 — tctool + videotool share `paths::resolve`

- [ ] In `src/tc_tool/tc_tool_main.cpp`, replace the inline CLI loop with
      `paths::resolve(...)` and write the converted TC under
      `r.userConfigNode / "TC" / tcName`. tctool's positional arg semantics
      differ from openliero's — handle at the call site.
- [ ] Audit `src/video_tool/tools_main.cpp` for any path that should respect
      the same rules; apply same treatment or document "no writes besides
      `--output`" in a code comment.
- **Acceptance**: `tctool /tmp/old-liero` with no flags writes to
  `$(SDL_GetPrefPath)/TC/old-liero/`; next `openliero` launch lists it.
  `--config-root` and `portable.txt` overrides behave correctly.
- **Files**: `src/tc_tool/tc_tool_main.cpp`, possibly `src/video_tool/tools_main.cpp`.

---

## Task 7 — CMake install layout reorg

- [ ] Replace `install(TARGETS openliero DESTINATION .)` and friends with
      `DESTINATION ${CMAKE_INSTALL_BINDIR}`.
- [ ] Replace `install(DIRECTORY data/... DESTINATION .)` with
      `DESTINATION ${CMAKE_INSTALL_DATADIR}/openliero`.
- [ ] macOS bundle: confirm whether stock data should land under
      `Contents/Resources/` (more idiomatic) or stay under
      `Contents/MacOS/`. Adjust `systemDataRoot()` if Resources is chosen.
      Resolves open question #3 in the spec.
- [ ] Windows MSIX: no change expected — verify only.
- **Acceptance**: `cmake --install build/linux-x64 --prefix /tmp/op` produces
  `/tmp/op/bin/openliero` and `/tmp/op/share/openliero/{TC,Setups,Profiles,Resources}/...`.
  Running from `$HOME` works; nothing under `/tmp/op` gains mtime.
- **Files**: `CMakeLists.txt`.

---

## Task 8 — Linux + Windows packaging artifacts

- [ ] Add `packaging/openliero.desktop` (Type=Application, Categories=Game;ActionGame;,
      Exec=openliero, Icon=openliero).
- [ ] Add `packaging/openliero.metainfo.xml` (AppStream metadata).
- [ ] Install both files under `${CMAKE_INSTALL_DATADIR}/applications` and
      `${CMAKE_INSTALL_DATADIR}/metainfo`.
- [ ] Append a "Where are my saves?" section to `dist/windows/INSTALL.md`
      explaining MSIX-vs-zip save location.
- [ ] Append a release-notes blurb telling upgrading users where to copy
      their old `Setups/`, `Profiles/`, `Replays/`, `TC/`.
- **Acceptance**: `cmake --install` places `.desktop` and metainfo in the
  right locations. `appstreamcli validate` passes.
- **Files**: `packaging/openliero.desktop`, `packaging/openliero.metainfo.xml`, `CMakeLists.txt`, `dist/windows/INSTALL.md`, `README.md`.

---

## Task 9 — Verification matrix

Run the full matrix from the spec. File defects as follow-up tasks rather
than expanding this one.

- [ ] Linux build-tree: launch from `build/linux-x64/`, confirm
      `liero.cfg` in `$XDG_DATA_HOME/openliero`.
- [ ] Linux installed prefix: install to `/tmp/op`, launch from `$HOME`.
- [ ] Linux `portable.txt`: drop file next to binary, confirm reads and
      writes both adjacent.
- [ ] Linux `--config-root /tmp/foo`: everything in `/tmp/foo`.
- [ ] macOS `.app` from `/Applications`, writes in
      `~/Library/Application Support/openliero`.
- [ ] Windows zip: extract, run, writes in `%APPDATA%\openliero\openliero\`.
- [ ] **Windows MSIX**: `Add-AppxPackage openliero.msix`, run, change setting,
      quit, relaunch, setting persists. No access-denied errors.
- [ ] Emscripten: build, serve, in-browser play works.
- [ ] tctool default-dir behaviour with new install layout.
- **Files**: none — verification only.

---

## Done criteria for the whole feature

- All 9 tasks land on master.
- Spec updated in-place when open questions resolve during implementation.
- Release notes mention the new save location and the upgrade hint.
