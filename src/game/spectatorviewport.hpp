#pragma once

#include "game.hpp"
#include "gfx/bitmap.hpp"
#include "math/rect.hpp"
#include "viewport.hpp"
#include "worm.hpp"

struct Renderer;

// Computes the spectator zoom factor that frames the worm bounding box while
// never zooming out past the whole-level fit. Pure (ints in, float out) so it
// can be unit-tested without a live Game/Renderer. Display-only: never touches
// the simulation, so floats are fine here.
float ComputeSpectatorZoom(int render_w, int render_h, int bbox_w, int bbox_h, int level_w,
                           int level_h);

// Centred, letterboxed destination rect (in window/logical pixels) for the
// spectator world composite: the scratch (scr_w×scr_h) scaled by `zoom`, capped
// to the render surface so it never overhangs. Pure so the CPU composite and
// the GPU composite share one source of truth and it can be unit-tested.
struct SpectatorDstRect {
  int x, y, w, h;
};
SpectatorDstRect ComputeSpectatorDstRect(int render_w, int render_h, int scr_w, int scr_h,
                                         float zoom);

// Render resolution of the spectator world pass (PR7 Task 1, downscaled pass).
// `scale` is the world→scratch factor: 1.0 when zoom ≥ 1 (small maps render
// 1:1, the GPU upscales) and `zoom` when zoomed out (the world is rendered at
// ~output resolution so its CPU cost and texture upload are bounded by the
// window, not the level area). `w`/`h` are the scratch size (the visible world
// region scaled by `scale`). Pure so it can be unit-tested.
struct WorldPassScratch {
  int w, h;
  float scale;
};
WorldPassScratch ComputeWorldPassScratch(int render_w, int render_h, float zoom, int level_w,
                                         int level_h);

// Capped spectator render resolution (PR8 Task 1). The spectator world pass,
// its memsets and its texture uploads all scale with the render surface, not
// the map; on very large (4K-class) windows that pushes the frame past budget
// while the zoomed-out world is already sub-pixel. This caps the *internal*
// render height to `cap_h` (deriving width from the window aspect) so all those
// costs are bounded by a constant; the existing SDL_SetRenderLogicalPresentation
// upscales the capped surface back to the physical window. A no-op (returns the
// window size unchanged) when `cap_h <= 0` (disabled) or `window_h <= cap_h`
// (small windows render at native res, byte-for-byte unchanged). Pure so it can
// be unit-tested.
struct CappedRenderResolution {
  int w, h;
};
CappedRenderResolution ComputeCappedRenderResolution(int window_w, int window_h, int cap_h);

struct SpectatorViewport : Viewport {
  explicit SpectatorViewport(Rect rect) : Viewport(rect, 0) {}

  void Draw(Game& game, Renderer& renderer, GameState state, bool is_replay) override;
  void Process(Game& game) override;

  // Reused scratch buffer for the world pass; sized to the visible world
  // region each frame and downscaled into the spectator rect.
  Bitmap scratch_bmp;
  // Current zoom factor (>1.0 = small level upscaled to fill window,
  // 1.0 = native, <1.0 = zoomed out). Display-only — never touches the
  // simulation and may use floats freely.
  float zoom{1.0F};
  // Render dimensions cached from the renderer at the start of each Draw()
  // call. Process() reads these so it stays consistent with the current
  // renderer resolution without needing a Renderer& parameter.
  int render_w{640};
  int render_h{400};
};
