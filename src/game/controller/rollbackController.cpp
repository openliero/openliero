#include "rollbackController.hpp"

#include "../gfx.hpp"
#include "../mixer/player.hpp"
#include "../viewport.hpp"
#include "../spectatorviewport.hpp"

#include <cstring>
#include <miniz.h>

RollbackController::RollbackController(
    std::shared_ptr<Common> common,
    std::shared_ptr<Settings> settings,
    int localPlayerIdx)
    : game(common, settings,
            std::shared_ptr<SoundPlayer>(new DefaultSoundPlayer(*common)))
    , localIdx(localPlayerIdx)
    , remoteIdx(localPlayerIdx ^ 1)
    , state(StateInitial)
    , fadeValue(0)
    , goingToMenu(false)
    , simFrame(0)
    , inputDelay(3)
    , lastSentFrame(UINT32_MAX)
    , rollbackBufferPrepared_(false)
    , confirmedSimFrame_(-1)
    , lastRemoteInput_(0)
{
  localPrevInput = 0;
  remotePrevInput = 0;
  localHeldFrames.fill(0);
  remoteHeldFrames.fill(0);
  skipWeaponSelection = false;
  levelPreloaded = false;
  localPaused_ = false;
  remotePaused_ = false;

  pauseMenu_.init(true);
  pauseMenu_.addItem(MenuItem(7, 6, "RESUME", 0));
  pauseMenu_.addItem(MenuItem(7, 6, "END MATCH", 2));
  pauseMenu_.addItem(MenuItem(7, 6, "DISCONNECT", 1));

  localInputs.fill(0);
  remoteInputs.fill(0);
  remoteInputReady.fill(false);

  for (int idx = 0; idx < 2; ++idx) {
    auto worm = std::make_shared<Worm>();
    worm->settings = (idx == localIdx)
        ? settings->wormSettings[Settings::NetworkPlayerIdx]
        : settings->wormSettings[idx];
    worm->health = worm->settings->health;
    worm->index = idx;
    worm->statsX = idx == 0 ? 0 : 218;
    game.addWorm(worm);
  }

  game.addViewport(
      new Viewport(Rect(0, 0, 158, 158), 0, 504, 350));
  game.addViewport(
      new Viewport(Rect(160, 0, 158 + 160, 158), 1, 504, 350));
  game.addSpectatorViewport(
      new SpectatorViewport(Rect(0, 0, 504 + 68, 350), 504, 350));
}

RollbackController::~RollbackController() {}

void RollbackController::loadLevelFromData(const std::vector<uint8_t>& data) {
  if (data.size() < 5)
    return;

  bool isCompressed = (data[0] != 0);
  uint32_t rawSize;
  std::memcpy(&rawSize, data.data() + 1, 4);

  static constexpr uint32_t MAX_RAW_SIZE = 10 * 1024 * 1024;
  if (rawSize > MAX_RAW_SIZE)
    return;

  std::vector<uint8_t> raw;
  if (isCompressed) {
    raw.resize(rawSize);
    mz_ulong destLen = rawSize;
    int status = mz_uncompress(raw.data(), &destLen, data.data() + 5,
                               static_cast<mz_ulong>(data.size() - 5));
    if (status != MZ_OK)
      return;
  } else {
    raw.assign(data.begin() + 5, data.end());
  }

  if (raw.size() < 8)
    return;

  uint16_t w, h;
  std::memcpy(&w, raw.data(), 2);
  std::memcpy(&h, raw.data() + 2, 2);

  if (w == 0 || h == 0 || w > 4096 || h > 4096)
    return;

  uint32_t randStateLen;
  std::memcpy(&randStateLen, raw.data() + 4, 4);

  if (randStateLen > 64 * 1024)
    return;
  if (raw.size() < 8 + randStateLen + 4)
    return;

  std::string randState(reinterpret_cast<const char*>(raw.data() + 8),
                        randStateLen);
  uint32_t randLast;
  std::memcpy(&randLast, raw.data() + 8 + randStateLen, 4);

  size_t pixelsOffset = 8 + randStateLen + 4;
  size_t pixelDataSize = static_cast<size_t>(w) * h;
  if (raw.size() < pixelsOffset + pixelDataSize + 768)
    return;

  game.level.resize(w, h);
  Common& common = *game.common;

  const uint8_t* pixels = raw.data() + pixelsOffset;
  for (size_t i = 0; i < pixelDataSize; ++i) {
    game.level.data[i] = pixels[i];
    game.level.materials[i] = common.materials[pixels[i]];
  }

  const uint8_t* palData = raw.data() + pixelsOffset + pixelDataSize;
  for (int i = 0; i < 256; ++i) {
    game.level.origpal.entries[i].r = palData[i * 3 + 0];
    game.level.origpal.entries[i].g = palData[i * 3 + 1];
    game.level.origpal.entries[i].b = palData[i * 3 + 2];
  }

  game.rand.deserialize(randState);
  game.rand.last = randLast;

  levelPreloaded = true;
}

