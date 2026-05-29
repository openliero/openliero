#pragma once

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

// Batched input send: emits the last K = kMaxRollback + 1 local inputs
// per tick so a dropped packet is covered by the next K-1.
// `localFrame` = sender's simFrame at send time (frame-advantage
// tracking); `generation` = sender's phase generation (receivers drop
// stale ones).
using InputBatchSendCallback = std::function<
    void(uint8_t generation, uint32_t baseFrame, uint8_t count,
         uint8_t const* inputs, uint32_t localFrame)>;

// Pull-based remote input hook. Returns -1 when no input is yet
// available for that frame.
using InputRecvCallback = std::function<int(uint32_t frame)>;

// Checksum emission for desync detection. `generation` = sender's
// phase generation.
using ChecksumSendCallback =
    std::function<void(uint8_t generation, uint32_t frame, uint32_t checksum)>;

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

  void setInputCallbacks(InputBatchSendCallback send, InputRecvCallback recv);
  void setChecksumCallback(ChecksumSendCallback cb) { sendChecksum = std::move(cb); }

  void injectRemoteInput(uint32_t frame, uint8_t input);

  // `generation` is the sender's phase generation. Batches from an older
  // generation are dropped (their simFrame numbering describes a phase
  // the local controller has already abandoned, and slots are reused
  // across the phase boundary). Batches from generation_+1 are buffered
  // until the local phase transition fires; anything further is dropped.
  void injectRemoteBatch(uint8_t generation, uint32_t baseFrame, uint8_t count,
                         uint8_t const* inputs, uint32_t remoteLocalFrame);
  // Same-generation overload for tests that aren't exercising the
  // wire-level generation filter.
  void injectRemoteBatch(uint32_t baseFrame, uint8_t count,
                         uint8_t const* inputs, uint32_t remoteLocalFrame) {
    injectRemoteBatch(generation_, baseFrame, count, inputs, remoteLocalFrame);
  }

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
  GameState gameState() const { return state; }
  void setLocalControlState(uint8_t packed) { localControlState.unpack(packed); }
  // Must be called before the first sim tick.
  void setInputDelay(uint32_t frames) { inputDelay = frames; }

  rollback::RollbackBuffer const& rollbackBuffer() const { return rollbackBuffer_; }

  // Highest simFrame run with real (received) remote input; anything
  // past this is currently a prediction. -1 before the first frame.
  int32_t confirmedFrame() const { return confirmedSimFrame_; }

  uint64_t rollbackCount() const { return rollbackCount_; }

  // Frames the resim loop replayed during the most recent process() tick.
  // Reset each tick. Used by the dev HUD overlay (`RB:n`).
  uint32_t lastTickResimFrames() const { return lastTickResimFrames_; }

  // Sender-side simFrame from the most recent batched packet accepted.
  // -1 before any packet arrives, so the frame-advantage stall stays
  // disarmed during warm-up.
  int32_t lastKnownRemoteFrame() const { return lastKnownRemoteFrame_; }
  uint64_t frameAdvantageStallCount() const { return frameAdvantageStalls_; }

  // Phase generation. 0 = weapon select, 1 = game, etc. Bumped at every
  // WS→game transition so the wire layer can drop pre-transition packets.
  uint8_t generation() const { return generation_; }
  void setGenerationForTest(uint8_t g) { generation_ = g; }
  void resetForGamePhaseForTest() { resetForGamePhase(); }
  uint64_t droppedOldGenerationBatches() const {
    return droppedOldGenerationBatches_;
  }
  uint8_t pendingFutureBatchCount() const { return pendingFutureCount_; }

  // Stall a tick when simFrame is at least this far ahead of the remote's
  // last reported simFrame. 5 absorbs natural ±1-2 frame jitter between
  // two independent 70 fps processes (lower values caused a ~25% stall
  // rate on a quiet loopback link) while still leaving 2 frames of headroom
  // before the kMaxRollback=7 stall fires.
  static constexpr int32_t kFrameAdvantage = 5;

  // Test hook: raises the threshold high enough that the stall never
  // fires, so tests exercising loss/reorder can freely run ahead.
  void setFrameAdvantageEnabled(bool enabled) {
    frameAdvantageThreshold_ = enabled ? kFrameAdvantage : INT32_MAX;
  }

  Game game;

 public:
  // Save / restore mutable state touched during weapon selection. Public
  // so tests can drive a round-trip. worm.weapons[].type and menu item
  // display strings are re-derived on restore from the snapshotted
  // weapon IDs via Common.
  void saveWeaponSelectSnap(WeaponSelectSnap& snap) const;
  void loadWeaponSelectSnap(WeaponSelectSnap const& snap);

 private:
  void advanceSimulation();
  void advanceWeaponSelection();
  // Apply (curLocal, curRemote) to worm control states, run one ws tick,
  // and return whether weapon selection is now complete. Shared between
  // the forward path and the rollback resim.
  bool weaponSelectStep(uint8_t curLocal, uint8_t curRemote);
  // Idempotent via the state check.
  void finishWeaponSelect();
  void sendInputWindow(uint32_t newestFrame, uint32_t localFrame);
  // Full controller state reset for a phase transition. Clears the
  // input ring, snapshot ring, frame counters, and edge-detection state;
  // bumps generation_.
  void resetForGamePhase();

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

  // Snapshots after each confirmed frame, plus the local/remote input
  // bytes that produced them. Pre-sized in focus() once the level is
  // generated.
  rollback::RollbackBuffer rollbackBuffer_;
  bool rollbackBufferPrepared_;

  // Prediction state.
  // confirmedSimFrame_ — highest simFrame already advanced whose remote
  //   input was the real (received) byte. -1 before the first frame.
  // lastRemoteInput_ — most recent real remote input byte; used as the
  //   prediction when remote input for the current frame is missing
  //   (GGPO-style input duplication).
  int32_t confirmedSimFrame_;
  uint8_t lastRemoteInput_;

  uint64_t rollbackCount_ = 0;
  uint32_t lastTickResimFrames_ = 0;

  // Monotonic; stale packets carrying smaller frames are ignored.
  int32_t lastKnownRemoteFrame_ = -1;
  uint64_t frameAdvantageStalls_ = 0;
  int32_t frameAdvantageThreshold_ = kFrameAdvantage;

  // 0 until the first WS→game transition increments it.
  uint8_t generation_ = 0;
  uint64_t droppedOldGenerationBatches_ = 0;

  // Bounded queue for batches arriving from a peer that has already
  // crossed the next phase boundary while we haven't yet. Capacity
  // matches the K-wide redundancy so one full peer-side resend fits.
  static constexpr uint8_t kMaxPendingFutureBatches =
      static_cast<uint8_t>(rollback::kMaxRollback + 1);
  struct PendingFutureBatch {
    uint32_t baseFrame;
    uint8_t count;
    std::array<uint8_t, rollback::kMaxRollback + 1> inputs;
    uint32_t remoteLocalFrame;
  };
  std::array<PendingFutureBatch, kMaxPendingFutureBatches>
      pendingFutureBatches_{};
  uint8_t pendingFutureCount_ = 0;
};
