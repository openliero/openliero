# Plan: Arbitrary Map Sizes + Zooming Spectator Viewport + Configurable Videotool Resolution

**Complexity**: Large (originally 5 sequenced PRs; PRs 1–7 landed, PR 8 is the active work)

## Summary

Adds: (1) arbitrary map sizes up to 4096×4096 via a new sized on-disk level
format (legacy headerless 504×350 still loads), (2) a spectator viewport that
auto-zooms to keep both worms visible on large maps, and (3) a selectable output
resolution for the videotool. PRs 1–7 are **merged to master**. The remaining
work (**PR 8**) pushes the spectator frame time under the 14 ms budget on very
large (4K-class) windows — see the dedicated section at the bottom; everything
above it is background.

## Decisions (still load-bearing)

- **D2** — Maximum map size **4096×4096** (matches the net wire-transfer cap).
- **D3** — Spectator zoom via **render-to-scratch + downscale**: the world pass
  renders into a scratch bitmap, then is scaled into the spectator rect; HUD
  overlays draw on top.
- The simulation core is **size-agnostic** (`Level` stores layers as
  `std::vector`, `Resize(w,h)` is dynamic; collision, snapshots, net transfer,
  and `Viewport` all read `level.width/height`). Rendering/zoom never runs in
  `processFrame`, and the spectator view is local display only (not
  checksummed) — so **zoom math may use floats freely** without touching the
  fixed-point determinism contract.

## Status of merged work (PRs 1–7)