void RollbackController::setInputCallbacks(InputBatchSendCallback send,
                                           InputRecvCallback recv) {
  sendInputBatch = std::move(send);
  recvInput = std::move(recv);
}

void RollbackController::sendInputWindow(uint32_t newestFrame,
                                         uint32_t localFrame) {
  if (!sendInputBatch) return;
  constexpr uint8_t K = static_cast<uint8_t>(rollback::kMaxRollback + 1);
  uint8_t count;
  uint32_t baseFrame;
  if (newestFrame + 1u < K) {
    count = static_cast<uint8_t>(newestFrame + 1u);
    baseFrame = 0;
  } else {
    count = K;
    baseFrame = newestFrame - (K - 1u);
  }
  std::array<uint8_t, K> window{};
  for (uint8_t i = 0; i < count; ++i) {
    window[i] = localInputs[(baseFrame + i) % INPUT_BUFFER_SIZE];
  }
  sendInputBatch(baseFrame, count, window.data(), localFrame);
}

void RollbackController::injectRemoteBatch(uint32_t baseFrame, uint8_t count,
                                           uint8_t const* inputs,
                                           uint32_t remoteLocalFrame) {
  for (uint8_t i = 0; i < count; ++i) {
    injectRemoteInput(baseFrame + i, inputs[i]);
  }
  // Monotonic update — out-of-order arrival of a stale packet must not
  // pull our knowledge of the remote's progress backwards, since the
  // frame-advantage stall reads this as "remote is at least here".
  int32_t f = static_cast<int32_t>(remoteLocalFrame);
  if (f > lastKnownRemoteFrame_) lastKnownRemoteFrame_ = f;
}

void RollbackController::injectRemoteInput(uint32_t frame, uint8_t input) {
  // Step 7.5: drop frames already confirmed. Redundant batch packets
  // routinely overlap the confirmation boundary — the leading entries
  // describe frames we've already advanced past, and re-injecting them
  // would re-set remoteInputReady on a ring slot whose frame number
  // wraps around into the live rollback window.
  if (static_cast<int32_t>(frame) <= confirmedSimFrame_) return;
  uint32_t slot = frame % INPUT_BUFFER_SIZE;
  remoteInputs[slot] = input;
  remoteInputReady[slot] = true;
}

void RollbackController::onKey(int key, bool keyState) {
  Worm::Control control;
  Worm* worm = game.worms[localIdx].get();
  bool found = false;

  if (worm->settings->inputDevice == WormSettingsExtensions::InputKeyboard) {
    uint32_t* controls = game.settings->extensions
        ? worm->settings->controlsEx : worm->settings->controls;
    std::size_t maxControl = game.settings->extensions
        ? WormSettings::MaxControlEx : WormSettings::MaxControl;
    for (std::size_t c = 0; c < maxControl; ++c) {
      if (controls[c] == static_cast<uint32_t>(key)) {
        control = static_cast<Worm::Control>(c);
        found = true;
        break;
      }
    }
  }

  if (found) {
    worm->cleanControlStates.set(control, keyState);

    if (control < Worm::MaxControl) {
      localControlState.set(control, keyState);
    }

    if (worm->cleanControlStates[WormSettings::Dig]) {
      localControlState.set(Worm::Left, true);
      localControlState.set(Worm::Right, true);
    } else {
      if (!worm->cleanControlStates[Worm::Left])
        localControlState.set(Worm::Left, false);
      if (!worm->cleanControlStates[Worm::Right])
        localControlState.set(Worm::Right, false);
    }
  }

  if (key == DkEscape && keyState) {
    if (localPaused_) {
      localPaused_ = false;
      if (onLocalResume) onLocalResume();
    } else if (remotePaused_ && !goingToMenu) {
      remotePaused_ = false;
      fadeValue = 0;
      goingToMenu = true;
    } else if (!goingToMenu) {
      localPaused_ = true;
      pauseMenu_.moveToFirstVisible();
      if (onLocalPause) onLocalPause();
    }
  }
}

