#pragma once

#include <vector>

#include "mixer/mixer.hpp"
#include "mixer/player.hpp"

struct Common;

struct Sfx {
  struct ChannelInfo {
    ChannelInfo() : id(0) {}

    void* id;  // ID of the sound playing on this channel
  };

  Sfx() : initialized(false) {}
  ~Sfx();

  void init();
  void deinit();

  void play(const Common& common, int sound, void* id = 0, int loops = 0);
  bool isPlaying(const void* id);

  void stop(const void* id);
  ChannelInfo channelInfo[8];

  sfx_mixer* mixer;
  bool initialized;
};

extern Sfx sfx;

struct DefaultSoundPlayer : SoundPlayer {
  DefaultSoundPlayer(Common& common) : common(common) {}

  Common& common;

  void play(int sound, void* id = 0, int loops = 0) override {
    sfx.play(common, sound, id, loops);
  }

  bool isPlaying(void* id) override { return sfx.isPlaying(id); }

  void stop(void* id) override { sfx.stop(id); }
};
