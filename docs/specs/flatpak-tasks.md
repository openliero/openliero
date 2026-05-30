# Tasks: Flatpak packaging

Spec: [flatpak.md](flatpak.md)

Tasks are ordered by dependency. Each task should leave the build green.

---

## Task 1 — PNG icon

- [ ] Extract the largest available PNG size from `packaging/icons/openliero.icns`
      and save to `packaging/icons/openliero-256.png` (use 256×256 or larger if
      present; rename to reflect actual size if different).
- [ ] In `CMakeLists.txt`, inside the existing `if(NOT WIN32)` icon/desktop block,
      add:
      ```cmake
      install(FILES packaging/icons/openliero-256.png
        RENAME io.github.openliero.openliero.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
      ```
- [ ] `cmake --install build/linux-x64 --config Release` places the PNG at
      `<prefix>/share/icons/hicolor/256x256/apps/io.github.openliero.openliero.png`.
- **Acceptance**: PNG file exists in the repo; `cmake --install` places it in the
  correct hicolor path.
- **Files**: `packaging/icons/openliero-256.png` (new), `CMakeLists.txt`.

---

## Task 2 — Metainfo icon reference

- [ ] Add `<icon type="stock">io.github.openliero.openliero</icon>` inside the
      `<component>` element of `packaging/openliero.metainfo.xml`.
- [ ] `appstreamcli validate packaging/openliero.metainfo.xml` still passes
      (pedantic note about releases is acceptable).
- **Acceptance**: validate passes; icon tag present with correct app ID.
- **Files**: `packaging/openliero.metainfo.xml`.

---

## Task 3 — Flatpak manifest

- [ ] Resolve exact git commit SHAs for bundled dependency modules:
      enet 2.6.5, libjuice 1.7.1, miniz, tomlplusplus, xxhash, cereal.
      SDL3 and SDL3_image are provided by the 25.08 runtime — do NOT bundle
      them. Document resolved SHAs as `commit:` fields in the manifest.
- [ ] Write `packaging/io.github.openliero.openliero.yml` with:
      - `app-id: io.github.openliero.openliero`
      - `runtime: org.freedesktop.Platform//25.08`
      - `sdk: org.freedesktop.Sdk//25.08`
      - `command: openliero`
      - `finish-args` as listed in the spec
      - One module per dependency, each with `buildsystem: cmake-ninja` (or
        `simple` for header-only libs)
      - openliero module last, `type: dir`, with `OPENLIERO_USE_VCPKG=OFF`,
        `OPENLIERO_BUILD_TCTOOL=ON`, `OPENLIERO_BUILD_TESTS=OFF`
- [ ] Confirm libjuice builds without the `dependencies.diff` patch when
      `-DUSE_NETTLE=OFF` is passed. Drop the patch from the module if so.
- **Acceptance**: manifest file exists and is valid YAML; `flatpak-builder
  --dry-run` (if available) does not error.
- **Files**: `packaging/io.github.openliero.openliero.yml` (new).

---

## Task 4 — Local build and smoke test

- [ ] Run `flatpak-builder --force-clean --user --install build-dir
      packaging/io.github.openliero.openliero.yml` to completion.
- [ ] Run `flatpak run io.github.openliero.openliero` — game window opens,
      audio plays, stock TC loads.
- [ ] Run `flatpak run --command=tctool io.github.openliero.openliero` — tctool
      prints usage without crashing.
- [ ] Verify save path: `~/.var/app/io.github.openliero.openliero/data/openliero/openliero/`
      is created after first run.
- [ ] Verify the game links correctly against the runtime's SDL3 3.2.22
      (the vcpkg baseline pins 3.4.8 — check for API differences). If the
      build fails, add an `sdl3` module pinned to 3.4.8 per spec open
      question 5.
- [ ] Fix any other build or runtime errors encountered.
- **Acceptance**: all four checks above pass.
- **Files**: manifest (fix-ups only), possibly CMakeLists.txt if install
  paths need adjustment.

---

## Done criteria

- All 4 tasks complete.
- Spec open questions resolved and spec updated in-place.
- Manifest committed to the repository under `packaging/`.