void RollbackController::unfocus() {
  if (state == StateWeaponSelection && ws)
    ws->unfocus();
  localPaused_ = false;
  remotePaused_ = false;
}

void RollbackController::focus() {
  if (state == StateGameEnded) {
    goingToMenu = true;
    fadeValue = 0;
    return;
  }
  if (state == StateWeaponSelection)
    ws->focus();
  if (state == StateInitial) {
    if (!levelPreloaded)
      game.level.generateFromSettings(*game.common, *game.settings, game.rand);

    if (skipWeaponSelection) {
      for (auto const& w : game.worms)
        w->initWeapons(game);
      for (auto const& w : game.worms)
        w->lives = game.settings->lives;
      game.startGame();
      game.resetWorms();
      state = StateGame;
    } else {
      state = StateWeaponSelection;

      for (auto const& w : game.worms) {
        w->settings->controller = 0;
      }

      ws = std::make_unique<WeaponSelection>(game);
    }
  }
  game.focus(gfx.playRenderer);
  game.focus(gfx.singleScreenRenderer);
  goingToMenu = false;
  fadeValue = 0;

  // Size the rollback ring buffer once the level (and therefore the
  // GameSnapshot vector sizes) are known. Idempotent across re-focus.
  if (!rollbackBufferPrepared_) {
    rollbackBuffer_.prepare(game);
    rollbackBufferPrepared_ = true;
  }
}

