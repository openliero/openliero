#pragma once

// Rollback Step 4–6 — RollbackController.
//
// Structurally a copy of NetworkController for the lockstep path. Step 6
// adds GGPO-style prediction: when remote input for the current frame
// hasn't arrived, predict it = last received remote input and advance the
// sim with game.speculative = true. Predictions are bounded by
// rollback::kMaxRollback in-flight frames; past that the controller
// stalls. Rollback on misprediction lands in Step 7 — for now a wrong
// prediction simply produces divergent state, which is acceptable under
// zero jitter (predictions never happen) and visible-but-untreated under
// jitter (the Step 7 verification test will be what proves the algorithm
// recovers).

#include <array>
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

  void setInputCallbacks(InputSendCallback send, InputRecvCallback recv);
  void setChecksumCallback(ChecksumSendCallback cb) { sendChecksum = std::move(cb); }

  void injectRemoteInput(uint32_t frame, uint8_t input);

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

  Game game;

 private:
  void advanceSimulation();
  void advanceWeaponSelection();

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

  InputSendCallback sendInput;
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
};
