#pragma once

// Rollback Step 4–7 — RollbackController.
//
// Structurally a copy of NetworkController for the lockstep path. Step 6
// adds GGPO-style prediction: when remote input for the current frame
// hasn't arrived, predict it = last received remote input and advance the
// sim with game.speculative = true. Predictions are bounded by
// rollback::kMaxRollback in-flight frames; past that the controller
// stalls. Step 7 closes the loop: when a real remote input arrives and
// disagrees with the prediction stored for that frame, advanceSimulation
// reloads the snapshot of the last confirmed frame and resimulates the
// gap with the freshest input known, marking each replayed frame
// speculative so side effects don't escape twice.

#include <array>
#include <climits>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "commonController.hpp"
#include "../game.hpp"
#include "../worm.hpp"
#include "../weapsel.hpp"
#include "../menu/menu.hpp"
#include "../rollback/buffer.hpp"

// Reuse the callback signatures from networkController.hpp. Including that
// header pulls in the typedefs without us re-declaring them and keeps the
// transport-side wiring identical across controllers.
#include "networkController.hpp"

struct RollbackController : CommonController {
  RollbackController(std::shared_ptr<Common> common,
                     std::shared_ptr<Settings> settings,
                     int localPlayerIdx);
  ~RollbackController();

  void onKey(int key, bool keyState) override;
  void unfocus() override;
  void focus() override;
  bool process() override;
  void draw(Renderer& renderer, bool useSpectatorViewports) override;
  void swapLevel(Level& newLevel) override;
  Level* currentLevel() override;
  Game* currentGame() override;
  bool running() override;

  // Step 7.5: batched send replaces the per-frame InputSendCallback.
  // `recv` is kept as a no-op-friendly pull hook for callers that still
  // want pull-based delivery; production tests inject via injectRemoteInput
  // on packet arrival instead.
  void setInputCallbacks(InputBatchSendCallback send, InputRecvCallback recv);
  void setChecksumCallback(ChecksumSendCallback cb) { sendChecksum = std::move(cb); }

  void injectRemoteInput(uint32_t frame, uint8_t input);

  // Step 8 — receive a batched packet plus the sender's simFrame at
  // send time. Wraps per-frame injectRemoteInput and updates the
  // frame-advantage estimate. Production receivers should prefer this
  // entry point over the bare injectRemoteInput.
  void injectRemoteBatch(uint32_t baseFrame, uint8_t count,
                         uint8_t const* inputs, uint32_t remoteLocalFrame);

  void setRemotePaused(bool paused) { remotePaused_ = paused; }
  bool isPaused() const { return localPaused_ || remotePaused_; }

  void setPauseCallbacks(std::function<void()> pauseCb, std::function<void()> resumeCb) {
    onLocalPause = std::move(pauseCb);
    onLocalResume = std::move(resumeCb);
  }

  void setEndMatchCallback(std::function<void()> cb) { onEndMatch = std::move(cb); }
  void endMatch();

  void setSkipWeaponSelection(bool skip) { skipWeaponSelection = skip; }

  void loadLevelFromData(const std::vector<uint8_t>& data);
  void setLevelPreloaded() { levelPreloaded = true; }

  uint32_t currentFrame() const { return simFrame; }
  void setLocalControlState(uint8_t packed) { localControlState.unpack(packed); }

  // Access the rollback ring buffer (Step 4: writes only; Step 5+ reads).
  rollback::RollbackBuffer const& rollbackBuffer() const { return rollbackBuffer_; }

  // Step 6 introspection — highest simFrame for which we ran with real
  // (received) remote input. Anything past this is currently a prediction.
  int32_t confirmedFrame() const { return confirmedSimFrame_; }

  // Step 7 introspection — total number of rollback events triggered so
  // far. Exposed for tests that want to assert "rollback actually fired"
  // (otherwise a correctness test could trivially pass by never
  // mispredicting).
  uint64_t rollbackCount() const { return rollbackCount_; }