bool RollbackController::process() {
  if (isPaused()) {
    if (fadeValue < 33)
      fadeValue += 1;

    if (localPaused_) {
      if (gfx.testSDLKeyOnce(SDL_SCANCODE_UP)
       || gfx.testControlOnce(WormSettingsExtensions::Up)
       || gfx.testGamepadDirOnce(SDL_GAMEPAD_BUTTON_DPAD_UP)) {
        g_soundPlayer->play(game.common->soundHook[SoundMenuMoveDown]);
        pauseMenu_.movement(-1);
      }

      if (gfx.testSDLKeyOnce(SDL_SCANCODE_DOWN)
       || gfx.testControlOnce(WormSettingsExtensions::Down)
       || gfx.testGamepadDirOnce(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
        g_soundPlayer->play(game.common->soundHook[SoundMenuMoveUp]);
        pauseMenu_.movement(1);
      }

      if (gfx.testSDLKeyOnce(SDL_SCANCODE_RETURN)
       || gfx.testSDLKeyOnce(SDL_SCANCODE_KP_ENTER)
       || gfx.testControlOnce(WormSettingsExtensions::Fire)
       || gfx.testGamepadButtonOnce(SDL_GAMEPAD_BUTTON_SOUTH)) {
        int sel = pauseMenu_.selectedId();
        if (sel == 0) {
          localPaused_ = false;
          if (onLocalResume) onLocalResume();
        } else if (sel == 2) {
          localPaused_ = false;
          if (onLocalResume) onLocalResume();
          if (onEndMatch) onEndMatch();
          endMatch();
        } else {
          localPaused_ = false;
          fadeValue = 0;
          goingToMenu = true;
        }
      }
    }

    return true;
  }

  if (state == StateWeaponSelection) {
    advanceWeaponSelection();
  } else if (state == StateGame || state == StateGameEnded) {
    advanceSimulation();
  }

  if (goingToMenu) {
    if (fadeValue > 0)
      fadeValue -= 1;
    else {
      if (state == StateGameEnded) {
        game.statsRecorder->finish(game);
      }
      return false;
    }
  } else {
    if (fadeValue < 33)
      fadeValue += 1;
  }

  return true;
}

void RollbackController::advanceWeaponSelection() {
  uint32_t inputFrame = simFrame + inputDelay;
  if (inputFrame != lastSentFrame) {
    uint32_t slot = inputFrame % INPUT_BUFFER_SIZE;
    localInputs[slot] = localControlState.pack() & 0x7f;
    lastSentFrame = inputFrame;
  }

  // Redundant window send — see advanceSimulation for the rationale; the
  // weapon-selection path is lockstep (Step 11 plan) and won't roll back,
  // but it shares the controller's transport and wire format.
  sendInputWindow(inputFrame, simFrame);

  if (recvInput) {
    int result = recvInput(simFrame);
    if (result >= 0) {
      injectRemoteInput(simFrame, static_cast<uint8_t>(result));
    }
  }

  uint32_t currentSlot = simFrame % INPUT_BUFFER_SIZE;
  if (!remoteInputReady[currentSlot]) {
    return;
  }

  uint8_t curLocal = localInputs[currentSlot];
  uint8_t curRemote = remoteInputs[currentSlot];
  uint8_t risingLocal = curLocal & ~localPrevInput;
  uint8_t risingRemote = curRemote & ~remotePrevInput;
  uint8_t releasedLocal = localPrevInput & ~curLocal;
  uint8_t releasedRemote = remotePrevInput & ~curRemote;

  game.worms[localIdx]->controlStates.istate |= risingLocal;
  game.worms[remoteIdx]->controlStates.istate |= risingRemote;
  game.worms[localIdx]->controlStates.istate &= ~releasedLocal;
  game.worms[remoteIdx]->controlStates.istate &= ~releasedRemote;

  for (int bit = 0; bit < 7; ++bit) {
    uint8_t mask = 1 << bit;
    if (risingLocal & mask) {
      localHeldFrames[bit] = 0;
    } else if (curLocal & mask) {
      ++localHeldFrames[bit];
      if (localHeldFrames[bit] >= KEY_REPEAT_INITIAL &&
          (localHeldFrames[bit] - KEY_REPEAT_INITIAL) % KEY_REPEAT_INTERVAL == 0) {
        game.worms[localIdx]->controlStates.istate |= mask;
      }
    } else {
      localHeldFrames[bit] = 0;
    }
    if (risingRemote & mask) {
      remoteHeldFrames[bit] = 0;
    } else if (curRemote & mask) {
      ++remoteHeldFrames[bit];
      if (remoteHeldFrames[bit] >= KEY_REPEAT_INITIAL &&
          (remoteHeldFrames[bit] - KEY_REPEAT_INITIAL) % KEY_REPEAT_INTERVAL == 0) {
        game.worms[remoteIdx]->controlStates.istate |= mask;
      }
    } else {
      remoteHeldFrames[bit] = 0;
    }
  }

  localPrevInput = curLocal;
  remotePrevInput = curRemote;

  remoteInputReady[currentSlot] = false;

  if (ws->processFrame()) {
    ws->finalize();
    ws.reset();

    localPrevInput = 0;
    remotePrevInput = 0;
    localHeldFrames.fill(0);
    remoteHeldFrames.fill(0);

    for (auto const& w : game.worms) {
      w->lives = game.settings->lives;
    }
    game.startGame();
    game.resetWorms();
    state = StateGame;

    // Game state vectors are now fully sized — make sure the rollback
    // buffer's snapshots track them. Safe even if already prepared.
    rollbackBuffer_.prepare(game);
    rollbackBufferPrepared_ = true;
  }

  ++simFrame;
}

void RollbackController::advanceSimulation() {
  uint32_t inputFrame = simFrame + inputDelay;
  if (inputFrame != lastSentFrame) {
    uint32_t slot = inputFrame % INPUT_BUFFER_SIZE;
    localInputs[slot] = localControlState.pack() & 0x7f;
    lastSentFrame = inputFrame;
  }

  // Step 7.5: emit the last K = kMaxRollback + 1 local inputs as a
  // redundant batch every tick, regardless of whether inputFrame
  // changed. Under a lossy unreliable-sequenced channel the next batch
  // covers any single dropped packet without a retransmit round-trip,
  // which is what lets rollback tolerate higher RTT than lockstep.
  // Sending continues even when the controller is stalled below so the
  // remote peer still receives our latest known inputs and can promote
  // out of its own stall.
  sendInputWindow(inputFrame, simFrame);

  if (recvInput) {
    int result = recvInput(simFrame);
    if (result >= 0) {
      injectRemoteInput(simFrame, static_cast<uint8_t>(result));
    }
  }

  // Step 7 — promote past predicted frames whose real input has now
  // arrived, and trigger a rollback when a prediction turns out to be
  // wrong. The loop walks confirmedSimFrame_+1 .. simFrame-1 in order:
  //   - Match (or non-Predicted slot): upgrade the slot to Confirmed
  //     and advance confirmedSimFrame_/lastRemoteInput_.
  //   - Mismatch: stop at the first wrong frame; the resim below
  //     reloads its predecessor's snapshot and re-runs forward.
  int32_t rollbackTo = -1;
  while (confirmedSimFrame_ + 1 < static_cast<int32_t>(simFrame)) {
    int32_t F = confirmedSimFrame_ + 1;
    uint32_t s = static_cast<uint32_t>(F) % INPUT_BUFFER_SIZE;
    if (!remoteInputReady[s]) break;
    uint8_t real = remoteInputs[s];

    auto* slot = rollbackBuffer_.find(F);
    bool match = true;
    if (slot && slot->remoteState == rollback::RemoteState::Predicted) {
      // slot stores inputs by worm index: localInput=worm0, remoteInput=worm1.
      // The misprediction question is about the *other* peer's input.
      uint8_t storedOther =
          (localIdx == 0) ? slot->remoteInput : slot->localInput;
      match = (storedOther == real);
    }

    if (!match) {
      rollbackTo = F - 1;
      break;
    }

    if (slot) slot->remoteState = rollback::RemoteState::Confirmed;
    lastRemoteInput_ = real;
    remoteInputReady[s] = false;
    confirmedSimFrame_ = F;
  }

  // Rollback resim — load the last known-good snapshot, then replay
  // every frame after it with the freshest input available. Speculative
  // for the whole window so previously-emitted sounds/stats don't
  // duplicate (Step 5's flag).
  if (rollbackTo >= 0) {
    ++rollbackCount_;
    auto* lastGood = rollbackBuffer_.find(rollbackTo);
    // Resident by construction: the stall guard caps simFrame - confirmedSimFrame_
    // at kMaxRollback, and the ring holds kMaxRollback+1 slots.
    game.loadSnapshotFast(lastGood->snapshot);

    uint8_t lastGoodWorm0 = lastGood->localInput;
    uint8_t lastGoodWorm1 = lastGood->remoteInput;
    localPrevInput  = (localIdx == 0) ? lastGoodWorm0 : lastGoodWorm1;
    remotePrevInput = (localIdx == 0) ? lastGoodWorm1 : lastGoodWorm0;

    game.setSpeculative(true);
    for (int32_t F = rollbackTo + 1; F < static_cast<int32_t>(simFrame); ++F) {
      uint32_t s = static_cast<uint32_t>(F) % INPUT_BUFFER_SIZE;
      uint8_t curLocal = localInputs[s];
      uint8_t curRemote;
      bool framePredicted;
      // Same contiguity rule as the new-frame block — only consume real
      // input (and clear the ring entry) when the chain reaches F-1.
      // Out-of-order real inputs stay in the ring for a later promote.
      bool resimContiguous = (confirmedSimFrame_ + 1 == F);
      if (remoteInputReady[s] && resimContiguous) {
        curRemote = remoteInputs[s];
        remoteInputReady[s] = false;
        lastRemoteInput_ = curRemote;
        framePredicted = false;
        confirmedSimFrame_ = F;
      } else {
        curRemote = lastRemoteInput_;
        framePredicted = true;
      }

      uint8_t risingLocal  = curLocal  & ~localPrevInput;
      uint8_t risingRemote = curRemote & ~remotePrevInput;
      uint8_t releasedLocal  = localPrevInput  & ~curLocal;
      uint8_t releasedRemote = remotePrevInput & ~curRemote;
      game.worms[localIdx ]->controlStates.istate |= risingLocal;
      game.worms[remoteIdx]->controlStates.istate |= risingRemote;
      game.worms[localIdx ]->controlStates.istate &= ~releasedLocal;
      game.worms[remoteIdx]->controlStates.istate &= ~releasedRemote;
      localPrevInput  = curLocal;
      remotePrevInput = curRemote;

      game.processFrame();

      auto& outSlot = rollbackBuffer_.write(static_cast<int>(F));
      outSlot.localInput  = (localIdx == 0) ? curLocal  : curRemote;
      outSlot.remoteInput = (localIdx == 0) ? curRemote : curLocal;
      outSlot.remoteState = framePredicted
          ? rollback::RemoteState::Predicted
          : rollback::RemoteState::Confirmed;
      game.saveSnapshotFast(outSlot.snapshot);
    }
    game.setSpeculative(false);
  }

  // Step 6 — stall guard. After this tick simFrame becomes simFrame+1,
  // so the count of in-flight predicted frames would be
  // (simFrame+1) - confirmedSimFrame_ - 1 = simFrame - confirmedSimFrame_.
  // Cap that at kMaxRollback so the ring buffer (kMaxRollback+1 slots)
  // can still cover the post-tick window.
  if (static_cast<int32_t>(simFrame) - confirmedSimFrame_ > rollback::kMaxRollback) {
    return;
  }

  // Step 8 — frame-advantage stall. Hold simFrame when we're too far
  // ahead of the most recent local frame the remote peer reported.
  // We always sent the K-wide redundant batch above, so the remote
  // still hears from us this tick; only the *advance* is skipped, not
  // the send. The -1 sentinel keeps this disarmed until we've seen at
  // least one packet (avoiding a false stall at the very start of a
  // session before any remote frame is known).
  if (lastKnownRemoteFrame_ >= 0 &&
      static_cast<int32_t>(simFrame) - lastKnownRemoteFrame_
          >= frameAdvantageThreshold_) {
    ++frameAdvantageStalls_;
    return;
  }

  uint32_t currentSlot = simFrame % INPUT_BUFFER_SIZE;

  uint8_t curLocal = localInputs[currentSlot];
  uint8_t curRemote;
  bool predicted;
  // Only consume real remote input here when the confirmed chain reaches
  // simFrame-1 — out-of-order arrival (real input for a future frame
  // when an earlier frame is still missing) must stay in the ring so the
  // promote loop on a later tick can fold it into a contiguous chain.
  // Consuming it now would clear the ring entry and leave us with no
  // record that the real value was ever known, forcing a permanent
  // stall the moment the missing predecessor finally arrives.
  bool chainContiguous =
      confirmedSimFrame_ + 1 == static_cast<int32_t>(simFrame);
  if (remoteInputReady[currentSlot] && chainContiguous) {
    curRemote = remoteInputs[currentSlot];
    remoteInputReady[currentSlot] = false;
    lastRemoteInput_ = curRemote;
    predicted = false;
  } else {
    curRemote = lastRemoteInput_;
    predicted = true;
  }

  uint8_t risingLocal = curLocal & ~localPrevInput;
  uint8_t risingRemote = curRemote & ~remotePrevInput;
  uint8_t releasedLocal = localPrevInput & ~curLocal;
  uint8_t releasedRemote = remotePrevInput & ~curRemote;

  game.worms[localIdx]->controlStates.istate |= risingLocal;
  game.worms[remoteIdx]->controlStates.istate |= risingRemote;
  game.worms[localIdx]->controlStates.istate &= ~releasedLocal;
  game.worms[remoteIdx]->controlStates.istate &= ~releasedRemote;

  localPrevInput = curLocal;
  remotePrevInput = curRemote;

  game.setSpeculative(predicted);
  game.processFrame();
  game.setSpeculative(false);
  ++simFrame;

  // predicted=false only when chainContiguous was true above, which by
  // construction means confirmedSimFrame_ + 1 == simFrame-1 after the
  // increment — so the chain trivially extends to the just-run frame.
  if (!predicted) {
    confirmedSimFrame_ = static_cast<int32_t>(simFrame) - 1;
  }

  // Snapshot the post-frame state. Stored as Predicted/Confirmed per
  // whether the input that produced it was real or guessed — Step 7
  // reads this to decide whether to rollback when the real byte arrives.
  {
    int snapFrame = static_cast<int>(simFrame) - 1;
    rollback::Slot& slot = rollbackBuffer_.write(snapFrame);
    slot.localInput = (localIdx == 0) ? curLocal : curRemote;
    slot.remoteInput = (localIdx == 0) ? curRemote : curLocal;
    slot.remoteState = predicted
        ? rollback::RemoteState::Predicted
        : rollback::RemoteState::Confirmed;
    game.saveSnapshotFast(slot.snapshot);
  }

  // Checksums for predicted frames would race the remote side and trip
  // the desync alarm spuriously; only emit on confirmed frames. The
  // contiguity check matches the confirmedSimFrame_ guard above —
  // out-of-order arrivals must wait until promote fills the gap. Step 10
  // generalises this to "checksum captured at confirmation time".
  // !predicted implies the chain is contiguous through simFrame-1, so
  // this checksum reflects state that the remote peer has also fully
  // confirmed — no false-positive desync. Step 10 will generalise this.
  if (!predicted && sendChecksum) {
    sendChecksum(simFrame - 1, fastGameChecksum(game));
  }

  if (game.isGameOver()) {
    state = StateGameEnded;
    if (!goingToMenu) {
      fadeValue = 180;
      goingToMenu = true;
    }
  }
}

void RollbackController::draw(Renderer& renderer, bool useSpectatorViewports) {
  if (state == StateWeaponSelection) {
    ws->draw(renderer, state, useSpectatorViewports);
  } else if (state == StateGame || state == StateGameEnded) {
    game.draw(renderer, state, useSpectatorViewports);
  }
  renderer.fadeValue = fadeValue;

  if (isPaused()) {
    fill(renderer.bmp, 0);
    Common& common = *game.common;
    Font& font = common.font;
    int cx = renderer.renderResX / 2;
    int cy = renderer.renderResY / 2 - 20;

    renderer.pal = game.common->exepal;
    renderer.pal.rotateFrom(game.common->exepal, 168, 174, gfx.menuCycles);
    renderer.pal.fade(fadeValue);

    if (localPaused_) {
      std::string title = "GAME PAUSED";
      int tw = font.getDims(title);
      font.drawText(renderer.bmp, title, cx - tw / 2, cy, 50);

      pauseMenu_.place(cx, cy + 16);
      pauseMenu_.draw(common, renderer, false);
    } else {
      std::string title = "PAUSED BY PEER";
      int tw = font.getDims(title);
      font.drawText(renderer.bmp, title, cx - tw / 2, cy, 50);

      std::string hint = "PRESS ESC TO DISCONNECT";
      int hw = font.getDims(hint);
      font.drawText(renderer.bmp, hint, cx - hw / 2, cy + 16, 6);
    }
  }
}

void RollbackController::swapLevel(Level& newLevel) {
  currentLevel()->swap(newLevel);
}

Level* RollbackController::currentLevel() { return &game.level; }

Game* RollbackController::currentGame() { return &game; }

bool RollbackController::running() {
  return state != StateGameEnded && state != StateInitial;
}

void RollbackController::endMatch() {
  if (state == StateGame || state == StateWeaponSelection) {
    state = StateGameEnded;
    goingToMenu = true;
    fadeValue = 33;
  }
}
