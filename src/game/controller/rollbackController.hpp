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
  //
  // Step 14 Task 14.2 — `generation` is the sender's phase generation.
  // Packets from a generation older than ours are dropped at the door
  // because they describe a pre-transition simFrame numbering that the
  // local controller has already abandoned; injecting them would corrupt
  // a live game slot (the slot's frame number is reused after reset).
  // Packets from a newer generation are also dropped for now — see
  // Task 14.5 for the future-generation handling decision.
  void injectRemoteBatch(uint8_t generation, uint32_t baseFrame, uint8_t count,
                         uint8_t const* inputs, uint32_t remoteLocalFrame);
  // Legacy two-arg overload used by tests written before Task 14.2.
  // Assumes generation == generation_ (i.e. the test isn't probing the
  // wire-level filter). Production callers (NetSession) use the 5-arg
  // form so they pass through the on-wire generation.
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
  // Lets tests observe the phase transition without poking at private
  // member state.
  GameState gameState() const { return state; }
  void setLocalControlState(uint8_t packed) { localControlState.unpack(packed); }
  // Step 11c — apply the host-authoritative inputDelay from Settings.
  // Must be called before the first sim tick (between construction and
  // focus). Production callers (NetSession) set this in createController.
  void setInputDelay(uint32_t frames) { inputDelay = frames; }

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

  // Step 11d — number of frames the resim loop replayed during the
  // most recent process() tick. 0 when no rollback fired this tick.
  // Used by the dev HUD overlay (`RB:n`) and by tests asserting the
  // resim window size.
  uint32_t lastTickResimFrames() const { return lastTickResimFrames_; }

  // Step 8 introspection — sender-side simFrame from the most recent
  // batched packet we accepted. -1 before any packet arrives. Used by
  // tests asserting the frame-advantage stall keeps the peers tightly
  // coupled.
  int32_t lastKnownRemoteFrame() const { return lastKnownRemoteFrame_; }
  uint64_t frameAdvantageStallCount() const { return frameAdvantageStalls_; }

  // Step 14 Task 14.2 — current phase generation. 0 = pre-game (weapon
  // select), 1 = game, etc. Bumped at every WS→game transition (Task
  // 14.4) so the wire layer can drop pre-transition packets. Exposed for
  // tests and for NetSession (so onChecksum knows which generation is
  // currently live).
  uint8_t generation() const { return generation_; }
  // Test-only — lets Task 14.2's unit test put the controller into a
  // post-transition generation without going through the full WS→game
  // path (that path is Task 14.4's job). Production code never calls
  // this; the generation bump happens inside resetForGamePhase().
  void setGenerationForTest(uint8_t g) { generation_ = g; }
  // Test-only — exposes the private Step 14 Task 14.3 reset path so
  // its unit test can drive it directly before Task 14.4 wires it into
  // finishWeaponSelect.
  void resetForGamePhaseForTest() { resetForGamePhase(); }
  // Test introspection: how many remote batches we dropped because
  // their generation was older than ours. Lets the unit test assert
  // the drop actually fired.
  uint64_t droppedOldGenerationBatches() const {
    return droppedOldGenerationBatches_;
  }
  // Step 14 Task 14.5 — current count of future-generation batches
  // we're holding pending the next phase transition. Exposed so the
  // unit test can assert the buffer fills + drains correctly.
  uint8_t pendingFutureBatchCount() const { return pendingFutureCount_; }

  // Threshold at which the frame-advantage stall fires: skip a tick
  // when simFrame is at least this many frames ahead of the remote's
  // last reported simFrame. See Step 8 learnings for the convergence
  // math.
  // Was 2; raised to 5 to absorb natural ±1-2 frame timing jitter between
  // two independent 70 fps processes. The previous threshold caused a
  // ~25% stall rate even on a quiet loopback link, slowing the effective
  // sim rate from 70 fps to ~52 fps. 5 still leaves 2 frames of headroom
  // before the kMaxRollback=7 stall fires.
  static constexpr int32_t kFrameAdvantage = 5;

  // Tests that exercise other axes (rollback algorithm correctness
  // under loss / reorder) want the time-sync stall out of the way so
  // peers freely run ahead and exercise prediction + rollback. Setting
  // false raises the threshold high enough that the stall never fires.
  // Production never calls this — frame-advantage stays on by default.
  void setFrameAdvantageEnabled(bool enabled) {
    frameAdvantageThreshold_ = enabled ? kFrameAdvantage : INT32_MAX;
  }

  Game game;

 public:
  // Save / restore the mutable state touched during weapon selection.
  // Public so tests can drive a round-trip. The snapshot covers:
  //   - per-worm: settings->weapons[], isReady[i], menu cursor/scroll,
  //               controlStates, currentWeapon
  //   - controller-level: edge inputs, held-frames
  //   - game.rand (the "Randomize" option calls it)
  // worm.weapons[].type and menu item display strings are re-derived on
  // restore from the snapshotted weapon IDs via Common.
  void saveWeaponSelectSnap(WeaponSelectSnap& snap) const;
  void loadWeaponSelectSnap(WeaponSelectSnap const& snap);

 private:
  void advanceSimulation();
  void advanceWeaponSelection();
  // Apply (curLocal, curRemote) to worm control states, run one ws tick,
  // and return whether weapon selection is now complete. Pure helper
  // shared between the forward path and the rollback resim.
  bool weaponSelectStep(uint8_t curLocal, uint8_t curRemote);
  // Perform the StateWeaponSelection -> StateGame transition. Idempotent
  // via the state check.
  void finishWeaponSelect();
  void sendInputWindow(uint32_t newestFrame, uint32_t localFrame);
  // Step 14 Task 14.3 — full controller state reset for a phase
  // transition. Clears the input ring, snapshot ring, frame counters,
  // and edge-detection state; bumps generation_. Centralised so the
  // WS→game transition (Task 14.4) doesn't have to enumerate every
  // field, and so future phase transitions (rematch, etc.) can reuse
  // the same path. Public test-only entry via resetForGamePhaseForTest.
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
  uint32_t lastTickResimFrames_ = 0;  // Step 11d — reset each tick.

  // Step 8 — sender-side simFrame from the most recent batched packet
  // we accepted (monotonic; stale packets carrying smaller frames are
  // ignored). -1 sentinel until any packet arrives so the
  // frame-advantage stall stays disarmed during warm-up.
  int32_t lastKnownRemoteFrame_ = -1;
  uint64_t frameAdvantageStalls_ = 0;
  int32_t frameAdvantageThreshold_ = kFrameAdvantage;

  // Step 14 Task 14.2 — phase generation. 0 until the first WS→game
  // transition increments it (Task 14.4). The send path stamps every
  // outbound batch/checksum with this value; the receive path drops
  // anything tagged with an older generation.
  uint8_t generation_ = 0;
  uint64_t droppedOldGenerationBatches_ = 0;

  // Step 14 Task 14.5 — bounded queue for batches arriving from a peer
  // that has already crossed the next phase boundary while we haven't
  // yet. We buffer at most one window so the boundary frames replay
  // cleanly once our own resetForGamePhase fires. Capacity matches the
  // K-wide redundancy so even one full peer-side resend fits.
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