  // Step 8 introspection — sender-side simFrame from the most recent
  // batched packet we accepted. -1 before any packet arrives. Used by
  // tests asserting the frame-advantage stall keeps the peers tightly
  // coupled.
  int32_t lastKnownRemoteFrame() const { return lastKnownRemoteFrame_; }
  uint64_t frameAdvantageStallCount() const { return frameAdvantageStalls_; }

  // Threshold at which the frame-advantage stall fires: skip a tick
  // when simFrame is at least this many frames ahead of the remote's
  // last reported simFrame. See Step 8 learnings for the convergence
  // math.
  static constexpr int32_t kFrameAdvantage = 2;

  // Tests that exercise other axes (rollback algorithm correctness
  // under loss / reorder) want the time-sync stall out of the way so
  // peers freely run ahead and exercise prediction + rollback. Setting
  // false raises the threshold high enough that the stall never fires.
  // Production never calls this — frame-advantage stays on by default.
  void setFrameAdvantageEnabled(bool enabled) {
    frameAdvantageThreshold_ = enabled ? kFrameAdvantage : INT32_MAX;
  }

  Game game;

 private:
  void advanceSimulation();
  void advanceWeaponSelection();
  void sendInputWindow(uint32_t newestFrame, uint32_t localFrame);

  int localIdx;
  int remoteIdx;

  GameState state;
  int fadeValue;
  bool goingToMenu;

  uint32_t simFrame;
  uint32_t inputDelay;
  uint32_t lastSentFrame;

  static constexpr uint32_t INPUT_BUFFER_SIZE = 256;
  std::array<uint8_t, INPUT_BUFFER_SIZE> localInputs;
  std::array<uint8_t, INPUT_BUFFER_SIZE> remoteInputs;
  std::array<bool, INPUT_BUFFER_SIZE> remoteInputReady;

  Worm::ControlState localControlState;

  uint8_t localPrevInput;
  uint8_t remotePrevInput;

  static constexpr int KEY_REPEAT_INITIAL = 12;
  static constexpr int KEY_REPEAT_INTERVAL = 3;
  std::array<uint16_t, 8> localHeldFrames;
  std::array<uint16_t, 8> remoteHeldFrames;

  bool skipWeaponSelection;
  bool levelPreloaded;

  bool localPaused_;
  bool remotePaused_;
  Menu pauseMenu_;

  InputBatchSendCallback sendInputBatch;
  InputRecvCallback recvInput;
  ChecksumSendCallback sendChecksum;

  std::function<void()> onLocalPause;
  std::function<void()> onLocalResume;
  std::function<void()> onEndMatch;

  std::unique_ptr<WeaponSelection> ws;

  // Rollback ring buffer — snapshots after each confirmed frame, plus the
  // local/remote input bytes that produced them. Pre-sized in focus()
  // once the level is generated. Not consulted yet (Step 4 only writes).
  rollback::RollbackBuffer rollbackBuffer_;
  bool rollbackBufferPrepared_;

  // Step 6 prediction state.
  // confirmedSimFrame_ — highest simFrame already advanced whose remote
  //   input was the real (received) byte. -1 before the first frame.
  // lastRemoteInput_ — most recent real remote input byte; used as the
  //   prediction when remote input for the current frame is missing
  //   (matches GGPO's "input duplication" default).
  int32_t confirmedSimFrame_;
  uint8_t lastRemoteInput_;

  uint64_t rollbackCount_ = 0;

  // Step 8 — sender-side simFrame from the most recent batched packet
  // we accepted (monotonic; stale packets carrying smaller frames are
  // ignored). -1 sentinel until any packet arrives so the
  // frame-advantage stall stays disarmed during warm-up.
  int32_t lastKnownRemoteFrame_ = -1;
  uint64_t frameAdvantageStalls_ = 0;
  int32_t frameAdvantageThreshold_ = kFrameAdvantage;
};