| PR | What shipped | Where the code lives |
|---|---|---|
| **PR 1** ([#103]) | Refactor: all hardcoded 504×350 read `level.width/height` (viewport ctors, AI arrays in `ai/dijkstra.hpp` + `ai/predictive_ai.hpp`, stats heatmaps in `stats_recorder.hpp`). No behavior change. | controllers, `ai/`, `stats_recorder.hpp` |
| **PR 2** | New `OLLEVEL2` sized on-disk format (8-byte magic + version + `width/height` u16 LE, then legacy body); legacy no-magic path unchanged. Tools (`lev_gen.py`, `lev_extract.py`, `gen_large_test.py`), 4096² generated-on-demand test level, `test_sized_level.cpp`, doc. | `level.cpp` (`Level::load`), `tools/`, `docs/modern-level-authoring.md` |
| **PR 3** | Random map size in MATCH SETUP (`random_map_width/height` settings, config v5, menu items, `GenerateRandom` resize). | `settings.hpp`, `cereal_types.hpp`, `gfx.{hpp,cpp}` |
| **PR 4** | Zooming spectator viewport: `ScaleDrawArea` CPU box-filter downscale; `SpectatorViewport::Draw` split into world pass → composite → HUD; zoom from worm bounding box; native-resolution spectator window. | `gfx/blit.{hpp,cpp}`, `spectatorviewport.{hpp,cpp}`, `gfx.cpp` |
| **PR 5** | Configurable videotool output resolution (`-w/-h`/`--res WxH`); dynamic sws scaler input. | `tools_main.cpp`, `replay_to_video.{hpp,cpp}`, `video_recorder.c` |
| **PR 6** ([#108]) | Rollback snapshot dirty-cell tracking so `SaveSnapshotFast` doesn't memcpy ~48 MB/frame on large maps. Dirty bitset in `Level`; `level.materials` dedup. Online-play-only. | `level.hpp` (`dirty_bits`/`dirty_list`), `fast_snapshot.hpp` |
| **PR 7** ([#114]) | Spectator rendering optimisation — **the direct predecessor to PR 8**. Detailed below. | `spectatorviewport.{hpp,cpp}`, `gfx.{hpp,cpp}`, `gfx/renderer.hpp` |

[#103]: https://github.com/openliero/openliero/pull/103
[#108]: https://github.com/openliero/openliero/pull/108
[#114]: https://github.com/openliero/openliero/pull/114

## PR 7 recap — the pipeline PR 8 builds on (merged, #114)

PR 7 replaced the CPU box-filter composite with a GPU scale and added a
downscaled world pass. The resulting **current** spectator present path (the
thing PR 8 optimises) is:

```
SpectatorViewport::Draw(game, single_screen_renderer, …)      spectatorviewport.cpp:102
  ├─ Fill(scratch_bmp, 0)                       full-scratch memset           :128
  ├─ World pass → scratch_bmp                                                 :130-649
  │     • zoom < 1  → DrawLevelScaled + BlitImageScaled at ~output res,
  │                    shadows/text/fire/laser/crosshair OMITTED (illegible)  :130-258
  │     • zoom >= 1 → 1:1 DrawLevel + shadow pass + sprite pass (byte-exact
  │                    legacy path; small maps unchanged)                     :259-649
  │     • all sprite/shadow loops are AABB frustum-culled (PR7 Task 2)
  ├─ GPU composite handoff (renderer.gpu_world_composite == true)             :658-675
  │     • stash scratch + used-rect + letterboxed dst-rect on the Renderer
  │     • FillTransparent(renderer.bmp)         full-overlay memset           :675
  └─ HUD overlay → renderer.bmp (native res, transparent bg)                  :704-842

Gfx::Flip()                                                   gfx.cpp:1116
  └─ if single_screen_renderer.gpu_world_src  → DrawSpectatorGpu(...)         :1128
        gpu_world_src is a strict ONE-SHOT (set per spectator draw, cleared
        every Flip) so menu/pause/direct-Flip frames fall back to CPU present.

Gfx::DrawSpectatorGpu(renderer)                               gfx.cpp:1065
  ├─ EnsureSpectatorWorldTexture(max_w, max_h)  grow-only STREAMING texture   :1067
  ├─ SDL_UpdateTexture(world, used_sub_rect, …) world DMA upload              :1078
  ├─ SDL_UpdateTexture(hud,   FULL window,   …) HUD overlay DMA upload        :1081
  ├─ SDL_SetTextureColorMod(both, fade)         reproduces ScaleDraw fade     :1089
  ├─ SDL_RenderClear (opaque black bars)                                      :1093
  ├─ SDL_RenderTexture(world, used → letterboxed dst, LINEAR)                 :1102
  ├─ SDL_RenderTexture(hud,   full → full)      BLEND over world              :1103
  ├─ SDL_RenderPresent                                                        :1104
  └─ SDL_SetTextureColorMod(both, 255)          restore neutral (bug 4f7a187) :1112
```

Relevant setup: the spectator renderer runs
`SDL_SetRenderLogicalPresentation(w, h, LETTERBOX)` with logical size **==**
window pixel size (`gfx.cpp` `OnWindowResize`, ~:405-414), so the
logical→physical transform is uniform and a manual letterboxed dst-rect lands
pixel-exact (proven by spike in PR7 Task 1a). `single_screen_renderer` renders
at the window size (`SetRenderResolution(w,h)`), and `render_res_x/y` on the
`Renderer` (`gfx/renderer.hpp:38-39`) is that size.

Two spectator-presentation bugs were fixed and are easy to re-break — keep them
in mind when touching this path:
- **`703af23`** — `gpu_world_src` made a strict one-shot (a stale handoff drove
  `DrawSpectatorGpu` against a resized menu layout → heap overread on pause).
- **`4f7a187`** — the fade `SDL_SetTextureColorMod` leaked onto the shared
  `sdl_spectator_texture`; the CPU present path never reset it → black spectator
  until resize. Always restore neutral colormod after a GPU present.

**PR 7 outcome:** spectator frame time improved **≈45 → ≈20 ms** (4096² level,
worms at max separation). Still **above the 14 ms budget at very large windows**
(≈20 ms at 3440×1440, max zoom-out). PR 8 closes that gap.

---

## PR 8 — Bound the spectator cost by a render-resolution cap (ACTIVE)

**Goal:** spectator frame time within the 14 ms budget at 4K-class windows
(3440×1440 and 3840×2160), 4096² level, worms at maximum separation (full
zoom-out — the worst case).

### Problem analysis (profiling-grounded; re-confirm before/after on real HW)

After PR 7 every remaining cost in the spectator path **scales with the window
pixel count**, not the map. At 3440×1440 each full-window ARGB buffer is
~19.8 MB. Per spectator frame, zoomed out:

| Step | Code | Cost driver |
|---|---|---|
| `Fill(scratch_bmp, 0)` | `spectatorviewport.cpp:128` | full-window memset (~20 MB) |
| Downscaled world CPU draw | `spectatorviewport.cpp:130-258` | ~5 Mpx terrain + sprites |
| `FillTransparent(renderer.bmp)` | `spectatorviewport.cpp:675` | full-window memset (~20 MB) |
| `SDL_UpdateTexture(world, used)` | `gfx.cpp:1078` | ~20 MB CPU→GPU DMA |
| `SDL_UpdateTexture(hud, full)` | `gfx.cpp:1081` | ~20 MB CPU→GPU DMA |
| clear + 2× `RenderTexture` + present | `gfx.cpp:1092-1104` | GPU, cheap |

So per frame: **two full-window memsets + two full-window uploads** (~80 MB CPU
memory traffic + ~40 MB DMA), all sized to the *window*. At max zoom-out the
spectator shows the whole 4096² map shrunk into the window — the source detail
is already sub-pixel, so **there is no visual benefit to rendering or uploading
at full 4K.** The window resolution, not the map, is the cost.

### Key insight that makes the fix cheap

`SpectatorViewport::Process` (`spectatorviewport.cpp:51-100`) computes zoom from
the **worm bounding box**, and the visible world region is
`kViewW = render_w / zoom` (≈ bbox width + margin). Both `render_w` and `zoom`
scale together, so **the visible region is bbox-driven and independent of the
render resolution.** Lowering the spectator's internal render resolution changes
only the *sampling* resolution, not what is on screen. HUD coordinates are
already `render_res`-relative, so they lay out in the reduced space and upscale
consistently.

### Approach (in priority order)

#### Task 1 — Cap the spectator internal render resolution (primary fix, low risk)

Decouple the spectator render resolution from the window size: render at
`min(window, cap)` preserving aspect, and let the **existing**
`SDL_SetRenderLogicalPresentation` upscale to the physical window (that
mechanism is already in place at `gfx.cpp` ~:407). This bounds **all five**
window-sized costs (both memsets, the world draw, both uploads) by a constant
cap instead of the window.

- **Cap value:** start with **1920×1080**, exposed as a hidden-menu setting
  (mirror an existing numeric setting — pattern: `IntegerBehavior`, `gfx.cpp`
  ~:1129; settings field + cereal nvp + `kConfigVersion` bump, `settings.hpp` /
  `cereal_types.hpp`). A single "max spectator render height" (derive width from
  window aspect) is simpler than two axes and probably sufficient. Default the
  cap so today's behavior is reproduced when window ≤ cap.
- **Where to apply:** the spectator renderer's `SetRenderResolution` and the
  paired `SDL_SetRenderLogicalPresentation` (both in `Gfx::OnWindowResize`,
  `gfx.cpp` ~:405-414) take `min(window, cap)` instead of the raw window size.
  `render_res_x/y` then carries the capped size; the world dst-rect math and HUD
  layout follow automatically. The physical present (logical→window) upscales on
  the GPU.
- **Expected:** under 14 ms on its own at 4K.
- **Cost / tradeoff:** HUD text is upscaled with the world and goes slightly
  soft. Acceptable for an overview window; if not, add Task 2.
- **Verify:** re-profile (Tracy zones already in place: `SpectatorViewport::Draw`,
  `Spectator::WorldPass::*`, `Spectator::Composite::GpuHandoff`,
  `Gfx::DrawSpectatorGpu`, `Gfx::Flip`) at 3440×1440 and 3840×2160, max
  zoom-out. Confirm the memsets/uploads shrank to the cap. Visual check: small
  maps (zoom ≥ 1, window ≤ cap) must be **byte-for-byte unchanged**. Smoke-launch
  under the dummy driver (the GPU path is guarded off there — must still not
  crash). `test_spectator_zoom` / `test_blit` stay green; add a unit case for the
  capped-resolution computation (pure helper, mirror `ComputeWorldPassScratch`
  in `test_spectator_zoom.cpp`).

#### Task 2 — Partial HUD upload + partial clear (optional, keeps HUD crisp)

Only needed if the soft HUD from Task 1 is unacceptable, or if Task 1 alone
leaves a remainder. Keep the HUD overlay at native resolution but stop touching
the whole buffer: track the HUD's dirty bands (bottom ~40 px strip; the banner
near `banner_y`; the `y=164` "Reloading" text — see the HUD block
`spectatorviewport.cpp:704-842`) and only `FillTransparent` + `SDL_UpdateTexture`
those sub-rects. Cuts ~40 MB/frame of HUD traffic to ~1 MB. Stacks cleanly on
Task 1 (crisp native HUD over a capped world). Watch: bands must cover the
previous frame's HUD extent too, so moving text (banner scroll) doesn't leave
stale pixels.

#### Task 3 — Render the world pass into a locked texture (optional, only if still over budget)

`SDL_LockTexture` the world texture, render the **downscaled** pass straight into
its pixels, unlock — removes the scratch→texture memcpy *and* the scratch memset.
Medium risk: locked texture memory is typically write-combined (slow reads), so
this is only safe because the downscaled pass omits shadows (the read-modify-write
path). The 1:1 path (zoom ≥ 1, small maps) does RMW shadows and must **not** use
the locked path. Reach for this last.

### Explicitly deferred / not pursued

- **SIMD `ScaleDrawArea`** (old PR7 Task 3): irrelevant to the GPU spectator path
  — only the videotool/CPU composite (`spectatorviewport.cpp:676-702`) still uses
  `ScaleDrawArea`. Leave it for an offline-render perf pass if ever needed.
- **Minimum-zoom cap** (old PR7 Task 4): Task 1 already bounds cost, so a zoom
  floor is now a pure UX/visibility choice, not a performance need.
- **Dirty-rectangle sharing with rollback:** declined in PR 7 — `Level`'s dirty
  tracking is cumulative/never-cleared and terrain-only; sprites move every frame
  and aren't tracked. The GPU composite makes terrain caching moot.

### Mergeable because

Purely display-side: does not touch simulation, net protocol, or on-disk format.
The cap is the only new user-visible surface (a hidden-menu setting + one config
version bump).

## Validation (every PR)

```bash
cmake --preset linux-x64 -DOPENLIERO_BUILD_TESTS=ON -DOPENLIERO_BUILD_VIDEOTOOL=ON
cmake --build build/linux-x64 --config Release
ctest --test-dir build/linux-x64 --build-config Release --output-on-failure
./build/linux-x64/Release/test_determinism && ./build/linux-x64/Release/test_rollback_correctness && ./build/linux-x64/Release/test_rollback_desync
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 8 ./install/linux-x64/bin/openliero
scripts/clang-format-diff.sh && scripts/clang-tidy-diff.sh build/linux-x64
```

For PR 8, additionally re-profile with Tracy on real hardware at 3440×1440 and
3840×2160, 4096² level, worms at max separation, and confirm frame time < 14 ms.

## Risks (open)

| Risk | Likelihood | Mitigation |
|---|---|---|
| PR8 Task 1: capped-res HUD text too soft | Medium | Default cap ≥ 1080p; add Task 2 (native-res partial HUD) if playtest dislikes it |
| PR8 Task 1: small-map / zoom≥1 path accidentally changed | Low | Cap is a no-op when window ≤ cap; assert byte-exact small-map output in test |
| PR8 Task 1: re-break the two PR7 present bugs (`703af23`, `4f7a187`) | Medium | Keep `gpu_world_src` one-shot + neutral-colormod-restore invariants; smoke-launch incl. pause/menu |
| PR8 Task 3: locked write-combined texture slow on the RMW (zoom≥1) path | Medium | Use locked path only for the shadow-omitting downscaled pass |
| Memory at 4096² (~117 MB layers + AI) | Medium | 4096 cap; fail loudly past it |

## Acceptance

- [x] PR1–PR6: see status table above (all merged).
- [x] PR7 ([#114]): spectator GPU composite + downscaled world pass + frustum
  cull; pause/start spectator bugs fixed; ≈45 → ≈20 ms. **Not yet within 14 ms**
  at very large windows.
- [ ] **PR8: spectator frame time < 14 ms at 3440×1440 and 3840×2160, 4096²,
  max zoom-out** — via render-resolution cap (Task 1; Tasks 2/3 as needed).
  Small-map (zoom ≥ 1) output unchanged. Determinism/rollback suites green.
