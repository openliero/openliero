# Spec: Flatpak packaging

## Objective

Package OpenLiero as a Flatpak for local distribution and self-hosting.
The Flatpak is an installable sandbox that gives Linux users a one-command
install without requiring system libraries.

**Not** targeting Flathub submission in this iteration — the goal is a
working, self-hostable manifest that can be built with `flatpak-builder`
and installed locally.

### Users

- Linux users who prefer Flatpak over raw tarballs.
- Distro packagers who want a reference for building a proper package.

### Success criteria

1. `flatpak-builder --force-clean build-dir packaging/io.github.openliero.openliero.yml`
   completes without error.
2. `flatpak run io.github.openliero.openliero` launches the game.
3. `flatpak run --command=tctool io.github.openliero.openliero` runs tctool.
4. Saves and settings land in `~/.var/app/io.github.openliero.openliero/data/openliero/openliero/`
   (SDL_GetPrefPath inside the Flatpak sandbox resolves there automatically).
5. Online multiplayer connects successfully (network share permitted).
6. `appstreamcli validate packaging/openliero.metainfo.xml` still passes.

## Tech Stack

- Flatpak / flatpak-builder
- Runtime: `org.freedesktop.Platform//24.08`
- SDK: `org.freedesktop.Sdk//24.08`
- Build system for game: CMake (existing), with `-DOPENLIERO_USE_VCPKG=OFF`
- All C/C++ deps compiled as Flatpak modules (no vcpkg in sandbox)

## Commands

```
Build Flatpak:    flatpak-builder --force-clean build-dir \
                      packaging/io.github.openliero.openliero.yml

Install locally:  flatpak-builder --force-clean --install --user build-dir \
                      packaging/io.github.openliero.openliero.yml

Run game:         flatpak run io.github.openliero.openliero
Run tctool:       flatpak run --command=tctool io.github.openliero.openliero

Extract icon:     python3 -c "
                    from PIL import Image
                    img = Image.open('packaging/icons/openliero.icns')
                    img.save('packaging/icons/openliero-256.png')
                  "
                  # or with ImageMagick:
                  convert 'packaging/icons/openliero.icns[8]' \
                      packaging/icons/openliero-256.png

Validate metainfo: appstreamcli validate packaging/openliero.metainfo.xml
```

## Project Structure

```
packaging/
  io.github.openliero.openliero.yml  → Flatpak manifest (new)
  openliero.desktop                  → already exists
  openliero.metainfo.xml             → already exists; may need <icon> tag
  icons/
    openliero.icns                   → existing macOS icon (source for PNG)
    openliero.ico                    → existing Windows icon
    openliero-256.png                → new: extracted from .icns for Linux
```

CMakeLists.txt gains one new install rule:
```cmake
install(FILES packaging/icons/openliero-256.png
  RENAME io.github.openliero.openliero.png
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
  COMPONENT game)
```
(Only installed on non-WIN32, same guard as .desktop and .metainfo.)

## Flatpak manifest overview

App ID: `io.github.openliero.openliero`

### Sandbox permissions (`finish-args`)

```yaml
finish-args:
  - --share=ipc          # X11 shared memory (faster rendering)
  - --share=network      # online multiplayer
  - --socket=wayland     # Wayland display
  - --socket=fallback-x11
  - --socket=pulseaudio  # audio (SDL3 also supports PipeWire via PA compat)
  - --device=all         # joysticks/controllers + DRI (OpenGL/Vulkan)
```

### Module dependency order

SDL3 and SDL3_image are **not** in the freedesktop SDK 24.08 and must be
compiled as modules. All other deps are bundled for version-pin consistency.

| Module | Version | Source | Notes |
|---|---|---|---|
| `sdl3` | 3.4.8 | github.com/libsdl-org/SDL | cmake-ninja |
| `sdl3-image` | pinned tag | github.com/libsdl-org/SDL_image | depends on sdl3 |
| `enet` | 2.6.5 | github.com/zpl-c/enet | cmake-ninja; custom port in repo |
| `libjuice` | 1.7.1 | github.com/paullouisageneau/libjuice | cmake-ninja; `-DNO_TESTS=ON -DUSE_NETTLE=OFF` |
| `miniz` | pinned tag | github.com/richgel999/miniz | cmake-ninja |
| `tomlplusplus` | pinned tag | github.com/marzer/tomlplusplus | header-only; cmake install |
| `xxhash` | pinned tag | github.com/Cyan4973/xxHash | cmake-ninja |
| `cereal` | pinned tag | github.com/USCiLab/cereal | header-only; cmake install |
| `openliero` | local | `type: dir` pointing at repo root | last module |

### openliero module config-opts

```yaml
- -DOPENLIERO_USE_VCPKG=OFF
- -DOPENLIERO_BUILD_TCTOOL=ON
- -DOPENLIERO_BUILD_TESTS=OFF
- -DOPENLIERO_BUILD_VIDEOTOOL=OFF
- -DCMAKE_BUILD_TYPE=Release
```

`OPENLIERO_USE_VCPKG=OFF` skips the toolchain injection; `find_package` then
resolves against `/app` (the Flatpak prefix) where the modules above installed.

## Code style

The manifest uses **YAML** format (`.yml`), matching convention for
flatpak-builder. Modules are listed from deepest dependency to the app itself.
Each module pins an exact git commit SHA (not a tag) to guarantee reproducible
builds. Obtain SHAs during implementation:

```bash
git ls-remote https://github.com/libsdl-org/SDL refs/tags/release-3.4.8
```

## Testing Strategy

Manual verification only (no automated CI for Flatpak in this iteration):

1. Build with `flatpak-builder --force-clean --user --install build-dir manifest.yml`
2. Run game and verify it launches, audio works, joystick input works.
3. Run in offline mode to confirm stock TC loads from `/app/share/openliero/`.
4. Run online to confirm network socket is open.
5. Check save path: `ls ~/.var/app/io.github.openliero.openliero/data/`

## Boundaries

**Always**
- Pin every module to an exact commit SHA — never `branch: main`.
- Test that `appstreamcli validate` still passes after metainfo changes.
- Guard the PNG icon install under `if(NOT WIN32)` same as .desktop.

**Ask first**
- Enabling any additional Flatpak permissions beyond those listed above.
- Splitting tctool into its own Flatpak extension.
- Adding a CI job for Flatpak builds (slow; needs self-hosted runner or
  large GitHub runner).

**Never**
- Pass `--allow-emulated-io` or `--filesystem=host` — keep the sandbox.
- Use `type: archive` with an unversioned URL — always pin the SHA.

## Open Questions

1. **SDL3_image version**: Determine the pinned version from the vcpkg
   baseline (run `git show HEAD:versions/s-/sdl3-image.json` in the vcpkg
   submodule).
2. **libjuice patch**: The overlay port has a `dependencies.diff` that
   replaces `find_package(Nettle)` with raw paths. Since we're passing
   `-DUSE_NETTLE=OFF`, the patch should be unnecessary — confirm during
   implementation that libjuice builds without it.
3. **Commit SHAs**: All module sources use `type: git` with a `tag:` field
   as a human-readable hint, but implementations should also include the
   `commit:` field once resolved. Resolve all SHAs during Task 3.
4. **Icon quality**: Verify the extracted PNG at 256×256 looks correct.
   The `.icns` may also contain a 512×512 or 1024×1024 size — use the
   largest available and install all common sizes if present.
